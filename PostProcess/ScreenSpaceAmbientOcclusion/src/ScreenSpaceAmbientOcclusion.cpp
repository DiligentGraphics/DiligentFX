/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "ScreenSpaceAmbientOcclusion.hpp"
#include "CommonlyUsedStates.h"
#include "RenderStateCache.hpp"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"
#include "GraphicsTypesX.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "ScreenSpaceReflection.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
} // namespace HLSL

ScreenSpaceAmbientOcclusion::ScreenSpaceAmbientOcclusion(IRenderDevice* pDevice) :
    m_SSAOAttribs{std::make_unique<HLSL::ScreenSpaceAmbientOcclusionAttribs>()}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs), "ScreenSpaceAmbientOcclusion::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_SSAOAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}

ScreenSpaceAmbientOcclusion::~ScreenSpaceAmbientOcclusion() = default;

void ScreenSpaceAmbientOcclusion::PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc         = pPostFXContext->GetFrameDesc();
    const auto& SupportedFeatures = pPostFXContext->GetSupportedFeatures();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height && m_FeatureFlags == FeatureFlags)
        return;

    for (auto& Iter : m_RenderTech)
        Iter.second.SRB.Release();

    m_BackBufferWidth  = FrameDesc.Width;
    m_BackBufferHeight = FrameDesc.Height;
    m_FeatureFlags     = FeatureFlags;

    RenderDeviceWithCache_N Device{pDevice};

    constexpr Uint32 DepthPrefilteredMipCount = SSAO_DEPTH_PREFILTERED_MAX_MIP + 1;
    {
        m_PrefilteredDepthMipMapRTV.clear();
        m_PrefilteredDepthMipMapSRV.clear();

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::DepthPrefiltered";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferWidth / 2 : m_BackBufferWidth;
        Desc.Height    = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferHeight / 2 : m_BackBufferHeight;
        Desc.Format    = (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) ? TEX_FORMAT_R16_UNORM : TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(m_BackBufferWidth, m_BackBufferHeight), DepthPrefilteredMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_PREFILTERED, Device.CreateTexture(Desc, nullptr));
        m_PrefilteredDepthMipMapSRV.resize(Desc.MipLevels);
        m_PrefilteredDepthMipMapRTV.resize(Desc.MipLevels);

        for (Uint32 MipLevel = 0; MipLevel < Desc.MipLevels; MipLevel++)
        {
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_RENDER_TARGET;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture()->CreateView(ViewDesc, &m_PrefilteredDepthMipMapRTV[MipLevel]);
            }

            if (SupportedFeatures.TextureSubresourceViews)
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_SHADER_RESOURCE;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture()->CreateView(ViewDesc, &m_PrefilteredDepthMipMapSRV[MipLevel]);
            }
        }
    }

    if (!SupportedFeatures.TextureSubresourceViews)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::DepthPrefilteredIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferWidth / 2 : m_BackBufferWidth;
        Desc.Height    = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferHeight / 2 : m_BackBufferHeight;
        Desc.Format    = (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) ? TEX_FORMAT_R16_UNORM : TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(m_BackBufferWidth, m_BackBufferHeight), DepthPrefilteredMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE, Device.CreateTexture(Desc));
    }

    constexpr Uint32 DepthHistoryConvolutedMipCount = SSAO_DEPTH_HISTORY_CONVOLUTED_MAX_MIP + 1;
    {
        m_ConvolutedHistoryMipMapRTV.clear();
        m_ConvolutedHistoryMipMapSRV.clear();

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::OcclusionHistoryConvoluted";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(Desc.Width, Desc.Height), DepthHistoryConvolutedMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED, Device.CreateTexture(Desc, nullptr));
        m_ConvolutedHistoryMipMapRTV.resize(Desc.MipLevels);
        m_ConvolutedHistoryMipMapSRV.resize(Desc.MipLevels);

        for (Uint32 MipLevel = 0; MipLevel < Desc.MipLevels; MipLevel++)
        {
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_RENDER_TARGET;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture()->CreateView(ViewDesc, &m_ConvolutedHistoryMipMapRTV[MipLevel]);
            }

            if (SupportedFeatures.TextureSubresourceViews)
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_SHADER_RESOURCE;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture()->CreateView(ViewDesc, &m_ConvolutedHistoryMipMapSRV[MipLevel]);
            }
        }
    }

    if (!SupportedFeatures.TextureSubresourceViews)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::OcclusionConvolutedIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(Desc.Width, Desc.Height), DepthPrefilteredMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED_INTERMEDIATE, Device.CreateTexture(Desc));
    }

    {
        m_ConvolutedDepthMipMapRTV.clear();
        m_ConvolutedDepthMipMapSRV.clear();

        TextureDesc Desc;
        Desc.Name   = "ScreenSpaceAmbientOcclusion::DepthConvoluted";
        Desc.Type   = RESOURCE_DIM_TEX_2D;
        Desc.Width  = m_BackBufferWidth;
        Desc.Height = m_BackBufferHeight;
        Desc.Format = (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) ? TEX_FORMAT_R16_UNORM : TEX_FORMAT_R32_FLOAT;
        ;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(Desc.Width, Desc.Height), DepthHistoryConvolutedMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED, Device.CreateTexture(Desc, nullptr));
        m_ConvolutedDepthMipMapRTV.resize(Desc.MipLevels);
        m_ConvolutedDepthMipMapSRV.resize(Desc.MipLevels);

        for (Uint32 MipLevel = 0; MipLevel < Desc.MipLevels; MipLevel++)
        {
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_RENDER_TARGET;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture()->CreateView(ViewDesc, &m_ConvolutedDepthMipMapRTV[MipLevel]);
            }

            if (SupportedFeatures.TextureSubresourceViews)
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_SHADER_RESOURCE;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture()->CreateView(ViewDesc, &m_ConvolutedDepthMipMapSRV[MipLevel]);
            }
        }
    }

    if (!SupportedFeatures.TextureSubresourceViews)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::DepthConvolutedIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) ? TEX_FORMAT_R16_UNORM : TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(Desc.Width, Desc.Height), DepthHistoryConvolutedMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED_INTERMEDIATE, Device.CreateTexture(Desc));
    }

    m_Resources[RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES].Release();
    if (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::DepthCheckerboard";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::Occlusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferWidth / 2 : m_BackBufferWidth;
        Desc.Height    = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_BackBufferHeight / 2 : m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION, Device.CreateTexture(Desc));
    }

    m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED].Release();
    if (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::UpsampledOcclusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED, Device.CreateTexture(Desc));
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_OCCLUSION_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name          = "ScreenSpaceAmbientOcclusion::OcclusionHistory";
        Desc.Type          = RESOURCE_DIM_TEX_2D;
        Desc.Width         = m_BackBufferWidth;
        Desc.Height        = m_BackBufferHeight;
        Desc.Format        = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags     = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        auto  pTexture     = Device.CreateTexture(Desc);
        float ClearColor[] = {1.0, 0.0, 0.0, 0.0};
        PostFXContext::ClearRenderTarget(pDeviceContext, pTexture, ClearColor);
        m_Resources.Insert(TextureIdx, pTexture);
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0; TextureIdx <= RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name          = "ScreenSpaceAmbientOcclusion::OcclusionHistoryLength";
        Desc.Type          = RESOURCE_DIM_TEX_2D;
        Desc.Width         = m_BackBufferWidth;
        Desc.Height        = m_BackBufferHeight;
        Desc.Format        = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags     = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        auto  pTexture     = Device.CreateTexture(Desc);
        float ClearColor[] = {1.0, 0.0, 0.0, 0.0};
        PostFXContext::ClearRenderTarget(pDeviceContext, pTexture, ClearColor);
        m_Resources.Insert(TextureIdx, pTexture);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::OcclusionHistoyResampled";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESAMPLED, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::OcclusionHistoyResolved";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED, Device.CreateTexture(Desc));
    }
}

