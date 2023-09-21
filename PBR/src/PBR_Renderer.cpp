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
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

const SamplerDesc PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

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
    m_Settings{CI}
{
    if (m_Settings.UseIBL)
    {
        PrecomputeBRDF(pDevice, pStateCache, pCtx, m_Settings.NumBRDFSamples);

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

        RefCntAutoPtr<ITexture> IrradainceCubeTex;
        pDevice->CreateTexture(TexDesc, nullptr, &IrradainceCubeTex);
        m_pIrradianceCubeSRV = IrradainceCubeTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name   = "Prefiltered environment map for PBR renderer";
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
        TexDesc.Name      = "White texture for PBR renderer";
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

        TexDesc.Name = "Black texture for PBR renderer";
        for (auto& c : Data) c = 0;
        RefCntAutoPtr<ITexture> pBlackTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pBlackTex);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default normal map for PBR renderer";
        for (auto& c : Data) c = 0x00FF7F7F;
        RefCntAutoPtr<ITexture> pDefaultNormalMap;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultNormalMap);
        m_pDefaultNormalMapSRV = pDefaultNormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default physical description map for PBR renderer";
        for (auto& c : Data) c = 0x0000FF00;
        RefCntAutoPtr<ITexture> pDefaultPhysDesc;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultPhysDesc);
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

        RefCntAutoPtr<ISampler> pDefaultSampler;
        pDevice->CreateSampler(Sam_LinearClamp, &pDefaultSampler);
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

    CreateSignature(pDevice, pStateCache);

    if (CI.RTVFmt != TEX_FORMAT_UNKNOWN || CI.DSVFmt != TEX_FORMAT_UNKNOWN)
    {
        CreatePSO(pDevice, pStateCache);
    }
}

PBR_Renderer::~PBR_Renderer()
{
}

void PBR_Renderer::PrecomputeBRDF(IRenderDevice*     pDevice,
                                  IRenderStateCache* pStateCache,
                                  IDeviceContext*    pCtx,
                                  Uint32             NumBRDFSamples)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    TextureDesc TexDesc;
    TexDesc.Name      = "BRDF Look-up texture";
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage     = USAGE_DEFAULT;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width     = BRDF_LUT_Dim;
    TexDesc.Height    = BRDF_LUT_Dim;
    TexDesc.Format    = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels = 1;
    auto pBRDF_LUT    = Device.CreateTexture(TexDesc);
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

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute BRDF PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "PrecomputeBRDF_PS";
            ShaderCI.FilePath   = "PrecomputeBRDF.psh";

            pPS = Device.CreateShader(ShaderCI);
        }

        // Finally, create the pipeline state
        PSOCreateInfo.pVS  = pVS;
        PSOCreateInfo.pPS  = pPS;
        PrecomputeBRDF_PSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
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

