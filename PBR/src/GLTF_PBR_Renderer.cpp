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
#include <functional>

#include "BasicMath.hpp"
#include "MapHelper.hpp"
#include "GraphicsAccessories.hpp"
#include "GLTFLoader.hpp"

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

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

namespace
{

// clang-format off
static constexpr std::array<PBR_Renderer::CreateInfo::ShaderTextureAttribIndex, 5> DefaultShaderTextureAttribIndices =
{
    PBR_Renderer::CreateInfo::ShaderTextureAttribIndex{"BaseColorTextureAttribId",          GLTF::DefaultBaseColorTextureAttribId},
    PBR_Renderer::CreateInfo::ShaderTextureAttribIndex{"PhysicalDescriptorTextureAttribId", GLTF::DefaultMetallicRoughnessTextureAttribId},
    PBR_Renderer::CreateInfo::ShaderTextureAttribIndex{"NormalTextureAttribId",             GLTF::DefaultNormalTextureAttribId},
    PBR_Renderer::CreateInfo::ShaderTextureAttribIndex{"OcclusionTextureAttribId",          GLTF::DefaultOcclusionTextureAttribId},
    PBR_Renderer::CreateInfo::ShaderTextureAttribIndex{"EmissiveTextureAttribId",           GLTF::DefaultEmissiveTextureAttribId}
};
// clang-format on

struct PBRRendererCreateInfoWrapper
{
    PBRRendererCreateInfoWrapper(const PBR_Renderer::CreateInfo& _CI) :
        CI{_CI}
    {
        if (CI.InputLayout.NumElements == 0)
        {
            InputLayout    = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), GLTF::DefaultVertexAttributes.size());
            CI.InputLayout = InputLayout;
        }

        if (CI.NumShaderTextureAttribs == 0)
        {
            CI.pShaderTextureAttribIndices = DefaultShaderTextureAttribIndices.data();
            CI.NumShaderTextureAttribs     = static_cast<Uint32>(DefaultShaderTextureAttribIndices.size());
        }
    }

    operator const PBR_Renderer::CreateInfo &() const
    {
        return CI;
    }

    PBR_Renderer::CreateInfo CI;
    InputLayoutDescX         InputLayout;
};

} // namespace
GLTF_PBR_Renderer::GLTF_PBR_Renderer(IRenderDevice*     pDevice,
                                     IRenderStateCache* pStateCache,
                                     IDeviceContext*    pCtx,
                                     const CreateInfo&  CI) :
    PBR_Renderer{pDevice, pStateCache, pCtx, PBRRendererCreateInfoWrapper{CI}}
{
    m_SupportedPSOFlags |=
        PSO_FLAG_USE_VERTEX_COLORS |
        PSO_FLAG_USE_VERTEX_NORMALS |
        PSO_FLAG_USE_TEXCOORD0 |
        PSO_FLAG_USE_TEXCOORD1 |
        PSO_FLAG_USE_JOINTS |
        PSO_FLAG_USE_COLOR_MAP |
        PSO_FLAG_USE_NORMAL_MAP |
        PSO_FLAG_USE_PHYS_DESC_MAP;

    if (CI.EnableAO)
        m_SupportedPSOFlags |= PSO_FLAG_USE_AO_MAP;
    if (CI.EnableEmissive)
        m_SupportedPSOFlags |= PSO_FLAG_USE_EMISSIVE_MAP;
    if (CI.EnableIBL)
        m_SupportedPSOFlags |= PSO_FLAG_USE_IBL;

    m_SupportedPSOFlags |=
        PSO_FLAG_USE_TEXTURE_ATLAS |
        PSO_FLAG_CONVERT_OUTPUT_TO_SRGB |
        PSO_FLAG_ENABLE_TONE_MAPPING;

    {
        GraphicsPipelineDesc GraphicsDesc;
        GraphicsDesc.NumRenderTargets                     = 1;
        GraphicsDesc.RTVFormats[0]                        = CI.RTVFmt;
        GraphicsDesc.DSVFormat                            = CI.DSVFmt;
        GraphicsDesc.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        GraphicsDesc.RasterizerDesc.FrontCounterClockwise = CI.FrontCounterClockwise;

        m_PbrPSOCache = GetPsoCacheAccessor(GraphicsDesc);

        GraphicsDesc.RasterizerDesc.FillMode = FILL_MODE_WIREFRAME;

        m_WireframePSOCache = GetPsoCacheAccessor(GraphicsDesc);
    }
}

