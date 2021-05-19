/*
 *  Copyright 2019-2021 Diligent Graphics LLC
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

#include <cstring>
#include <array>

#include "GLTF_PBR_Renderer.hpp"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "CommonlyUsedStates.h"
#include "HashUtils.hpp"
#include "ShaderMacroHelper.hpp"
#include "BasicMath.hpp"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "GraphicsAccessories.hpp"

namespace Diligent
{

const SamplerDesc GLTF_PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

GLTF_PBR_Renderer::GLTF_PBR_Renderer(IRenderDevice*    pDevice,
                                     IDeviceContext*   pCtx,
                                     const CreateInfo& CI) :
    m_Settings{CI}
{
    if (m_Settings.UseIBL)
    {
        PrecomputeBRDF(pDevice, pCtx);

        TextureDesc TexDesc;
        TexDesc.Name      = "Irradiance cube map for GLTF renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_CUBE;
        TexDesc.Usage     = USAGE_DEFAULT;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        TexDesc.Width     = IrradianceCubeDim;
        TexDesc.Height    = IrradianceCubeDim;
        TexDesc.Format    = IrradianceCubeFmt;
        TexDesc.ArraySize = 6;
        TexDesc.MipLevels = 0;

        RefCntAutoPtr<ITexture> IrradainceCubeTex;
        pDevice->CreateTexture(TexDesc, nullptr, &IrradainceCubeTex);
        m_pIrradianceCubeSRV = IrradainceCubeTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name   = "Prefiltered environment map for GLTF renderer";
        TexDesc.Width  = PrefilteredEnvMapDim;
        TexDesc.Height = PrefilteredEnvMapDim;
        TexDesc.Format = PrefilteredEnvMapFmt;
        RefCntAutoPtr<ITexture> PrefilteredEnvMapTex;
        pDevice->CreateTexture(TexDesc, nullptr, &PrefilteredEnvMapTex);
        m_pPrefilteredEnvMapSRV = PrefilteredEnvMapTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    {
        static constexpr Uint32 TexDim = 8;

        TextureDesc TexDesc;
        TexDesc.Name      = "White texture for GLTF renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        TexDesc.Usage     = USAGE_IMMUTABLE;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE;
        TexDesc.Width     = TexDim;
        TexDesc.Height    = TexDim;
        TexDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
        TexDesc.MipLevels = 1;
        std::vector<Uint32>     Data(TexDim * TexDim, 0xFFFFFFFF);
        TextureSubResData       Level0Data{Data.data(), TexDim * 4};
        TextureData             InitData{&Level0Data, 1};
        RefCntAutoPtr<ITexture> pWhiteTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
        m_pWhiteTexSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Black texture for GLTF renderer";
        for (auto& c : Data) c = 0;
        RefCntAutoPtr<ITexture> pBlackTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pBlackTex);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default normal map for GLTF renderer";
        for (auto& c : Data) c = 0x00FF7F7F;
        RefCntAutoPtr<ITexture> pDefaultNormalMap;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultNormalMap);
        m_pDefaultNormalMapSRV = pDefaultNormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default physical description map for GLTF renderer";
        for (auto& c : Data) c = 0x0000FF00;
        RefCntAutoPtr<ITexture> pDefaultPhysDesc;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultPhysDesc);
        m_pDefaultPhysDescSRV = pDefaultPhysDesc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        // clang-format off
        StateTransitionDesc Barriers[] = 
        {
            {pWhiteTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
            {pBlackTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
            {pDefaultNormalMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
            {pDefaultPhysDesc,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true} 
        };
        // clang-format on
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        RefCntAutoPtr<ISampler> pDefaultSampler;
        pDevice->CreateSampler(Sam_LinearClamp, &pDefaultSampler);
        m_pWhiteTexSRV->SetSampler(pDefaultSampler);
        m_pBlackTexSRV->SetSampler(pDefaultSampler);
        m_pDefaultNormalMapSRV->SetSampler(pDefaultSampler);
    }

    if (CI.RTVFmt != TEX_FORMAT_UNKNOWN || CI.DSVFmt != TEX_FORMAT_UNKNOWN)
    {
        CreateUniformBuffer(pDevice, sizeof(GLTFNodeShaderTransforms), "GLTF node transforms CB", &m_TransformsCB);
        CreateUniformBuffer(pDevice, sizeof(GLTFMaterialShaderInfo) + sizeof(GLTFRendererShaderParameters), "GLTF attribs CB", &m_GLTFAttribsCB);
        CreateUniformBuffer(pDevice, static_cast<Uint32>(sizeof(float4x4) * m_Settings.MaxJointCount), "GLTF joint transforms", &m_JointsBuffer);

        // clang-format off
        StateTransitionDesc Barriers[] = 
        {
            {m_TransformsCB,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true},
            {m_GLTFAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true},
            {m_JointsBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true}
        };
        // clang-format on
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        CreatePSO(pDevice);
    }
}

void GLTF_PBR_Renderer::PrecomputeBRDF(IRenderDevice*  pDevice,
                                       IDeviceContext* pCtx)
{
    TextureDesc TexDesc;
    TexDesc.Name      = "GLTF BRDF Look-up texture";
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage     = USAGE_DEFAULT;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width     = BRDF_LUT_Dim;
    TexDesc.Height    = BRDF_LUT_Dim;
    TexDesc.Format    = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels = 1;
    RefCntAutoPtr<ITexture> pBRDF_LUT;
    pDevice->CreateTexture(TexDesc, nullptr, &pBRDF_LUT);
    m_pBRDF_LUT_SRV = pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<IPipelineState> PrecomputeBRDF_PSO;
    {
        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Precompute GLTF BRDF LUT PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = TexDesc.Format;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "FullScreenTriangleVS";
            ShaderCI.Desc.Name       = "Full screen triangle VS";
            ShaderCI.FilePath        = "FullScreenTriangleVS.fx";
            pDevice->CreateShader(ShaderCI, &pVS);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "PrecomputeBRDF_PS";
            ShaderCI.Desc.Name       = "Precompute GLTF BRDF PS";
            ShaderCI.FilePath        = "PrecomputeGLTF_BRDF.psh";
            pDevice->CreateShader(ShaderCI, &pPS);
        }

        // Finally, create the pipeline state
        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;
        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &PrecomputeBRDF_PSO);
    }
    pCtx->SetPipelineState(PrecomputeBRDF_PSO);

    ITextureView* pRTVs[] = {pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    pCtx->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attrs(3, DRAW_FLAG_VERIFY_ALL);
    pCtx->Draw(attrs);

    // clang-format off
    StateTransitionDesc Barriers[] =
    {
        {pBRDF_LUT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
}

void GLTF_PBR_Renderer::CreatePSO(IRenderDevice* pDevice)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    PSODesc.Name         = "Render GLTF PBR PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipeline.NumRenderTargets                     = 1;
    GraphicsPipeline.RTVFormats[0]                        = m_Settings.RTVFmt;
    GraphicsPipeline.DSVFormat                            = m_Settings.DSVFmt;
    GraphicsPipeline.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = m_Settings.FrontCCW;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.UseCombinedTextureSamplers = true;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    ShaderMacroHelper Macros;
    Macros.AddShaderMacro("MAX_JOINT_COUNT", m_Settings.MaxJointCount);
    Macros.AddShaderMacro("ALLOW_DEBUG_VIEW", m_Settings.AllowDebugView);
    Macros.AddShaderMacro("TONE_MAPPING_MODE", "TONE_MAPPING_MODE_UNCHARTED2");
    Macros.AddShaderMacro("GLTF_PBR_USE_IBL", m_Settings.UseIBL);
    Macros.AddShaderMacro("GLTF_PBR_USE_AO", m_Settings.UseAO);
    Macros.AddShaderMacro("GLTF_PBR_USE_EMISSIVE", m_Settings.UseEmissive);
    Macros.AddShaderMacro("USE_TEXTURE_ATLAS", m_Settings.UseTextureAtals);
    Macros.AddShaderMacro("PBR_WORKFLOW_METALLIC_ROUGHNESS", GLTF::Material::PBR_WORKFLOW_METALL_ROUGH);
    Macros.AddShaderMacro("PBR_WORKFLOW_SPECULAR_GLOSINESS", GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS);
    Macros.AddShaderMacro("GLTF_ALPHA_MODE_OPAQUE", GLTF::Material::ALPHA_MODE_OPAQUE);
    Macros.AddShaderMacro("GLTF_ALPHA_MODE_MASK", GLTF::Material::ALPHA_MODE_MASK);
    Macros.AddShaderMacro("GLTF_ALPHA_MODE_BLEND", GLTF::Material::ALPHA_MODE_BLEND);
    ShaderCI.Macros = Macros;
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "GLTF PBR VS";
        ShaderCI.FilePath        = "RenderGLTF_PBR.vsh";
        pDevice->CreateShader(ShaderCI, &pVS);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "GLTF PBR PS";
        ShaderCI.FilePath        = "RenderGLTF_PBR.psh";
        pDevice->CreateShader(ShaderCI, &pPS);
    }

    // clang-format off
    LayoutElement Inputs[] =
    {
        {0, 0, 3, VT_FLOAT32},   //float3 Pos     : ATTRIB0;
        {1, 0, 3, VT_FLOAT32},   //float3 Normal  : ATTRIB1;
        {2, 0, 2, VT_FLOAT32},   //float2 UV0     : ATTRIB2;
        {3, 0, 2, VT_FLOAT32},   //float2 UV1     : ATTRIB3;
        {4, 1, 4, VT_FLOAT32},   //float4 Joint0  : ATTRIB4;
        {5, 1, 4, VT_FLOAT32}    //float4 Weight0 : ATTRIB5;
    };
    // clang-format on
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = Inputs;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements    = _countof(Inputs);

    PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    // clang-format off
    std::vector<ShaderResourceVariableDesc> Vars = 
    {
        {SHADER_TYPE_VERTEX, "cbTransforms",      SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL,  "cbGLTFAttribs",     SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_VERTEX, "cbJointTransforms", SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
    };
    // clang-format on

    std::vector<ImmutableSamplerDesc> ImtblSamplers;
    // clang-format off
    if (m_Settings.UseImmutableSamplers)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_ColorMap",              m_Settings.ColorMapImmutableSampler);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_PhysicalDescriptorMap", m_Settings.PhysDescMapImmutableSampler);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_NormalMap",             m_Settings.NormalMapImmutableSampler);
    }
    // clang-format on

    if (m_Settings.UseAO)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_AOMap", m_Settings.AOMapImmutableSampler);
    }

    if (m_Settings.UseEmissive)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_EmissiveMap", m_Settings.EmissiveMapImmutableSampler);
    }

    if (m_Settings.UseIBL)
    {
        Vars.emplace_back(SHADER_TYPE_PIXEL, "g_BRDF_LUT", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        // clang-format off
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_BRDF_LUT",          Sam_LinearClamp);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_IrradianceMap",     Sam_LinearClamp);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap", Sam_LinearClamp);
        // clang-format on
    }

    PSODesc.ResourceLayout.NumVariables         = static_cast<Uint32>(Vars.size());
    PSODesc.ResourceLayout.Variables            = Vars.data();
    PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Uint32>(ImtblSamplers.size());
    PSODesc.ResourceLayout.ImmutableSamplers    = !ImtblSamplers.empty() ? ImtblSamplers.data() : nullptr;

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    {
        PSOKey Key{GLTF::Material::ALPHA_MODE_OPAQUE, false};

        RefCntAutoPtr<IPipelineState> pSingleSidedOpaquePSO;
        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pSingleSidedOpaquePSO);
        AddPSO(Key, std::move(pSingleSidedOpaquePSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        RefCntAutoPtr<IPipelineState> pDobleSidedOpaquePSO;
        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pDobleSidedOpaquePSO);
        AddPSO(Key, std::move(pDobleSidedOpaquePSO));
    }

    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;

    auto& RT0          = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable    = true;
    RT0.SrcBlend       = BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp        = BLEND_OPERATION_ADD;
    RT0.SrcBlendAlpha  = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.DestBlendAlpha = BLEND_FACTOR_ZERO;
    RT0.BlendOpAlpha   = BLEND_OPERATION_ADD;

    {
        PSOKey Key{GLTF::Material::ALPHA_MODE_BLEND, false};

        RefCntAutoPtr<IPipelineState> pSingleSidedBlendPSO;
        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pSingleSidedBlendPSO);
        AddPSO(Key, std::move(pSingleSidedBlendPSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        RefCntAutoPtr<IPipelineState> pDoubleSidedBlendPSO;
        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pDoubleSidedBlendPSO);
        AddPSO(Key, std::move(pDoubleSidedBlendPSO));
    }

    for (auto& PSO : m_PSOCache)
    {
        if (m_Settings.UseIBL)
        {
            PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BRDF_LUT")->Set(m_pBRDF_LUT_SRV);
        }
        // clang-format off
        PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransforms")->Set(m_TransformsCB);
        PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbGLTFAttribs")->Set(m_GLTFAttribsCB);
        PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbJointTransforms")->Set(m_JointsBuffer);
        // clang-format on
    }
}

void GLTF_PBR_Renderer::InitCommonSRBVars(IShaderResourceBinding* pSRB,
                                          IBuffer*                pCameraAttribs,
                                          IBuffer*                pLightAttribs)
{
    VERIFY_EXPR(pSRB != nullptr);

    if (pCameraAttribs != nullptr)
    {
        if (auto* pCameraAttribsVSVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs"))
            pCameraAttribsVSVar->Set(pCameraAttribs);

        if (auto* pCameraAttribsPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbCameraAttribs"))
            pCameraAttribsPSVar->Set(pCameraAttribs);
    }

    if (pLightAttribs != nullptr)
    {
        if (auto* pLightAttribsPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbLightAttribs"))
            pLightAttribsPSVar->Set(pLightAttribs);
    }

    if (m_Settings.UseIBL)
    {
        if (auto* pIrradianceMapPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap"))
            pIrradianceMapPSVar->Set(m_pIrradianceCubeSRV);

        if (auto* pPrefilteredEnvMap = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap"))
            pPrefilteredEnvMap->Set(m_pPrefilteredEnvMapSRV);
    }
}


void GLTF_PBR_Renderer::CreateMaterialSRB(GLTF::Model&             Model,
                                          GLTF::Material&          Material,
                                          IBuffer*                 pCameraAttribs,
                                          IBuffer*                 pLightAttribs,
                                          IPipelineState*          pPSO,
                                          IShaderResourceBinding** ppMaterialSRB)
{
    if (pPSO == nullptr)
        pPSO = GetPSO(PSOKey{});

    pPSO->CreateShaderResourceBinding(ppMaterialSRB, true);
    auto* const pSRB = *ppMaterialSRB;
    if (pSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create material SRB");
        return;
    }

    InitCommonSRBVars(pSRB, pCameraAttribs, pLightAttribs);

    auto SetTexture = [&](GLTF::Material::TEXTURE_ID TexId, ITextureView* pDefaultTexSRV, const char* VarName) //
    {
        RefCntAutoPtr<ITextureView> pTexSRV;

        auto TexIdx = Material.TextureIds[TexId];
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

        if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
            pVar->Set(pTexSRV);
    };

    SetTexture(GLTF::Material::TEXTURE_ID_BASE_COLOR, m_pWhiteTexSRV, "g_ColorMap");
    SetTexture(GLTF::Material::TEXTURE_ID_PHYSICAL_DESC, m_pDefaultPhysDescSRV, "g_PhysicalDescriptorMap");
    SetTexture(GLTF::Material::TEXTURE_ID_NORMAL_MAP, m_pDefaultNormalMapSRV, "g_NormalMap");
    if (m_Settings.UseAO)
    {
        SetTexture(GLTF::Material::TEXTURE_ID_OCCLUSION, m_pWhiteTexSRV, "g_AOMap");
    }
    if (m_Settings.UseEmissive)
    {
        SetTexture(GLTF::Material::TEXTURE_ID_EMISSIVE, m_pBlackTexSRV, "g_EmissiveMap");
    }
}

void GLTF_PBR_Renderer::CreateResourceCacheSRB(IRenderDevice*              pDevice,
                                               IDeviceContext*             pCtx,
                                               GLTF::ResourceCacheUseInfo& CacheUseInfo,
                                               IBuffer*                    pCameraAttribs,
                                               IBuffer*                    pLightAttribs,
                                               IPipelineState*             pPSO,
                                               IShaderResourceBinding**    ppCacheSRB)
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
            if (auto* pTexture = CacheUseInfo.pResourceMgr->GetTexture(Fmt, pDevice, pCtx))
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

void GLTF_PBR_Renderer::PrecomputeCubemaps(IRenderDevice*  pDevice,
                                           IDeviceContext* pCtx,
                                           ITextureView*   pEnvironmentMap)
{
    if (!m_Settings.UseIBL)
    {
        LOG_WARNING_MESSAGE("IBL is disabled, so precomputing cube maps will have no effect");
        return;
    }

    struct PrecomputeEnvMapAttribs
    {
        float4x4 Rotation;

        float Roughness;
        float EnvMapDim;
        uint  NumSamples;
        float Dummy;
    };

    if (!m_PrecomputeEnvMapAttribsCB)
    {
        CreateUniformBuffer(pDevice, sizeof(PrecomputeEnvMapAttribs), "Precompute env map attribs CB", &m_PrecomputeEnvMapAttribsCB);
    }

    if (!m_pPrecomputeIrradianceCubePSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_PHI_SAMPLES", 64);
        Macros.AddShaderMacro("NUM_THETA_SAMPLES", 32);
        ShaderCI.Macros = Macros;
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Cubemap face VS";
            ShaderCI.FilePath        = "CubemapFace.vsh";
            pDevice->CreateShader(ShaderCI, &pVS);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Precompute irradiance cube map PS";
            ShaderCI.FilePath        = "ComputeIrradianceMap.psh";
            pDevice->CreateShader(ShaderCI, &pPS);
        }

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Precompute irradiance cube PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = IrradianceCubeFmt;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        // clang-format off
        ShaderResourceVariableDesc Vars[] = 
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumVariables = _countof(Vars);
        PSODesc.ResourceLayout.Variables    = Vars;

        // clang-format off
        ImmutableSamplerDesc ImtblSamplers[] =
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;

        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPrecomputeIrradianceCubePSO);
        m_pPrecomputeIrradianceCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrecomputeIrradianceCubePSO->CreateShaderResourceBinding(&m_pPrecomputeIrradianceCubeSRB, true);
    }

    if (!m_pPrefilterEnvMapPSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("OPTIMIZE_SAMPLES", 1);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Cubemap face VS";
            ShaderCI.FilePath        = "CubemapFace.vsh";
            pDevice->CreateShader(ShaderCI, &pVS);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Prefilter environment map PS";
            ShaderCI.FilePath        = "PrefilterEnvMap.psh";
            pDevice->CreateShader(ShaderCI, &pPS);
        }

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Prefilter environment map PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = PrefilteredEnvMapFmt;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        // clang-format off
        ShaderResourceVariableDesc Vars[] = 
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumVariables = _countof(Vars);
        PSODesc.ResourceLayout.Variables    = Vars;

        // clang-format off
        ImmutableSamplerDesc ImtblSamplers[] =
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;

        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPrefilterEnvMapPSO);
        m_pPrefilterEnvMapPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrefilterEnvMapPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FilterAttribs")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrefilterEnvMapPSO->CreateShaderResourceBinding(&m_pPrefilterEnvMapSRB, true);
    }


    // clang-format off
	const std::array<float4x4, 6> Matrices =
    {
/* +X */ float4x4::RotationY(+PI_F / 2.f),
/* -X */ float4x4::RotationY(-PI_F / 2.f),
/* +Y */ float4x4::RotationX(-PI_F / 2.f),
/* -Y */ float4x4::RotationX(+PI_F / 2.f),
/* +Z */ float4x4::Identity(),
/* -Z */ float4x4::RotationY(PI_F)
	};
    // clang-format on

    pCtx->SetPipelineState(m_pPrecomputeIrradianceCubePSO);
    m_pPrecomputeIrradianceCubeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(m_pPrecomputeIrradianceCubeSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto*       pIrradianceCube    = m_pIrradianceCubeSRV->GetTexture();
    const auto& IrradianceCubeDesc = pIrradianceCube->GetDesc();
    for (Uint32 mip = 0; mip < IrradianceCubeDesc.MipLevels; ++mip)
    {
        for (Uint32 face = 0; face < 6; ++face)
        {
            TextureViewDesc RTVDesc(TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY);
            RTVDesc.Name            = "RTV for irradiance cube texture";
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pIrradianceCube->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs(pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);
                Attribs->Rotation = Matrices[face];
            }
            DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
            pCtx->Draw(drawAttrs);
        }
    }

    pCtx->SetPipelineState(m_pPrefilterEnvMapPSO);
    m_pPrefilterEnvMapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(m_pPrefilterEnvMapSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto*       pPrefilteredEnvMap    = m_pPrefilteredEnvMapSRV->GetTexture();
    const auto& PrefilteredEnvMapDesc = pPrefilteredEnvMap->GetDesc();
    for (Uint32 mip = 0; mip < PrefilteredEnvMapDesc.MipLevels; ++mip)
    {
        for (Uint32 face = 0; face < 6; ++face)
        {
            TextureViewDesc RTVDesc(TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY);
            RTVDesc.Name            = "RTV for prefiltered env map cube texture";
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pPrefilteredEnvMap->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs(pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);
                Attribs->Rotation   = Matrices[face];
                Attribs->Roughness  = static_cast<float>(mip) / static_cast<float>(PrefilteredEnvMapDesc.MipLevels);
                Attribs->EnvMapDim  = static_cast<float>(PrefilteredEnvMapDesc.Width);
                Attribs->NumSamples = 256;
            }

            DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
            pCtx->Draw(drawAttrs);
        }
    }

    // clang-format off
    StateTransitionDesc Barriers[] = 
    {
        {m_pPrefilteredEnvMapSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
        {m_pIrradianceCubeSRV->GetTexture(),    RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

    // To avoid crashes on some low-end Android devices
    pCtx->Flush();
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
        CreateMaterialSRB(GLTFModel, GLTFModel.Materials[mat], pCameraAttribs, pLightAttribs, nullptr, &ResourceBindings.MaterialSRB[mat]);
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

void GLTF_PBR_Renderer::Begin(IRenderDevice*              pDevice,
                              IDeviceContext*             pCtx,
                              GLTF::ResourceCacheUseInfo& CacheUseInfo,
                              ResourceCacheBindings&      Bindings,
                              IBuffer*                    pCameraAttribs,
                              IBuffer*                    pLightAttribs,
                              IPipelineState*             pPSO)
{
    VERIFY_EXPR(CacheUseInfo.pResourceMgr != nullptr);

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

    std::array<Uint32, 2>   Offsets = {};
    std::array<IBuffer*, 2> pVBs =
        {
            CacheUseInfo.pResourceMgr->GetBuffer(CacheUseInfo.VertexBuffer0Idx, pDevice, pCtx),
            CacheUseInfo.pResourceMgr->GetBuffer(CacheUseInfo.VertexBuffer1Idx, pDevice, pCtx) //
        };
    pCtx->SetVertexBuffers(0, static_cast<Uint32>(pVBs.size()), pVBs.data(), Offsets.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

    auto* pIndexBuffer = CacheUseInfo.pResourceMgr->GetBuffer(CacheUseInfo.IndexBufferIdx, pDevice, pCtx);
    pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GLTF_PBR_Renderer::Render(IDeviceContext*        pCtx,
                               GLTF::Model&           GLTFModel,
                               const RenderInfo&      RenderParams,
                               ModelResourceBindings* pModelBindings,
                               ResourceCacheBindings* pCacheBindings)
{
    DEV_CHECK_ERR((pModelBindings != nullptr) ^ (pCacheBindings != nullptr), "Either model bindings or cache bindings must not be null");
    DEV_CHECK_ERR(pModelBindings == nullptr || pModelBindings->MaterialSRB.size() == GLTFModel.Materials.size(),
                  "The number of material shader resource bindings is not consistent with the number of materials");

    m_RenderParams = RenderParams;

    if (pModelBindings != nullptr)
    {
        std::array<Uint32, 2>   Offsets = {};
        std::array<IBuffer*, 2> pVBs =
            {
                GLTFModel.GetBuffer(GLTF::Model::BUFFER_ID_VERTEX_BASIC_ATTRIBS),
                GLTFModel.GetBuffer(GLTF::Model::BUFFER_ID_VERTEX_SKIN_ATTRIBS) //
            };
        pCtx->SetVertexBuffers(0, static_cast<Uint32>(pVBs.size()), pVBs.data(), Offsets.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

        if (auto* pIndexBuffer = GLTFModel.GetBuffer(GLTF::Model::BUFFER_ID_INDEX))
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

    const GLTF::Mesh*       pLastAnimatedMesh = nullptr;
    IPipelineState*         pCurrPSO          = nullptr;
    IShaderResourceBinding* pCurrSRB          = nullptr;
    PSOKey                  CurrPSOKey;

    for (auto AlphaMode : AlphaModes)
    {
        for (const auto* pNode : GLTFModel.LinearNodes)
        {
            if (!pNode->pMesh)
                continue;

            const auto& Mesh = *pNode->pMesh;

            // Render mesh primitives
            for (const auto& primitive : Mesh.Primitives)
            {
                const auto& material = GLTFModel.Materials[primitive.MaterialId];
                if (material.Attribs.AlphaMode != AlphaMode)
                    continue;

                const PSOKey Key{AlphaMode, material.DoubleSided};
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
                    VERIFY_EXPR(pCurrPSO == GetPSO(PSOKey{AlphaMode, material.DoubleSided}));
                }

                if (pModelBindings != nullptr)
                {
                    VERIFY(primitive.MaterialId < pModelBindings->MaterialSRB.size(),
                           "Material index is out of bounds. This mostl likely indicates that shader resources were initialized for a different model.");

                    IShaderResourceBinding* const pSRB = pModelBindings->MaterialSRB[primitive.MaterialId].RawPtr<IShaderResourceBinding>();
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

                size_t JointCount = Mesh.Transforms.jointMatrices.size();
                if (JointCount > m_Settings.MaxJointCount)
                {
                    LOG_WARNING_MESSAGE("The number of joints in the mesh (", JointCount, ") exceeds the maximum number (", m_Settings.MaxJointCount,
                                        ") reserved in the buffer. Increase MaxJointCount when initializing the renderer.");
                    JointCount = m_Settings.MaxJointCount;
                }

                {
                    MapHelper<GLTFNodeShaderTransforms> pTransforms{pCtx, m_TransformsCB, MAP_WRITE, MAP_FLAG_DISCARD};
                    pTransforms->NodeMatrix = Mesh.Transforms.matrix * RenderParams.ModelTransform;
                    pTransforms->JointCount = static_cast<int>(JointCount);
                }

                if (JointCount != 0 && pLastAnimatedMesh != &Mesh)
                {
                    MapHelper<float4x4> pJoints{pCtx, m_JointsBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
                    memcpy(pJoints, Mesh.Transforms.jointMatrices.data(), JointCount * sizeof(float4x4));
                    pLastAnimatedMesh = &Mesh;
                }

                {
                    struct GLTFAttribs
                    {
                        GLTFRendererShaderParameters  RenderParameters;
                        GLTF::Material::ShaderAttribs MaterialInfo;
                        static_assert(sizeof(GLTFMaterialShaderInfo) == sizeof(GLTF::Material::ShaderAttribs),
                                      "The sizeof(GLTFMaterialShaderInfo) is inconsistent with sizeof(GLTF::Material::ShaderAttribs)");
                    };
                    static_assert(sizeof(GLTFAttribs) <= 256, "Size of dynamic GLTFAttribs buffer exceeds 256 bytes. "
                                                              "It may be worth trying to reduce the size or just live with it.");

                    MapHelper<GLTFAttribs> pGLTFAttribs{pCtx, m_GLTFAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

                    pGLTFAttribs->MaterialInfo = material.Attribs;

                    auto& ShaderParams = pGLTFAttribs->RenderParameters;

                    ShaderParams.DebugViewType            = static_cast<int>(m_RenderParams.DebugView);
                    ShaderParams.OcclusionStrength        = m_RenderParams.OcclusionStrength;
                    ShaderParams.EmissionScale            = m_RenderParams.EmissionScale;
                    ShaderParams.AverageLogLum            = m_RenderParams.AverageLogLum;
                    ShaderParams.MiddleGray               = m_RenderParams.MiddleGray;
                    ShaderParams.WhitePoint               = m_RenderParams.WhitePoint;
                    ShaderParams.IBLScale                 = m_RenderParams.IBLScale;
                    ShaderParams.PrefilteredCubeMipLevels = m_Settings.UseIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
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
