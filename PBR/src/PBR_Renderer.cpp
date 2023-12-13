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
#include "../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.h"

namespace Diligent
{

const SamplerDesc PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

PBR_Renderer::PSOKey::PSOKey(PSO_FLAGS     _Flags,
                             ALPHA_MODE    _AlphaMode,
                             bool          _DoubleSided,
                             DebugViewType _DebugView) noexcept :
    Flags{_Flags},
    AlphaMode{_AlphaMode},
    DoubleSided{_DoubleSided},
    DebugView{_DebugView}
{
    if (Flags & PSO_FLAG_UNSHADED)
    {
        AlphaMode = ALPHA_MODE_OPAQUE;

        constexpr auto SupportedUnshadedFlags = PSO_FLAG_USE_JOINTS | PSO_FLAG_ALL_USER_DEFINED | PSO_FLAG_UNSHADED;
        Flags &= SupportedUnshadedFlags;

        DebugView = DebugViewType::None;
    }

    Hash = ComputeHash(Flags, AlphaMode, DoubleSided, static_cast<Uint32>(DebugView));
}

static std::vector<std::string> CopyShaderTextureAttribIndexNames(const PBR_Renderer::CreateInfo& CI)
{
    std::vector<std::string> Names(CI.NumShaderTextureAttribIndices);
    if (CI.pShaderTextureAttribIndices != nullptr)
    {
        for (Uint32 i = 0; i < CI.NumShaderTextureAttribIndices; ++i)
        {
            const char* SrcName = CI.pShaderTextureAttribIndices[i].Name;
            if (SrcName == nullptr || *SrcName == '\0')
            {
                DEV_ERROR("Shader texture attribute name must not be null or empty");
                continue;
            }
            Names[i] = SrcName;
        }
    }
    return Names;
}

static std::vector<PBR_Renderer::CreateInfo::ShaderTextureAttribIndex> CopyShaderTextureAttribIndices(const PBR_Renderer::CreateInfo& CI,
                                                                                                      const std::vector<std::string>& Names)
{
    std::vector<PBR_Renderer::CreateInfo::ShaderTextureAttribIndex> Indices;
    if (CI.pShaderTextureAttribIndices != nullptr)
    {
        Indices = {CI.pShaderTextureAttribIndices, CI.pShaderTextureAttribIndices + CI.NumShaderTextureAttribIndices};
        VERIFY_EXPR(Indices.size() == Names.size());
        for (size_t i = 0; i < Indices.size(); ++i)
        {
            Indices[i].Name = Names[i].c_str();
        }
    }
    return Indices;
}

static Uint32 GetMaxShaderTextureAttribIndex(const PBR_Renderer::CreateInfo& CI)
{
    Uint32 MaxIndex = 0;
    if (CI.pShaderTextureAttribIndices != nullptr)
    {
        for (Uint32 i = 0; i < CI.NumShaderTextureAttribIndices; ++i)
        {
            MaxIndex = std::max(MaxIndex, CI.pShaderTextureAttribIndices[i].Index);
        }
    }
    return MaxIndex;
}

PBR_Renderer::PBR_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    m_InputLayout{CI.InputLayout},
    m_ShaderTextureAttribIndexNames{CopyShaderTextureAttribIndexNames(CI)},
    m_ShaderTextureAttribIndices{CopyShaderTextureAttribIndices(CI, m_ShaderTextureAttribIndexNames)},
    m_Settings{
        [this](CreateInfo CI) {
            CI.InputLayout = m_InputLayout;
            if (CI.pShaderTextureAttribIndices != nullptr)
            {
                CI.pShaderTextureAttribIndices = m_ShaderTextureAttribIndices.data();
            }
            return CI;
        }(CI)},
    m_NumShaderTextureAttribs{GetMaxShaderTextureAttribIndex(CI) + 1},
    m_Device{pDevice, pStateCache},
    m_PBRPrimitiveAttribsCB{CI.pPrimitiveAttribsCB}
{
    DEV_CHECK_ERR(m_Settings.InputLayout.NumElements != 0, "Input layout must not be empty");
    DEV_CHECK_ERR(m_Settings.NumShaderTextureAttribIndices > 0, "The number of shader texture attribute indices must be greater than 0");

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

    if (m_Settings.CreateDefaultTextures)
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
        if (!m_PBRPrimitiveAttribsCB)
        {
            CreateUniformBuffer(pDevice, GetPBRPrimitiveAttribsSize(), "PBR primitive attribs CB", &m_PBRPrimitiveAttribsCB);
        }
        if (m_Settings.MaxJointCount > 0)
        {
            CreateUniformBuffer(pDevice, static_cast<Uint32>(sizeof(float4x4) * m_Settings.MaxJointCount), "PBR joint transforms", &m_JointsBuffer);
        }
        std::vector<StateTransitionDesc> Barriers;
        Barriers.emplace_back(m_PBRPrimitiveAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE);
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


void PBR_Renderer::InitCommonSRBVars(IShaderResourceBinding* pSRB, IBuffer* pFrameAttribs)
{
    if (pSRB == nullptr)
    {
        UNEXPECTED("SRB must not be null");
        return;
    }

    if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs"))
    {
        if (pVar->Get() == nullptr)
            pVar->Set(m_PBRPrimitiveAttribsCB);
    }

    if (m_Settings.MaxJointCount > 0)
    {
        if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbJointTransforms"))
        {
            if (pVar->Get() == nullptr)
                pVar->Set(m_JointsBuffer);
        }
    }

    if (pFrameAttribs != nullptr)
    {
        if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbFrameAttribs"))
            pVar->Set(pFrameAttribs);
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
        .AddResource(SHADER_TYPE_VS_PS, "cbFrameAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_VS_PS, "cbPrimitiveAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    if (m_Settings.MaxJointCount > 0)
        SignatureDesc.AddResource(SHADER_TYPE_VERTEX, "cbJointTransforms", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

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
}

ShaderMacroHelper PBR_Renderer::DefineMacros(PSO_FLAGS     PSOFlags,
                                             DebugViewType DebugView) const
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

    static_assert(static_cast<int>(DebugViewType::NumDebugViews) == 19, "Did you add debug view? You may need to handle it here.");
    // clang-format off
    Macros.Add("DEBUG_VIEW",                  static_cast<int>(DebugView));
    Macros.Add("DEBUG_VIEW_NONE",             static_cast<int>(DebugViewType::None));
    Macros.Add("DEBUG_VIEW_TEXCOORD0",        static_cast<int>(DebugViewType::Texcoord0));
    Macros.Add("DEBUG_VIEW_TEXCOORD1",        static_cast<int>(DebugViewType::Texcoord1));
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

    static_assert(PSO_FLAG_LAST == 1u << 18u, "Did you add new PSO Flag? You may need to handle it here.");
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
    ADD_PSO_FLAG_MACRO(USE_TEXTURE_ATLAS);
    ADD_PSO_FLAG_MACRO(ENABLE_TEXCOORD_TRANSFORM);
    ADD_PSO_FLAG_MACRO(CONVERT_OUTPUT_TO_SRGB);
    ADD_PSO_FLAG_MACRO(ENABLE_CUSTOM_DATA_OUTPUT);
    ADD_PSO_FLAG_MACRO(ENABLE_TONE_MAPPING);
    ADD_PSO_FLAG_MACRO(UNSHADED);
#undef ADD_PSO_FLAG_MACRO

    Macros.Add("TEX_COLOR_CONVERSION_MODE_NONE", CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE);
    Macros.Add("TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR", CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR);
    Macros.Add("TEX_COLOR_CONVERSION_MODE", m_Settings.TexColorConversionMode);

    Macros.Add("PBR_NUM_TEXTURE_ATTRIBUTES", static_cast<int>(m_NumShaderTextureAttribs));
    for (const auto& AttribIdx : m_ShaderTextureAttribIndices)
    {
        if (*AttribIdx.Name != '\0')
            Macros.Add(AttribIdx.Name, static_cast<int>(AttribIdx.Index));
    }

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

std::string PBR_Renderer::GetVSOutputStruct(PSO_FLAGS PSOFlags, bool UseVkPointSize)
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
    if (UseVkPointSize)
    {
        ss << "    [[vk::builtin(\"PointSize\")]] float PointSize : PSIZE;" << std::endl;
    }
    ss << "};" << std::endl;
    return ss.str();
}

std::string PBR_Renderer::GetPSOutputStruct(PSO_FLAGS PSOFlags)
{
    // struct PSOutput
    // {
    //     float4 Color      : SV_Target0;
    //     float4 CustomData : SV_Target1;
    // };

    std::stringstream ss;
    ss << "struct PSOutput" << std::endl
       << "{" << std::endl
       << "    float4 Color      : SV_Target0;" << std::endl;
    if (PSOFlags & PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT)
    {
        ss << "    float4 CustomData : SV_Target1;" << std::endl;
    }
    ss << "};" << std::endl;
    return ss.str();
}

static constexpr char DefaultPSMainSource[] = R"(
void main(in VSOutput VSOut,
        in bool IsFrontFace : SV_IsFrontFace,
        out PSOutput PSOut)
{
#if UNSHADED
    PSOut.Color = g_Frame.Renderer.UnshadedColor + g_Frame.Renderer.HighlightColor;
#else
    PSOut.Color = ComputePbrSurfaceColor(VSOut, IsFrontFace);
#endif
 
#if ENABLE_CUSTOM_DATA_OUTPUT
    {
        PSOut.CustomData = g_Primitive.CustomData;
    }
#endif
}
)";

void PBR_Renderer::CreatePSO(PsoHashMapType& PsoHashMap, const GraphicsPipelineDesc& GraphicsDesc, const PSOKey& Key)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    const auto PSOFlags   = Key.GetFlags();
    const auto IsUnshaded = (PSOFlags & PSO_FLAG_UNSHADED) != 0;

    InputLayoutDescX InputLayout;
    std::string      VSInputStruct;
    GetVSInputStructAndLayout(PSOFlags, VSInputStruct, InputLayout);

    const bool UseVkPointSize = GraphicsDesc.PrimitiveTopology == PRIMITIVE_TOPOLOGY_POINT_LIST && m_Device.GetDeviceInfo().IsVulkanDevice();
    const auto VSOutputStruct = GetVSOutputStruct(PSOFlags, UseVkPointSize);

    std::string PSMainSource;
    if (m_Settings.GetPSMainSource)
    {
        PSMainSource = m_Settings.GetPSMainSource(PSOFlags);
    }
    else
    {
        PSMainSource = GetPSOutputStruct(PSOFlags) + DefaultPSMainSource;
    }
    MemoryShaderSourceFileInfo GeneratedSources[] =
        {
            MemoryShaderSourceFileInfo{"VSInputStruct.generated", VSInputStruct},
            MemoryShaderSourceFileInfo{"VSOutputStruct.generated", VSOutputStruct},
            MemoryShaderSourceFileInfo{"PSMainGenerated.generated", PSMainSource},
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

    auto Macros = DefineMacros(PSOFlags, Key.GetDebugView());
    if (GraphicsDesc.PrimitiveTopology == PRIMITIVE_TOPOLOGY_POINT_LIST && (m_Device.GetDeviceInfo().IsGLDevice() || m_Device.GetDeviceInfo().IsVulkanDevice()))
    {
        // If gl_PointSize is not defined, points are not rendered in GLES.
        Macros.Add("USE_GL_POINT_SIZE", "1");
    }
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"PBR VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "RenderPBR.vsh";

        pVS = m_Device.CreateShader(ShaderCI);
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {!IsUnshaded ? "PBR PS" : "Unshaded PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = !IsUnshaded ? "RenderPBR.psh" : "RenderUnshaded.psh";

        pPS = m_Device.CreateShader(ShaderCI);
    }

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
            if (IsUnshaded)
                continue;

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
            std::string PSOName{!IsUnshaded ? "PBR PSO" : "Unshaded PSO"};
            PSOName += (AlphaMode == ALPHA_MODE_OPAQUE ? " - opaque" : " - blend");
            PSOName += (CullMode == CULL_MODE_BACK ? " - backface culling" : " - no culling");
            PSODesc.Name = PSOName.c_str();

            GraphicsPipeline.RasterizerDesc.CullMode = CullMode;
            const auto DoubleSided                   = CullMode == CULL_MODE_NONE;
            auto       PSO                           = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);

            PsoHashMap[{PSOFlags, AlphaMode, DoubleSided, Key.GetDebugView()}] = PSO;
            if (AlphaMode == ALPHA_MODE_OPAQUE)
            {
                // Mask and opaque use the same PSO
                PsoHashMap[{PSOFlags, ALPHA_MODE_MASK, DoubleSided, Key.GetDebugView()}] = PSO;
            }
        }
    }
}

