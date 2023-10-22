/*
 *  Copyright 2023 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "HnRenderPass.hpp"
#include "HnRenderPassState.hpp"
#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"

#include "pxr/imaging/hd/renderIndex.h"

#include "USD_Renderer.hpp"
#include "MapHelper.hpp"

namespace Diligent
{

namespace USD
{

pxr::HdRenderPassSharedPtr HnRenderPass::Create(pxr::HdRenderIndex*           pIndex,
                                                const pxr::HdRprimCollection& Collection)
{
    return pxr::HdRenderPassSharedPtr{new HnRenderPass{pIndex, Collection}};
}

HnRenderPass::HnRenderPass(pxr::HdRenderIndex*           pIndex,
                           const pxr::HdRprimCollection& Collection) :
    pxr::HdRenderPass{pIndex, Collection}
{
}

struct HnRenderPass::RenderState
{
    IDeviceContext* const pCtx;
    IBuffer* const        pPBRAttribsCB;

    const HnRenderPassState&       RPState;
    const PBR_Renderer::ALPHA_MODE AlphaMode;

    USD_Renderer::PbrPsoCacheAccessor       PbrPSOCache;
    USD_Renderer::WireframePsoCacheAccessor WireframePSOCache;

    IPipelineState* pPSO = nullptr;
};

void HnRenderPass::_Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                            const pxr::TfTokenVector&              Tags)
{
    UpdateDrawItems(Tags);
    if (m_DrawItems.empty())
        return;

    // Render pass state is initialized by the setup rendering task, and
    // passed from the render Rprims task.
    if (!RPState)
    {
        UNEXPECTED("Render pass state should not be null");
        return;
    }

    pxr::HdRenderIndex* pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    auto& USDRenderer = pRenderDelegate->GetUSDRenderer();
    if (!USDRenderer)
    {
        UNEXPECTED("USD renderer is not initialized");
        return;
    }

    RenderState State{
        pRenderDelegate->GetDeviceContext(),
        USDRenderer->GetPBRAttribsCB(),
        static_cast<const HnRenderPassState&>(*RPState),
        PBR_Renderer::ALPHA_MODE_OPAQUE,
    };

    GraphicsPipelineDesc GraphicsDesc = State.RPState.GetGraphicsPipelineDesc();
    const HN_RENDER_MODE RenderMode   = State.RPState.GetRenderMode();
    if (RenderMode == HN_RENDER_MODE_SOLID)
    {
        GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        State.PbrPSOCache              = USDRenderer->GetPbrPsoCacheAccessor(GraphicsDesc);
        VERIFY_EXPR(State.PbrPSOCache);
    }
    else if (RenderMode == HN_RENDER_MODE_MESH_EDGES)
    {
        GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;
        State.WireframePSOCache        = USDRenderer->GetWireframePsoCacheAccessor(GraphicsDesc);
        VERIFY_EXPR(State.WireframePSOCache);
    }
    else
    {
        UNEXPECTED("Unexpected render mode");
        return;
    }

    for (const pxr::HdDrawItem* pDrawItem : m_DrawItems)
    {
        if (!pDrawItem->GetVisible())
            continue;

        const pxr::SdfPath& RPrimID = pDrawItem->GetRprimID();
        if (auto& pMesh = pRenderDelegate->GetMesh(RPrimID))
        {
            const auto& MaterialId = pMesh->GetMaterialId();
            const auto* pMaterial  = pRenderDelegate->GetMaterial(MaterialId);
            if (pMaterial == nullptr)
                continue;

            RenderMesh(State, *pMesh, *pMaterial);
        }
    }
}

void HnRenderPass::_MarkCollectionDirty()
{
    // Force any cached data based on collection to be refreshed.
    m_CollectionVersion = ~0u;
}

void HnRenderPass::UpdateDrawItems(const pxr::TfTokenVector& RenderTags)
{
    pxr::HdRenderIndex* pRenderIndex = GetRenderIndex();
    //HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    const pxr::HdRprimCollection& Collection = GetRprimCollection();
    const pxr::HdChangeTracker&   Tracker    = pRenderIndex->GetChangeTracker();

    const unsigned int CollectionVersion     = Tracker.GetCollectionVersion(Collection.GetName());
    const unsigned int RprimRenderTagVersion = Tracker.GetRenderTagVersion();
    const unsigned int TaskRenderTagsVersion = Tracker.GetTaskRenderTagsVersion();
    //const unsigned int MaterialTagsVersion        = GetMaterialTagsVersion(pRenderIndex);
    //const unsigned int GeomSubsetDrawItemsVersion = GetGeomSubsetDrawItemsVersion(pRenderIndex);

    const bool CollectionChanged     = (m_CollectionVersion != CollectionVersion);
    const bool RprimRenderTagChanged = (m_RprimRenderTagVersion != RprimRenderTagVersion);
    //const bool MaterialTagsChanged        = (m_MaterialTagsVersion != MaterialTagsVersion);
    //const bool GeomSubsetDrawItemsChanged = (m_GeomSubsetDrawItemsVersion != GeomSubsetDrawItemsVersion);

    bool TaskRenderTagsChanged = false;
    if (m_TaskRenderTagsVersion != TaskRenderTagsVersion)
    {
        m_TaskRenderTagsVersion = TaskRenderTagsVersion;
        if (m_RenderTags != RenderTags)
        {
            m_RenderTags          = RenderTags;
            TaskRenderTagsChanged = true;
        }
    }

    if (CollectionChanged ||
        RprimRenderTagChanged ||
        //MaterialTagsChanged ||
        //GeomSubsetDrawItemsChanged ||
        TaskRenderTagsChanged)
    {
        //const HnRenderParam* const RenderParam = static_cast<HnRenderParam*>(pRenderIndex->GetRenderDelegate()->GetRenderParam());
        //if (RenderParam->HasMaterialTag(Collection.GetMaterialTag()))
        {
            m_DrawItems = GetRenderIndex()->GetDrawItems(Collection, RenderTags);
        }
        //else
        //{
        //    // There is no prim with the desired material tag.
        //    m_DrawItems.clear()
        //}
    }

    m_CollectionVersion     = CollectionVersion;
    m_RprimRenderTagVersion = RprimRenderTagVersion;
}


void HnRenderPass::RenderMesh(RenderState&      State,
                              const HnMesh&     Mesh,
                              const HnMaterial& Material)
{
    auto* const pSRB          = Material.GetSRB();
    auto* const pPosVB        = Mesh.GetVertexBuffer(pxr::HdTokens->points);
    auto* const pNormalsVB    = Mesh.GetVertexBuffer(pxr::HdTokens->normals);
    const auto& ShaderAttribs = Material.GetShaderAttribs();

    // Our shader currently supports two texture coordinate sets.
    // Gather vertex buffers for both sets.
    const auto& TexCoordSets    = Material.GetTextureCoordinateSets();
    IBuffer*    pTexCoordVBs[2] = {};
    for (size_t i = 0; i < TexCoordSets.size(); ++i)
    {
        const auto& TexCoordSet = TexCoordSets[i];
        if (!TexCoordSet.PrimVarName.IsEmpty())
        {
            pTexCoordVBs[i] = Mesh.GetVertexBuffer(TexCoordSet.PrimVarName);
            if (!pTexCoordVBs[i])
            {
                LOG_ERROR_MESSAGE("Failed to find texture coordinates vertex buffer '", TexCoordSet.PrimVarName.GetText(), "' in mesh '", Mesh.GetId().GetText(), "'");
            }
        }
    }

    const auto RenderMode = State.RPState.GetRenderMode();

    auto* pIB = (RenderMode == HN_RENDER_MODE_MESH_EDGES) ?
        Mesh.GetEdgeIndexBuffer() :
        Mesh.GetTriangleIndexBuffer();

    if (pPosVB == nullptr || pIB == nullptr || pSRB == nullptr)
        return;

    auto PSOFlags = PBR_Renderer::PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT;

    IPipelineState* pPSO = nullptr;
    if (RenderMode == HN_RENDER_MODE_SOLID)
    {
        if (pNormalsVB != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        if (pTexCoordVBs[0] != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        if (pTexCoordVBs[1] != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;

        PSOFlags |=
            PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
            PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
            PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
            PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
            PBR_Renderer::PSO_FLAG_USE_AO_MAP |
            PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP |
            PBR_Renderer::PSO_FLAG_USE_IBL |
            PBR_Renderer::PSO_FLAG_ENABLE_DEBUG_VIEW;

        pPSO = State.PbrPSOCache.Get({PSOFlags, static_cast<PBR_Renderer::ALPHA_MODE>(ShaderAttribs.AlphaMode), /*DoubleSided = */ false}, true);
    }
    else if (RenderMode == HN_RENDER_MODE_MESH_EDGES)
    {
        pPSO = State.WireframePSOCache.Get({PSOFlags, /*DoubleSided = */ false}, true);
    }
    else
    {
        UNEXPECTED("Unexpected render mode");
        return;
    }

    if (State.pPSO != pPSO)
    {
        State.pCtx->SetPipelineState(pPSO);
        State.pPSO = pPSO;
    }

    // Bind vertex and index buffers
    IBuffer* pBuffs[] = {pPosVB, pNormalsVB, pTexCoordVBs[0], pTexCoordVBs[1]};
    State.pCtx->SetVertexBuffers(0, _countof(pBuffs), pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    State.pCtx->SetIndexBuffer(pIB, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<HLSL::PBRShaderAttribs> pDstShaderAttribs{State.pCtx, State.pPBRAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

        pDstShaderAttribs->Transforms.NodeMatrix = Mesh.GetTransform() * State.RPState.GetTransform();
        pDstShaderAttribs->Transforms.JointCount = 0;

        static_assert(sizeof(pDstShaderAttribs->Material) == sizeof(ShaderAttribs), "The sizeof(PBRMaterialShaderInfo) is inconsistent with sizeof(ShaderAttribs)");
        memcpy(&pDstShaderAttribs->Material, &ShaderAttribs, sizeof(ShaderAttribs));

        auto& RendererParams = pDstShaderAttribs->Renderer;

        RendererParams.DebugViewType     = State.RPState.GetDebugView();
        RendererParams.OcclusionStrength = State.RPState.GetOcclusionStrength();
        RendererParams.EmissionScale     = State.RPState.GetEmissionScale();
        RendererParams.IBLScale          = State.RPState.GetIBLScale();

        RendererParams.PrefilteredCubeMipLevels = 5;                  //m_Settings.UseIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
        RendererParams.WireframeColor           = float4{1, 1, 1, 1}; //Attribs.WireframeColor;
        RendererParams.HighlightColor           = float4{0, 0, 0, 0};

        RendererParams.AverageLogLum = 0.3f;  //Attribs.AverageLogLum;
        RendererParams.MiddleGray    = 0.18f; //Attribs.MiddleGray;
        RendererParams.WhitePoint    = 3.0f;  //Attribs.WhitePoint;

        auto CustomData = float4{static_cast<float>(Mesh.GetUID()), 0, 0, 1};
        //if (Attribs.SelectedPrim != nullptr && Mesh.GetId() == *Attribs.SelectedPrim)
        //    CustomData.x *= -1;
        RendererParams.CustomData = CustomData;
    }

    State.pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawIndexedAttribs DrawAttrs = (RenderMode == HN_RENDER_MODE_MESH_EDGES) ?
        DrawIndexedAttribs{Mesh.GetNumEdges() * 2, VT_UINT32, DRAW_FLAG_VERIFY_ALL} :
        DrawIndexedAttribs{Mesh.GetNumTriangles() * 3, VT_UINT32, DRAW_FLAG_VERIFY_ALL};

    State.pCtx->DrawIndexed(DrawAttrs);
}

} // namespace USD

} // namespace Diligent