void GLTF_PBR_Renderer::InitMaterialSRB(GLTF::Model&            Model,
                                        GLTF::Material&         Material,
                                        IBuffer*                pFrameAttribs,
                                        IShaderResourceBinding* pMaterialSRB)
{
    if (pMaterialSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create material SRB");
        return;
    }

    InitCommonSRBVars(pMaterialSRB, pFrameAttribs);

    auto SetTexture = [&](Uint32 TexAttribId, ITextureView* pDefaultTexSRV, const char* VarName) //
    {
        RefCntAutoPtr<ITextureView> pTexSRV;

        auto TexIdx = Material.GetTextureId(TexAttribId);
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

    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::BaseColorTextureName) == GLTF::DefaultBaseColorTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::MetallicRoughnessTextureName) == GLTF::DefaultMetallicRoughnessTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::NormalTextureName) == GLTF::DefaultNormalTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::OcclusionTextureName) == GLTF::DefaultOcclusionTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::EmissiveTextureName) == GLTF::DefaultEmissiveTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::DiffuseTextureName) == GLTF::DefaultDiffuseTextureAttribId);
    VERIFY_EXPR(Model.GetTextureAttributeIndex(GLTF::SpecularGlossinessTextureName) == GLTF::DefaultSpecularGlossinessTextureAttibId);

    SetTexture(GLTF::DefaultBaseColorTextureAttribId, m_pWhiteTexSRV, "g_ColorMap");
    SetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId, m_pDefaultPhysDescSRV, "g_PhysicalDescriptorMap");
    SetTexture(GLTF::DefaultNormalTextureAttribId, m_pDefaultNormalMapSRV, "g_NormalMap");
    if (m_Settings.EnableAO)
    {
        SetTexture(GLTF::DefaultOcclusionTextureAttribId, m_pWhiteTexSRV, "g_AOMap");
    }
    if (m_Settings.EnableEmissive)
    {
        SetTexture(GLTF::DefaultEmissiveTextureAttribId, m_pBlackTexSRV, "g_EmissiveMap");
    }
}

