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
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
}


ScreenSpaceAmbientOcclusion::ScreenSpaceAmbientOcclusion(IRenderDevice* pDevice) :
    m_SSAOAttribs{std::make_unique<HLSL::ScreenSpaceAmbientOcclusionAttribs>()}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs), "ScreenSpaceAmbientOcclusion::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_SSAOAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}

ScreenSpaceAmbientOcclusion::~ScreenSpaceAmbientOcclusion() = default;

void ScreenSpaceAmbientOcclusion::PrepareResources(IRenderDevice* pDevice, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc         = pPostFXContext->GetFrameDesc();
    const auto& SupportedFeatures = pPostFXContext->GetSupportedFeatures();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height && m_FeatureFlags == FeatureFlags)
        return;

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
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
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
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = (m_FeatureFlags & FEATURE_FLAG_HALF_PRECISION_DEPTH) ? TEX_FORMAT_R16_UNORM : TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(m_BackBufferWidth, m_BackBufferHeight), DepthPrefilteredMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::Occlusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::ResolvedOcclusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OCCLUSION_RESOLVED, Device.CreateTexture(Desc));
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_OCCLUSION_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceAmbientOcclusion::OcclusionHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }
}

void ScreenSpaceAmbientOcclusion::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pCurrDepthBufferSRV != nullptr, "RenderAttribs.pCurrDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPrevDepthBufferSRV != nullptr, "RenderAttribs.pPrevDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pMotionVectorsSRV != nullptr, "RenderAttribs.pMotionVectorsSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pNormalBufferSRV != nullptr, "RenderAttribs.pNormalBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pSSAOAttribs != nullptr, "RenderAttribs.pSSAOAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_CURR_DEPTH, RenderAttribs.pCurrDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_PREV_DEPTH, RenderAttribs.pPrevDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_NORMAL, RenderAttribs.pNormalBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS, RenderAttribs.pMotionVectorsSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "ScreenSpaceAmbientOcclusion"};
    if (memcmp(RenderAttribs.pSSAOAttribs, m_SSAOAttribs.get(), sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs)) != 0)
    {
        memcpy(m_SSAOAttribs.get(), RenderAttribs.pSSAOAttribs, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs));
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::ScreenSpaceAmbientOcclusionAttribs), RenderAttribs.pSSAOAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    ComputePrefilteredDepth(RenderAttribs);
    ComputeAmbientOcclusion(RenderAttribs);
    ComputeSpatialReconstruction(RenderAttribs);
    ComputeTemporalAccumulation(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

bool ScreenSpaceAmbientOcclusion::UpdateUI(HLSL::ScreenSpaceAmbientOcclusionAttribs& SSRAttribs)
{
    bool AttribsChanged = true;

    if (ImGui::SliderFloat("Effect Radius", &SSRAttribs.EffectRadius, 0.0f, 10.0f))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Effect Falloff Range", &SSRAttribs.EffectFalloffRange, 0.0f, 1.0f))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Radius Multiplier", &SSRAttribs.RadiusMultiplier, 0.3f, 3.0f))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Depth MIP Sampling Offset", &SSRAttribs.DepthMIPSamplingOffset, 2.0, 6.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Temporal Stability Factor", &SSRAttribs.TemporalStabilityFactor, 0.0, 1.0))
        AttribsChanged = true;

    return AttribsChanged;
}

ITextureView* ScreenSpaceAmbientOcclusion::GetAmbientOcclusionSRV() const
{
    const Uint32 CurrFrameIdx = (m_CurrentFrameIdx + 0) & 0x01;
    return m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + CurrFrameIdx].GetTextureSRV();
}

void ScreenSpaceAmbientOcclusion::CopyTextureDepth(const RenderAttributes& RenderAttribs, ITextureView* pSRV, ITextureView* pRTV)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COPY_DEPTH, m_FeatureFlags);
    if (!RenderTech.IsInitialized())
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
        RenderTech.InitializeSRB(false);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(pSRV);

    RenderAttribs.pDeviceContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceAmbientOcclusion::ComputePrefilteredDepth(const RenderAttributes& RenderAttribs)
{
    auto&       RenderTech        = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_DEPTH_BUFFER, m_FeatureFlags);
    const auto& SupportedFeatures = RenderAttribs.pPostFXContext->GetSupportedFeatures();
    if (!RenderTech.IsInitialized())
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
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeHierarchicalDepthBuffer"};

    CopyTextureDepth(RenderAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_CURR_DEPTH].GetTextureSRV(), m_PrefilteredDepthMipMapRTV[0]);

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
    if (!RenderTech.IsInitialized())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("SSAO_OPTION_UNIFORM_WEIGHTING", (m_FeatureFlags & FEATURE_FLAG_UNIFORM_WEIGHTING) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeAmbientOcclusion.fx", "ComputeAmbientOcclusionPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureBlueNoise", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth", Sam_PointClamp);

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
        RenderTech.InitializeSRB(true);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrefilteredDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureBlueNoise"}.Set(RenderAttribs.pPostFXContext->Get2DBlueNoiseSRV(PostFXContext::BLUE_NOISE_DIMENSION_ZW));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeAmbientOcclusion"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].GetTextureRTV(),
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
    if (!RenderTech.IsInitialized())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SSAO_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("SSAO_OPTION_GUIDED_FILTER", (m_FeatureFlags & FEATURE_FLAG_GUIDED_FILTER) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "SSAO_ComputeSpatialReconstruction.fx", "ComputeSpatialReconstructionPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureOcclusion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeSpatialReconstruction",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_RESOLVED].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_PREFILTERED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeSpatialReconstruction"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_RESOLVED].GetTextureRTV(),
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
    if (!RenderTech.IsInitialized())
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
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevOcclusion", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceAmbientOcclusion::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceAmbientOcclusionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;
    const Uint32 PrevFrameIdx = (FrameIndex + 1) & 0x01;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_RESOLVED].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevOcclusion"}.Set(m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_CURR_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_PREV_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeTemporalAccumulation"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0 + CurrFrameIdx].GetTextureRTV(),
    };

    constexpr float4 RTVClearColor = float4(1.0, 1.0, 1.0, 1.0);

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
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