void PBR_Renderer::PrecomputeCubemaps(IRenderDevice*     pDevice,
                                      IRenderStateCache* pStateCache,
                                      IDeviceContext*    pCtx,
                                      ITextureView*      pEnvironmentMap,
                                      Uint32             NumPhiSamples,
                                      Uint32             NumThetaSamples,
                                      bool               OptimizeSamples)
{
    if (!m_Settings.UseIBL)
    {
        LOG_WARNING_MESSAGE("IBL is disabled, so precomputing cube maps will have no effect");
        return;
    }

    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

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

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute irradiance cube map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "ComputeIrradianceMap.psh";

            pPS = Device.CreateShader(ShaderCI);
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

        m_pPrecomputeIrradianceCubePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
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

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Prefilter environment map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "PrefilterEnvMap.psh";

            pPS = Device.CreateShader(ShaderCI);
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

        m_pPrefilterEnvMapPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
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

    if (m_Settings.UseIBL)
    {
        if (auto* pIrradianceMapPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap"))
            pIrradianceMapPSVar->Set(m_pIrradianceCubeSRV);

        if (auto* pPrefilteredEnvMap = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap"))
            pPrefilteredEnvMap->Set(m_pPrefilteredEnvMapSRV);
    }
}

void PBR_Renderer::CreateSignature(IRenderDevice* pDevice, IRenderStateCache* pStateCache)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

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

    if (m_Settings.UseAO)
    {
        AddTextureAndSampler("g_AOMap", m_Settings.AOMapImmutableSampler);
    }

    if (m_Settings.UseEmissive)
    {
        AddTextureAndSampler("g_EmissiveMap", m_Settings.EmissiveMapImmutableSampler);
    }

    if (m_Settings.UseIBL)
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

    m_ResourceSignature = Device.CreatePipelineResourceSignature(SignatureDesc);

    if (m_Settings.UseIBL)
    {
        m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BRDF_LUT")->Set(m_pBRDF_LUT_SRV);
    }
    m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPBRAttribs")->Set(m_PBRAttribsCB);
    if (m_Settings.MaxJointCount > 0)
    {
        m_ResourceSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbJointTransforms")->Set(m_JointsBuffer);
    }
}

ShaderMacroHelper PBR_Renderer::DefineMacros() const
{
    ShaderMacroHelper Macros;
    Macros.Add("MAX_JOINT_COUNT", static_cast<int>(m_Settings.MaxJointCount));
    Macros.Add("ALLOW_DEBUG_VIEW", m_Settings.AllowDebugView);
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", m_Settings.ConvertOutputToSRGB);
    Macros.Add("TONE_MAPPING_MODE", "TONE_MAPPING_MODE_UNCHARTED2");
    Macros.Add("PBR_USE_IBL", m_Settings.UseIBL);
    Macros.Add("PBR_USE_AO", m_Settings.UseAO);
    Macros.Add("PBR_USE_EMISSIVE", m_Settings.UseEmissive);
    Macros.Add("USE_TEXTURE_ATLAS", m_Settings.UseTextureAtlas);
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

    Macros.Add("TEX_COLOR_CONVERSION_MODE_NONE", CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE);
    Macros.Add("TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR", CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR);
    Macros.Add("TEX_COLOR_CONVERSION_MODE", m_Settings.TexColorConversionMode);

    return Macros;
}

InputLayoutDescX PBR_Renderer::GetInputLayout() const
{
    InputLayoutDescX InputLayout;
    if (m_Settings.InputLayout.NumElements != 0)
    {
        InputLayout = m_Settings.InputLayout;
    }
    else
    {
        InputLayout
            .Add(0u, 0u, 3u, VT_FLOAT32)  //float3 Pos     : ATTRIB0;
            .Add(1u, 0u, 3u, VT_FLOAT32)  //float3 Normal  : ATTRIB1;
            .Add(2u, 0u, 2u, VT_FLOAT32)  //float2 UV0     : ATTRIB2;
            .Add(3u, 0u, 2u, VT_FLOAT32); //float2 UV1     : ATTRIB3;
        if (m_Settings.MaxJointCount > 0)
        {
            InputLayout
                .Add(4u, 1u, 4u, VT_FLOAT32)  //float4 Joint0  : ATTRIB4;
                .Add(5u, 1u, 4u, VT_FLOAT32); //float4 Weight0 : ATTRIB5;
        }
    }

    return InputLayout;
}

void PBR_Renderer::CreatePSO(IRenderDevice* pDevice, IRenderStateCache* pStateCache)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    PSODesc.Name         = "Render PBR PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipeline.NumRenderTargets                     = 1;
    GraphicsPipeline.RTVFormats[0]                        = m_Settings.RTVFmt;
    GraphicsPipeline.DSVFormat                            = m_Settings.DSVFmt;
    GraphicsPipeline.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = m_Settings.FrontCCW;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    const auto Macros = DefineMacros();
    ShaderCI.Macros   = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"PBR VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = m_Settings.MaxJointCount > 0 ? "VSMainSkinned" : "VSMain";
        ShaderCI.FilePath   = "RenderPBR.vsh";

        pVS = Device.CreateShader(ShaderCI);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"PBR PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "RenderPBR.psh";

        pPS = Device.CreateShader(ShaderCI);
    }

    const auto InputLayout                     = GetInputLayout();
    PSOCreateInfo.GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[] = {m_ResourceSignature};
    PSOCreateInfo.ppResourceSignatures         = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount      = _countof(ppSignatures);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    {
        PSOKey Key{ALPHA_MODE_OPAQUE, false};

        auto pSingleSidedOpaquePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pSingleSidedOpaquePSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        auto pDobleSidedOpaquePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pDobleSidedOpaquePSO));
    }

    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;

    auto& RT0          = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable    = true;
    RT0.SrcBlend       = BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp        = BLEND_OPERATION_ADD;
    RT0.SrcBlendAlpha  = BLEND_FACTOR_ONE;
    RT0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOpAlpha   = BLEND_OPERATION_ADD;

    {
        PSOKey Key{ALPHA_MODE_BLEND, false};

        auto pSingleSidedBlendPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pSingleSidedBlendPSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        auto pDoubleSidedBlendPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pDoubleSidedBlendPSO));
    }


    if (m_Settings.EnableMeshIdRendering)
    {
        PSOCreateInfo.PSODesc.Name = "Render Mesh Id PSO";

        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        PSOCreateInfo.GraphicsPipeline.BlendDesc                  = BS_Default;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0]              = MeshIdFmt;

        RefCntAutoPtr<IShader> pMeshIdPS;
        {
            ShaderCI.Desc       = {"Mesh Id PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "RenderMeshId.psh";

            pMeshIdPS = Device.CreateShader(ShaderCI);
        }
        PSOCreateInfo.pPS = pMeshIdPS;

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;

        m_MeshIdPSO[0] = Device.CreateGraphicsPipelineState(PSOCreateInfo);

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        m_MeshIdPSO[1] = Device.CreateGraphicsPipelineState(PSOCreateInfo);
    }
}

void PBR_Renderer::AddPSO(const PSOKey& Key, RefCntAutoPtr<IPipelineState> pPSO)
{
    auto Idx = GetPSOIdx(Key);
    if (Idx >= m_PSOCache.size())
        m_PSOCache.resize(Idx + 1);
    VERIFY_EXPR(!m_PSOCache[Idx]);
    m_PSOCache[Idx] = std::move(pPSO);
}

void PBR_Renderer::CreateResourceBinding(IShaderResourceBinding** ppSRB)
{
    m_ResourceSignature->CreateShaderResourceBinding(ppSRB, true);
}

} // namespace Diligent