void ScreenSpaceAmbientOcclusion::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pNormalBufferSRV != nullptr, "RenderAttribs.pNormalBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pSSAOAttribs != nullptr, "RenderAttribs.pSSAOAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_DEPTH, RenderAttribs.pDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_NORMAL, RenderAttribs.pNormalBufferSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "ScreenSpaceAmbientOcclusion"};

    bool ResetAccumulation =
        m_LastFrameIdx == ~0u ||                            // No history on the first frame
        m_CurrentFrameIdx != m_LastFrameIdx + 1 ||          // Reset history if frames were skipped
        RenderAttribs.pSSAOAttribs->ResetAccumulation != 0; // Reset history if requested

    bool UpdateConstantBuffer = false;
    if (memcmp(RenderAttribs.pSSAOAttribs, m_SSAOAttribs.get(), sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs)) != 0)
    {
        UpdateConstantBuffer = true;
        memcpy(m_SSAOAttribs.get(), RenderAttribs.pSSAOAttribs, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs));
    }

    if (ResetAccumulation && (m_SSAOAttribs->ResetAccumulation != 0) != ResetAccumulation)
    {
        m_SSAOAttribs->ResetAccumulation = 1;
        UpdateConstantBuffer             = true;
    }

    if (UpdateConstantBuffer)
    {
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs), m_SSAOAttribs.get(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    ComputeDepthCheckerboard(RenderAttribs);
    ComputePrefilteredDepth(RenderAttribs);
    ComputeAmbientOcclusion(RenderAttribs);
    ComputeBilateralUpsampling(RenderAttribs);
    ComputeTemporalAccumulation(RenderAttribs);
    ComputeConvolutedDepthHistory(RenderAttribs);
    ComputeResampledHistory(RenderAttribs);
    ComputeSpatialReconstruction(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();

    m_LastFrameIdx = m_CurrentFrameIdx;
}

bool ScreenSpaceAmbientOcclusion::UpdateUI(HLSL::ScreenSpaceAmbientOcclusionAttribs& SSAOAttribs, FEATURE_FLAGS& FeatureFlags)
{
    const char* AlgorithmTypeNames[] = {"GTAO", "HBAO"};

    Int32 AlgorithmType             = FeatureFlags & FEATURE_FLAG_UNIFORM_WEIGHTING;
    bool  FeatureHalfResolution     = FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION;
    bool  FeatureHalfPrecisionDepth = FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH;

    bool AttribsChanged = false;

    if (ImGui::Combo("Algorithm", &AlgorithmType, AlgorithmTypeNames, _countof(AlgorithmTypeNames)))
        AttribsChanged = true;
    ImGui::HelpMarker("GTAO uses a cosine-weighted sum to calculate AO. In the HBAO, the contribution from all directions is uniform weighted");

    if (ImGui::SliderFloat("Effect Radius", &SSAOAttribs.EffectRadius, 0.0f, 10.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("World-space radius of ambient occlusion");

    if (ImGui::SliderFloat("Effect Falloff Range", &SSAOAttribs.EffectFalloffRange, 0.0f, 1.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The value gently reduces sample impact as it gets out of the 'Effect radius' bounds");

    if (ImGui::SliderFloat("Radius Multiplier", &SSAOAttribs.RadiusMultiplier, 0.3f, 3.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The value allows using different value as compared to the ground truth radius to counter inherent screen space biases");

    if (ImGui::SliderFloat("Depth MIP Sampling Offset", &SSAOAttribs.DepthMIPSamplingOffset, 2.0, 6.0))
        AttribsChanged = true;
    ImGui::HelpMarker("Defines the main trade-off between the performance (memory bandwidth) and quality (temporal stability is affected first, followed by thin objects)");

    if (ImGui::SliderFloat("Temporal Stability Factor", &SSAOAttribs.TemporalStabilityFactor, 0.0, 1.0))
        AttribsChanged = true;
    ImGui::HelpMarker("Controls the interpolation between the current and previous frames");

    if (ImGui::SliderFloat("Spatial Reconstruction", &SSAOAttribs.SpatialReconstructionRadius, 0.0, 8.0))
        AttribsChanged = true;
    ImGui::HelpMarker("Controls the kernel size in the spatial reconstruction step. Increasing the value increases the deviation from the ground truth but reduces the noise");

    if (ImGui::Checkbox("Enable Half Resolution", &FeatureHalfResolution))
        AttribsChanged = true;
    ImGui::HelpMarker("Calculate ambient occlusion at half resolution");

    if (ImGui::Checkbox("Enable Half Precision Depth", &FeatureHalfPrecisionDepth))
        AttribsChanged = true;
    ImGui::HelpMarker("Use 16-bit depth to compute ambient occlusion");


    auto ResetStateFeatureMask = [](FEATURE_FLAGS& FeatureFlags, FEATURE_FLAGS Flag, bool State) {
        if (State)
            FeatureFlags |= Flag;
        else
            FeatureFlags &= ~Flag;
    };

    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_UNIFORM_WEIGHTING, static_cast<bool>(AlgorithmType));
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_HALF_RESOLUTION, FeatureHalfResolution);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_HALF_PRECISION_DEPTH, FeatureHalfPrecisionDepth);

    return AttribsChanged;
}

ITextureView* ScreenSpaceAmbientOcclusion::GetAmbientOcclusionSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED].GetTextureSRV();
}

void ScreenSpaceAmbientOcclusion::CopyTextureDepth(const RenderAttributes& RenderAttribs, ITextureView* pSRV, ITextureView* pRTV)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COPY_DEPTH, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "CopyTextureDepth.fx", "CopyDepthPS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 nullptr, "ScreenSpaceAmbientOcclusion::CopyDepth",
                                 VS, PS, ResourceLayout,
                                 {
                                     pRTV->GetTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(pSRV);

    RenderAttribs.pDeviceContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeDepthCheckerboard(const RenderAttributes& RenderAttribs)
{
    if (!(m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION))
        return;

    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_DOWNSAMPLED_DEPTH_BUFFER, m_FeatureFlags);

    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeDownsampledDepth.fx", "ComputeDownsampledDepthPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 nullptr, "ScreenSpaceAmbientOcclusion::ComputeDownsampledDepth",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeDownsampledDepth"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputePrefilteredDepth(const RenderAttributes& RenderAttribs)
{
    auto&       RenderTech        = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_DEPTH_BUFFER, m_FeatureFlags);
    const auto& SupportedFeatures = RenderAttribs.pPostFXContext->GetSupportedFeatures();
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SUPPORTED_SHADER_SRV", SupportedFeatures.TextureSubresourceViews);
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputePrefilteredDepthBuffer.fx", "ComputePrefilteredDepthBufferPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
        ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        if (SupportedFeatures.TextureSubresourceViews)
        {
            ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
        }
        else
        {
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMips", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                // Immutable samplers are required for WebGL to work properly
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureMips", Sam_PointWrap);
        }

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputePrefilteredDepthBuffer",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeHierarchicalDepthBuffer"};

    auto DepthResource = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_Resources[RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES] : m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH];

    CopyTextureDepth(RenderAttribs, DepthResource.GetTextureSRV(), m_PrefilteredDepthMipMapRTV[0]);

    if (!SupportedFeatures.TextureSubresourceViews)
    {
        CopyTextureAttribs CopyAttribs;
        CopyAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED];
        CopyAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE];
        CopyAttribs.SrcMipLevel              = 0;
        CopyAttribs.DstMipLevel              = 0;
        CopyAttribs.SrcSlice                 = 0;
        CopyAttribs.DstSlice                 = 0;
        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
    }

    if (SupportedFeatures.TransitionSubresources)
    {
        StateTransitionDesc TransitionDescW2W[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture(),
                                RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET,
                                STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2W), TransitionDescW2W);

        ShaderResourceVariableX TextureLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureLastMip"};
        for (Uint32 MipLevel = 1; MipLevel < m_PrefilteredDepthMipMapRTV.size(); MipLevel++)
        {
            StateTransitionDesc TranslationW2R[] = {
                StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture(),
                                    RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                    MipLevel - 1, 1, 0, REMAINING_ARRAY_SLICES,
                                    STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_NONE},
            };

            TextureLastMipSV.Set(m_PrefilteredDepthMipMapSRV[MipLevel - 1]);
            RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TranslationW2R), TranslationW2R);
            RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_PrefilteredDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
            RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        }

        StateTransitionDesc TransitionDescW2R[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].AsTexture(),
                                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                static_cast<Uint32>(m_PrefilteredDepthMipMapRTV.size() - 1), 1, 0, REMAINING_ARRAY_SLICES,
                                STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2R), TransitionDescW2R);
    }
    else
    {
        if (SupportedFeatures.TextureSubresourceViews)
        {
            ShaderResourceVariableX TextureLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureLastMip"};
            for (Uint32 MipLevel = 1; MipLevel < m_PrefilteredDepthMipMapRTV.size(); MipLevel++)
            {
                TextureLastMipSV.Set(m_PrefilteredDepthMipMapSRV[MipLevel - 1]);
                RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_PrefilteredDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
            }
        }
        else
        {
            ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMips"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE].GetTextureSRV());

            for (Uint32 MipLevel = 1; MipLevel < m_PrefilteredDepthMipMapRTV.size(); MipLevel++)
            {
                // We use StartVertexLocation to pass the mipmap level of the depth texture for convolution
                VERIFY_EXPR(SupportedFeatures.ShaderBaseVertexOffset);
                const Uint32 VertexOffset = 3u * (MipLevel - 1);
                RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_PrefilteredDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1, VertexOffset});
                RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                CopyTextureAttribs CopyMipAttribs;
                CopyMipAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED];
                CopyMipAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE];
                CopyMipAttribs.SrcMipLevel              = MipLevel;
                CopyMipAttribs.DstMipLevel              = MipLevel;
                CopyMipAttribs.SrcSlice                 = 0;
                CopyMipAttribs.DstSlice                 = 0;
                CopyMipAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                CopyMipAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                RenderAttribs.pDeviceContext->CopyTexture(CopyMipAttribs);
            }
        }
    }
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeAmbientOcclusion(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_AMBIENT_OCCLUSION, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("SSAO_OPTION_UNIFORM_WEIGHTING", (m_FeatureFlags & FEATURE_FLAG_UNIFORM_WEIGHTING) != 0);
        Macros.Add("SSAO_OPTION_HALF_RESOLUTION", (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) != 0);
        Macros.Add("SSAO_OPTION_HALF_PRECISION_DEPTH", (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeAmbientOcclusion.fx", "ComputeAmbientOcclusionPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureBlueNoise", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth", Sam_PointClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureNormal", Sam_PointClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeAmbientOcclusion",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureBlueNoise"}.Set(RenderAttribs.pPostFXContext->Get2DBlueNoiseSRV(PostFXContext::BLUE_NOISE_DIMENSION_ZW));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeAmbientOcclusion"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].GetTextureRTV(),
    };

    float ClearColor[] = {1.0, 0.0, 0.0, 0.0};

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeBilateralUpsampling(const RenderAttributes& RenderAttribs)
{
    if (!(m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION))
        return;

    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BILATERAL_UPSAMPLING, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeBilateralUpsampling.fx", "ComputeBilateralUpsamplingPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDepth", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureOcclusion", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeBilateralUpsampling",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBilateralUpsampling"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeTemporalAccumulation.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureHistory", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureCurrOcclusion", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;
    const Uint32 PrevFrameIdx = (FrameIndex + 1) & 0x01;

    auto OcclusionResource = (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) ? m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED] :
                                                                               m_Resources[RESOURCE_IDENTIFIER_OCCLUSION];

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrOcclusion"}.Set(OcclusionResource.GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistory"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrDepth"}.Set(RenderAttribs.pPostFXContext->GetReprojectedDepth());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevDepth"}.Set(RenderAttribs.pPostFXContext->GetPreviousDepth());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(RenderAttribs.pPostFXContext->GetClosestMotionVectors());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeTemporalAccumulation"};

    float ClearColor[] = {1.0, 0.0, 0.0, 0.0};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + CurrFrameIdx].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0 + CurrFrameIdx].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[1], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeConvolutedDepthHistory(const RenderAttributes& RenderAttribs)
{
    auto&       RenderTech        = GetRenderTechnique(RENDER_TECH_COMPUTE_CONVOLUTED_DEPTH_HISTORY, m_FeatureFlags);
    const auto& SupportedFeatures = RenderAttribs.pPostFXContext->GetSupportedFeatures();
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SUPPORTED_SHADER_SRV", SupportedFeatures.TextureSubresourceViews);
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeConvolutedDepthHistory.fx", "ComputeConvolutedDepthHistoryPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        if (SupportedFeatures.TextureSubresourceViews)
        {
            ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureHistoryLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
            ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepthLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
        }
        else
        {
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureHistoryMips", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepthMips", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                // Immutable samplers are required for WebGL to work properly
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureHistoryMips", Sam_PointWrap)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDepthMips", Sam_PointWrap);
        }

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeConvolutedDepthHistory",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeConvolutedDepthHistory"};

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    {
        CopyTextureAttribs CopyAttribsHistory;
        CopyAttribsHistory.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + CurrFrameIdx];
        CopyAttribsHistory.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED];
        CopyAttribsHistory.SrcMipLevel              = 0;
        CopyAttribsHistory.DstMipLevel              = 0;
        CopyAttribsHistory.SrcSlice                 = 0;
        CopyAttribsHistory.DstSlice                 = 0;
        CopyAttribsHistory.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribsHistory.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribsHistory);

        CopyTextureDepth(RenderAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV(), m_ConvolutedDepthMipMapRTV[0]);
    }

    if (!SupportedFeatures.TextureSubresourceViews)
    {
        CopyTextureAttribs CopyAttribsHistory;
        CopyAttribsHistory.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED];
        CopyAttribsHistory.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED_INTERMEDIATE];
        CopyAttribsHistory.SrcMipLevel              = 0;
        CopyAttribsHistory.DstMipLevel              = 0;
        CopyAttribsHistory.SrcSlice                 = 0;
        CopyAttribsHistory.DstSlice                 = 0;
        CopyAttribsHistory.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribsHistory.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribsHistory);

        CopyTextureAttribs CopyAttribsDepth;
        CopyAttribsDepth.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED];
        CopyAttribsDepth.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED_INTERMEDIATE];
        CopyAttribsDepth.SrcMipLevel              = 0;
        CopyAttribsDepth.DstMipLevel              = 0;
        CopyAttribsDepth.SrcSlice                 = 0;
        CopyAttribsDepth.DstSlice                 = 0;
        CopyAttribsDepth.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribsDepth.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribsDepth);
    }

    if (SupportedFeatures.TransitionSubresources)
    {
        StateTransitionDesc TransitionDescW2W[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture(),
                                RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE},

            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture(),
                                RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2W), TransitionDescW2W);

        ShaderResourceVariableX TextureHistoryLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistoryLastMip"};
        ShaderResourceVariableX TextureDepthLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepthLastMip"};

        for (Uint32 MipLevel = 1; MipLevel < m_ConvolutedHistoryMipMapRTV.size(); MipLevel++)
        {
            StateTransitionDesc TranslationW2R[] = {
                StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture(),
                                    RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                    MipLevel - 1, 1, 0, REMAINING_ARRAY_SLICES,
                                    STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_NONE},
                StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture(),
                                    RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                    MipLevel - 1, 1, 0, REMAINING_ARRAY_SLICES,
                                    STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_NONE},
            };

            TextureHistoryLastMipSV.Set(m_ConvolutedHistoryMipMapSRV[MipLevel - 1]);
            TextureDepthLastMipSV.Set(m_ConvolutedDepthMipMapSRV[MipLevel - 1]);

            ITextureView* pRTVs[] = {
                m_ConvolutedHistoryMipMapRTV[MipLevel],
                m_ConvolutedDepthMipMapRTV[MipLevel]};

            RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TranslationW2R), TranslationW2R);
            RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
            RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        }

        StateTransitionDesc TransitionDescW2R[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].AsTexture(),
                                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                static_cast<Uint32>(m_ConvolutedHistoryMipMapRTV.size() - 1), 1, 0, REMAINING_ARRAY_SLICES,
                                STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].AsTexture(),
                                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                static_cast<Uint32>(m_ConvolutedDepthMipMapRTV.size() - 1), 1, 0, REMAINING_ARRAY_SLICES,
                                STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2R), TransitionDescW2R);
    }
    else
    {
        if (SupportedFeatures.TextureSubresourceViews)
        {
            ShaderResourceVariableX TextureHistoryLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistoryLastMip"};
            ShaderResourceVariableX TextureDepthLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepthLastMip"};

            for (Uint32 MipLevel = 1; MipLevel < m_ConvolutedHistoryMipMapRTV.size(); MipLevel++)
            {
                TextureHistoryLastMipSV.Set(m_ConvolutedHistoryMipMapSRV[MipLevel - 1]);
                TextureDepthLastMipSV.Set(m_ConvolutedDepthMipMapSRV[MipLevel - 1]);

                ITextureView* pRTVs[] = {
                    m_ConvolutedHistoryMipMapRTV[MipLevel],
                    m_ConvolutedDepthMipMapRTV[MipLevel]};

                RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
            }
        }
        else
        {
            ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistoryMips"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED_INTERMEDIATE].GetTextureSRV());
            ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepthMips"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED_INTERMEDIATE].GetTextureSRV());

            for (Uint32 MipLevel = 1; MipLevel < m_ConvolutedHistoryMipMapRTV.size(); MipLevel++)
            {
                // We use StartVertexLocation to pass the mipmap level of the depth texture for convolution
                VERIFY_EXPR(SupportedFeatures.ShaderBaseVertexOffset);
                const Uint32 VertexOffset = 3u * (MipLevel - 1);

                ITextureView* pRTVs[] = {
                    m_ConvolutedHistoryMipMapRTV[MipLevel],
                    m_ConvolutedDepthMipMapRTV[MipLevel]};

                RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1, VertexOffset});
                RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                CopyTextureAttribs CopyAttribsHistory;
                CopyAttribsHistory.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED];
                CopyAttribsHistory.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED_INTERMEDIATE];
                CopyAttribsHistory.SrcMipLevel              = MipLevel;
                CopyAttribsHistory.DstMipLevel              = MipLevel;
                CopyAttribsHistory.SrcSlice                 = 0;
                CopyAttribsHistory.DstSlice                 = 0;
                CopyAttribsHistory.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                CopyAttribsHistory.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                RenderAttribs.pDeviceContext->CopyTexture(CopyAttribsHistory);

                CopyTextureAttribs CopyAttribsDepth;
                CopyAttribsDepth.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED];
                CopyAttribsDepth.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED_INTERMEDIATE];
                CopyAttribsDepth.SrcMipLevel              = MipLevel;
                CopyAttribsDepth.DstMipLevel              = MipLevel;
                CopyAttribsDepth.SrcSlice                 = 0;
                CopyAttribsDepth.DstSlice                 = 0;
                CopyAttribsDepth.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                CopyAttribsDepth.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                RenderAttribs.pDeviceContext->CopyTexture(CopyAttribsDepth);
            }
        }
    }
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeResampledHistory(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_RESAMPLED_HISTORY, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeResampledHistory.fx", "ComputeResampledHistoryPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureHistory", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDepth", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureOcclusion", Sam_PointClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeResampledHistory",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistory"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0 + CurrFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeResampledHistory"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESAMPLED].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("SSAO_OPTION_HALF_RESOLUTION", (m_FeatureFlags & FEATURE_FLAG_HALF_RESOLUTION) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeSpatialReconstruction.fx", "ComputeSpatialReconstructionPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureHistory", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeSpatialReconstruction",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESAMPLED].GetTextureSRV());
    ShaderResourceVariableX(RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal").Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHistory"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0 + CurrFrameIdx].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeSpatialReconstruction"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    CopyTextureAttribs CopyAttribs;
    CopyAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED];
    CopyAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + CurrFrameIdx];
    CopyAttribs.SrcMipLevel              = 0;
    CopyAttribs.DstMipLevel              = 0;
    CopyAttribs.SrcSlice                 = 0;
    CopyAttribs.DstSlice                 = 0;
    CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
}

ScreenSpaceAmbientOcclusion::RenderTechnique& ScreenSpaceAmbientOcclusion::GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags)
{
    auto Iter = m_RenderTech.find({RenderTech, FeatureFlags});
    if (Iter != m_RenderTech.end())
        return Iter->second;

    auto Condition = m_RenderTech.emplace(RenderTechniqueKey{RenderTech, FeatureFlags}, RenderTechnique{});
    return Condition.first->second;
}

} // namespace Diligent
