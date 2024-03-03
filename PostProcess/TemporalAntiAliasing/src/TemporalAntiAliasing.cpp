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

#include "imgui.h"

#include "TemporalAntiAliasing.hpp"

#include "CommonlyUsedStates.h"
#include "GraphicsTypesX.hpp"
#include "MapHelper.hpp"
#include "RenderStateCache.hpp"
#include "ScopedDebugGroup.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

// https://en.wikipedia.org/wiki/Halton_sequence#Implementation_in_pseudocode
static float HaltonSequence(Uint32 Base, Uint32 Index)
{
    float Result = 0.0;
    float F      = 1.0;
    while (Index > 0)
    {
        F      = F / static_cast<float>(Base);
        Result = Result + F * static_cast<float>(Index % Base);
        Index  = static_cast<Uint32>(floorf(static_cast<float>(Index) / static_cast<float>(Base)));
    }
    return Result;
}

namespace HLSL
{
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
}


TemporalAntiAliasing::TemporalAntiAliasing(IRenderDevice* pDevice) :
    m_ShaderAttribs{std::make_unique<HLSL::TemporalAntiAliasingAttribs>()}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::TemporalAntiAliasingAttribs), "TemporalAntiAliasing::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_ShaderAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}

TemporalAntiAliasing::~TemporalAntiAliasing() = default;

float2 TemporalAntiAliasing::GetJitterOffset() const
{
    if (m_BackBufferWidth == 0 || m_BackBufferHeight == 0)
        return float2{0.0f, 0.0f};

    constexpr Uint32 SampleCount = 16u;
    const float      JitterX     = (HaltonSequence(2u, (m_CurrentFrameIdx % SampleCount) + 1) - 0.5f) / (0.5f * static_cast<float>(m_BackBufferWidth));
    const float      JitterY     = (HaltonSequence(3u, (m_CurrentFrameIdx % SampleCount) + 1) - 0.5f) / (0.5f * static_cast<float>(m_BackBufferHeight));
    return float2{JitterX, JitterY};
}

void TemporalAntiAliasing::PrepareResources(IRenderDevice* pDevice, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc = pPostFXContext->GetFrameDesc();

    m_CurrentFrameIdx = FrameDesc.Index;
    m_FeatureFlags    = FeatureFlags;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height)
        return;

    m_BackBufferWidth  = FrameDesc.Width;
    m_BackBufferHeight = FrameDesc.Height;

    RenderDeviceWithCache_N Device{pDevice};

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0; TextureIdx <= RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER1; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name      = "TemporalAntiAliasing::AccumulatedBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }
}

void TemporalAntiAliasing::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pCurrDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPrevDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pMotionVectorsSRV != nullptr, "RenderAttribs.pMotionVectorsSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pTAAAttribs != nullptr, "RenderAttribs.pTAAAttribs must not be null");


    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_CURR_DEPTH, RenderAttribs.pCurrDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_PREV_DEPTH, RenderAttribs.pPrevDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS, RenderAttribs.pMotionVectorsSRV->GetTexture());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "TemporalAccumulation"};

    bool ResetAccumulation =
        m_LastFrameIdx == ~0u ||                           // No history on the first frame
        m_CurrentFrameIdx != m_LastFrameIdx + 1 ||         // Reset history if frames were skipped
        RenderAttribs.pTAAAttribs->ResetAccumulation != 0; // Reset history if requested

    bool UpdateConstantBuffer = false;
    if (memcmp(m_ShaderAttribs.get(), RenderAttribs.pTAAAttribs, sizeof(HLSL::TemporalAntiAliasingAttribs)) != 0)
    {
        UpdateConstantBuffer = true;
        memcpy(m_ShaderAttribs.get(), RenderAttribs.pTAAAttribs, sizeof(HLSL::TemporalAntiAliasingAttribs));
    }

    if (ResetAccumulation && (m_ShaderAttribs->ResetAccumulation != 0) != ResetAccumulation)
    {
        m_ShaderAttribs->ResetAccumulation = 1;
        UpdateConstantBuffer               = true;
    }

    if (UpdateConstantBuffer)
    {
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER], 0, sizeof(HLSL::TemporalAntiAliasingAttribs),
                                                   m_ShaderAttribs.get(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    ComputeTemporalAccumulation(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();

    m_LastFrameIdx = m_CurrentFrameIdx;
}

bool TemporalAntiAliasing::UpdateUI(HLSL::TemporalAntiAliasingAttribs& TAAAttribs)
{
    return ImGui::SliderFloat("Temporal Stability Factor", &TAAAttribs.TemporalStabilityFactor, 0.0f, 1.0f);
}

ITextureView* TemporalAntiAliasing::GetAccumulatedFrameSRV(bool IsPrevFrame) const
{
    const Uint32 CurrFrameIdx = (m_CurrentFrameIdx + static_cast<Uint32>(IsPrevFrame)) & 0x01u;
    return m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + CurrFrameIdx].GetTextureSRV();
}

void TemporalAntiAliasing::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION, m_FeatureFlags);
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbTemporalAntiAliasingAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevColor", Sam_LinearClamp);

        ShaderMacroHelper Macros;
        Macros.Add("TAA_OPTION_GAUSSIAN_WEIGHTING", (m_FeatureFlags & FEATURE_FLAG_GAUSSIAN_WEIGHTING) != 0);
        Macros.Add("TAA_OPTION_INVERTED_DEPTH", (m_FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("TAA_OPTION_BICUBIC_FILTER", (m_FeatureFlags & FEATURE_FLAG_BICUBIC_FILTER) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeTemporalAntiAliasing.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL, Macros);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "TemporalAntiAliasing::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbTemporalAntiAliasingAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer());
        RenderTech.InitializeSRB(true);
    }

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;
    const Uint32 PrevFrameIdx = (FrameIndex + 1) & 0x01;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_CURR_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_PREV_DEPTH].GetTextureSRV());

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + CurrFrameIdx].GetTextureRTV(),
    };
    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

TemporalAntiAliasing::RenderTechnique& TemporalAntiAliasing::GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags)
{
    auto Iter = m_RenderTech.find({RenderTech, FeatureFlags});
    if (Iter != m_RenderTech.end())
        return Iter->second;

    auto Condition = m_RenderTech.emplace(RenderTechniqueKey{RenderTech, FeatureFlags}, RenderTechnique{});
    return Condition.first->second;
}

} // namespace Diligent
