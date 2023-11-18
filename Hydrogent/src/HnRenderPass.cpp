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
#include "HnDrawItem.hpp"
#include "HnTypeConversions.hpp"

#include <array>

#include "pxr/imaging/hd/renderIndex.h"

#include "USD_Renderer.hpp"
#include "MapHelper.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"

} // namespace HLSL

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

    const Uint32 PrimitiveAttribsAlignedOffset;

    IPipelineState*         pPSO = nullptr;
    IShaderResourceBinding* pSRB = nullptr;

    IBuffer*                pIndexBuffer    = nullptr;
    std::array<IBuffer*, 4> ppVertexBuffers = {};

    void SetPipelineState(IPipelineState* pNewPSO)
    {
        VERIFY_EXPR(pNewPSO != nullptr);
        if (pNewPSO == nullptr || pNewPSO == pPSO)
            return;

        pCtx->SetPipelineState(pNewPSO);
        pPSO = pNewPSO;
    }

    void CommitShaderResources(IShaderResourceBinding* pNewSRB)
    {
        if (pNewSRB == nullptr || pNewSRB == this->pSRB)
            return;

        pCtx->CommitShaderResources(pNewSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pSRB = pNewSRB;
    }

    void SetIndexBuffer(IBuffer* pNewIndexBuffer)
    {
        if (pNewIndexBuffer == nullptr || pNewIndexBuffer == pIndexBuffer)
            return;

        pIndexBuffer = pNewIndexBuffer;
        pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void SetVertexBuffers(IBuffer** ppBuffers, Uint32 NumBuffers)
    {
        VERIFY_EXPR(NumBuffers < 4);
        bool SetBuffers = false;
        for (Uint32 i = 0; i < NumBuffers; ++i)
        {
            if (ppBuffers[i] != nullptr && ppBuffers[i] != ppVertexBuffers[i])
            {
                SetBuffers         = true;
                ppVertexBuffers[i] = ppBuffers[i];
            }
        }

        if (SetBuffers)
        {
            pCtx->SetVertexBuffers(0, NumBuffers, ppBuffers, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
        }
    }
};

void HnRenderPass::_Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                            const pxr::TfTokenVector&              Tags)
{
    UpdateDrawItems(Tags);
    if (m_DrawItems.empty())
        return;

    // Render pass state is initialized by HnBeginFrameTask, and
    // passed from the render Rprims task.
    if (!RPState)
    {
        UNEXPECTED("Render pass state should not be null");
        return;
    }

    if (m_DrawItemsGPUResourcesDirty)
        UpdateDrawItemsGPUResources(*static_cast<const HnRenderPassState*>(RPState.get()));

    pxr::HdRenderIndex* pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    RenderState State{
        pRenderDelegate->GetDeviceContext(),
        pRenderDelegate->GetPrimitiveAttribsAlignedOffset(),
    };

    IBuffer* const pPrimitiveAttribsCB = pRenderDelegate->GetPrimitiveAttribsCB();
    VERIFY_EXPR(pPrimitiveAttribsCB != nullptr);

    const auto& Desc                 = pPrimitiveAttribsCB->GetDesc();
    const auto  MaxDrawItemsInBuffer = Desc.Size / State.PrimitiveAttribsAlignedOffset;
    const auto  NumDrawItems         = m_DrawItems.size();

    m_PendingDrawItems.clear();
    HLSL::PBRPrimitiveAttribs* pCurrPrimitive = nullptr;
    for (size_t DrawItemIdx = 0; DrawItemIdx < NumDrawItems; ++DrawItemIdx)
    {
        const auto& DrawItem = m_DrawItems[DrawItemIdx];
        if (!DrawItem.IsValid() || !DrawItem.GetHdDrawItem().GetVisible())
            continue;

        if (pCurrPrimitive == nullptr)
        {
            State.pCtx->MapBuffer(pPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, reinterpret_cast<PVoid&>(pCurrPrimitive));
            if (pCurrPrimitive == nullptr)
            {
                UNEXPECTED("Failed to map the primitive attributes buffer");
                break;
            }
        }

        // Write current primitive attributes

        const auto& Geo = DrawItem.GetGeometryData();

        pCurrPrimitive->Transforms.NodeMatrix = Geo.pMesh->GetTransform() * m_RenderParams.Transform;
        pCurrPrimitive->Transforms.JointCount = 0;

        const HLSL::PBRMaterialShaderInfo& ShaderAttribs = Geo.pMaterial->GetShaderAttribs();
        static_assert(sizeof(pCurrPrimitive->Material) == sizeof(ShaderAttribs), "The sizeof(PBRMaterialShaderInfo) is inconsistent with sizeof(ShaderAttribs)");
        memcpy(&pCurrPrimitive->Material, &ShaderAttribs, sizeof(ShaderAttribs));

        if (Geo.IsFallbackMaterial)
        {
            pCurrPrimitive->Material.BaseColorFactor = Geo.pMesh->GetDisplayColor();
        }

        pCurrPrimitive->CustomData = float4{
            static_cast<float>(Geo.pMesh->GetUID()),
            Geo.pMesh->GetId().HasPrefix(m_RenderParams.SelectedPrimId) ? 1.f : 0.f,
            0,
            0,
        };

        pCurrPrimitive = reinterpret_cast<HLSL::PBRPrimitiveAttribs*>(reinterpret_cast<Uint8*>(pCurrPrimitive) + State.PrimitiveAttribsAlignedOffset);
        m_PendingDrawItems.push_back(&DrawItem);

        if (m_PendingDrawItems.size() == MaxDrawItemsInBuffer || DrawItemIdx == NumDrawItems - 1)
        {
            // Either the buffer is full or this is the last item. Render the pending items and start
            // filling the buffer from the beginning.
            State.pCtx->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            pCurrPrimitive = nullptr;
            RenderPendingDrawItems(State);
            VERIFY_EXPR(m_PendingDrawItems.empty());
        }
    }
    VERIFY_EXPR(pCurrPrimitive == nullptr);
}

void HnRenderPass::_MarkCollectionDirty()
{
    // Force any cached data based on collection to be refreshed.
    m_CollectionVersion = ~0u;
}

void HnRenderPass::SetMeshRenderParams(const HnMeshRenderParams& Params)
{
    if (m_RenderParams.SelectedPrimId != Params.SelectedPrimId)
        _MarkCollectionDirty();

    if (m_RenderParams.RenderMode != Params.RenderMode ||
        m_RenderParams.DebugViewMode != Params.DebugViewMode)
        m_DrawItemsGPUResourcesDirty = true;

    m_RenderParams = Params;
}

void HnRenderPass::SetParams(const HnRenderPassParams& Params)
{
    if (m_Params.UsdPsoFlags != Params.UsdPsoFlags)
        m_DrawItemsGPUResourcesDirty = true;

    m_Params = Params;
}

void HnRenderPass::UpdateDrawItems(const pxr::TfTokenVector& RenderTags)
{
    pxr::HdRenderIndex* pRenderIndex = GetRenderIndex();
    //HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    const pxr::HdRprimCollection& Collection  = GetRprimCollection();
    const pxr::HdChangeTracker&   Tracker     = pRenderIndex->GetChangeTracker();
    const pxr::TfToken&           MaterialTag = Collection.GetMaterialTag();

    const unsigned int CollectionVersion     = Tracker.GetCollectionVersion(Collection.GetName());
    const unsigned int RprimRenderTagVersion = Tracker.GetRenderTagVersion();
    const unsigned int TaskRenderTagsVersion = Tracker.GetTaskRenderTagsVersion();
    //const unsigned int GeomSubsetDrawItemsVersion = GetGeomSubsetDrawItemsVersion(pRenderIndex);

    const bool CollectionChanged     = (m_CollectionVersion != CollectionVersion);
    const bool RprimRenderTagChanged = (m_RprimRenderTagVersion != RprimRenderTagVersion);
    const bool MaterialTagChanged    = (m_MaterialTag != MaterialTag);
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
        MaterialTagChanged ||
        //GeomSubsetDrawItemsChanged ||
        TaskRenderTagsChanged)
    {
        m_DrawItems.clear();
        //const HnRenderParam* const RenderParam = static_cast<HnRenderParam*>(pRenderIndex->GetRenderDelegate()->GetRenderParam());
        //if (RenderParam->HasMaterialTag(Collection.GetMaterialTag()))
        {
            pxr::HdRenderIndex::HdDrawItemPtrVector DrawItems = GetRenderIndex()->GetDrawItems(Collection, RenderTags);
            // GetDrawItems() uses multithreading, so the order of draw items is not deterministic.
            std::sort(DrawItems.begin(), DrawItems.end());

            for (const pxr::HdDrawItem* pDrawItem : DrawItems)
            {
                if (pDrawItem == nullptr)
                    continue;

                const bool IsSelected = pDrawItem->GetRprimID().HasPrefix(m_RenderParams.SelectedPrimId);
                if ((m_Params.Selection == HnRenderPassParams::SelectionType::All) ||
                    (m_Params.Selection == HnRenderPassParams::SelectionType::Selected && IsSelected) ||
                    (m_Params.Selection == HnRenderPassParams::SelectionType::Unselected && !IsSelected))
                {
                    m_DrawItems.push_back(HnDrawItem{*pDrawItem});
                }
            }

            m_DrawItemsGPUResourcesDirty = true;
        }
        //else
        //{
        //    // There is no prim with the desired material tag.
        //    m_DrawItems.clear()
        //}
    }

    m_CollectionVersion     = CollectionVersion;
    m_RprimRenderTagVersion = RprimRenderTagVersion;
    m_MaterialTag           = MaterialTag;
}