void GLTF_PBR_Renderer::CreateResourceCacheSRB(IRenderDevice*           pDevice,
                                               IDeviceContext*          pCtx,
                                               ResourceCacheUseInfo&    CacheUseInfo,
                                               IBuffer*                 pFrameAttribs,
                                               IShaderResourceBinding** ppCacheSRB)
{
    DEV_CHECK_ERR(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null");

    m_ResourceSignature->CreateShaderResourceBinding(ppCacheSRB, true);
    IShaderResourceBinding* const pSRB = *ppCacheSRB;
    if (pSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create an SRB");
        return;
    }

    InitCommonSRBVars(pSRB, pFrameAttribs);

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
    if (m_Settings.EnableAO)
    {
        SetTexture(CacheUseInfo.OcclusionFormat, "g_AOMap");
    }
    if (m_Settings.EnableEmissive)
    {
        SetTexture(CacheUseInfo.EmissiveFormat, "g_EmissiveMap");
    }
}

GLTF_PBR_Renderer::ModelResourceBindings GLTF_PBR_Renderer::CreateResourceBindings(
    GLTF::Model& GLTFModel,
    IBuffer*     pFrameAttribs)
{
    ModelResourceBindings ResourceBindings;
    ResourceBindings.MaterialSRB.resize(GLTFModel.Materials.size());
    for (size_t mat = 0; mat < GLTFModel.Materials.size(); ++mat)
    {
        auto& pMatSRB = ResourceBindings.MaterialSRB[mat];
        CreateResourceBinding(&pMatSRB);
        InitMaterialSRB(GLTFModel, GLTFModel.Materials[mat], pFrameAttribs, pMatSRB);
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
                              IBuffer*               pFrameAttribs)
{
    VERIFY(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null.");
    VERIFY(CacheUseInfo.VtxLayoutKey != GLTF::ResourceManager::VertexLayoutKey{}, "Vertex layout key must not be null.");

    Begin(pCtx);

    auto TextureVersion = CacheUseInfo.pResourceMgr->GetTextureVersion();
    if (!Bindings.pSRB || Bindings.Version != TextureVersion)
    {
        Bindings.pSRB.Release();
        CreateResourceCacheSRB(pDevice, pCtx, CacheUseInfo, pFrameAttribs, &Bindings.pSRB);
        if (!Bindings.pSRB)
        {
            LOG_ERROR_MESSAGE("Failed to create an SRB for GLTF resource cache");
            return;
        }
        Bindings.Version = TextureVersion;
    }

    pCtx->TransitionShaderResources(Bindings.pSRB);

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
        std::array<IBuffer*, 8> pVBs;

        const auto NumVBs = static_cast<Uint32>(GLTFModel.GetVertexBufferCount());
        VERIFY_EXPR(NumVBs <= pVBs.size());
        for (Uint32 i = 0; i < NumVBs; ++i)
            pVBs[i] = GLTFModel.GetVertexBuffer(i);
        pCtx->SetVertexBuffers(0, NumVBs, pVBs.data(), nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

        if (auto* pIndexBuffer = GLTFModel.GetIndexBuffer())
        {
            pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    auto VertexAttribFlags = PSO_FLAG_NONE;
    for (Uint32 i = 0; i < GLTFModel.GetNumVertexAttributes(); ++i)
    {
        if (!GLTFModel.IsVertexAttributeEnabled(i))
            continue;
        const auto& Attrib = GLTFModel.GetVertexAttribute(i);
        if (strcmp(Attrib.Name, GLTF::PositionAttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_NONE; // Position is always enabled
        else if (strcmp(Attrib.Name, GLTF::NormalAttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_USE_VERTEX_NORMALS;
        else if (strcmp(Attrib.Name, GLTF::Texcoord0AttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_USE_TEXCOORD0;
        else if (strcmp(Attrib.Name, GLTF::Texcoord1AttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_USE_TEXCOORD1;
        else if (strcmp(Attrib.Name, GLTF::JointsAttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_USE_JOINTS;
        else if (strcmp(Attrib.Name, GLTF::VertexColorAttributeName) == 0)
            VertexAttribFlags |= PSO_FLAG_USE_VERTEX_COLORS;
    }

    auto PSOFlags = RenderParams.Flags & m_SupportedPSOFlags & (VertexAttribFlags | ~PSO_FLAG_VERTEX_ATTRIBS);
    if (RenderParams.Wireframe)
        PSOFlags |= PSO_FLAG_UNSHADED;

    for (auto& List : m_RenderLists)
        List.clear();

    for (const auto* pNode : Scene.LinearNodes)
    {
        VERIFY_EXPR(pNode != nullptr);
        if (pNode->pMesh == nullptr)
            continue;

        for (const auto& primitive : pNode->pMesh->Primitives)
        {
            if (primitive.VertexCount == 0 && primitive.IndexCount == 0)
                continue;

            const auto& Material  = GLTFModel.Materials[primitive.MaterialId];
            const auto  AlphaMode = Material.Attribs.AlphaMode;
            if ((RenderParams.AlphaModes & (1u << AlphaMode)) == 0)
                continue;

            m_RenderLists[AlphaMode].emplace_back(primitive, *pNode);
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
    PSOKey                  CurrPsoKey;

    for (auto AlphaMode : AlphaModes)
    {
        const auto& RenderList = m_RenderLists[AlphaMode];
        for (const auto& PrimRI : RenderList)
        {
            const auto& Node             = PrimRI.Node;
            const auto& primitive        = PrimRI.Primitive;
            const auto& material         = GLTFModel.Materials[primitive.MaterialId];
            const auto& NodeGlobalMatrix = Transforms.NodeGlobalMatrices[Node.Index];

            const PSOKey NewKey{PSOFlags, GltfAlphaModeToAlphaMode(AlphaMode), material.DoubleSided, RenderParams.DebugView};
            if (NewKey != CurrPsoKey)
            {
                CurrPsoKey = NewKey;
                pCurrPSO   = nullptr;
            }

            if (pCurrPSO == nullptr)
            {
                pCurrPSO = (RenderParams.Wireframe ? m_WireframePSOCache : m_PbrPSOCache).Get(NewKey, true);
                VERIFY_EXPR(pCurrPSO != nullptr);
                pCtx->SetPipelineState(pCurrPSO);
            }
            else
            {
                VERIFY_EXPR(pCurrPSO == (RenderParams.Wireframe ? m_WireframePSOCache : m_PbrPSOCache).Get(NewKey, false));
            }

            if (pModelBindings != nullptr)
            {
                VERIFY(primitive.MaterialId < pModelBindings->MaterialSRB.size(),
                       "Material index is out of bounds. This most likely indicates that shader resources were initialized for a different model.");

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
                void* pAttribsData = nullptr;
                pCtx->MapBuffer(m_PBRPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, pAttribsData);
                if (pAttribsData != nullptr)
                {
                    static_assert(static_cast<PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_METALL_ROUGH) == PBR_WORKFLOW_METALL_ROUGH, "GLTF::Material::PBR_WORKFLOW_METALL_ROUGH != PBR_WORKFLOW_METALL_ROUGH");
                    static_assert(static_cast<PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS) == PBR_WORKFLOW_SPEC_GLOSS, "GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS != PBR_WORKFLOW_SPEC_GLOSS");

                    static_assert(sizeof(HLSL::PBRMaterialBasicAttribs) == sizeof(material.Attribs),
                                  "The sizeof(HLSL::PBRMaterialBasicAttribs) is inconsistent with sizeof(GLTF::Material::ShaderAttribs)");
                    static_assert(sizeof(HLSL::PBRMaterialTextureAttribs) == sizeof(GLTF::Material::TextureShaderAttribs),
                                  "The sizeof(HLSL::PBRMaterialTextureAttribs) is inconsistent with sizeof(GLTF::Material::TextureShaderAttribs)");

                    auto* pEndPtr = WritePBRPrimitiveShaderAttribs(pAttribsData, NodeGlobalMatrix * RenderParams.ModelTransform, static_cast<Uint32>(JointCount),
                                                                   reinterpret_cast<const HLSL::PBRMaterialBasicAttribs&>(material.Attribs),
                                                                   reinterpret_cast<const HLSL::PBRMaterialTextureAttribs*>(&material.GetTextureAttrib(0)),
                                                                   material.GetNumTextureAttribs());

                    VERIFY(reinterpret_cast<uint8_t*>(pEndPtr) <= static_cast<uint8_t*>(pAttribsData) + m_PBRPrimitiveAttribsCB->GetDesc().Size,
                           "Not enough space in the buffer to store primitive attributes");

                    pCtx->UnmapBuffer(m_PBRPrimitiveAttribsCB, MAP_WRITE);
                }
                else
                {
                    UNEXPECTED("Unable to map the buffer");
                }
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

} // namespace Diligent