void PBR_Renderer::CreateResourceBinding(IShaderResourceBinding** ppSRB)
{
    m_ResourceSignature->CreateShaderResourceBinding(ppSRB, true);
}

PBR_Renderer::PsoCacheAccessor PBR_Renderer::GetPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc)
{
    VERIFY(GraphicsDesc.InputLayout == InputLayoutDesc{}, "Input layout is ignored. It is defined in create info");

    auto it = m_PSOs.find(GraphicsDesc);
    if (it == m_PSOs.end())
        it = m_PSOs.emplace(GraphicsDesc, PsoHashMapType{}).first;
    return {*this, it->second, it->first};
}

IPipelineState* PBR_Renderer::GetPSO(PsoHashMapType&             PsoHashMap,
                                     const GraphicsPipelineDesc& GraphicsDesc,
                                     const PSOKey&               Key,
                                     bool                        CreateIfNull)
{
    auto Flags = Key.GetFlags();
    if (!m_Settings.EnableIBL)
    {
        Flags &= ~PSO_FLAG_USE_IBL;
    }
    if (!m_Settings.EnableAO)
    {
        Flags &= ~PSO_FLAG_USE_AO_MAP;
    }
    if (!m_Settings.EnableEmissive)
    {
        Flags &= ~PSO_FLAG_USE_EMISSIVE_MAP;
    }
    if (m_Settings.MaxJointCount == 0)
    {
        Flags &= ~PSO_FLAG_USE_JOINTS;
    }
    if (m_Settings.UseSeparateMetallicRoughnessTextures)
    {
        DEV_CHECK_ERR((Flags & PSO_FLAG_USE_PHYS_DESC_MAP) == 0, "Physical descriptor map is not enabled");
    }
    else
    {
        DEV_CHECK_ERR((Flags & (PSO_FLAG_USE_METALLIC_MAP | PSO_FLAG_USE_ROUGHNESS_MAP)) == 0, "Separate metallic and roughness maps are not enaled");
    }
    if ((Flags & (PSO_FLAG_USE_TEXCOORD0 | PSO_FLAG_USE_TEXCOORD1)) == 0)
    {
        Flags &= ~PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;
    }

    const PSOKey UpdatedKey{Flags, Key.GetAlphaMode(), Key.IsDoubleSided(), Key.GetDebugView()};

    auto it = PsoHashMap.find(UpdatedKey);
    if (it == PsoHashMap.end())
    {
        if (CreateIfNull)
        {
            CreatePSO(PsoHashMap, GraphicsDesc, UpdatedKey);
            it = PsoHashMap.find(UpdatedKey);
            VERIFY_EXPR(it != PsoHashMap.end());
        }
    }

    return it != PsoHashMap.end() ? it->second.RawPtr() : nullptr;
}

