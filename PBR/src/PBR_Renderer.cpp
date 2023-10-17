/*
 *  Copyright 2019-2023 Diligent Graphics LLC
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

#include "PBR_Renderer.hpp"

#include <array>
#include <vector>

#include "RenderStateCache.hpp"
#include "GraphicsUtilities.h"
#include "CommonlyUsedStates.h"
#include "BasicMath.hpp"
#include "MapHelper.hpp"
#include "GraphicsAccessories.hpp"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.h"

namespace Diligent
{

const SamplerDesc PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

const PBR_Renderer::PSO_FLAGS PBR_Renderer::PbrPSOKey::SupportedFlags       = PSO_FLAG_ALL;
const PBR_Renderer::PSO_FLAGS PBR_Renderer::WireframePSOKey::SupportedFlags = PSO_FLAG_USE_JOINTS;
const PBR_Renderer::PSO_FLAGS PBR_Renderer::MeshIdPSOKey::SupportedFlags    = PSO_FLAG_USE_JOINTS;

namespace
{

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

}

} // namespace

PBR_Renderer::PBR_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    m_InputLayout{CI.InputLayout},
    m_Settings{
        [](CreateInfo CI, const InputLayoutDesc& InputLayout) {
            CI.InputLayout = InputLayout;
            return CI;
        }(CI, m_InputLayout)},
    m_Device{pDevice, pStateCache}
{
    DEV_CHECK_ERR(m_Settings.InputLayout.NumElements != 0, "Input layout must not be empty");

    if (m_Settings.EnableIBL)
    {
        PrecomputeBRDF(pCtx, m_Settings.NumBRDFSamples);

        TextureDesc TexDesc;
        TexDesc.Name      = "Irradiance cube map for PBR renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_CUBE;
        TexDesc.Usage     = USAGE_DEFAULT;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        TexDesc.Width     = IrradianceCubeDim;
        TexDesc.Height    = IrradianceCubeDim;
        TexDesc.Format    = IrradianceCubeFmt;
        TexDesc.ArraySize = 6;
        TexDesc.MipLevels = 0;

        auto IrradainceCubeTex = m_Device.CreateTexture(TexDesc);
        m_pIrradianceCubeSRV   = IrradainceCubeTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name   = "Prefiltered environment map for PBR renderer";
        TexDesc.Width  = PrefilteredEnvMapDim;
        TexDesc.Height = PrefilteredEnvMapDim;
        TexDesc.Format = PrefilteredEnvMapFmt;

        auto PrefilteredEnvMapTex = m_Device.CreateTexture(TexDesc);
        m_pPrefilteredEnvMapSRV   = PrefilteredEnvMapTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    {
        static constexpr Uint32 TexDim = 8;

        TextureDesc TexDesc;
        TexDesc.Name      = "White texture for PBR renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        TexDesc.Usage     = USAGE_IMMUTABLE;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE;
        TexDesc.Width     = TexDim;
        TexDesc.Height    = TexDim;
        TexDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
        TexDesc.MipLevels = 1;
        std::vector<Uint32> Data(TexDim * TexDim, 0xFFFFFFFF);
        TextureSubResData   Level0Data{Data.data(), TexDim * 4};
        TextureData         InitData{&Level0Data, 1};

        auto pWhiteTex = m_Device.CreateTexture(TexDesc, &InitData);
        m_pWhiteTexSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Black texture for PBR renderer";
        for (auto& c : Data) c = 0;
        auto pBlackTex = m_Device.CreateTexture(TexDesc, &InitData);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default normal map for PBR renderer";
        for (auto& c : Data) c = 0x00FF7F7F;
        auto pDefaultNormalMap = m_Device.CreateTexture(TexDesc, &InitData);
        m_pDefaultNormalMapSRV = pDefaultNormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default physical description map for PBR renderer";
        for (auto& c : Data) c = 0x0000FF00;
        auto pDefaultPhysDesc = m_Device.CreateTexture(TexDesc, &InitData);
        m_pDefaultPhysDescSRV = pDefaultPhysDesc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        // clang-format off
        StateTransitionDesc Barriers[] = 
        {
            {pWhiteTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pBlackTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pDefaultNormalMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pDefaultPhysDesc,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
        };
        // clang-format on
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        auto pDefaultSampler = m_Device.CreateSampler(Sam_LinearClamp);
        m_pWhiteTexSRV->SetSampler(pDefaultSampler);
        m_pBlackTexSRV->SetSampler(pDefaultSampler);
        m_pDefaultNormalMapSRV->SetSampler(pDefaultSampler);
        m_pDefaultPhysDescSRV->SetSampler(pDefaultSampler);
    }

    {
        CreateUniformBuffer(pDevice, sizeof(HLSL::PBRShaderAttribs), "PBR attribs CB", &m_PBRAttribsCB);
        if (m_Settings.MaxJointCount > 0)
        {
            CreateUniformBuffer(pDevice, static_cast<Uint32>(sizeof(float4x4) * m_Settings.MaxJointCount), "PBR joint transforms", &m_JointsBuffer);
        }
        std::vector<StateTransitionDesc> Barriers;
        Barriers.emplace_back(m_PBRAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE);
        if (m_JointsBuffer)
            Barriers.emplace_back(m_JointsBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE);
        pCtx->TransitionResourceStates(static_cast<Uint32>(Barriers.size()), Barriers.data());
    }

    CreateSignature();
}

PBR_Renderer::~PBR_Renderer()
{
}

void PBR_Renderer::PrecomputeBRDF(IDeviceContext* pCtx,
                                  Uint32          NumBRDFSamples)
{
    TextureDesc TexDesc;
    TexDesc.Name      = "BRDF Look-up texture";
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage     = USAGE_DEFAULT;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width     = BRDF_LUT_Dim;
    TexDesc.Height    = BRDF_LUT_Dim;
    TexDesc.Format    = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels = 1;
    auto pBRDF_LUT    = m_Device.CreateTexture(TexDesc);
    m_pBRDF_LUT_SRV   = pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<IPipelineState> PrecomputeBRDF_PSO;
    {
        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Precompute BRDF LUT PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = TexDesc.Format;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_SAMPLES", NumBRDFSamples);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Full screen triangle VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "FullScreenTriangleVS";
            ShaderCI.FilePath   = "FullScreenTriangleVS.fx";

            pVS = m_Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute BRDF PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "PrecomputeBRDF_PS";
            ShaderCI.FilePath   = "PrecomputeBRDF.psh";

            pPS = m_Device.CreateShader(ShaderCI);
        }

        // Finally, create the pipeline state
        PSOCreateInfo.pVS  = pVS;
        PSOCreateInfo.pPS  = pPS;
        PrecomputeBRDF_PSO = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
    }
    pCtx->SetPipelineState(PrecomputeBRDF_PSO);

    ITextureView* pRTVs[] = {pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    pCtx->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attrs(3, DRAW_FLAG_VERIFY_ALL);
    pCtx->Draw(attrs);

    // clang-format off
    StateTransitionDesc Barriers[] =
    {
        {pBRDF_LUT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
}

void PBR_Renderer::PrecomputeCubemaps(IDeviceContext* pCtx,
                                      ITextureView*   pEnvironmentMap,
                                      Uint32          NumPhiSamples,
                                      Uint32          NumThetaSamples,
                                      bool            OptimizeSamples)
{
    if (!m_Settings.EnableIBL)
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
        CreateUniformBuffer(m_Device, sizeof(PrecomputeEnvMapAttribs), "Precompute env map attribs CB", &m_PrecomputeEnvMapAttribsCB);
    }

    if (!m_pPrecomputeIrradianceCubePSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_PHI_SAMPLES", static_cast<int>(NumPhiSamples));
        Macros.AddShaderMacro("NUM_THETA_SAMPLES", static_cast<int>(NumThetaSamples));
        ShaderCI.Macros = Macros;
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Cubemap face VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "CubemapFace.vsh";

            pVS = m_Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute irradiance cube map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "ComputeIrradianceMap.psh";

            pPS = m_Device.CreateShader(ShaderCI);
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

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .SetDefaultVariableType(SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp);
        PSODesc.ResourceLayout = ResourceLayout;

        m_pPrecomputeIrradianceCubePSO = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
        m_pPrecomputeIrradianceCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrecomputeIrradianceCubePSO->CreateShaderResourceBinding(&m_pPrecomputeIrradianceCubeSRB, true);
    }

    if (!m_pPrefilterEnvMapPSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("OPTIMIZE_SAMPLES", OptimizeSamples ? 1 : 0);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Cubemap face VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "CubemapFace.vsh";

            pVS = m_Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Prefilter environment map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "PrefilterEnvMap.psh";

            pPS = m_Device.CreateShader(ShaderCI);
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

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .SetDefaultVariableType(SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp);
        PSODesc.ResourceLayout = ResourceLayout;

        m_pPrefilterEnvMapPSO = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
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
            TextureViewDesc RTVDesc{"RTV for irradiance cube texture", TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY};
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pIrradianceCube->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
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
            TextureViewDesc RTVDesc{"RTV for prefiltered env map cube texture", TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY};
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pPrefilteredEnvMap->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
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
        {m_pPrefilteredEnvMapSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        {m_pIrradianceCubeSRV->GetTexture(),    RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

    // To avoid crashes on some low-end Android devices
    pCtx->Flush();
}


void PBR_Renderer::InitCommonSRBVars(IShaderResourceBinding* pSRB,
                                     IBuffer*                pCameraAttribs,
                                     IBuffer*                pLightAttribs)
{
    VERIFY_EXPR(pSRB != nullptr);

    if (pCameraAttribs != nullptr)
    {
        if (auto* pCameraAttribsVSVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs"))
            pCameraAttribsVSVar->Set(pCameraAttribs);
    }

    if (pLightAttribs != nullptr)
    {
        if (auto* pLightAttribsPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbLightAttribs"))
            pLightAttribsPSVar->Set(pLightAttribs);
    }

    if (m_Settings.EnableIBL)
    {
        if (auto* pIrradianceMapPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap"))
            pIrradianceMapPSVar->Set(m_pIrradianceCubeSRV);

        if (auto* pPrefilteredEnvMap = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap"))
            pPrefilteredEnvMap->Set(m_pPrefilteredEnvMapSRV);
    }
}

void PBR_Renderer::CreateSignature()
{
    PipelineResourceSignatureDescX SignatureDesc{"PBR Renderer Resource Signature"};
    SignatureDesc
        .SetUseCombinedTextureSamplers(true)
        .AddResource(SHADER_TYPE_VS_PS, "cbPBRAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddResource(SHADER_TYPE_VS_PS, "cbCameraAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_VS_PS, "cbLightAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    if (m_Settings.MaxJointCount > 0)
        SignatureDesc.AddResource(SHADER_TYPE_VERTEX, "cbJointTransforms", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

    auto AddTextureAndSampler = [&](const char* Name, const SamplerDesc& Desc) //
    {
        SignatureDesc.AddResource(SHADER_TYPE_PIXEL, Name, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
        if (m_Settings.UseImmutableSamplers)
            SignatureDesc.AddImmutableSampler(SHADER_TYPE_PIXEL, Name, Desc);
        else
        {
            const auto SamplerName = std::string{Name} + "_sampler";
            SignatureDesc.AddResource(SHADER_TYPE_PIXEL, SamplerName.c_str(), SHADER_RESOURCE_TYPE_SAMPLER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
        }
    };

    AddTextureAndSampler("g_ColorMap", m_Settings.ColorMapImmutableSampler);
    AddTextureAndSampler("g_NormalMap", m_Settings.NormalMapImmutableSampler);

    if (m_Settings.UseSeparateMetallicRoughnessTextures)
    {
        AddTextureAndSampler("g_MetallicMap", m_Settings.PhysDescMapImmutableSampler);
        AddTextureAndSampler("g_RoughnessMap", m_Settings.PhysDescMapImmutableSampler);
    }
    else
    {
        AddTextureAndSampler("g_PhysicalDescriptorMap", m_Settings.PhysDescMapImmutableSampler);
    }

    if (m_Settings.EnableAO)
    {
        AddTextureAndSampler("g_AOMap", m_Settings.AOMapImmutableSampler);
    }

    if (m_Settings.EnableEmissive)
    {
        AddTextureAndSampler("g_EmissiveMap", m_Settings.EmissiveMapImmutableSampler);
    }

    if (m_Settings.EnableIBL)
    {
        SignatureDesc
            .AddResource(SHADER_TYPE_PIXEL, "g_BRDF_LUT", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddResource(SHADER_TYPE_PIXEL, "g_IrradianceMap", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
            .AddResource(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

        SignatureDesc
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_BRDF_LUT", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_IrradianceMap", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap", Sam_LinearClamp);
    }

    m_ResourceSignature = m_Device.CreatePipelineResourceSignature(SignatureDesc);

    if (m_Settings.EnableIBL)
    {
        m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BRDF_LUT")->Set(m_pBRDF_LUT_SRV);
    }
    m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPBRAttribs")->Set(m_PBRAttribsCB);
    if (m_Settings.MaxJointCount > 0)
    {
        m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbJointTransforms")->Set(m_JointsBuffer);
    }
}

ShaderMacroHelper PBR_Renderer::DefineMacros(PSO_FLAGS PSOFlags) const
{
    ShaderMacroHelper Macros;
    Macros.Add("MAX_JOINT_COUNT", static_cast<int>(m_Settings.MaxJointCount));
    Macros.Add("TONE_MAPPING_MODE", "TONE_MAPPING_MODE_UNCHARTED2");
    Macros.Add("PBR_WORKFLOW_METALLIC_ROUGHNESS", PBR_WORKFLOW_METALL_ROUGH);
    Macros.Add("PBR_WORKFLOW_SPECULAR_GLOSINESS", PBR_WORKFLOW_SPEC_GLOSS);
    Macros.Add("PBR_ALPHA_MODE_OPAQUE", ALPHA_MODE_OPAQUE);
    Macros.Add("PBR_ALPHA_MODE_MASK", ALPHA_MODE_MASK);
    Macros.Add("PBR_ALPHA_MODE_BLEND", ALPHA_MODE_BLEND);
    Macros.Add("USE_IBL_ENV_MAP_LOD", true);
    Macros.Add("USE_HDR_IBL_CUBEMAPS", true);
    Macros.Add("USE_SEPARATE_METALLIC_ROUGHNESS_TEXTURES", m_Settings.UseSeparateMetallicRoughnessTextures);

    // clang-format off
    Macros.Add("DEBUG_VIEW_NONE",             static_cast<int>(DebugViewType::None));
    Macros.Add("DEBUG_VIEW_TEXCOORD0",        static_cast<int>(DebugViewType::Texcoord0));
    Macros.Add("DEBUG_VIEW_BASE_COLOR",       static_cast<int>(DebugViewType::BaseColor));
    Macros.Add("DEBUG_VIEW_TRANSPARENCY",     static_cast<int>(DebugViewType::Transparency));
    Macros.Add("DEBUG_VIEW_NORMAL_MAP",       static_cast<int>(DebugViewType::NormalMap));
    Macros.Add("DEBUG_VIEW_OCCLUSION",        static_cast<int>(DebugViewType::Occlusion));
    Macros.Add("DEBUG_VIEW_EMISSIVE",         static_cast<int>(DebugViewType::Emissive));
    Macros.Add("DEBUG_VIEW_METALLIC",         static_cast<int>(DebugViewType::Metallic));
    Macros.Add("DEBUG_VIEW_ROUGHNESS",        static_cast<int>(DebugViewType::Roughness));
    Macros.Add("DEBUG_VIEW_DIFFUSE_COLOR",    static_cast<int>(DebugViewType::DiffuseColor));
    Macros.Add("DEBUG_VIEW_SPECULAR_COLOR",   static_cast<int>(DebugViewType::SpecularColor));
    Macros.Add("DEBUG_VIEW_REFLECTANCE90",    static_cast<int>(DebugViewType::Reflectance90));
    Macros.Add("DEBUG_VIEW_MESH_NORMAL",      static_cast<int>(DebugViewType::MeshNormal));
    Macros.Add("DEBUG_VIEW_PERTURBED_NORMAL", static_cast<int>(DebugViewType::PerturbedNormal));
    Macros.Add("DEBUG_VIEW_NDOTV",            static_cast<int>(DebugViewType::NdotV));
    Macros.Add("DEBUG_VIEW_DIRECT_LIGHTING",  static_cast<int>(DebugViewType::DirectLighting));
    Macros.Add("DEBUG_VIEW_DIFFUSE_IBL",      static_cast<int>(DebugViewType::DiffuseIBL));
    Macros.Add("DEBUG_VIEW_SPECULAR_IBL",     static_cast<int>(DebugViewType::SpecularIBL));
    // clang-format on

#define ADD_PSO_FLAG_MACRO(Flag) Macros.Add(#Flag, (PSOFlags & PSO_FLAG_##Flag) != PSO_FLAG_NONE)
    ADD_PSO_FLAG_MACRO(USE_VERTEX_COLORS);
    ADD_PSO_FLAG_MACRO(USE_VERTEX_NORMALS);
    ADD_PSO_FLAG_MACRO(USE_TEXCOORD0);
    ADD_PSO_FLAG_MACRO(USE_TEXCOORD1);
    ADD_PSO_FLAG_MACRO(USE_JOINTS);

    ADD_PSO_FLAG_MACRO(USE_COLOR_MAP);
    ADD_PSO_FLAG_MACRO(USE_NORMAL_MAP);
    ADD_PSO_FLAG_MACRO(USE_METALLIC_MAP);
    ADD_PSO_FLAG_MACRO(USE_ROUGHNESS_MAP);
    ADD_PSO_FLAG_MACRO(USE_PHYS_DESC_MAP);
    ADD_PSO_FLAG_MACRO(USE_AO_MAP);
    ADD_PSO_FLAG_MACRO(USE_EMISSIVE_MAP);
    ADD_PSO_FLAG_MACRO(USE_IBL);

    //ADD_PSO_FLAG_MACRO(FRONT_CCW);
    ADD_PSO_FLAG_MACRO(ENABLE_DEBUG_VIEW);
    ADD_PSO_FLAG_MACRO(USE_TEXTURE_ATLAS);
    ADD_PSO_FLAG_MACRO(CONVERT_OUTPUT_TO_SRGB);
#undef ADD_PSO_FLAG_MACRO

    Macros.Add("TEX_COLOR_CONVERSION_MODE_NONE", CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE);
    Macros.Add("TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR", CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR);
    Macros.Add("TEX_COLOR_CONVERSION_MODE", m_Settings.TexColorConversionMode);

    return Macros;
}

void PBR_Renderer::GetVSInputStructAndLayout(PSO_FLAGS         PSOFlags,
                                             std::string&      VSInputStruct,
                                             InputLayoutDescX& InputLayout) const
{
    //struct VSInput
    //{
    //    float3 Pos     : ATTRIB0;
    //    float3 Normal  : ATTRIB1;
    //    float2 UV0     : ATTRIB2;
    //    float2 UV1     : ATTRIB3;
    //    float4 Joint0  : ATTRIB4;
    //    float4 Weight0 : ATTRIB5;
    //    float4 Color   : ATTRIB6;
    //};
    struct VSAttribInfo
    {
        const Uint32      Index;
        const char* const Name;
        const VALUE_TYPE  Type;
        const Uint32      NumComponents;
        const PSO_FLAGS   Flag;
    };
    static constexpr std::array<VSAttribInfo, 7> VSAttribs = //
        {
            // clang-format off
            VSAttribInfo{0, "Pos",     VT_FLOAT32, 3, PSO_FLAG_NONE},
            VSAttribInfo{1, "Normal",  VT_FLOAT32, 3, PSO_FLAG_USE_VERTEX_NORMALS},
            VSAttribInfo{2, "UV0",     VT_FLOAT32, 2, PSO_FLAG_USE_TEXCOORD0},
            VSAttribInfo{3, "UV1",     VT_FLOAT32, 2, PSO_FLAG_USE_TEXCOORD1},
            VSAttribInfo{4, "Joint0",  VT_FLOAT32, 4, PSO_FLAG_USE_JOINTS},
            VSAttribInfo{5, "Weight0", VT_FLOAT32, 4, PSO_FLAG_USE_JOINTS},
            VSAttribInfo{6, "Color",   VT_FLOAT32, 4, PSO_FLAG_USE_VERTEX_COLORS}
            // clang-format on
        };

    InputLayout = m_Settings.InputLayout;
    InputLayout.ResolveAutoOffsetsAndStrides();

    std::stringstream ss;
    ss << "struct VSInput" << std::endl
       << "{" << std::endl;

    for (const auto& Attrib : VSAttribs)
    {
        if (Attrib.Flag == PSO_FLAG_NONE || (PSOFlags & Attrib.Flag) != 0)
        {
#ifdef DILIGENT_DEVELOPMENT
            {
                bool AttribFound = false;
                for (Uint32 i = 0; i < InputLayout.GetNumElements(); ++i)
                {
                    const auto& Elem = InputLayout[i];
                    if (Elem.InputIndex == Attrib.Index)
                    {
                        AttribFound = true;
                        DEV_CHECK_ERR(Elem.NumComponents == Attrib.NumComponents, "Input layout element '", Attrib.Name, "' (index ", Attrib.Index, ") has ", Elem.NumComponents, " components, but shader expects ", Attrib.NumComponents);
                        DEV_CHECK_ERR(Elem.ValueType == Attrib.Type, "Input layout element '", Attrib.Name, "' (index ", Attrib.Index, ") has value type ", GetValueTypeString(Elem.ValueType), ", but shader expects ", GetValueTypeString(Attrib.Type));
                        break;
                    }
                }
                DEV_CHECK_ERR(AttribFound, "Input layout does not contain attribute '", Attrib.Name, "' (index ", Attrib.Index, ")");
            }
#endif
            VERIFY_EXPR(Attrib.Type == VT_FLOAT32);
            ss << "    " << std::setw(7) << "float" << Attrib.NumComponents << std::setw(9) << Attrib.Name << ": ATTRIB" << Attrib.Index << ";" << std::endl;
        }
        else
        {
            // Remove attribute from layout
            for (Uint32 i = 0; i < InputLayout.GetNumElements(); ++i)
            {
                const auto& Elem = InputLayout[i];
                if (Elem.InputIndex == Attrib.Index)
                {
                    InputLayout.Remove(i);
                    break;
                }
            }
        }
    }

    ss << "};" << std::endl;

    VSInputStruct = ss.str();
}

std::string PBR_Renderer::GetVSOutputStruct(PSO_FLAGS PSOFlags)
{
    // struct VSOutput
    // {
    //     float4 ClipPos  : SV_Position;
    //     float3 WorldPos : WORLD_POS;
    //     float4 Color    : COLOR;
    //     float3 Normal   : NORMAL;
    //     float2 UV0      : UV0;
    //     float2 UV1      : UV1;
    // };

    std::stringstream ss;
    ss << "struct VSOutput" << std::endl
       << "{" << std::endl
       << "    float4 ClipPos  : SV_Position;" << std::endl
       << "    float3 WorldPos : WORLD_POS;" << std::endl;
    if (PSOFlags & PSO_FLAG_USE_VERTEX_COLORS)
    {
        ss << "    float4 Color    : COLOR;" << std::endl;
    }
    if (PSOFlags & PSO_FLAG_USE_VERTEX_NORMALS)
    {
        ss << "    float3 Normal   : NORMAL;" << std::endl;
    }
    if (PSOFlags & PSO_FLAG_USE_TEXCOORD0)
    {
        ss << "    float2 UV0      : UV0;" << std::endl;
    }
    if (PSOFlags & PSO_FLAG_USE_TEXCOORD1)
    {
        ss << "    float2 UV1      : UV1;" << std::endl;
    }
    ss << "};" << std::endl;
    return ss.str();
}

void PBR_Renderer::CreateShaders(PSO_FLAGS               PSOFlags,
                                 const char*             VSPath,
                                 const char*             VSName,
                                 const char*             PSPath,
                                 const char*             PSName,
                                 RefCntAutoPtr<IShader>& pVS,
                                 RefCntAutoPtr<IShader>& pPS,
                                 InputLayoutDescX&       InputLayout)
{
    std::string VSInputStruct;
    GetVSInputStructAndLayout(PSOFlags, VSInputStruct, InputLayout);

    auto VSOutputStruct = GetVSOutputStruct(PSOFlags);

    MemoryShaderSourceFileInfo GeneratedSources[] =
        {
            MemoryShaderSourceFileInfo{"VSInputStruct.generated", VSInputStruct},
            MemoryShaderSourceFileInfo{"VSOutputStruct.generated", VSOutputStruct},
        };
    MemoryShaderSourceFactoryCreateInfo            MemorySourceFactoryCI{GeneratedSources, _countof(GeneratedSources)};
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pMemorySourceFactory;
    CreateMemoryShaderSourceFactory(MemorySourceFactoryCI, &pMemorySourceFactory);

    IShaderSourceInputStreamFactory*               ppShaderSourceFactories[] = {&DiligentFXShaderSourceStreamFactory::GetInstance(), pMemorySourceFactory};
    CompoundShaderSourceFactoryCreateInfo          CompoundSourceFactoryCI{ppShaderSourceFactories, _countof(ppShaderSourceFactories)};
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pCompoundSourceFactory;
    CreateCompoundShaderSourceFactory(CompoundSourceFactoryCI, &pCompoundSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = pCompoundSourceFactory;

    const auto Macros = DefineMacros(PSOFlags);
    ShaderCI.Macros   = Macros;

    {
        ShaderCI.Desc       = {VSName, SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = VSPath;

        pVS = m_Device.CreateShader(ShaderCI);
    }

    // Create pixel shader
    {
        ShaderCI.Desc       = {PSName, SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = PSPath;

        pPS = m_Device.CreateShader(ShaderCI);
    }
}


void PBR_Renderer::CreatePbrPSO(PbrPsoHashMapType& PbrPSOs, const GraphicsPipelineDesc& GraphicsDesc, const PbrPSOKey& Key)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    InputLayoutDescX       InputLayout;
    RefCntAutoPtr<IShader> pVS;
    RefCntAutoPtr<IShader> pPS;
    CreateShaders(Key.Flags, "RenderPBR.vsh", "PBR VS", "RenderPBR.psh", "PBR PS", pVS, pPS, InputLayout);

    GraphicsPipeline             = GraphicsDesc;
    GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[] = {m_ResourceSignature};
    PSOCreateInfo.ppResourceSignatures         = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount      = _countof(ppSignatures);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    for (auto AlphaMode : {ALPHA_MODE_OPAQUE, ALPHA_MODE_BLEND})
    {
        if (AlphaMode == ALPHA_MODE_OPAQUE)
        {
            PSOCreateInfo.GraphicsPipeline.BlendDesc = BS_Default;
        }
        else
        {
            auto& RT0          = GraphicsPipeline.BlendDesc.RenderTargets[0];
            RT0.BlendEnable    = true;
            RT0.SrcBlend       = BLEND_FACTOR_SRC_ALPHA;
            RT0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
            RT0.BlendOp        = BLEND_OPERATION_ADD;
            RT0.SrcBlendAlpha  = BLEND_FACTOR_ONE;
            RT0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
            RT0.BlendOpAlpha   = BLEND_OPERATION_ADD;
        }

        for (auto CullMode : {CULL_MODE_BACK, CULL_MODE_NONE})
        {
            std::string PSOName{"PBR PSO"};
            PSOName += (AlphaMode == ALPHA_MODE_OPAQUE ? " - opaque" : " - blend");
            PSOName += (CullMode == CULL_MODE_BACK ? " - backface culling" : " - no culling");
            PSODesc.Name = PSOName.c_str();

            GraphicsPipeline.RasterizerDesc.CullMode     = CullMode;
            const auto DoubleSided                       = CullMode == CULL_MODE_NONE;
            auto       PSO                               = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
            PbrPSOs[{Key.Flags, AlphaMode, DoubleSided}] = PSO;
            if (AlphaMode == ALPHA_MODE_BLEND)
            {
                // Mask and blend use the same PSO
                PbrPSOs[{Key.Flags, ALPHA_MODE_MASK, DoubleSided}] = PSO;
            }
        }
    }
}

void PBR_Renderer::CreateMeshIdPSO(MeshIdPsoHashMapType& MeshIdPSOs, const GraphicsPipelineDesc& GraphicsDesc, const MeshIdPSOKey& Key)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    InputLayoutDescX       InputLayout;
    RefCntAutoPtr<IShader> pVS;
    RefCntAutoPtr<IShader> pPS;
    CreateShaders(Key.Flags, "RenderPBR.vsh", "Mesh ID VS", "RenderMeshId.psh", "Mesh ID PS", pVS, pPS, InputLayout);

    GraphicsPipeline             = GraphicsDesc;
    GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[] = {m_ResourceSignature};
    PSOCreateInfo.ppResourceSignatures         = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount      = _countof(ppSignatures);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    for (auto CullMode : {CULL_MODE_BACK, CULL_MODE_NONE})
    {
        std::string PSOName{"Mesh ID PSO"};
        PSOName += (CullMode == CULL_MODE_BACK ? " - backface culling" : " - no culling");
        PSODesc.Name = PSOName.c_str();

        GraphicsPipeline.RasterizerDesc.CullMode = CullMode;
        const auto DoubleSided                   = CullMode == CULL_MODE_NONE;

        MeshIdPSOs[{Key.Flags, DoubleSided}] = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
    }
}

void PBR_Renderer::CreateWireframePSO(WireframePsoHashMapType& WireframePSOs, const GraphicsPipelineDesc& GraphicsDesc, const WireframePSOKey& Key)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    InputLayoutDescX       InputLayout;
    RefCntAutoPtr<IShader> pVS;
    RefCntAutoPtr<IShader> pPS;
    CreateShaders(Key.Flags, "RenderPBR.vsh", "Wireframe VS", "RenderWireframe.psh", "Wireframe PS", pVS, pPS, InputLayout);

    GraphicsPipeline             = GraphicsDesc;
    GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[] = {m_ResourceSignature};
    PSOCreateInfo.ppResourceSignatures         = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount      = _countof(ppSignatures);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    for (auto CullMode : {CULL_MODE_BACK, CULL_MODE_NONE})
    {
        std::string PSOName{"Wireframe PSO"};
        PSOName += (CullMode == CULL_MODE_BACK ? " - backface culling" : " - no culling");
        PSODesc.Name = PSOName.c_str();

        GraphicsPipeline.RasterizerDesc.CullMode = CullMode;
        const auto DoubleSided                   = CullMode == CULL_MODE_NONE;

        WireframePSOs[{Key.Flags, DoubleSided}] = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
    }
}

void PBR_Renderer::CreateResourceBinding(IShaderResourceBinding** ppSRB)
{
    m_ResourceSignature->CreateShaderResourceBinding(ppSRB, true);
}

PBR_Renderer::PbrPsoCacheAccessor PBR_Renderer::GetPbrPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc)
{
    auto it = m_PbrPSOs.find(GraphicsDesc);
    if (it == m_PbrPSOs.end())
        it = m_PbrPSOs.emplace(GraphicsDesc, PbrPsoHashMapType{}).first;
    return {*this, it->second, it->first};
}

PBR_Renderer::MeshIdPsoCacheAccessor PBR_Renderer::GetMeshIdPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc)
{
    auto it = m_MeshIdPSOs.find(GraphicsDesc);
    if (it == m_MeshIdPSOs.end())
        it = m_MeshIdPSOs.emplace(GraphicsDesc, MeshIdPsoHashMapType{}).first;
    return {*this, it->second, it->first};
}

PBR_Renderer::WireframePsoCacheAccessor PBR_Renderer::GetWireframePsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc)
{
    auto it = m_WireframePSOs.find(GraphicsDesc);
    if (it == m_WireframePSOs.end())
        it = m_WireframePSOs.emplace(GraphicsDesc, WireframePsoHashMapType{}).first;
    return {*this, it->second, it->first};
}

} // namespace Diligent
