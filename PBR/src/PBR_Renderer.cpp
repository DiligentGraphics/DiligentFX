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

PBR_Renderer::PSOKey::PSOKey(PSO_FLAGS            _Flags,
                             ALPHA_MODE           _AlphaMode,
                             CULL_MODE            _CullMode,
                             DebugViewType        _DebugView,
                             LoadingAnimationMode _LoadingAnimation,
                             Uint64               _UserValue) noexcept :
    Flags{_Flags},
    AlphaMode{_AlphaMode},
    CullMode{_CullMode},
    DebugView{_DebugView},
    LoadingAnimation{_LoadingAnimation},
    UserValue{_UserValue}
{
    if (Flags & PSO_FLAG_UNSHADED)
    {
        AlphaMode = ALPHA_MODE_OPAQUE;

        constexpr auto SupportedUnshadedFlags = PSO_FLAG_USE_JOINTS | PSO_FLAG_ALL_USER_DEFINED | PSO_FLAG_UNSHADED;
        Flags &= SupportedUnshadedFlags;

        DebugView = DebugViewType::None;
    }

    Hash = ComputeHash(Flags, AlphaMode, CullMode, static_cast<Uint32>(DebugView), static_cast<Uint32>(LoadingAnimation), UserValue);
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

const char* PBR_Renderer::GetAlphaModeString(ALPHA_MODE AlphaMode)
{
    switch (AlphaMode)
    {
        case ALPHA_MODE_OPAQUE: return "opaque";
        case ALPHA_MODE_MASK: return "mask";
        case ALPHA_MODE_BLEND: return "blend";
        default: UNEXPECTED("Unknown alpha mode");
    }
    return "";
}

template <typename FaceHandlerType>
void ProcessCubemapFaces(IDeviceContext* pCtx, ITexture* pCubemap, FaceHandlerType&& FaceHandler)
{
    const auto& CubemapDesc = pCubemap->GetDesc();
    for (Uint32 mip = 0; mip < CubemapDesc.MipLevels; ++mip)
    {
        for (Uint32 face = 0; face < 6; ++face)
        {
            const std::string Name = "RTV for face " + std::to_string(face) + " mip " + std::to_string(mip) + " of cubemap '" + CubemapDesc.Name + "'";
            TextureViewDesc   RTVDesc{Name.c_str(), TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY};
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pCubemap->CreateView(RTVDesc, &pRTV);
            VERIFY_EXPR(pCubemap);

            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            FaceHandler(pRTV, mip, face);
        }
    }
}

static void ClearCubemap(IDeviceContext* pCtx, ITexture* pCubemap)
{
    ProcessCubemapFaces(pCtx, pCubemap, [pCtx](ITextureView* pRTV, Uint32 mip, Uint32 face) {
        pCtx->ClearRenderTarget(pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    });
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
        TexDesc.Type      = RESOURCE_DIM_TEX_CUBE;
        TexDesc.Usage     = USAGE_DEFAULT;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        TexDesc.ArraySize = 6;

        {
            TexDesc.Name      = "Irradiance cube map for PBR renderer";
            TexDesc.Width     = IrradianceCubeDim;
            TexDesc.Height    = IrradianceCubeDim;
            TexDesc.Format    = IrradianceCubeFmt;
            TexDesc.MipLevels = 1;

            auto IrradainceCubeTex = m_Device.CreateTexture(TexDesc);
            VERIFY_EXPR(IrradainceCubeTex);
            ClearCubemap(pCtx, IrradainceCubeTex);

            m_pIrradianceCubeSRV = IrradainceCubeTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            VERIFY_EXPR(m_pIrradianceCubeSRV);
        }


        {
            TexDesc.Name      = "Prefiltered environment map for PBR renderer";
            TexDesc.Width     = PrefilteredEnvMapDim;
            TexDesc.Height    = PrefilteredEnvMapDim;
            TexDesc.Format    = PrefilteredEnvMapFmt;
            TexDesc.MipLevels = 0;

            auto PrefilteredEnvMapTex = m_Device.CreateTexture(TexDesc);
            VERIFY_EXPR(PrefilteredEnvMapTex);
            ClearCubemap(pCtx, PrefilteredEnvMapTex);

            m_pPrefilteredEnvMapSRV = PrefilteredEnvMapTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            VERIFY_EXPR(m_pPrefilteredEnvMapSRV);
        }
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
        LOG_INFO_MESSAGE("PBR Renderer objects: PSO: ", NumPSOs, "; VS: ", m_VertexShaders.size(), "; PS: ", m_PixelShaders.size());
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

static Uint32 GetDefaultDiffuseSamplesCount(const GraphicsAdapterInfo& AdapterInfo)
{
#if PLATFORM_WIN32 || PLATFORM_UNIVERSAL_WINDOWS || PLATFORM_LINUX || PLATFORM_MACOS
    {
        if (AdapterInfo.Type == ADAPTER_TYPE_DISCRETE)
        {
            // Any discrete GPU should be able to handle 8192 samples
            return 8192;
        }

        // Note that in OpenGL, the adapter type is not detected.
        if (AdapterInfo.Vendor == ADAPTER_VENDOR_NVIDIA ||
            AdapterInfo.Vendor == ADAPTER_VENDOR_AMD)
        {
            // NVidia and AMD GPUs should be able to handle 8192 samples
            return 8192;
        }

        if (AdapterInfo.Type == ADAPTER_TYPE_INTEGRATED)
            return 2048;

        if (AdapterInfo.Type == ADAPTER_TYPE_SOFTWARE)
            return 1024;

        // Desktop GPUs should be able to handle 4096 samples
        return 4096;
    }
#elif PLATFORM_ANDROID || PLATFORM_IOS || PLATFORM_TVOS
    {
        return 512;
    }
#elif PLATFORM_EMSCRIPTEN
    {
        return 1024;
    }
#else
    {
        return 512;
    }
#endif
}

void PBR_Renderer::PrecomputeCubemaps(IDeviceContext* pCtx,
                                      ITextureView*   pEnvironmentMap,
                                      Uint32          NumDiffuseSamples,
                                      Uint32          NumSpecularSamples,
                                      bool            OptimizeSamples)
{
    if (!m_Settings.EnableIBL)
    {
        LOG_WARNING_MESSAGE("IBL is disabled, so precomputing cube maps will have no effect");
        return;
    }

    if (NumSpecularSamples == 0)
    {
        NumSpecularSamples = 256;
    }
    if (NumDiffuseSamples == 0)
    {
        NumDiffuseSamples = GetDefaultDiffuseSamplesCount(m_Device.GetAdapterInfo());
    }

    struct PrecomputeEnvMapAttribs
    {
        float4x4 Rotation;

        float Roughness;
        float EnvMapWidth;
        float EnvMapHeight;
        float EnvMapMipCount;

        uint NumSamples;
        uint Padding0;
        uint Padding1;
        uint Padding2;
    };

    if (!m_PrecomputeEnvMapAttribsCB)
    {
        CreateUniformBuffer(m_Device, sizeof(PrecomputeEnvMapAttribs), "Precompute env map attribs CB", &m_PrecomputeEnvMapAttribsCB);
    }

    IBL_FEATURE_FLAGS FeatureFlags = IBL_FEATURE_FLAG_NONE;

    if (OptimizeSamples)
        FeatureFlags |= IBL_FEATURE_FLAG_OPTIMIZE_SAMPLES;

    const IBL_PSOKey::ENV_MAP_TYPE EnvMapType = pEnvironmentMap->GetTexture()->GetDesc().IsCube() ?
        IBL_PSOKey::ENV_MAP_TYPE_CUBE :
        IBL_PSOKey::ENV_MAP_TYPE_SPHERE;

    ShaderMacroHelper Macros;
    Macros
        .Add("OPTIMIZE_SAMPLES", (FeatureFlags & IBL_FEATURE_FLAG_OPTIMIZE_SAMPLES) != 0)
        .Add("ENV_MAP_TYPE_CUBE", static_cast<int>(IBL_PSOKey::ENV_MAP_TYPE_CUBE))
        .Add("ENV_MAP_TYPE_SPHERE", static_cast<int>(IBL_PSOKey::ENV_MAP_TYPE_SPHERE))
        .Add("ENV_MAP_TYPE", static_cast<int>(EnvMapType));

    auto& PrecomputeIrradianceCubeTech = m_IBL_PSOCache[IBL_PSOKey{IBL_PSOKey::PSO_TYPE_IRRADIANCE_CUBE, EnvMapType, FeatureFlags, m_pIrradianceCubeSRV->GetDesc().Format}];
    if (!PrecomputeIrradianceCubeTech.IsInitialized())
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        ShaderCI.Macros                     = Macros;
        // WebGPU only supports row-major matrices
        ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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
        GraphicsPipeline.RTVFormats[0]                = m_pIrradianceCubeSRV->GetDesc().Format;
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

        PrecomputeIrradianceCubeTech.PSO = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
        PrecomputeIrradianceCubeTech.PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        PrecomputeIrradianceCubeTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FilterAttribs")->Set(m_PrecomputeEnvMapAttribsCB);
        PrecomputeIrradianceCubeTech.PSO->CreateShaderResourceBinding(&PrecomputeIrradianceCubeTech.SRB, true);
    }

    auto& PrefilterEnvMapTech = m_IBL_PSOCache[IBL_PSOKey{IBL_PSOKey::PSO_TYPE_PREFILTERED_ENV_MAP, EnvMapType, FeatureFlags, m_pPrefilteredEnvMapSRV->GetDesc().Format}];
    if (!PrefilterEnvMapTech.IsInitialized())
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        ShaderCI.Macros                     = Macros;
        ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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
        GraphicsPipeline.RTVFormats[0]                = m_pPrefilteredEnvMapSRV->GetDesc().Format;
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

        PrefilterEnvMapTech.PSO = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
        PrefilterEnvMapTech.PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        PrefilterEnvMapTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FilterAttribs")->Set(m_PrecomputeEnvMapAttribsCB);
        PrefilterEnvMapTech.PSO->CreateShaderResourceBinding(&PrefilterEnvMapTech.SRB, true);
    }

    // clang-format off
    const std::array<float4x4, 6> Matrices =
    {
/* +X */ float4x4::RotationY(-PI_F / 2.f),
/* -X */ float4x4::RotationY(+PI_F / 2.f),
/* +Y */ float4x4::RotationX(+PI_F / 2.f),
/* -Y */ float4x4::RotationX(-PI_F / 2.f),
/* +Z */ float4x4::Identity(),
/* -Z */ float4x4::RotationY(-PI_F)
    };
    // clang-format on

    pCtx->SetPipelineState(PrecomputeIrradianceCubeTech.PSO);
    PrecomputeIrradianceCubeTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(PrecomputeIrradianceCubeTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto* pIrradianceCube = m_pIrradianceCubeSRV->GetTexture();
    ProcessCubemapFaces(pCtx, pIrradianceCube, [&](ITextureView* pRTV, Uint32 mip, Uint32 face) {
        VERIFY_EXPR(mip == 0);
        {
            MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
            Attribs->Rotation       = Matrices[face];
            Attribs->EnvMapWidth    = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().Width);
            Attribs->EnvMapHeight   = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().Height);
            Attribs->EnvMapMipCount = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().MipLevels);
            Attribs->NumSamples     = NumDiffuseSamples;
        }
        DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
        pCtx->Draw(drawAttrs);
    });
    // Release reference to the environment map
    PrecomputeIrradianceCubeTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(nullptr);


    pCtx->SetPipelineState(PrefilterEnvMapTech.PSO);
    PrefilterEnvMapTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(PrefilterEnvMapTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto* pPrefilteredEnvMap = m_pPrefilteredEnvMapSRV->GetTexture();
    ProcessCubemapFaces(pCtx, pPrefilteredEnvMap, [&](ITextureView* pRTV, Uint32 mip, Uint32 face) {
        {
            MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
            Attribs->Rotation       = Matrices[face];
            Attribs->Roughness      = static_cast<float>(mip) / static_cast<float>(pPrefilteredEnvMap->GetDesc().MipLevels - 1);
            Attribs->EnvMapWidth    = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().Width);
            Attribs->EnvMapHeight   = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().Height);
            Attribs->EnvMapMipCount = static_cast<float>(pEnvironmentMap->GetTexture()->GetDesc().MipLevels);
            Attribs->NumSamples     = NumSpecularSamples;
        }

        DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
        pCtx->Draw(drawAttrs);
    });

    // Release reference to the environment map
    PrefilterEnvMapTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(nullptr);


    // clang-format off
    StateTransitionDesc Barriers[] = 
    {
        {m_pPrefilteredEnvMapSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        {m_pIrradianceCubeSRV->GetTexture(),    RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
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
            SignatureDesc.AddResource(SHADER_TYPE_PIXEL, TextureName.c_str(), SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                      PIPELINE_RESOURCE_FLAG_NONE, WebGPUResourceAttribs{WEB_GPU_BINDING_TYPE_DEFAULT, RESOURCE_DIM_TEX_2D_ARRAY});
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
        SignatureDesc.AddResource(SHADER_TYPE_PIXEL, "g_MaterialTextures", MaterialTexturesArraySize, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                  PIPELINE_RESOURCE_FLAG_NONE, WebGPUResourceAttribs{WEB_GPU_BINDING_TYPE_DEFAULT, RESOURCE_DIM_TEX_2D_ARRAY});
    }

    auto AddTextureAndSampler = [&](const char*                   TextureName,
                                    const SamplerDesc&            SamDesc,
                                    const char*                   SeparateSamplerName,
                                    SHADER_RESOURCE_VARIABLE_TYPE VarType     = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                    const WebGPUResourceAttribs&  WGPUAttribs = {}) //
    {
        SignatureDesc.AddResource(SHADER_TYPE_PIXEL, TextureName, SHADER_RESOURCE_TYPE_TEXTURE_SRV, VarType, PIPELINE_RESOURCE_FLAG_NONE, WGPUAttribs);

        const char* SamplerName = m_Device.GetDeviceInfo().IsGLDevice() ? TextureName : SeparateSamplerName;
        if (Samplers.emplace(SamplerName).second)
        {
            SignatureDesc.AddImmutableSampler(SHADER_TYPE_PIXEL, SamplerName, SamDesc);
        }
    };

    if (m_Settings.EnableIBL)
    {
        constexpr WebGPUResourceAttribs WGPUCubeMap{WEB_GPU_BINDING_TYPE_DEFAULT, RESOURCE_DIM_TEX_CUBE};
        AddTextureAndSampler("g_PreintegratedGGX", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
        AddTextureAndSampler("g_IrradianceMap", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, WGPUCubeMap);
        AddTextureAndSampler("g_PrefilteredEnvMap", Sam_LinearClamp, "g_LinearClampSampler", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, WGPUCubeMap);

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
        constexpr WebGPUResourceAttribs WGPUShadowMap{WEB_GPU_BINDING_TYPE_DEPTH_TEXTURE, RESOURCE_DIM_TEX_2D_ARRAY};
        AddTextureAndSampler("g_ShadowMap", Sam_ComparisonLinearClamp, "g_ShadowMap_sampler", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, WGPUShadowMap);
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
        if (m_Device.GetDeviceInfo().Features.NativeMultiDraw)
        {
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
                UNEXPECTED("Native multi-draw is only expected in GL and Vulkan");
                PrimitiveID = "0";
            }
        }
        else
        {
            // Use instance ID as primitive ID
            PrimitiveID = "int(VSIn.InstanceID)";
        }
        Macros.Add("PRIMITIVE_ID", PrimitiveID);
    }

    Macros.Add("PBR_WORKFLOW_METALLIC_ROUGHNESS", static_cast<int>(PBR_WORKFLOW_METALL_ROUGH));
    Macros.Add("PBR_WORKFLOW_SPECULAR_GLOSINESS", static_cast<int>(PBR_WORKFLOW_SPEC_GLOSS));
    Macros.Add("PBR_WORKFLOW_UNLIT", static_cast<int>(PBR_WORKFLOW_UNLIT));

    Macros.Add("PBR_ALPHA_MODE_OPAQUE", static_cast<int>(ALPHA_MODE_OPAQUE));
    Macros.Add("PBR_ALPHA_MODE_MASK", static_cast<int>(ALPHA_MODE_MASK));
    Macros.Add("PBR_ALPHA_MODE_BLEND", static_cast<int>(ALPHA_MODE_BLEND));
    Macros.Add("PBR_ALPHA_MODE", static_cast<int>(Key.GetAlphaMode()));

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

    static_assert(static_cast<int>(LoadingAnimationMode::Count) == 3, "Did you add new loading animation mode? You may need to handle it here.");
    // clang-format off
    Macros.Add("LOADING_ANIMATION",               static_cast<int>(Key.GetLoadingAnimation()));
    Macros.Add("LOADING_ANIMATION_NONE",          static_cast<int>(LoadingAnimationMode::None));
    Macros.Add("LOADING_ANIMATION_ALWAYS",        static_cast<int>(LoadingAnimationMode::Always));
    Macros.Add("LOADING_ANIMATION_TRANSITIONING", static_cast<int>(LoadingAnimationMode::Transitioning));
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

    if (m_Device.GetDeviceInfo().IsWebGPUDevice())
    {
        std::string UnrolledMaterialTextureArray{"Texture2DArray "};
        for (Uint32 i = 0; i < m_Settings.MaterialTexturesArraySize; ++i)
        {
            UnrolledMaterialTextureArray += std::string{"g_MaterialTextures_"} + std::to_string(i);
            UnrolledMaterialTextureArray += i + 1 < m_Settings.MaterialTexturesArraySize ? ", " : ";";
        }
        Macros
            .Add("UNROLLED_MATERIAL_TEXTURES_ARRAY", UnrolledMaterialTextureArray)
            .Add("MATERIAL_TEXTURE(Idx)", "g_MaterialTextures_##Idx");
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
    //    float4 Color   : ATTRIB6; // May be float3
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

    InputLayout = m_Settings.InputLayout;
    InputLayout.ResolveAutoOffsetsAndStrides();

    Uint32 NumColorComp = 4;
    if (PSOFlags & PSO_FLAG_USE_VERTEX_COLORS)
    {
        for (Uint32 i = 0; i < InputLayout.GetNumElements(); ++i)
        {
            const auto& Elem = InputLayout[i];
            if (Elem.InputIndex == VERTEX_ATTRIB_ID_COLOR)
            {
                NumColorComp = Elem.NumComponents;
                DEV_CHECK_ERR(NumColorComp == 3 || NumColorComp == 4, "Color attribute must have 3 or 4 components");
                break;
            }
        }
    }

    const std::array<VSAttribInfo, 8> VSAttribs = //
        {
            // clang-format off
            VSAttribInfo{VERTEX_ATTRIB_ID_POSITION,  "Pos",     VT_FLOAT32, 3,            PSO_FLAG_NONE},
            VSAttribInfo{VERTEX_ATTRIB_ID_NORMAL,    "Normal",  VT_FLOAT32, 3,            PSO_FLAG_USE_VERTEX_NORMALS},
            VSAttribInfo{VERTEX_ATTRIB_ID_TEXCOORD0, "UV0",     VT_FLOAT32, 2,            PSO_FLAG_USE_TEXCOORD0},
            VSAttribInfo{VERTEX_ATTRIB_ID_TEXCOORD1, "UV1",     VT_FLOAT32, 2,            PSO_FLAG_USE_TEXCOORD1},
            VSAttribInfo{VERTEX_ATTRIB_ID_JOINTS,    "Joint0",  VT_FLOAT32, 4,            PSO_FLAG_USE_JOINTS},
            VSAttribInfo{VERTEX_ATTRIB_ID_WEIGHTS,   "Weight0", VT_FLOAT32, 4,            PSO_FLAG_USE_JOINTS},
            VSAttribInfo{VERTEX_ATTRIB_ID_COLOR,     "Color",   VT_FLOAT32, NumColorComp, PSO_FLAG_USE_VERTEX_COLORS},
            VSAttribInfo{VERTEX_ATTRIB_ID_TANGENT,   "Tangent", VT_FLOAT32, 3,            PSO_FLAG_USE_VERTEX_TANGENTS}
            // clang-format on
        };

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
            ss << "    float" << Attrib.NumComponents << std::setw(9) << Attrib.Name << " : ATTRIB" << Attrib.Index << ";" << std::endl;
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

    if (m_Settings.PrimitiveArraySize > 0 && !m_Device.GetDeviceInfo().Features.NativeMultiDraw)
    {
        // Draw id is emulated using instance id
        ss << "    uint InstanceID : SV_InstanceID;" << std::endl;
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
        ss << "    int PrimitiveID : PRIM_ID;" << std::endl;
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

void PBR_Renderer::CreatePSO(PsoHashMapType&             PsoHashMap,
                             const GraphicsPipelineDesc& GraphicsDesc,
                             const PSOKey&               Key,
                             bool                        AsyncCompile)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    if (AsyncCompile)
        PSOCreateInfo.Flags |= PSO_CREATE_FLAG_ASYNCHRONOUS;

    const PSO_FLAGS PSOFlags   = Key.GetFlags();
    const bool      IsUnshaded = (PSOFlags & PSO_FLAG_UNSHADED) != 0;

#ifdef DILIGENT_DEVELOPMENT
    {
        std::stringstream msg_ss;
        msg_ss << "PBR Renderer: creating PSO with flags: " << GetPSOFlagsString(PSOFlags)
               << ": cull: " << GetCullModeLiteralName(Key.GetCullMode())
               << "; alpha: " << GetAlphaModeString(Key.GetAlphaMode());
        if (Key.GetDebugView() != DebugViewType::None)
        {
            msg_ss << "; debug view: " << static_cast<int>(Key.GetDebugView());
        }
        if (Key.GetLoadingAnimation() != LoadingAnimationMode::None)
        {
            msg_ss << "; loading animation: " << static_cast<int>(Key.GetLoadingAnimation());
        }
        if (Key.GetUserValue() != 0)
        {
            msg_ss << "; user value: " << Key.GetUserValue();
        }
        LOG_INFO_MESSAGE(msg_ss.str());
    }
#endif

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
                                            MemoryShaderSourceFileInfo{"VSInputStruct.generated", AsyncCompile ? *m_GeneratedIncludes.emplace(VSInputStruct).first : VSInputStruct},
                                            MemoryShaderSourceFileInfo{"VSOutputStruct.generated", AsyncCompile ? *m_GeneratedIncludes.emplace(VSOutputStruct).first : VSOutputStruct},
                                            MemoryShaderSourceFileInfo{"PSOutputStruct.generated", AsyncCompile ? *m_GeneratedIncludes.emplace(PSMainSource.OutputStruct).first : PSMainSource.OutputStruct},
                                            MemoryShaderSourceFileInfo{"PSMainFooter.generated", AsyncCompile ? *m_GeneratedIncludes.emplace(PSMainSource.Footer).first : PSMainSource.Footer},
                                        },
                                        CopyGeneratedStrings);
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory =
        CreateCompoundShaderSourceFactory({&DiligentFXShaderSourceStreamFactory::GetInstance(), pMemorySourceFactory});

    ShaderMacroHelper Macros = DefineMacros(Key);

    const bool UseGLPointSize =
        (GraphicsDesc.PrimitiveTopology == PRIMITIVE_TOPOLOGY_POINT_LIST) &&
        (m_Device.GetDeviceInfo().IsGLDevice() || m_Device.GetDeviceInfo().IsVulkanDevice());
    if (UseGLPointSize)
    {
        // If gl_PointSize is not defined, points are not rendered in GLES.
        Macros.Add("USE_GL_POINT_SIZE", "1");
    }

    const bool UseCombinedSamplers = m_Device.GetDeviceInfo().IsGLDevice();

    const SHADER_COMPILE_FLAGS ShaderCompileFlags =
        (AsyncCompile ? SHADER_COMPILE_FLAG_ASYNCHRONOUS : SHADER_COMPILE_FLAG_NONE) |
        (m_Settings.PackMatrixRowMajor ? SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR : SHADER_COMPILE_FLAG_NONE);

    RefCntAutoPtr<IShader>& pVS = m_VertexShaders[{
        PSOFlags,
        ALPHA_MODE_OPAQUE,
        // Cull mode is irrelevant for the shader, but we need different keys when GL point size is used,
        // so we use the cull mode to differentiate between the two.
        UseGLPointSize ? CULL_MODE_NONE : CULL_MODE_BACK,
        DebugViewType::None,
        LoadingAnimationMode::None,
        Key.GetUserValue(),
    }];
    if (!pVS)
    {
        ShaderCreateInfo ShaderCI{
            "RenderPBR.vsh",
            pShaderSourceFactory,
            "main",
            Macros,
            SHADER_SOURCE_LANGUAGE_HLSL,
            {"PBR VS", SHADER_TYPE_VERTEX, UseCombinedSamplers},
        };
        ShaderCI.CompileFlags = ShaderCompileFlags;

        std::string GLSLSource;
        if (m_Settings.PrimitiveArraySize > 0)
        {
            if (m_Device.GetDeviceInfo().Features.NativeMultiDraw)
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
                    UNEXPECTED("Native multi-draw is only expected in GL and Vulkan");
                }
            }
        }

        pVS = m_Device.CreateShader(ShaderCI);
    }

    RefCntAutoPtr<IShader>& pPS = m_PixelShaders[{
        PSOFlags,
        // Opaque and Blend modes use the same shader
        Key.GetAlphaMode() == ALPHA_MODE_MASK ? ALPHA_MODE_MASK : ALPHA_MODE_OPAQUE,
        CULL_MODE_BACK,
        Key,
    }];
    if (!pPS)
    {
        ShaderCreateInfo ShaderCI{
            !IsUnshaded ? "RenderPBR.psh" : "RenderUnshaded.psh",
            pShaderSourceFactory,
            "main",
            Macros,
            SHADER_SOURCE_LANGUAGE_HLSL,
            {!IsUnshaded ? "PBR PS" : "Unshaded PS", SHADER_TYPE_PIXEL, UseCombinedSamplers},
        };
        ShaderCI.CompileFlags                   = ShaderCompileFlags;
        ShaderCI.WebGPUEmulatedArrayIndexSuffix = "_";

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

    const ALPHA_MODE AlphaMode = Key.GetAlphaMode();
    if (AlphaMode == ALPHA_MODE_OPAQUE ||
        AlphaMode == ALPHA_MODE_MASK)
    {
        PSOCreateInfo.GraphicsPipeline.BlendDesc = BS_Default;
    }
    else if (AlphaMode == ALPHA_MODE_BLEND)
    {
        VERIFY(!IsUnshaded, "Unshaded mode should use OpaquePSO. The PSOKey's ctor sets the alpha mode to opaque.");

        auto& RT0          = GraphicsPipeline.BlendDesc.RenderTargets[0];
        RT0.BlendEnable    = true;
        RT0.SrcBlend       = BLEND_FACTOR_ONE;
        RT0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
        RT0.BlendOp        = BLEND_OPERATION_ADD;
        RT0.SrcBlendAlpha  = BLEND_FACTOR_ONE;
        RT0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        RT0.BlendOpAlpha   = BLEND_OPERATION_ADD;
    }
    else
    {
        UNEXPECTED("Unknown alpha mode");
    }

    std::string PSOName{!IsUnshaded ? "PBR PSO " : "Unshaded PSO "};
    PSOName += std::to_string(Uint64{PSOFlags});
    PSOName += " - ";
    PSOName += GetAlphaModeString(AlphaMode);
    PSOName += " - ";
    PSOName += GetCullModeLiteralName(Key.GetCullMode());
    PSOName += " - ";
    PSOName += std::to_string(static_cast<int>(Key.GetDebugView()));
    PSOName += " - ";
    PSOName += std::to_string(static_cast<int>(Key.GetLoadingAnimation()));
    PSOName += " - ";
    PSOName += std::to_string(Key.GetUserValue());
    PSODesc.Name = PSOName.c_str();

    GraphicsPipeline.RasterizerDesc.CullMode = Key.GetCullMode();
    RefCntAutoPtr<IPipelineState> PSO        = m_Device.CreateGraphicsPipelineState(PSOCreateInfo);
    VERIFY_EXPR(PSO);

    PsoHashMap[Key] = PSO;
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
                                     PsoCacheAccessor::GET_FLAGS GetFlags)
{
    PSO_FLAGS Flags = Key.GetFlags();
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
        if (GetFlags & PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL)
        {
            CreatePSO(PsoHashMap, GraphicsDesc, UpdatedKey, GetFlags & PsoCacheAccessor::GET_FLAG_ASYNC_COMPILE);
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

Uint32 PBR_Renderer::GetPBRPrimitiveAttribsSize(PSO_FLAGS Flags, Uint32 CustomDataSize) const
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
    //    UserDefined CustomData;
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
            CustomDataSize);
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