void PBR_Renderer::SetInternalShaderParameters(HLSL::PBRRendererShaderParameters& Renderer)
{
    Renderer.PrefilteredCubeMipLevels = m_Settings.EnableIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
}

Uint32 PBR_Renderer::GetPBRPrimitiveAttribsSize() const
{
    //struct PBRPrimitiveAttribs
    //{
    //    GLTFNodeShaderTransforms Transforms;
    //    struct PBRMaterialShaderInfo
    //    {
    //        PBRMaterialBasicAttribs   Basic;
    //        PBRMaterialTextureAttribs Textures[PBR_NUM_TEXTURE_ATTRIBUTES];
    //    } Material;
    //    float4 CustomData;
    //};
    return (sizeof(HLSL::GLTFNodeShaderTransforms) +
            sizeof(HLSL::PBRMaterialBasicAttribs) +
            sizeof(HLSL::PBRMaterialTextureAttribs) * m_NumShaderTextureAttribs +
            sizeof(float4));
}

void* PBR_Renderer::WritePBRPrimitiveShaderAttribs(void* pDstShaderAttribs, const PBRPrimitiveShaderAttribsData& AttribsData)
{
    //struct PBRPrimitiveAttribs
    //{
    //    GLTFNodeShaderTransforms Transforms;
    //    struct PBRMaterialShaderInfo
    //    {
    //        PBRMaterialBasicAttribs   Basic;
    //        PBRMaterialTextureAttribs Textures[PBR_NUM_TEXTURE_ATTRIBUTES];
    //    } Material;
    //    float4 CustomData;
    //};

    HLSL::GLTFNodeShaderTransforms* pDstTransforms = reinterpret_cast<HLSL::GLTFNodeShaderTransforms*>(pDstShaderAttribs);
    static_assert(sizeof(HLSL::GLTFNodeShaderTransforms) % 16 == 0, "Size of HLSL::GLTFNodeShaderTransforms must be a multiple of 16");
    VERIFY(AttribsData.NodeMatrix != nullptr, "Node matrix must not be null");
    memcpy(&pDstTransforms->NodeMatrix, AttribsData.NodeMatrix, sizeof(float4x4));
    pDstTransforms->JointCount = static_cast<int>(AttribsData.JointCount);

    HLSL::PBRMaterialBasicAttribs* pDstBasicAttribs = reinterpret_cast<HLSL::PBRMaterialBasicAttribs*>(pDstTransforms + 1);
    static_assert(sizeof(HLSL::PBRMaterialBasicAttribs) % 16 == 0, "Size of HLSL::PBRMaterialBasicAttribs must be a multiple of 16");
    VERIFY(AttribsData.BasicAttribs != nullptr, "Basic attribs must not be null");
    memcpy(pDstBasicAttribs, AttribsData.BasicAttribs, sizeof(HLSL::PBRMaterialBasicAttribs));

    HLSL::PBRMaterialTextureAttribs* pDstTextures = reinterpret_cast<HLSL::PBRMaterialTextureAttribs*>(pDstBasicAttribs + 1);
    static_assert(sizeof(HLSL::PBRMaterialTextureAttribs) % 16 == 0, "Size of HLSL::PBRMaterialTextureAttribs must be a multiple of 16");

    VERIFY(AttribsData.NumTextureAttribs <= m_NumShaderTextureAttribs,
           "Material data contains ", AttribsData.NumTextureAttribs, " texture attributes, while the shader only supports ", m_NumShaderTextureAttribs);
    memcpy(pDstTextures, AttribsData.TextureAttribs, sizeof(HLSL::PBRMaterialTextureAttribs) * std::min(AttribsData.NumTextureAttribs, m_NumShaderTextureAttribs));

    Uint8* pDstCustomData = reinterpret_cast<Uint8*>(pDstTextures + m_NumShaderTextureAttribs);
    if (AttribsData.CustomData != nullptr)
    {
        VERIFY_EXPR(AttribsData.CustomDataSize > 0);
        memcpy(pDstCustomData, AttribsData.CustomData, AttribsData.CustomDataSize);
    }
    return pDstCustomData + AttribsData.CustomDataSize;
}

} // namespace Diligent
