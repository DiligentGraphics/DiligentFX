/*
 *  Copyright 2019-2024 Diligent Graphics LLC
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
#include "PlatformMisc.hpp"
#include "TextureUtilities.h"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.hpp"

#if HLSL2GLSL_CONVERTER_SUPPORTED
#    include "../include/HLSL2GLSLConverterImpl.hpp"
#endif

namespace Diligent
{

const SamplerDesc PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

#if PLATFORM_EMSCRIPTEN
static constexpr char MultiDrawGLSLExtension[] = "#extension GL_ANGLE_multi_draw : enable";
#else
static constexpr char MultiDrawGLSLExtension[] = "#extension GL_ARB_shader_draw_parameters : enable";
#endif

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

PBR_Renderer::PSOKey::PSOKey(PSO_FLAGS     _Flags,
                             ALPHA_MODE    _AlphaMode,
                             bool          _DoubleSided,
                             DebugViewType _DebugView,
                             Uint64        _UserValue) noexcept :
    Flags{_Flags},
    AlphaMode{_AlphaMode},
    DoubleSided{_DoubleSided},
    DebugView{_DebugView},
    UserValue{_UserValue}
{
    if (Flags & PSO_FLAG_UNSHADED)
    {
        AlphaMode = ALPHA_MODE_OPAQUE;

        constexpr auto SupportedUnshadedFlags = PSO_FLAG_USE_JOINTS | PSO_FLAG_ALL_USER_DEFINED | PSO_FLAG_UNSHADED;
        Flags &= SupportedUnshadedFlags;

        DebugView = DebugViewType::None;
    }

    Hash = ComputeHash(Flags, AlphaMode, DoubleSided, static_cast<Uint32>(DebugView), UserValue);
}

static const char* GetTextureAttribString(PBR_Renderer::TEXTURE_ATTRIB_ID Id)
{
    static const std::array<const char*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureAttribStrings =
        []() {
            std::array<const char*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> AttribStrings;
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR]            = "BaseColor";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]                = "Normal";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]             = "PhysicalDescriptor";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC]              = "Metallic";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS]             = "Roughness";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]             = "Occlusion";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]              = "Emissive";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT]            = "ClearCoat";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS]  = "ClearCoatRoughness";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL]     = "ClearCoatNormal";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR]           = "SheenColor";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS]       = "SheenRoughness";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY]            = "Anisotropy";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE]           = "Iridescence";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS] = "IridescenceThickness";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION]          = "Transmission";
            AttribStrings[PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS]             = "Thickness";
            static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 17, "Not all texture names are initialized");
            return AttribStrings;
        }();
    return TextureAttribStrings[Id];
}

static std::string GetTextureShaderName(PBR_Renderer::TEXTURE_ATTRIB_ID Id)
{
    return std::string{"g_"} + GetTextureAttribString(Id) + "Map";
}

static std::string GetTextureAttribIdString(PBR_Renderer::TEXTURE_ATTRIB_ID Id)
{
    return std::string{GetTextureAttribString(Id)} + "TextureAttribId";
}

static std::string GetTextureIdString(PBR_Renderer::TEXTURE_ATTRIB_ID Id)
{
    return std::string{GetTextureAttribString(Id)} + "TextureId";
}

std::string PBR_Renderer::GetPSOFlagsString(PSO_FLAGS Flags)
{
    std::string FlagsStr;
    while (Flags != PSO_FLAG_NONE)
    {
        if (!FlagsStr.empty())
            FlagsStr += " | ";

        const PSO_FLAGS Flag = ExtractLSB(Flags);
        switch (Flag)
        {
            // clang-format off
            case PSO_FLAG_USE_COLOR_MAP:                 FlagsStr += "COLOR_MAP"; break;
            case PSO_FLAG_USE_NORMAL_MAP:                FlagsStr += "NORMAL_MAP"; break;
            case PSO_FLAG_USE_PHYS_DESC_MAP:             FlagsStr += "PHYS_DESC_MAP"; break;
            case PSO_FLAG_USE_METALLIC_MAP:              FlagsStr += "METALLIC_MAP"; break;
            case PSO_FLAG_USE_ROUGHNESS_MAP:             FlagsStr += "ROUGHNESS_MAP"; break;
            case PSO_FLAG_USE_AO_MAP:                    FlagsStr += "AO_MAP"; break;
            case PSO_FLAG_USE_EMISSIVE_MAP:              FlagsStr += "EMISSIVE_MAP"; break;
            case PSO_FLAG_USE_CLEAR_COAT_MAP:            FlagsStr += "CLEAR_COAT_MAP"; break;
            case PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP:  FlagsStr += "CLEAR_COAT_ROUGHNESS_MAP"; break;
            case PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP:     FlagsStr += "CLEAR_COAT_NORMAL_MAP"; break;
            case PSO_FLAG_USE_SHEEN_COLOR_MAP:           FlagsStr += "SHEEN_COLOR_MAP"; break;
            case PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP:       FlagsStr += "SHEEN_ROUGHNESS_MAP"; break;
            case PSO_FLAG_USE_ANISOTROPY_MAP:            FlagsStr += "ANISOTROPY_MAP"; break;
            case PSO_FLAG_USE_IRIDESCENCE_MAP:           FlagsStr += "IRIDESCENCE_MAP"; break;
            case PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP: FlagsStr += "IRIDESCENCE_THICKNESS_MAP"; break;
            case PSO_FLAG_USE_TRANSMISSION_MAP:          FlagsStr += "TRANSMISSION_MAP"; break;

            case PSO_FLAG_USE_VERTEX_COLORS:   FlagsStr += "VERTEX_COLORS"; break;
            case PSO_FLAG_USE_VERTEX_NORMALS:  FlagsStr += "VERTEX_NORMALS"; break;
            case PSO_FLAG_USE_VERTEX_TANGENTS: FlagsStr += "VERTEX_TANGENTS"; break;
            case PSO_FLAG_USE_TEXCOORD0:       FlagsStr += "TEXCOORD0"; break;
            case PSO_FLAG_USE_TEXCOORD1:       FlagsStr += "TEXCOORD1"; break;
            case PSO_FLAG_USE_JOINTS:          FlagsStr += "JOINTS"; break;
            case PSO_FLAG_ENABLE_CLEAR_COAT:   FlagsStr += "CLEAR_COAT"; break;
            case PSO_FLAG_ENABLE_SHEEN:        FlagsStr += "SHEEN"; break;
            case PSO_FLAG_ENABLE_ANISOTROPY:   FlagsStr += "ANISOTROPY"; break;
            case PSO_FLAG_ENABLE_IRIDESCENCE:  FlagsStr += "IRIDESCENCE"; break;
            case PSO_FLAG_ENABLE_TRANSMISSION: FlagsStr += "TRANSMISSION"; break;
            case PSO_FLAG_ENABLE_VOLUME:       FlagsStr += "VOLUME"; break;

            case PSO_FLAG_USE_IBL:                   FlagsStr += "IBL"; break;
            case PSO_FLAG_USE_LIGHTS:                FlagsStr += "LIGHTS"; break;
            case PSO_FLAG_USE_TEXTURE_ATLAS:         FlagsStr += "TEXTURE_ATLAS"; break;
            case PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM: FlagsStr += "TEXCOORD_TRANSFORM"; break;
            case PSO_FLAG_CONVERT_OUTPUT_TO_SRGB:    FlagsStr += "SRGB"; break;
            case PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT: FlagsStr += "CUSTOM_DATA"; break;
            case PSO_FLAG_ENABLE_TONE_MAPPING:       FlagsStr += "TONE_MAPPING"; break;
            case PSO_FLAG_UNSHADED:                  FlagsStr += "UNSHADED"; break;
            case PSO_FLAG_COMPUTE_MOTION_VECTORS:    FlagsStr += "MOTION_VECTORS"; break;
            case PSO_FLAG_ENABLE_SHADOWS:            FlagsStr += "SHADOWS"; break;
                // clang-format on

            default:
                FlagsStr += std::to_string(PlatformMisc::GetLSB(Flag));
        }
    }
    static_assert(PSO_FLAG_LAST == 1ull << 38ull, "Please update the switch above to handle the new flag");

    return FlagsStr;
}

PBR_Renderer::PBR_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI,
                           bool               InitSignature) :
    m_InputLayout{CI.InputLayout},
    m_Settings{
        [this](CreateInfo CI) {
            CI.InputLayout               = m_InputLayout;
            CI.SheenAlbedoScalingLUTPath = nullptr;
            return CI;
        }(CI)},
    m_Device{pDevice, pStateCache},
    m_PBRPrimitiveAttribsCB{CI.pPrimitiveAttribsCB},
    m_JointsBuffer{CI.pJointsBuffer}
{
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

    if (m_Settings.EnableSheen)
    {
        if (CI.SheenAlbedoScalingLUTPath != nullptr)
        {
            TextureLoadInfo LoadInfo{"Sheen Albedo Scaling"};
            LoadInfo.Format = TEX_FORMAT_R8_UNORM;
            RefCntAutoPtr<ITexture> SheenAlbedoScalingLUT;
            CreateTextureFromFile(CI.SheenAlbedoScalingLUTPath, LoadInfo, m_Device, &SheenAlbedoScalingLUT);
            if (SheenAlbedoScalingLUT)
            {
                m_pSheenAlbedoScaling_LUT_SRV = SheenAlbedoScalingLUT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                StateTransitionDesc Barriers{SheenAlbedoScalingLUT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
                pCtx->TransitionResourceStates(1, &Barriers);
            }
            else
            {
                LOG_ERROR_MESSAGE("Failed to load sheen albedo scaling look-up table from file ", CI.SheenAlbedoScalingLUTPath);
            }
        }
        else
        {
            UNEXPECTED("Sheen albedo scaling look-up table path is not specified");
        }

        if (m_Settings.EnableIBL)
        {
            if (CI.PreintegratedCharlieBRDFPath != nullptr)
            {
                TextureLoadInfo LoadInfo{"Preintegrated Charlie BRDF"};
                LoadInfo.Format    = TEX_FORMAT_R8_UNORM;
                LoadInfo.Swizzle.R = TEXTURE_COMPONENT_SWIZZLE_B;
                RefCntAutoPtr<ITexture> PreintegratedCharlieBRDF;
                CreateTextureFromFile(CI.PreintegratedCharlieBRDFPath, LoadInfo, m_Device, &PreintegratedCharlieBRDF);
                if (PreintegratedCharlieBRDF)
                {
                    m_pPreintegratedCharlie_SRV = PreintegratedCharlieBRDF->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    StateTransitionDesc Barriers{PreintegratedCharlieBRDF, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
                    pCtx->TransitionResourceStates(1, &Barriers);
                }
                else
                {
                    LOG_ERROR_MESSAGE("Failed to load preintegrated Charlie BRDF look-up table from file ", CI.PreintegratedCharlieBRDFPath);
                }
            }
            else
            {
                UNEXPECTED("Preintegrated Charlie BRDF look-up table path is not specified");
            }
        }
    }

    {
        if (!m_PBRPrimitiveAttribsCB)
        {
            CreateUniformBuffer(pDevice, GetPBRPrimitiveAttribsSize(PSO_FLAG_ALL), "PBR primitive attribs CB", &m_PBRPrimitiveAttribsCB);
        }
        if (m_Settings.MaxJointCount > 0)
        {
            const size_t JointsBufferSize = sizeof(float4x4) * m_Settings.MaxJointCount * 2; // Current and previous transforms
            if (!m_JointsBuffer)
            {
                CreateUniformBuffer(pDevice, static_cast<Uint32>(JointsBufferSize), "PBR joint transforms", &m_JointsBuffer);
            }
            else
            {
                DEV_CHECK_ERR(m_JointsBuffer->GetDesc().Size >= JointsBufferSize, "PBR joint transforms buffer is too small to hold ", m_Settings.MaxJointCount, " joints.");
            }
        }
        std::vector<StateTransitionDesc> Barriers;
        Barriers.emplace_back(m_PBRPrimitiveAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE);
        if (m_JointsBuffer)
            Barriers.emplace_back(m_JointsBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE);
        pCtx->TransitionResourceStates(static_cast<Uint32>(Barriers.size()), Barriers.data());
    }

    if (InitSignature)
    {
        CreateSignature();
    }
}

PBR_Renderer::~PBR_Renderer()
{
#ifdef DILIGENT_DEVELOPMENT
    {
        size_t NumPSOs = 0;
        for (const auto& it : m_PSOs)
        {
            NumPSOs += it.second.size();
        }
        LOG_INFO_MESSAGE("PBR Renderer: PSO cache size: ", NumPSOs, ".");
    }
#endif
}

void PBR_Renderer::PrecomputeBRDF(IDeviceContext* pCtx,
                                  Uint32          NumBRDFSamples)
{
    TextureDesc TexDesc;
    TexDesc.Name            = "Preintegrated GGX";
    TexDesc.Type            = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage           = USAGE_DEFAULT;
    TexDesc.BindFlags       = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width           = BRDF_LUT_Dim;
    TexDesc.Height          = BRDF_LUT_Dim;
    TexDesc.Format          = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels       = 1;
    auto pPreintegratedGGX  = m_Device.CreateTexture(TexDesc);
    m_pPreintegratedGGX_SRV = pPreintegratedGGX->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

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

    ITextureView* pRTVs[] = {pPreintegratedGGX->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    pCtx->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attrs(3, DRAW_FLAG_VERIFY_ALL);
    pCtx->Draw(attrs);

    // clang-format off
    StateTransitionDesc Barriers[] =
    {
        {pPreintegratedGGX, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
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
                Attribs->Roughness  = static_cast<float>(mip) / static_cast<float>(PrefilteredEnvMapDesc.MipLevels - 1);
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
                                     IBuffer*                pFrameAttribs,
                                     bool                    BindPrimitiveAttribsBuffer,
                                     ITextureView*           pShadowMap) const
{
    if (pSRB == nullptr)
    {
        UNEXPECTED("SRB must not be null");
        return;
    }

    if (BindPrimitiveAttribsBuffer)
    {
        if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs"))
        {
            if (pVar->Get() == nullptr)
                pVar->Set(m_PBRPrimitiveAttribsCB);
        }
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

    if (m_Settings.EnableShadows && pShadowMap != nullptr)
    {
        if (auto* pShadowMapVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
            pShadowMapVar->Set(pShadowMap);
    }
}

void PBR_Renderer::SetMaterialTexture(IShaderResourceBinding* pSRB, ITextureView* pTexSRV, TEXTURE_ATTRIB_ID TextureId) const
{
    if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_NONE)
    {
        const std::string TextureName = GetTextureShaderName(TextureId);
        if (IShaderResourceVariable* pTexVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, TextureName.c_str()))
        {
            pTexVar->Set(pTexSRV);
        }
    }
    else if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC)
    {
        if (m_StaticShaderTextureIds)
        {
            const auto TextureIdx = (*m_StaticShaderTextureIds)[TextureId];
            if (TextureIdx == InvalidMaterialTextureId)
            {
                UNEXPECTED("Texture is not initialized");
                return;
            }

            if (IShaderResourceVariable* pMatTexturesVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_MaterialTextures"))
            {
                IDeviceObject* pObj[] = {pTexSRV};
                pMatTexturesVar->SetArray(pObj, TextureIdx, 1);
            }
        }
        else
        {
            UNEXPECTED("Static material texture indices are not initialized, which indicates that the client uses a custom GetStaticShaderTextureIds function. "
                       "In this case it is expected that the client binds the textures.");
        }
    }
    else if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_DYNAMIC)
    {
        UNEXPECTED("This method should not be called when shader textures array mode is dynamic.");
    }
    else
    {
        UNEXPECTED("Unexpected shader textures array mode");
    }
}

void PBR_Renderer::CreateSignature()
{
    VERIFY(m_ResourceSignatures.empty(), "Resource signature has already been created");

    PipelineResourceSignatureDescX SignatureDesc{"PBR Renderer Resource Signature"};
    SignatureDesc
        .SetUseCombinedTextureSamplers(m_Device.GetDeviceInfo().IsGLDevice())
        .AddResource(SHADER_TYPE_VS_PS, "cbFrameAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_VS_PS, "cbPrimitiveAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    if (m_Settings.MaxJointCount > 0)
        SignatureDesc.AddResource(SHADER_TYPE_VERTEX, "cbJointTransforms", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    std::unordered_set<std::string> Samplers;
    if (!m_Device.GetDeviceInfo().IsGLDevice())
    {
        SignatureDesc.AddImmutableSampler(SHADER_TYPE_PIXEL, "g_LinearClampSampler", Sam_LinearClamp);
        Samplers.emplace("g_LinearClampSampler");
    }

    auto& MaterialTexturesArraySize = m_Settings.MaterialTexturesArraySize;
    if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC &&
        m_Settings.GetStaticShaderTextureIds == nullptr)
    {
        MaterialTexturesArraySize = 0;
        m_StaticShaderTextureIds  = std::make_unique<StaticShaderTextureIdsArrayType>();
        m_StaticShaderTextureIds->fill(decltype(PBR_Renderer::InvalidMaterialTextureId){InvalidMaterialTextureId});
    }

    auto AddMaterialTextureAndSampler = [&](TEXTURE_ATTRIB_ID  TexId,
                                            const char*        SamplerName,
                                            const SamplerDesc& SamDesc) {
        std::string TextureName;
        if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC ||
            m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_DYNAMIC)
        {
            if (m_StaticShaderTextureIds)
            {
                VERIFY_EXPR(m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC);
                VERIFY((*m_StaticShaderTextureIds)[TexId] == InvalidMaterialTextureId, "Material texture has already been added");
                (*m_StaticShaderTextureIds)[TexId] = static_cast<Uint16>(MaterialTexturesArraySize++);
            }
            if (m_Device.GetDeviceInfo().IsGLDevice())
            {
                // Use the same immutable sampler for all textures as immutable sampler arrays are not supported.
                SamplerName = "g_MaterialTextures";
            }
        }
        else if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_NONE)
        {
            TextureName = GetTextureShaderName(TexId);
            SignatureDesc.AddResource(SHADER_TYPE_PIXEL, TextureName.c_str(), SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            if (m_Device.GetDeviceInfo().IsGLDevice())
            {
                SamplerName = TextureName.c_str();
            }
        }
        else
        {
            UNEXPECTED("Unexpected shader textures array mode");
        }

        if (Samplers.emplace(SamplerName).second)
        {
            SignatureDesc.AddImmutableSampler(SHADER_TYPE_PIXEL, SamplerName, SamDesc);
        }
    };

    AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_BASE_COLOR, "g_BaseColorMap_sampler", m_Settings.ColorMapImmutableSampler);
    AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_NORMAL, "g_NormalMap_sampler", m_Settings.NormalMapImmutableSampler);

    if (m_Settings.UseSeparateMetallicRoughnessTextures)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_METALLIC, "g_MetallicMap_sampler", m_Settings.PhysDescMapImmutableSampler);
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_ROUGHNESS, "g_RoughnessMap_sampler", m_Settings.PhysDescMapImmutableSampler);
    }
    else
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_PHYS_DESC, "g_PhysicalDescriptorMap_sampler", m_Settings.PhysDescMapImmutableSampler);
    }

    if (m_Settings.EnableAO)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_OCCLUSION, "g_OcclusionMap_sampler", m_Settings.AOMapImmutableSampler);
    }

    if (m_Settings.EnableEmissive)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_EMISSIVE, "g_EmissiveMap_sampler", m_Settings.EmissiveMapImmutableSampler);
    }

    if (m_Settings.EnableClearCoat)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_CLEAR_COAT, "g_ClearCoat_sampler", m_Settings.ClearCoatMapImmutableSampler);
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS, "g_ClearCoat_sampler", m_Settings.ClearCoatMapImmutableSampler);
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL, "g_ClearCoat_sampler", m_Settings.ClearCoatMapImmutableSampler);
    }

    if (m_Settings.EnableSheen)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_SHEEN_COLOR, "g_Sheen_sampler", m_Settings.SheenMapImmutableSampler);
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS, "g_Sheen_sampler", m_Settings.SheenMapImmutableSampler);
    }

    if (m_Settings.EnableAnisotropy)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_ANISOTROPY, "g_AnisotropyMap_sampler", m_Settings.AnisotropyMapImmutableSampler);
    }

    if (m_Settings.EnableIridescence)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_IRIDESCENCE, "g_Iridescence_sampler", m_Settings.IridescenceMapImmutableSampler);
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS, "g_Iridescence_sampler", m_Settings.IridescenceMapImmutableSampler);
    }

    if (m_Settings.EnableTransmission)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_TRANSMISSION, "g_TransmissionMap_sampler", m_Settings.TransmissionMapImmutableSampler);
    }

    if (m_Settings.EnableVolume)
    {
        AddMaterialTextureAndSampler(TEXTURE_ATTRIB_ID_THICKNESS, "g_ThicknessMap_sampler", m_Settings.ThicknessMapImmutableSampler);
    }

    if (MaterialTexturesArraySize > 0)
    {
        SignatureDesc.AddResource(SHADER_TYPE_PIXEL, "g_MaterialTextures", MaterialTexturesArraySize, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
    }

    auto AddTextureAndSampler = [&](const char*                   TextureName,
                                    const SamplerDesc&            SamDesc,
                                    const char*                   SeparateSamplerName,
                                    SHADER_RESOURCE_VARIABLE_TYPE VarType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE) //
    {
        SignatureDesc.AddResource(SHADER_TYPE_PIXEL, TextureName, SHADER_RESOURCE_TYPE_TEXTURE_SRV, VarType);

        const char* SamplerName = m_Device.GetDeviceInfo().IsGLDevice() ? TextureName : SeparateSamplerName;
        if (Samplers.emplace(SamplerName).second)
        {
            SignatureDesc.AddImmutableSampler(SHADER_TYPE_PIXEL, SamplerName, SamDesc);
        }
    };

    if (m_Settings.EnableIBL)
    {
        AddTextureAndSampler("g_PreintegratedGGX", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
        AddTextureAndSampler("g_IrradianceMap", Sam_LinearClamp, "g_LinearClampSampler");
        AddTextureAndSampler("g_PrefilteredEnvMap", Sam_LinearClamp, "g_LinearClampSampler");

        if (m_Settings.EnableSheen)
        {
            AddTextureAndSampler("g_PreintegratedCharlie", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
        }
    }

    if (m_Settings.EnableSheen)
    {
        AddTextureAndSampler("g_SheenAlbedoScalingLUT", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    }

    if (m_Settings.EnableShadows)
    {
        AddTextureAndSampler("g_ShadowMap", Sam_ComparisonLinearClamp, "g_ShadowMap_sampler");
    }

    CreateCustomSignature(std::move(SignatureDesc));
}

void PBR_Renderer::CreateCustomSignature(PipelineResourceSignatureDescX&& SignatureDesc)
{
    RefCntAutoPtr<IPipelineResourceSignature> ResourceSignature = m_Device.CreatePipelineResourceSignature(SignatureDesc);
    VERIFY_EXPR(ResourceSignature);

    if (m_Settings.EnableIBL)
    {
        ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX")->Set(m_pPreintegratedGGX_SRV);
        if (m_Settings.EnableSheen)
        {
            ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedCharlie")->Set(m_pPreintegratedCharlie_SRV);
        }
    }

    if (m_Settings.EnableSheen)
    {
        ResourceSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SheenAlbedoScalingLUT")->Set(m_pSheenAlbedoScaling_LUT_SRV);
    }
    m_ResourceSignatures = {std::move(ResourceSignature)};
}

ShaderMacroHelper PBR_Renderer::DefineMacros(const PSOKey& Key) const
{
    const PSO_FLAGS PSOFlags = Key.GetFlags();

    ShaderMacroHelper Macros;
    Macros.Add("MAX_JOINT_COUNT", static_cast<int>(m_Settings.MaxJointCount));
    Macros.Add("TONE_MAPPING_MODE", "TONE_MAPPING_MODE_UNCHARTED2");

    Macros.Add("PRIMITIVE_ARRAY_SIZE", static_cast<int>(m_Settings.PrimitiveArraySize));
    if (m_Settings.PrimitiveArraySize > 0)
    {
        const char* PrimitiveID = nullptr;
        if (m_Device.GetDeviceInfo().IsGLDevice())
        {
#if PLATFORM_EMSCRIPTEN
            PrimitiveID = "gl_DrawID";
#else
            PrimitiveID = "gl_DrawIDARB";
#endif
        }
        else if (m_Device.GetDeviceInfo().IsVulkanDevice())
        {
#ifdef HLSL2GLSL_CONVERTER_SUPPORTED
            PrimitiveID = "gl_DrawID";
#else
            UNSUPPORTED("Primitive ID on Vulkan requires HLSL2GLSL converter");
            PrimitiveID = "0";
#endif
        }
        else
        {
            UNSUPPORTED("Primitive ID is only supported in GL and Vulkan");
            PrimitiveID = "0";
        }
        Macros.Add("PRIMITIVE_ID", PrimitiveID);
    }

    Macros.Add("PBR_WORKFLOW_METALLIC_ROUGHNESS", static_cast<int>(PBR_WORKFLOW_METALL_ROUGH));
    Macros.Add("PBR_WORKFLOW_SPECULAR_GLOSINESS", static_cast<int>(PBR_WORKFLOW_SPEC_GLOSS));
    Macros.Add("PBR_WORKFLOW_UNLIT", static_cast<int>(PBR_WORKFLOW_UNLIT));

    Macros.Add("PBR_ALPHA_MODE_OPAQUE", static_cast<int>(ALPHA_MODE_OPAQUE));
    Macros.Add("PBR_ALPHA_MODE_MASK", static_cast<int>(ALPHA_MODE_MASK));
    Macros.Add("PBR_ALPHA_MODE_BLEND", static_cast<int>(ALPHA_MODE_BLEND));

    Macros.Add("PBR_MAX_LIGHTS", (PSOFlags & PSO_FLAG_USE_LIGHTS) != 0 ? static_cast<int>(m_Settings.MaxLightCount) : 0);
    Macros.Add("PBR_LIGHT_TYPE_DIRECTIONAL", static_cast<int>(LIGHT_TYPE_DIRECTIONAL));
    Macros.Add("PBR_LIGHT_TYPE_POINT", static_cast<int>(LIGHT_TYPE_POINT));
    Macros.Add("PBR_LIGHT_TYPE_SPOT", static_cast<int>(LIGHT_TYPE_SPOT));
    Macros.Add("PBR_MAX_SHADOW_MAPS", (PSOFlags & PSO_FLAG_USE_LIGHTS) != 0 ? static_cast<int>(m_Settings.MaxShadowCastingLightCount) : 0);

    Macros.Add("USE_IBL_ENV_MAP_LOD", true);
    Macros.Add("USE_HDR_IBL_CUBEMAPS", true);
    Macros.Add("USE_SEPARATE_METALLIC_ROUGHNESS_TEXTURES", m_Settings.UseSeparateMetallicRoughnessTextures);

    if (m_Settings.EnableShadows)
    {
        int KernelSize = static_cast<int>(m_Settings.PCFKernelSize);
        if (KernelSize != 2 && KernelSize != 3 && KernelSize != 5 && KernelSize != 7)
        {
            LOG_WARNING_MESSAGE(KernelSize, " is not a valid PCF kernel size. Allowed values are 2, 3, 5, and 7. Using 3x3 kernel.");
            KernelSize = 3;
        }
        Macros.Add("PCF_FILTER_SIZE", KernelSize);
    }

    static_assert(static_cast<int>(DebugViewType::NumDebugViews) == 34, "Did you add debug view? You may need to handle it here.");
    // clang-format off
    Macros.Add("DEBUG_VIEW",                       static_cast<int>(Key.GetDebugView()));
    Macros.Add("DEBUG_VIEW_NONE",                  static_cast<int>(DebugViewType::None));
    Macros.Add("DEBUG_VIEW_TEXCOORD0",             static_cast<int>(DebugViewType::Texcoord0));
    Macros.Add("DEBUG_VIEW_TEXCOORD1",             static_cast<int>(DebugViewType::Texcoord1));
    Macros.Add("DEBUG_VIEW_BASE_COLOR",            static_cast<int>(DebugViewType::BaseColor));
    Macros.Add("DEBUG_VIEW_TRANSPARENCY",          static_cast<int>(DebugViewType::Transparency));
    Macros.Add("DEBUG_VIEW_OCCLUSION",             static_cast<int>(DebugViewType::Occlusion));
    Macros.Add("DEBUG_VIEW_EMISSIVE",              static_cast<int>(DebugViewType::Emissive));
    Macros.Add("DEBUG_VIEW_METALLIC",              static_cast<int>(DebugViewType::Metallic));
    Macros.Add("DEBUG_VIEW_ROUGHNESS",             static_cast<int>(DebugViewType::Roughness));
    Macros.Add("DEBUG_VIEW_DIFFUSE_COLOR",         static_cast<int>(DebugViewType::DiffuseColor));
    Macros.Add("DEBUG_VIEW_SPECULAR_COLOR",        static_cast<int>(DebugViewType::SpecularColor));
    Macros.Add("DEBUG_VIEW_REFLECTANCE90",         static_cast<int>(DebugViewType::Reflectance90));
    Macros.Add("DEBUG_VIEW_MESH_NORMAL",           static_cast<int>(DebugViewType::MeshNormal));
    Macros.Add("DEBUG_VIEW_SHADING_NORMAL",        static_cast<int>(DebugViewType::ShadingNormal));
    Macros.Add("DEBUG_VIEW_MOTION_VECTORS",        static_cast<int>(DebugViewType::MotionVectors));
    Macros.Add("DEBUG_VIEW_NDOTV",                 static_cast<int>(DebugViewType::NdotV));
    Macros.Add("DEBUG_VIEW_PUNCTUAL_LIGHTING",     static_cast<int>(DebugViewType::PunctualLighting));
    Macros.Add("DEBUG_VIEW_DIFFUSE_IBL",           static_cast<int>(DebugViewType::DiffuseIBL));
    Macros.Add("DEBUG_VIEW_SPECULAR_IBL",          static_cast<int>(DebugViewType::SpecularIBL));
    Macros.Add("DEBUG_VIEW_WHITE_BASE_COLOR",      static_cast<int>(DebugViewType::WhiteBaseColor));
    Macros.Add("DEBUG_VIEW_CLEAR_COAT",            static_cast<int>(DebugViewType::ClearCoat));
    Macros.Add("DEBUG_VIEW_CLEAR_COAT_FACTOR",     static_cast<int>(DebugViewType::ClearCoatFactor));
    Macros.Add("DEBUG_VIEW_CLEAR_COAT_ROUGHNESS",  static_cast<int>(DebugViewType::ClearCoatRoughness));
	Macros.Add("DEBUG_VIEW_CLEAR_COAT_NORMAL",     static_cast<int>(DebugViewType::ClearCoatNormal));
    Macros.Add("DEBUG_VIEW_SHEEN",                 static_cast<int>(DebugViewType::Sheen));
    Macros.Add("DEBUG_VIEW_SHEEN_COLOR",           static_cast<int>(DebugViewType::SheenColor));
    Macros.Add("DEBUG_VIEW_SHEEN_ROUGHNESS",       static_cast<int>(DebugViewType::SheenRoughness));
    Macros.Add("DEBUG_VIEW_ANISOTROPY_STRENGTH",   static_cast<int>(DebugViewType::AnisotropyStrength));
    Macros.Add("DEBUG_VIEW_ANISOTROPY_DIRECTION",  static_cast<int>(DebugViewType::AnisotropyDirection));
    Macros.Add("DEBUG_VIEW_IRIDESCENCE",           static_cast<int>(DebugViewType::Iridescence));
    Macros.Add("DEBUG_VIEW_IRIDESCENCE_FACTOR",    static_cast<int>(DebugViewType::IridescenceFactor));
    Macros.Add("DEBUG_VIEW_IRIDESCENCE_THICKNESS", static_cast<int>(DebugViewType::IridescenceThickness));
    Macros.Add("DEBUG_VIEW_TRANSMISSION",          static_cast<int>(DebugViewType::Transmission));
    Macros.Add("DEBUG_VIEW_THICKNESS",             static_cast<int>(DebugViewType::Thickness));
    // clang-format on

    static_assert(PSO_FLAG_LAST == PSO_FLAG_BIT(38), "Did you add new PSO Flag? You may need to handle it here.");
#define ADD_PSO_FLAG_MACRO(Flag) Macros.Add(#Flag, (PSOFlags & PSO_FLAG_##Flag) != PSO_FLAG_NONE)
    ADD_PSO_FLAG_MACRO(USE_COLOR_MAP);
    ADD_PSO_FLAG_MACRO(USE_NORMAL_MAP);
    ADD_PSO_FLAG_MACRO(USE_METALLIC_MAP);
    ADD_PSO_FLAG_MACRO(USE_ROUGHNESS_MAP);
    ADD_PSO_FLAG_MACRO(USE_PHYS_DESC_MAP);
    ADD_PSO_FLAG_MACRO(USE_AO_MAP);
    ADD_PSO_FLAG_MACRO(USE_EMISSIVE_MAP);
    ADD_PSO_FLAG_MACRO(USE_CLEAR_COAT_MAP);
    ADD_PSO_FLAG_MACRO(USE_CLEAR_COAT_ROUGHNESS_MAP);
    ADD_PSO_FLAG_MACRO(USE_CLEAR_COAT_NORMAL_MAP);
    ADD_PSO_FLAG_MACRO(USE_SHEEN_COLOR_MAP);
    ADD_PSO_FLAG_MACRO(USE_SHEEN_ROUGHNESS_MAP);
    ADD_PSO_FLAG_MACRO(USE_ANISOTROPY_MAP);
    ADD_PSO_FLAG_MACRO(USE_IRIDESCENCE_MAP);
    ADD_PSO_FLAG_MACRO(USE_IRIDESCENCE_THICKNESS_MAP);
    ADD_PSO_FLAG_MACRO(USE_TRANSMISSION_MAP);
    ADD_PSO_FLAG_MACRO(USE_THICKNESS_MAP);

    ADD_PSO_FLAG_MACRO(USE_VERTEX_COLORS);
    ADD_PSO_FLAG_MACRO(USE_VERTEX_NORMALS);
    ADD_PSO_FLAG_MACRO(USE_VERTEX_TANGENTS);
    ADD_PSO_FLAG_MACRO(USE_TEXCOORD0);
    ADD_PSO_FLAG_MACRO(USE_TEXCOORD1);
    ADD_PSO_FLAG_MACRO(USE_JOINTS);
    ADD_PSO_FLAG_MACRO(ENABLE_CLEAR_COAT);
    ADD_PSO_FLAG_MACRO(ENABLE_SHEEN);
    ADD_PSO_FLAG_MACRO(ENABLE_ANISOTROPY);
    ADD_PSO_FLAG_MACRO(ENABLE_IRIDESCENCE);
    ADD_PSO_FLAG_MACRO(ENABLE_TRANSMISSION);
    ADD_PSO_FLAG_MACRO(ENABLE_VOLUME);

    ADD_PSO_FLAG_MACRO(USE_IBL);

    //ADD_PSO_FLAG_MACRO(FRONT_CCW);
    ADD_PSO_FLAG_MACRO(USE_TEXTURE_ATLAS);
    ADD_PSO_FLAG_MACRO(ENABLE_TEXCOORD_TRANSFORM);
    ADD_PSO_FLAG_MACRO(CONVERT_OUTPUT_TO_SRGB);
    ADD_PSO_FLAG_MACRO(ENABLE_CUSTOM_DATA_OUTPUT);
    ADD_PSO_FLAG_MACRO(ENABLE_TONE_MAPPING);
    ADD_PSO_FLAG_MACRO(UNSHADED);
    ADD_PSO_FLAG_MACRO(COMPUTE_MOTION_VECTORS);
    ADD_PSO_FLAG_MACRO(ENABLE_SHADOWS);
#undef ADD_PSO_FLAG_MACRO

    Macros.Add("TEX_COLOR_CONVERSION_MODE_NONE", CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE);
    Macros.Add("TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR", CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR);
    Macros.Add("TEX_COLOR_CONVERSION_MODE", m_Settings.TexColorConversionMode);

    StaticShaderTextureIdsArrayType MaterialTextureIds;
    MaterialTextureIds.fill(decltype(PBR_Renderer::InvalidMaterialTextureId){InvalidMaterialTextureId});
    if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC)
    {
        MaterialTextureIds = m_Settings.GetStaticShaderTextureIds ?
            m_Settings.GetStaticShaderTextureIds(Key) :
            *m_StaticShaderTextureIds;
    }

    // Tightly pack these attributes that are used by the shader
    int MaxIndex = -1;
    ProcessTexturAttribs(PSOFlags, [&](int CurrIndex, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) //
                         {
                             if (m_Settings.TextureAttribIndices[AttribId] >= 0)
                             {
                                 const std::string AttribIdName = GetTextureAttribIdString(AttribId);
                                 Macros.Add(AttribIdName.c_str(), CurrIndex);
                             }
                             else
                             {
                                 DEV_ERROR("Shader uses ", GetTextureAttribString(AttribId), " texture, but its attribute index is not provided.");
                             }
                             VERIFY_EXPR(CurrIndex == MaxIndex + 1);
                             MaxIndex = std::max(MaxIndex, CurrIndex);

                             if (m_Settings.ShaderTexturesArrayMode == SHADER_TEXTURE_ARRAY_MODE_STATIC)
                             {
                                 if (MaterialTextureIds[AttribId] != InvalidMaterialTextureId)
                                 {
                                     const std::string TextureIdName = GetTextureIdString(AttribId);
                                     Macros.Add(TextureIdName.c_str(), static_cast<int>(MaterialTextureIds[AttribId]));
                                 }
                                 else
                                 {
                                     DEV_ERROR("Shader uses ", GetTextureAttribString(AttribId), " texture, but its index is not provided.");
                                 }
                             }
                         });
    Macros
        .Add("PBR_NUM_TEXTURE_ATTRIBUTES", MaxIndex + 1)
        .Add("PBR_NUM_MATERIAL_TEXTURES", static_cast<int>(m_Settings.MaterialTexturesArraySize));

    Macros
        .Add("PBR_TEXTURE_ARRAY_INDEXING_MODE_NONE", static_cast<int>(SHADER_TEXTURE_ARRAY_MODE_NONE))
        .Add("PBR_TEXTURE_ARRAY_INDEXING_MODE_STATIC", static_cast<int>(SHADER_TEXTURE_ARRAY_MODE_STATIC))
        .Add("PBR_TEXTURE_ARRAY_INDEXING_MODE_DYNAMIC", static_cast<int>(SHADER_TEXTURE_ARRAY_MODE_DYNAMIC))
        .Add("PBR_TEXTURE_ARRAY_INDEXING_MODE", static_cast<int>(m_Settings.ShaderTexturesArrayMode));

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
    //    float3 Tangent : ATTRIB7;
    //};
    struct VSAttribInfo
    {
        const Uint32      Index;
        const char* const Name;
        const VALUE_TYPE  Type;
        const Uint32      NumComponents;
        const PSO_FLAGS   Flag;
    };
    static constexpr std::array<VSAttribInfo, 8> VSAttribs = //
        {
            // clang-format off
            VSAttribInfo{0, "Pos",     VT_FLOAT32, 3, PSO_FLAG_NONE},
            VSAttribInfo{1, "Normal",  VT_FLOAT32, 3, PSO_FLAG_USE_VERTEX_NORMALS},
            VSAttribInfo{2, "UV0",     VT_FLOAT32, 2, PSO_FLAG_USE_TEXCOORD0},
            VSAttribInfo{3, "UV1",     VT_FLOAT32, 2, PSO_FLAG_USE_TEXCOORD1},
            VSAttribInfo{4, "Joint0",  VT_FLOAT32, 4, PSO_FLAG_USE_JOINTS},
            VSAttribInfo{5, "Weight0", VT_FLOAT32, 4, PSO_FLAG_USE_JOINTS},
            VSAttribInfo{6, "Color",   VT_FLOAT32, 4, PSO_FLAG_USE_VERTEX_COLORS},
            VSAttribInfo{7, "Tangent", VT_FLOAT32, 3, PSO_FLAG_USE_VERTEX_TANGENTS}
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

std::string PBR_Renderer::GetVSOutputStruct(PSO_FLAGS PSOFlags, bool UseVkPointSize, bool UsePrimitiveId)
{
    // struct VSOutput
    // {
    //     float4 ClipPos     : SV_Position;
    //     float3 WorldPos    : WORLD_POS;
    //     float4 Color       : COLOR;
    //     float3 Normal      : NORMAL;
    //     float2 UV0         : UV0;
    //     float2 UV1         : UV1;
    //     float3 Tangent     : TANGENT;
    //     float4 PrevClipPos : PREV_CLIP_POS;
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
    if (PSOFlags & PSO_FLAG_USE_VERTEX_TANGENTS)
    {
        ss << "    float3 Tangent  : TANGENT;" << std::endl;
    }
    if (PSOFlags & PSO_FLAG_COMPUTE_MOTION_VECTORS)
    {
        ss << "    float4 PrevClipPos : PREV_CLIP_POS;" << std::endl;
    }
    if (UseVkPointSize)
    {
        ss << "    [[vk::builtin(\"PointSize\")]] float PointSize : PSIZE;" << std::endl;
    }
    if (UsePrimitiveId)
    {
        ss << "    int PrimitiveID : PRIMITIVE_ID;" << std::endl;
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

static constexpr char DefaultPSMainFooter[] = R"(
    PSOutput PSOut;
#if UNSHADED
    PSOut.Color = g_Frame.Renderer.UnshadedColor + g_Frame.Renderer.HighlightColor;
#else
    PSOut.Color = OutColor;
#endif
 
#if ENABLE_CUSTOM_DATA_OUTPUT
    {
        PSOut.CustomData = g_Primitive.CustomData;
    }
#endif

    return PSOut;
)";

void PBR_Renderer::CreatePSO(PsoHashMapType& PsoHashMap, const GraphicsPipelineDesc& GraphicsDesc, const PSOKey& Key)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    const auto PSOFlags   = Key.GetFlags();
    const auto IsUnshaded = (PSOFlags & PSO_FLAG_UNSHADED) != 0;

    LOG_DVP_INFO_MESSAGE("PBR Renderer: creating PSO with flags: ", GetPSOFlagsString(PSOFlags), "; debug view: ", static_cast<int>(Key.GetDebugView()), "; user value: ", Key.GetUserValue());

    InputLayoutDescX InputLayout;
    std::string      VSInputStruct;
    GetVSInputStructAndLayout(PSOFlags, VSInputStruct, InputLayout);

    const bool UseVkPointSize =
        GraphicsDesc.PrimitiveTopology == PRIMITIVE_TOPOLOGY_POINT_LIST &&
        m_Device.GetDeviceInfo().IsVulkanDevice() &&
        m_Settings.PrimitiveArraySize == 0; // When PrimitiveArraySize > 0, we convert HLSL to GLSL
    const auto VSOutputStruct = GetVSOutputStruct(PSOFlags, UseVkPointSize, m_Settings.PrimitiveArraySize > 0);

    CreateInfo::PSMainSourceInfo PSMainSource;
    if (m_Settings.GetPSMainSource)
    {
        PSMainSource = m_Settings.GetPSMainSource(PSOFlags);
    }
    else
    {
        PSMainSource.OutputStruct = GetPSOutputStruct(PSOFlags);
        PSMainSource.Footer       = DefaultPSMainFooter;
    }

    // Keep copies of generated strings in the factory when hot shader reload is allowed.
    const bool CopyGeneratedStrings = m_Settings.AllowHotShaderReload;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pMemorySourceFactory =
        CreateMemoryShaderSourceFactory({
                                            MemoryShaderSourceFileInfo{"VSInputStruct.generated", VSInputStruct},
                                            MemoryShaderSourceFileInfo{"VSOutputStruct.generated", VSOutputStruct},
                                            MemoryShaderSourceFileInfo{"PSOutputStruct.generated", PSMainSource.OutputStruct},
                                            MemoryShaderSourceFileInfo{"PSMainFooter.generated", PSMainSource.Footer},
                                        },
                                        CopyGeneratedStrings);
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory =
        CreateCompoundShaderSourceFactory({&DiligentFXShaderSourceStreamFactory::GetInstance(), pMemorySourceFactory});

    auto Macros = DefineMacros(Key);
    if (GraphicsDesc.PrimitiveTopology == PRIMITIVE_TOPOLOGY_POINT_LIST && (m_Device.GetDeviceInfo().IsGLDevice() || m_Device.GetDeviceInfo().IsVulkanDevice()))
    {
        // If gl_PointSize is not defined, points are not rendered in GLES.
        Macros.Add("USE_GL_POINT_SIZE", "1");
    }

    const bool UseCombinedSamplers = m_Device.GetDeviceInfo().IsGLDevice();

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCreateInfo ShaderCI{
            "RenderPBR.vsh",
            pShaderSourceFactory,
            "main",
            Macros,
            SHADER_SOURCE_LANGUAGE_HLSL,
            {"PBR VS", SHADER_TYPE_VERTEX, UseCombinedSamplers},
        };

        std::string GLSLSource;
        if (m_Settings.PrimitiveArraySize > 0)
        {
            if (m_Device.GetDeviceInfo().IsGLDevice())
            {
                ShaderCI.GLSLExtensions = MultiDrawGLSLExtension;
            }
            else if (m_Device.GetDeviceInfo().IsVulkanDevice())
            {
#ifdef HLSL2GLSL_CONVERTER_SUPPORTED
                // Since we use gl_DrawID in HLSL, we need to manually convert the shader to GLSL
                HLSL2GLSLConverterImpl::ConversionAttribs Attribs;
                Attribs.pSourceStreamFactory       = ShaderCI.pShaderSourceStreamFactory;
                Attribs.EntryPoint                 = ShaderCI.EntryPoint;
                Attribs.ShaderType                 = ShaderCI.Desc.ShaderType;
                Attribs.InputFileName              = ShaderCI.FilePath;
                Attribs.SamplerSuffix              = UseCombinedSamplers ? ShaderCI.Desc.CombinedSamplerSuffix : ShaderDesc{}.CombinedSamplerSuffix;
                Attribs.UseInOutLocationQualifiers = true;
                Attribs.IncludeDefinitions         = true;

                GLSLSource = HLSL2GLSLConverterImpl::GetInstance().Convert(Attribs);
                if (GLSLSource.empty())
                {
                    UNEXPECTED("Failed to convert HLSL source to GLSL");
                }
                ShaderCI.FilePath       = nullptr;
                ShaderCI.Source         = GLSLSource.c_str();
                ShaderCI.SourceLength   = GLSLSource.length();
                ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL;
#else
                UNSUPPORTED("Primitive array on Vulkan requires HLSL2GLSL converter");
#endif
            }
            else
            {
                UNSUPPORTED("Primitive array is only supported in GL and Vulkan");
            }
        }

        pVS = m_Device.CreateShader(ShaderCI);
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCreateInfo ShaderCI{
            !IsUnshaded ? "RenderPBR.psh" : "RenderUnshaded.psh",
            pShaderSourceFactory,
            "main",
            Macros,
            SHADER_SOURCE_LANGUAGE_HLSL,
            {!IsUnshaded ? "PBR PS" : "Unshaded PS", SHADER_TYPE_PIXEL, UseCombinedSamplers},
        };
        pPS = m_Device.CreateShader(ShaderCI);
    }

    GraphicsPipeline             = GraphicsDesc;
    GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[MAX_RESOURCE_SIGNATURES];
    for (size_t i = 0; i < m_ResourceSignatures.size(); ++i)
        ppSignatures[i] = m_ResourceSignatures[i];
    PSOCreateInfo.ppResourceSignatures    = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount = static_cast<Uint32>(m_ResourceSignatures.size());

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
            RT0.SrcBlend       = BLEND_FACTOR_ONE;
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

            PsoHashMap[{PSOFlags, AlphaMode, DoubleSided, Key}] = PSO;
            if (AlphaMode == ALPHA_MODE_OPAQUE)
            {
                // Mask and opaque use the same PSO
                PsoHashMap[{PSOFlags, ALPHA_MODE_MASK, DoubleSided, Key}] = PSO;
            }
        }
    }
}

void PBR_Renderer::CreateResourceBinding(IShaderResourceBinding** ppSRB, Uint32 Idx) const
{
    m_ResourceSignatures[Idx]->CreateShaderResourceBinding(ppSRB, true);
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
    if (!m_Settings.EnableClearCoat)
    {
        Flags &= ~PSO_FLAG_ENABLE_CLEAR_COAT;
    }
    if (!m_Settings.EnableSheen)
    {
        Flags &= ~PSO_FLAG_ENABLE_SHEEN;
    }
    if (!m_Settings.EnableAnisotropy)
    {
        Flags &= ~PSO_FLAG_ENABLE_ANISOTROPY;
    }
    if (!m_Settings.EnableIridescence)
    {
        Flags &= ~PSO_FLAG_ENABLE_IRIDESCENCE;
    }
    if (!m_Settings.EnableTransmission)
    {
        Flags &= ~PSO_FLAG_ENABLE_TRANSMISSION;
    }
    if (!m_Settings.EnableVolume)
    {
        Flags &= ~PSO_FLAG_ENABLE_VOLUME;
    }
    if (!m_Settings.EnableShadows)
    {
        Flags &= ~PSO_FLAG_ENABLE_SHADOWS;
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
    if ((Flags & PSO_FLAG_ENABLE_SHEEN) == 0)
    {
        Flags &= ~(PSO_FLAG_USE_SHEEN_COLOR_MAP | PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP);
    }
    if ((Flags & PSO_FLAG_ENABLE_ANISOTROPY) == 0)
    {
        Flags &= ~PSO_FLAG_USE_ANISOTROPY_MAP;
    }
    if ((Flags & PSO_FLAG_ENABLE_IRIDESCENCE) == 0)
    {
        Flags &= ~(PSO_FLAG_USE_IRIDESCENCE_MAP | PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP);
    }
    if ((Flags & PSO_FLAG_ENABLE_TRANSMISSION) == 0)
    {
        Flags &= ~PSO_FLAG_USE_TRANSMISSION_MAP;
    }
    if ((Flags & PSO_FLAG_ENABLE_VOLUME) == 0)
    {
        Flags &= ~PSO_FLAG_USE_THICKNESS_MAP;
    }

    const PSOKey UpdatedKey{Flags, Key};

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
    Renderer.PrefilteredCubeLastMip = m_Settings.EnableIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels - 1) : 0.f;
}

Uint32 PBR_Renderer::GetPBRPrimitiveAttribsSize(PSO_FLAGS Flags) const
{
    //struct PBRPrimitiveAttribs
    //{
    //    GLTFNodeShaderTransforms Transforms;
    //    float4x4                 PrevNodeMatrix; // #if ENABLE_MOTION_VECTORS
    //    struct PBRMaterialShaderInfo
    //    {
    //        PBRMaterialBasicAttribs        Basic;
    //        PBRMaterialSheenAttribs        Sheen;        // #if ENABLE_SHEEN
    //        PBRMaterialAnisotropyAttribs   Anisotropy;   // #if ENABLE_ANISOTROPY
    //        PBRMaterialIridescenceAttribs  Iridescence;  // #if ENABLE_IRIDESCENCE
    //        PBRMaterialTransmissionAttribs Transmission; // #if ENABLE_TRANSMISSION
    //        PBRMaterialVolumeAttribs       Volume;       // #if ENABLE_VOLUME
    //        PBRMaterialTextureAttribs Textures[PBR_NUM_TEXTURE_ATTRIBUTES];
    //    } Material;
    //    float4 CustomData;
    //};

    Uint32 NumTextureAttribs = 0;
    ProcessTexturAttribs(Flags, [&](int CurrIndex, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) //
                         {
                             const int SrcAttribIndex = m_Settings.TextureAttribIndices[AttribId];
                             if (SrcAttribIndex >= 0)
                             {
                                 ++NumTextureAttribs;
                             }
                         });

    return (sizeof(HLSL::GLTFNodeShaderTransforms) +
            ((Flags & PSO_FLAG_COMPUTE_MOTION_VECTORS) ? sizeof(float4x4) : 0) +
            sizeof(HLSL::PBRMaterialBasicAttribs) +
            ((Flags & PSO_FLAG_ENABLE_SHEEN) ? sizeof(HLSL::PBRMaterialSheenAttribs) : 0) +
            ((Flags & PSO_FLAG_ENABLE_ANISOTROPY) ? sizeof(HLSL::PBRMaterialAnisotropyAttribs) : 0) +
            ((Flags & PSO_FLAG_ENABLE_IRIDESCENCE) ? sizeof(HLSL::PBRMaterialIridescenceAttribs) : 0) +
            ((Flags & PSO_FLAG_ENABLE_TRANSMISSION) ? sizeof(HLSL::PBRMaterialTransmissionAttribs) : 0) +
            ((Flags & PSO_FLAG_ENABLE_VOLUME) ? sizeof(HLSL::PBRMaterialVolumeAttribs) : 0) +
            sizeof(HLSL::PBRMaterialTextureAttribs) * NumTextureAttribs +
            sizeof(float4));
}

Uint32 PBR_Renderer::GetPRBFrameAttribsSize(Uint32 LightCount, Uint32 ShadowCastingLightCount)
{
    return (sizeof(HLSL::CameraAttribs) * 2 +
            sizeof(HLSL::PBRRendererShaderParameters) +
            sizeof(HLSL::PBRLightAttribs) * LightCount +
            sizeof(HLSL::PBRShadowMapInfo) * ShadowCastingLightCount);
}

Uint32 PBR_Renderer::GetPRBFrameAttribsSize() const
{
    return GetPRBFrameAttribsSize(m_Settings.MaxLightCount, m_Settings.MaxShadowCastingLightCount);
}

} // namespace Diligent