void HnRenderPass::UpdateDrawItemsGPUResources(const HnRenderPassState& RPState)
{
    VERIFY_EXPR(m_DrawItemsGPUResourcesDirty);

    pxr::HdRenderIndex* pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    auto USDRenderer = pRenderDelegate->GetUSDRenderer();

    GraphicsPipelineDesc GraphicsDesc = RPState.GetGraphicsPipelineDesc();
    if (m_Params.UsdPsoFlags == USD_Renderer::USD_PSO_FLAG_NONE)
    {
        for (Uint32 i = 0; i < GraphicsDesc.NumRenderTargets; ++i)
            GraphicsDesc.RTVFormats[i] = TEX_FORMAT_UNKNOWN;
        GraphicsDesc.NumRenderTargets = 0;
    }

    switch (m_RenderParams.RenderMode)
    {
        case HN_RENDER_MODE_SOLID:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;

        case HN_RENDER_MODE_MESH_EDGES:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;

        case HN_RENDER_MODE_POINTS:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;

        default:
            UNEXPECTED("Unexpected render mode");
            return;
    }
    static_assert(HN_RENDER_MODE_COUNT == 3, "Please handle the new render mode in the switch above");

    USD_Renderer::PsoCacheAccessor PsoCache = USDRenderer->GetPsoCacheAccessor(GraphicsDesc);
    VERIFY_EXPR(PsoCache);

    const USD_Renderer::ALPHA_MODE AlphaMode = MaterialTagToPbrAlphaMode(m_MaterialTag);
    for (HnDrawItem& DrawItem : m_DrawItems)
    {
        const pxr::SdfPath& RPrimID = DrawItem.GetHdDrawItem().GetRprimID();
        const pxr::HdRprim* pRPrim  = pRenderIndex->GetRprim(RPrimID);
        if (pRPrim == nullptr)
            continue;

        bool IsFallbackMaterial = false;

        const pxr::SdfPath& MaterialId = pRPrim->GetMaterialId();
        const pxr::HdSprim* pSPrim     = pRenderIndex->GetSprim(pxr::HdPrimTypeTokens->material, MaterialId);
        if (pSPrim == nullptr)
        {
            pSPrim = pRenderIndex->GetFallbackSprim(pxr::HdPrimTypeTokens->material);
            if (pSPrim == nullptr)
            {
                UNEXPECTED("Unable to get fallback sprim. This is unexpected as default material is initialized in the render delegate.");
                continue;
            }
            IsFallbackMaterial = true;
        }

        const HnMesh&     Mesh     = *static_cast<const HnMesh*>(pRPrim);
        const HnMaterial& Material = *static_cast<const HnMaterial*>(pSPrim);

        {
            HnDrawItem::GeometryData Geo{Mesh, Material, IsFallbackMaterial};

            // Get vertex buffers
            Geo.FaceVerts = Mesh.GetFaceVertexBuffer(pxr::HdTokens->points);
            Geo.Points    = Mesh.GetPointsVertexBuffer();
            Geo.Normals   = Mesh.GetFaceVertexBuffer(pxr::HdTokens->normals);

            // Our shader currently supports two texture coordinate sets.
            // Gather vertex buffers for both sets.
            {
                const auto& TexCoordSets = Material.GetTextureCoordinateSets();
                for (size_t i = 0; i < TexCoordSets.size(); ++i)
                {
                    const auto& TexCoordSet = TexCoordSets[i];
                    if (!TexCoordSet.PrimVarName.IsEmpty())
                    {
                        Geo.TexCoords[i] = Mesh.GetFaceVertexBuffer(TexCoordSet.PrimVarName);
                        if (!Geo.TexCoords[i])
                        {
                            LOG_ERROR_MESSAGE("Failed to find texture coordinates vertex buffer '", TexCoordSet.PrimVarName.GetText(), "' in mesh '", Mesh.GetId().GetText(), "'");
                        }
                    }
                }
            }

            // Get index buffers
            Geo.FaceIndices     = Mesh.GetFaceIndexBuffer();
            Geo.EdgeIndices     = Mesh.GetEdgeIndexBuffer();
            Geo.NumFaceVertices = Mesh.GetNumFaceTriangles() * 3;
            Geo.NumEdgeVertices = Mesh.GetNumEdges() * 2;
            Geo.NumPoints       = Mesh.GetNumPoints();
            Geo.FaceStartIndex  = Mesh.GetFaceStartIndex();
            Geo.EdgeStartIndex  = Mesh.GetEdgeStartIndex();

            DrawItem.SetGeometryData(std::move(Geo));
        }

        {
            auto PSOFlags = static_cast<PBR_Renderer::PSO_FLAGS>(m_Params.UsdPsoFlags);

            IPipelineState* pPSO = nullptr;
            if (m_RenderParams.RenderMode == HN_RENDER_MODE_SOLID)
            {
                const auto& Geo = DrawItem.GetGeometryData();

                if (Geo.Normals != nullptr && (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0)
                    PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
                if (Geo.TexCoords[0] != nullptr)
                    PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
                if (Geo.TexCoords[1] != nullptr)
                    PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;

                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_COLOR_MAP;
                if (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT)
                {
                    PSOFlags |=
                        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
                        PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
                        PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
                        PBR_Renderer::PSO_FLAG_USE_AO_MAP |
                        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP |
                        PBR_Renderer::PSO_FLAG_USE_IBL;
                }
                VERIFY(Material.GetShaderAttribs().AlphaMode == AlphaMode || IsFallbackMaterial,
                       "Alpha mode derived from the material tag is not consistent with the alpha mode in the shader attributes. "
                       "This may indicate an issue in how alpha mode is determined in the material, or (less likely) an issue in Rprim sorting by Hydra.");
                pPSO = PsoCache.Get({PSOFlags, static_cast<PBR_Renderer::ALPHA_MODE>(AlphaMode), /*DoubleSided = */ false, static_cast<PBR_Renderer::DebugViewType>(m_RenderParams.DebugViewMode)}, true);
            }
            else if (m_RenderParams.RenderMode == HN_RENDER_MODE_MESH_EDGES ||
                     m_RenderParams.RenderMode == HN_RENDER_MODE_POINTS)
            {
                PSOFlags |= PBR_Renderer::PSO_FLAG_UNSHADED;
                pPSO = PsoCache.Get({PSOFlags, /*DoubleSided = */ false, static_cast<PBR_Renderer::DebugViewType>(m_RenderParams.DebugViewMode)}, true);
            }
            else
            {
                UNEXPECTED("Unexpected render mode");
                continue;
            }

            VERIFY_EXPR(pPSO != nullptr);
            DrawItem.SetPSO(pPSO);
        }

        if (IShaderResourceBinding* pSRB = Material.GetSRB())
        {
            DrawItem.SetSRB(pSRB);
        }
    }

    m_DrawItemsGPUResourcesDirty = false;
}


void HnRenderPass::RenderPendingDrawItems(RenderState& State)
{
    for (size_t i = 0; i < m_PendingDrawItems.size(); ++i)
    {
        const HnDrawItem& DrawItem = *m_PendingDrawItems[i];

        State.SetPipelineState(DrawItem.GetPSO());

        DrawItem.GetPrimitiveAttribsVar()->SetBufferOffset(static_cast<Uint32>(i * State.PrimitiveAttribsAlignedOffset));
        State.CommitShaderResources(DrawItem.GetSRB());

        const auto& Geo = DrawItem.GetGeometryData();

        IBuffer* IndexBuffer = nullptr;
        Uint32   StartIndex  = 0;
        switch (m_RenderParams.RenderMode)
        {
            case HN_RENDER_MODE_SOLID:
                IndexBuffer = Geo.FaceIndices;
                StartIndex  = Geo.FaceStartIndex;
                break;

            case HN_RENDER_MODE_MESH_EDGES:
                IndexBuffer = Geo.EdgeIndices;
                StartIndex  = Geo.EdgeStartIndex;
                break;

            case HN_RENDER_MODE_POINTS:
                IndexBuffer = nullptr;
                break;

            default:
                UNEXPECTED("Unexpected render mode");
                return;
        }
        static_assert(HN_RENDER_MODE_COUNT == 3, "Please handle the new render mode in the switch above");
        State.SetIndexBuffer(IndexBuffer);

        switch (m_RenderParams.RenderMode)
        {
            case HN_RENDER_MODE_SOLID:
            {
                IBuffer* pBuffs[] = {Geo.FaceVerts, Geo.Normals, Geo.TexCoords[0], Geo.TexCoords[1]};
                State.SetVertexBuffers(pBuffs, _countof(pBuffs));
                if (IndexBuffer != nullptr)
                {
                    DrawIndexedAttribs DrawAttrs{Geo.NumFaceVertices, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    DrawAttrs.FirstIndexLocation = StartIndex;
                    State.pCtx->DrawIndexed(DrawAttrs);
                }
                else
                {
                    State.pCtx->Draw({Geo.NumFaceVertices, DRAW_FLAG_VERIFY_ALL});
                }
            }
            break;

            case HN_RENDER_MODE_MESH_EDGES:
                if (IndexBuffer != nullptr)
                {
                    IBuffer* pBuffs[] = {Geo.Points};
                    State.SetVertexBuffers(pBuffs, _countof(pBuffs));
                    DrawIndexedAttribs DrawAttrs{Geo.NumEdgeVertices, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    DrawAttrs.FirstIndexLocation = StartIndex;
                    State.pCtx->DrawIndexed(DrawAttrs);
                }
                else
                {
                    UNEXPECTED("Edge index buffer is not initialized");
                }
                break;

            case HN_RENDER_MODE_POINTS:
            {
                IBuffer* pBuffs[] = {Geo.Points};
                State.SetVertexBuffers(pBuffs, _countof(pBuffs));
                State.pCtx->Draw({Geo.NumPoints, DRAW_FLAG_VERIFY_ALL});
            }
            break;

            default:
                UNEXPECTED("Unexpected render mode");
                return;
        }
        static_assert(HN_RENDER_MODE_COUNT == 3, "Please handle the new render mode in the switch above");
    }

    m_PendingDrawItems.clear();
}

} // namespace USD

} // namespace Diligent
