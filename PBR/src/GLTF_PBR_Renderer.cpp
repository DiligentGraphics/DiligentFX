/*
 *  Copyright 2019-2023 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
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

#include "GLTF_PBR_Renderer.hpp"

#include <array>

#include "BasicMath.hpp"
#include "MapHelper.hpp"
#include "GraphicsAccessories.hpp"

namespace Diligent
{

GLTF_PBR_Renderer::ALPHA_MODE GLTF_PBR_Renderer::GltfAlphaModeToAlphaMode(GLTF::Material::ALPHA_MODE GltfAlphaMode)
{
    static_assert(static_cast<ALPHA_MODE>(GLTF::Material::ALPHA_MODE_OPAQUE) == ALPHA_MODE_OPAQUE, "GLTF::Material::ALPHA_MODE_OPAQUE != ALPHA_MODE_OPAQUE");
    static_assert(static_cast<ALPHA_MODE>(GLTF::Material::ALPHA_MODE_MASK) == ALPHA_MODE_MASK, "GLTF::Material::ALPHA_MODE_MASK != ALPHA_MODE_MASK");
    static_assert(static_cast<ALPHA_MODE>(GLTF::Material::ALPHA_MODE_BLEND) == ALPHA_MODE_BLEND, "GLTF::Material::ALPHA_MODE_BLEND != ALPHA_MODE_BLEND");
    static_assert(static_cast<ALPHA_MODE>(GLTF::Material::ALPHA_MODE_NUM_MODES) == ALPHA_MODE_NUM_MODES, "GLTF::Material::ALPHA_MODE_NUM_MODES != ALPHA_MODE_NUM_MODES");
    return static_cast<ALPHA_MODE>(GltfAlphaMode);
}

namespace
{

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

} // namespace

GLTF_PBR_Renderer::GLTF_PBR_Renderer(IRenderDevice*     pDevice,
                                     IRenderStateCache* pStateCache,
                                     IDeviceContext*    pCtx,
                                     const CreateInfo&  CI) :
    PBR_Renderer{pDevice, pStateCache, pCtx, CI}
{
}

void GLTF_PBR_Renderer::InitMaterialSRB(GLTF::Model&            Model,
                                        GLTF::Material&         Material,
                                        IBuffer*                pCameraAttribs,
                                        IBuffer*                pLightAttribs,
                                        IShaderResourceBinding* pMaterialSRB)
{
    if (pMaterialSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create material SRB");
        return;
    }

    InitCommonSRBVars(pMaterialSRB, pCameraAttribs, pLightAttribs);

    auto SetTexture = [&](Uint32 TexAttribId, ITextureView* pDefaultTexSRV, const char* VarName) //
    {
        RefCntAutoPtr<ITextureView> pTexSRV;

        auto TexIdx = Material.TextureIds[TexAttribId];
        if (TexIdx >= 0)
        {
            if (auto* pTexture = Model.GetTexture(TexIdx))
            {
                if (pTexture->GetDesc().Type == RESOURCE_DIM_TEX_2D_ARRAY)
                    pTexSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                else
                {
                    TextureViewDesc SRVDesc;
                    SRVDesc.ViewType   = TEXTURE_VIEW_SHADER_RESOURCE;
                    SRVDesc.TextureDim = RESOURCE_DIM_TEX_2D_ARRAY;
                    pTexture->CreateView(SRVDesc, &pTexSRV);
                }
            }
        }

        if (pTexSRV == nullptr)
            pTexSRV = pDefaultTexSRV;

        if (auto* pVar = pMaterialSRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
            pVar->Set(pTexSRV);
    };

    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::BaseColorTextureName) == GLTF::DefaultBaseColorTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::MetallicRoughnessTextureName) == GLTF::DefaultMetallicRoughnessTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::NormalTextureName) == GLTF::DefaultNormalTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::OcclusionTextureName) == GLTF::DefaultOcclusionTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::EmissiveTextureName) == GLTF::DefaultEmissiveTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::DiffuseTextureName) == GLTF::DefaultDiffuseTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttibuteIndex(GLTF::SpecularGlossinessTextureName) == GLTF::DefaultSpecularGlossinessTextureAttibId);

    SetTexture(GLTF::DefaultBaseColorTextureAttribId, m_pWhiteTexSRV, "g_ColorMap");
    SetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId, m_pDefaultPhysDescSRV, "g_PhysicalDescriptorMap");
    SetTexture(GLTF::DefaultNormalTextureAttribId, m_pDefaultNormalMapSRV, "g_NormalMap");
    if (m_Settings.UseAO)
    {
        SetTexture(GLTF::DefaultOcclusionTextureAttribId, m_pWhiteTexSRV, "g_AOMap");
    }
    if (m_Settings.UseEmissive)
    {
        SetTexture(GLTF::DefaultEmissiveTextureAttribId, m_pBlackTexSRV, "g_EmissiveMap");
    }
}

void GLTF_PBR_Renderer::CreateResourceCacheSRB(IRenderDevice*           pDevice,
                                               IDeviceContext*          pCtx,
                                               ResourceCacheUseInfo&    CacheUseInfo,
                                               IBuffer*                 pCameraAttribs,
                                               IBuffer*                 pLightAttribs,
                                               IPipelineState*          pPSO,
                                               IShaderResourceBinding** ppCacheSRB)
{
    DEV_CHECK_ERR(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null");

    pPSO->CreateShaderResourceBinding(ppCacheSRB, true);
    IShaderResourceBinding* const pSRB = *ppCacheSRB;
    if (pSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create an SRB");
        return;
    }

    InitCommonSRBVars(pSRB, pCameraAttribs, pLightAttribs);

    auto SetTexture = [&](TEXTURE_FORMAT Fmt, const char* VarName) //
    {
        if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
        {
            if (auto* pTexture = CacheUseInfo.pResourceMgr->UpdateTexture(Fmt, pDevice, pCtx))
            {
                pVar->Set(pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            }
        }
    };

    SetTexture(CacheUseInfo.BaseColorFormat, "g_ColorMap");
    SetTexture(CacheUseInfo.PhysicalDescFormat, "g_PhysicalDescriptorMap");
    SetTexture(CacheUseInfo.NormalFormat, "g_NormalMap");
    if (m_Settings.UseAO)
    {
        SetTexture(CacheUseInfo.OcclusionFormat, "g_AOMap");
    }
    if (m_Settings.UseEmissive)
    {
        SetTexture(CacheUseInfo.EmissiveFormat, "g_EmissiveMap");
    }
}

GLTF_PBR_Renderer::ModelResourceBindings GLTF_PBR_Renderer::CreateResourceBindings(
    GLTF::Model& GLTFModel,
    IBuffer*     pCameraAttribs,
    IBuffer*     pLightAttribs)
{
    ModelResourceBindings ResourceBindings;
    ResourceBindings.MaterialSRB.resize(GLTFModel.Materials.size());
    for (size_t mat = 0; mat < GLTFModel.Materials.size(); ++mat)
    {
        auto& pMatSRB = ResourceBindings.MaterialSRB[mat];
        CreateResourceBinding(&pMatSRB);
        InitMaterialSRB(GLTFModel, GLTFModel.Materials[mat], pCameraAttribs, pLightAttribs, pMatSRB);
    }
    return ResourceBindings;
}

void GLTF_PBR_Renderer::Begin(IDeviceContext* pCtx)
{
    if (m_JointsBuffer)
    {
        // In next-gen backends, dynamic buffers must be mapped before the first use in every frame
        MapHelper<float4x4> pJoints{pCtx, m_JointsBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
    }
}

void GLTF_PBR_Renderer::Begin(IRenderDevice*         pDevice,
                              IDeviceContext*        pCtx,
                              ResourceCacheUseInfo&  CacheUseInfo,
                              ResourceCacheBindings& Bindings,
                              IBuffer*               pCameraAttribs,
                              IBuffer*               pLightAttribs,
                              IPipelineState*        pPSO)
{
    VERIFY(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null.");
    VERIFY(CacheUseInfo.VtxLayoutKey != GLTF::ResourceManager::VertexLayoutKey{}, "Vertex layout key must not be null.");

    Begin(pCtx);

    if (pPSO == nullptr)
        pPSO = GetPSO(PSOKey{});

    auto TextureVersion = CacheUseInfo.pResourceMgr->GetTextureVersion();
    if (!Bindings.pSRB || Bindings.Version != TextureVersion)
    {
        Bindings.pSRB.Release();
        CreateResourceCacheSRB(pDevice, pCtx, CacheUseInfo, pCameraAttribs, pLightAttribs, pPSO, &Bindings.pSRB);
        if (!Bindings.pSRB)
        {
            LOG_ERROR_MESSAGE("Failed to create an SRB for GLTF resource cache");
            return;
        }
        Bindings.Version = TextureVersion;
    }

    pCtx->TransitionShaderResources(pPSO, Bindings.pSRB);

    if (auto* pVertexPool = CacheUseInfo.pResourceMgr->GetVertexPool(CacheUseInfo.VtxLayoutKey))
    {
        const auto& PoolDesc = pVertexPool->GetDesc();

        std::array<IBuffer*, 8> pVBs; // Do not zero-initialize
        for (Uint32 i = 0; i < PoolDesc.NumElements; ++i)
        {
            pVBs[i] = pVertexPool->Update(i, pDevice, pCtx);
            if ((pVBs[i]->GetDesc().BindFlags & BIND_VERTEX_BUFFER) == 0)
                pVBs[i] = nullptr;
        }

        pCtx->SetVertexBuffers(0, PoolDesc.NumElements, pVBs.data(), nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    }

    auto* pIndexBuffer = CacheUseInfo.pResourceMgr->UpdateIndexBuffer(pDevice, pCtx);
    pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GLTF_PBR_Renderer::Render(IDeviceContext*              pCtx,
                               const GLTF::Model&           GLTFModel,
                               const GLTF::ModelTransforms& Transforms,
                               const RenderInfo&            RenderParams,
                               ModelResourceBindings*       pModelBindings,
                               ResourceCacheBindings*       pCacheBindings)
{
    DEV_CHECK_ERR((pModelBindings != nullptr) ^ (pCacheBindings != nullptr), "Either model bindings or cache bindings must not be null");
    DEV_CHECK_ERR(pModelBindings == nullptr || pModelBindings->MaterialSRB.size() == GLTFModel.Materials.size(),
                  "The number of material shader resource bindings is not consistent with the number of materials");

    if (!GLTFModel.CompatibleWithTransforms(Transforms))
    {
        DEV_ERROR("Model transforms are incompatible with the model");
        return;
    }
    if (RenderParams.SceneIndex >= GLTFModel.Scenes.size())
    {
        DEV_ERROR("Invalid scene index ", RenderParams.SceneIndex);
        return;
    }
    const auto& Scene = GLTFModel.Scenes[RenderParams.SceneIndex];

    m_RenderParams = RenderParams;

    if (pModelBindings != nullptr)
    {
        std::array<IBuffer*, 2> pVBs =
            {
                GLTFModel.GetVertexBuffer(GLTF::Model::VERTEX_BUFFER_ID_BASIC_ATTRIBS),
                GLTFModel.GetVertexBuffer(GLTF::Model::VERTEX_BUFFER_ID_SKIN_ATTRIBS) //
            };
        pCtx->SetVertexBuffers(0, static_cast<Uint32>(pVBs.size()), pVBs.data(), nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

        if (auto* pIndexBuffer = GLTFModel.GetIndexBuffer())
        {
            pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    const auto FirstIndexLocation = GLTFModel.GetFirstIndexLocation();
    const auto BaseVertex         = GLTFModel.GetBaseVertex();

    const std::array<GLTF::Material::ALPHA_MODE, 3> AlphaModes //
        {
            GLTF::Material::ALPHA_MODE_OPAQUE, // Opaque primitives - first
            GLTF::Material::ALPHA_MODE_MASK,   // Alpha-masked primitives - second
            GLTF::Material::ALPHA_MODE_BLEND,  // Transparent primitives - last (TODO: depth sorting)
        };

    IPipelineState*         pCurrPSO = nullptr;
    IShaderResourceBinding* pCurrSRB = nullptr;
    PSOKey                  CurrPSOKey;

    for (auto AlphaMode : AlphaModes)
    {
        for (const auto* pNode : Scene.LinearNodes)
        {
            VERIFY_EXPR(pNode != nullptr);
            const auto& Node = *pNode;
            if (!Node.pMesh)
                continue;

            const auto& Mesh             = *Node.pMesh;
            const auto& NodeGlobalMatrix = Transforms.NodeGlobalMatrices[Node.Index];

            // Render mesh primitives
            for (const auto& primitive : Mesh.Primitives)
            {
                const auto& material = GLTFModel.Materials[primitive.MaterialId];
                if (material.Attribs.AlphaMode != AlphaMode)
                    continue;

                const PSOKey Key{GltfAlphaModeToAlphaMode(AlphaMode), material.DoubleSided};

                if (Key != CurrPSOKey)
                {
                    CurrPSOKey = Key;
                    pCurrPSO   = nullptr;
                }
                if (pCurrPSO == nullptr)
                {
                    pCurrPSO = GetPSO(CurrPSOKey);
                    VERIFY_EXPR(pCurrPSO != nullptr);
                    pCtx->SetPipelineState(pCurrPSO);
                    pCurrSRB = nullptr;
                }
                else
                {
                    VERIFY_EXPR(pCurrPSO == GetPSO(PSOKey{GltfAlphaModeToAlphaMode(AlphaMode), material.DoubleSided}));
                }

                if (pModelBindings != nullptr)
                {
                    VERIFY(primitive.MaterialId < pModelBindings->MaterialSRB.size(),
                           "Material index is out of bounds. This mostl likely indicates that shader resources were initialized for a different model.");

                    IShaderResourceBinding* const pSRB = pModelBindings->MaterialSRB[primitive.MaterialId];
                    DEV_CHECK_ERR(pSRB != nullptr, "Unable to find SRB for GLTF material.");
                    if (pCurrSRB != pSRB)
                    {
                        pCurrSRB = pSRB;
                        pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }
                }
                else
                {
                    VERIFY_EXPR(pCacheBindings != nullptr);
                    if (pCurrSRB != pCacheBindings->pSRB)
                    {
                        pCurrSRB = pCacheBindings->pSRB;
                        pCtx->CommitShaderResources(pCurrSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }
                }

                size_t JointCount = 0;
                if (Node.SkinTransformsIndex >= 0 && Node.SkinTransformsIndex < static_cast<int>(Transforms.Skins.size()))
                {
                    const auto& JointMatrices = Transforms.Skins[Node.SkinTransformsIndex].JointMatrices;

                    JointCount = JointMatrices.size();
                    if (JointCount > m_Settings.MaxJointCount)
                    {
                        LOG_WARNING_MESSAGE("The number of joints in the mesh (", JointCount, ") exceeds the maximum number (", m_Settings.MaxJointCount,
                                            ") reserved in the buffer. Increase MaxJointCount when initializing the renderer.");
                        JointCount = m_Settings.MaxJointCount;
                    }

                    if (JointCount != 0)
                    {
                        MapHelper<float4x4> pJoints{pCtx, m_JointsBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
                        memcpy(pJoints, JointMatrices.data(), JointCount * sizeof(float4x4));
                    }
                }

                {
                    MapHelper<HLSL::PBRShaderAttribs> pAttribs{pCtx, m_PBRAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

                    pAttribs->Transforms.NodeMatrix = NodeGlobalMatrix * RenderParams.ModelTransform;
                    pAttribs->Transforms.JointCount = static_cast<int>(JointCount);


                    static_assert(sizeof(pAttribs->Material) == sizeof(material.Attribs),
                                  "The sizeof(PBRMaterialShaderInfo) is inconsistent with sizeof(GLTF::Material::ShaderAttribs)");
                    memcpy(&pAttribs->Material, &material.Attribs, sizeof(material.Attribs));
                    static_assert(static_cast<PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_METALL_ROUGH) == PBR_WORKFLOW_METALL_ROUGH, "GLTF::Material::PBR_WORKFLOW_METALL_ROUGH != PBR_WORKFLOW_METALL_ROUGH");
                    static_assert(static_cast<PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS) == PBR_WORKFLOW_SPEC_GLOSS, "GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS != PBR_WORKFLOW_SPEC_GLOSS");

                    auto& RendererParams = pAttribs->Renderer;

                    RendererParams.DebugViewType            = static_cast<int>(m_RenderParams.DebugView);
                    RendererParams.OcclusionStrength        = m_RenderParams.OcclusionStrength;
                    RendererParams.EmissionScale            = m_RenderParams.EmissionScale;
                    RendererParams.AverageLogLum            = m_RenderParams.AverageLogLum;
                    RendererParams.MiddleGray               = m_RenderParams.MiddleGray;
                    RendererParams.WhitePoint               = m_RenderParams.WhitePoint;
                    RendererParams.IBLScale                 = m_RenderParams.IBLScale;
                    RendererParams.PrefilteredCubeMipLevels = m_Settings.UseIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
                }

                if (primitive.HasIndices())
                {
                    DrawIndexedAttribs drawAttrs{primitive.IndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    drawAttrs.FirstIndexLocation = FirstIndexLocation + primitive.FirstIndex;
                    drawAttrs.BaseVertex         = BaseVertex;
                    pCtx->DrawIndexed(drawAttrs);
                }
                else
                {
                    DrawAttribs drawAttrs{primitive.VertexCount, DRAW_FLAG_VERIFY_ALL};
                    drawAttrs.StartVertexLocation = BaseVertex;
                    pCtx->Draw(drawAttrs);
                }
            }
        }
    }
}

} // namespace Diligent
