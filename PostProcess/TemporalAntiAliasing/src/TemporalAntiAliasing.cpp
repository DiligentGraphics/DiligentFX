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
#include "ImGuiUtils.hpp"

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

TemporalAntiAliasing::TemporalAntiAliasing(IRenderDevice* pDevice)
{
}

TemporalAntiAliasing::~TemporalAntiAliasing() = default;

float2 TemporalAntiAliasing::GetJitterOffset(Uint32 AccumulationBufferIdx) const
{
    const auto& it = m_AccumulationBuffers.find(AccumulationBufferIdx);
    if (it == m_AccumulationBuffers.end())
        return float2{0.0f, 0.0f};

    const auto& AccBuffer = it->second;

    if (AccBuffer.Width == 0 || AccBuffer.Height == 0)
        return float2{0.0f, 0.0f};

    constexpr Uint32 SampleCount = 16u;
    const float      JitterX     = (HaltonSequence(2u, (AccBuffer.CurrentFrameIdx % SampleCount) + 1) - 0.5f) / (0.5f * static_cast<float>(AccBuffer.Width));
    const float      JitterY     = (HaltonSequence(3u, (AccBuffer.CurrentFrameIdx % SampleCount) + 1) - 0.5f) / (0.5f * static_cast<float>(AccBuffer.Height));
    return float2{JitterX, JitterY};
}

void TemporalAntiAliasing::AccumulationBufferInfo::Prepare(IRenderDevice* pDevice, IDeviceContext* pCtx, Uint32 _Width, Uint32 _Height, Uint32 _CurrFrameIdx, FEATURE_FLAGS _FeatureFlags)
{
    FeatureFlags    = _FeatureFlags;
    CurrentFrameIdx = _CurrFrameIdx;

    if (Width == _Width && Height == _Height)
        return;

    SRB.Release();
    Width  = _Width;
    Height = _Height;

    if (!Resources[RESOURCE_ID_CONSTANT_BUFFER])
    {
        RefCntAutoPtr<IBuffer> pBuffer;
        CreateUniformBuffer(pDevice, sizeof(HLSL::TemporalAntiAliasingAttribs), "TemporalAntiAliasing::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, &ShaderAttribs);
        Resources.Insert(RESOURCE_ID_CONSTANT_BUFFER, pBuffer);
    }

    RenderDeviceWithCache_N Device{pDevice};
    for (Uint32 TextureIdx = RESOURCE_ID_ACCUMULATED_BUFFER0; TextureIdx <= RESOURCE_ID_ACCUMULATED_BUFFER1; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name          = "TemporalAntiAliasing::AccumulatedBuffer";
        Desc.Type          = RESOURCE_DIM_TEX_2D;
        Desc.Width         = Width;
        Desc.Height        = Height;
        Desc.Format        = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags     = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        auto  pTexture     = Device.CreateTexture(Desc);
        float ClearColor[] = {0.0, 0.0, 0.0, 0.0};
        PostFXContext::ClearRenderTarget(pCtx, pTexture, ClearColor);
        Resources.Insert(TextureIdx, pTexture);
    }
}

void TemporalAntiAliasing::AccumulationBufferInfo::UpdateConstantBuffer(IDeviceContext* pCtx, const HLSL::TemporalAntiAliasingAttribs& Attribs)
{
    bool ResetAccumulation =
        LastFrameIdx == ~0u ||                 // No history on the first frame
        CurrentFrameIdx != LastFrameIdx + 1 || // Reset history if frames were skipped
        Attribs.ResetAccumulation != 0;        // Reset history if requested

    bool UpdateConstantBuffer = false;
    if (memcmp(&ShaderAttribs, &Attribs, sizeof(HLSL::TemporalAntiAliasingAttribs)) != 0)
    {
        UpdateConstantBuffer = true;
        memcpy(&ShaderAttribs, &Attribs, sizeof(HLSL::TemporalAntiAliasingAttribs));
    }

    if (ResetAccumulation && (ShaderAttribs.ResetAccumulation != 0) != ResetAccumulation)
    {
        ShaderAttribs.ResetAccumulation = 1;
        UpdateConstantBuffer            = true;
    }

    if (UpdateConstantBuffer)
    {
        pCtx->UpdateBuffer(Resources[RESOURCE_ID_CONSTANT_BUFFER], 0, sizeof(HLSL::TemporalAntiAliasingAttribs),
                           &ShaderAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    LastFrameIdx = CurrentFrameIdx;
}

void TemporalAntiAliasing::PrepareResources(IRenderDevice*  pDevice,
                                            IDeviceContext* pDeviceContext,
                                            PostFXContext*  pPostFXContext,
                                            FEATURE_FLAGS   FeatureFlags,
                                            Uint32          AccumulationBufferIdx)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc = pPostFXContext->GetFrameDesc();
    m_AccumulationBuffers[AccumulationBufferIdx].Prepare(pDevice, pDeviceContext, FrameDesc.Width, FrameDesc.Height, FrameDesc.Index, FeatureFlags);
}

void TemporalAntiAliasing::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pTAAAttribs != nullptr, "RenderAttribs.pTAAAttribs must not be null");

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "TemporalAccumulation"};

    const auto& it = m_AccumulationBuffers.find(RenderAttribs.AccumulationBufferIdx);
    if (it == m_AccumulationBuffers.end())
    {
        LOG_ERROR_MESSAGE("Accumulation buffer with index ", RenderAttribs.AccumulationBufferIdx,
                          " is not found, which indicates that PrepareResources() method was not called.");
        return;
    }

    auto& AccBuffer = it->second;
    AccBuffer.UpdateConstantBuffer(RenderAttribs.pDeviceContext, *RenderAttribs.pTAAAttribs);
    ComputeTemporalAccumulation(RenderAttribs, AccBuffer);
}

bool TemporalAntiAliasing::UpdateUI(HLSL::TemporalAntiAliasingAttribs& TAAAttribs, FEATURE_FLAGS& FeatureFlags)
{
    bool FeatureBicubicFiltering = FeatureFlags & FEATURE_FLAG_BICUBIC_FILTER;
    bool FeatureGaussWeighting   = FeatureFlags & FEATURE_FLAG_GAUSSIAN_WEIGHTING;
    bool FeatureYCoCgColorSpace  = FeatureFlags & FEATURE_FLAG_YCOCG_COLOR_SPACE;

    bool AttribsChanged = false;

    if (ImGui::SliderFloat("Temporal Stability Factor", &TAAAttribs.TemporalStabilityFactor, 0.0f, 1.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("Controls the interpolation between the current and previous frames. Increasing the value increases temporal stability but may introduce ghosting)");

    if (ImGui::Checkbox("Enable Bicubic Filtering", &FeatureBicubicFiltering))
        AttribsChanged = true;
    ImGui::HelpMarker("Use bicubic filtering instead of the bilinear filtering from history buffer");

    if (ImGui::Checkbox("Enable Gauss Weighting", &FeatureGaussWeighting))
        AttribsChanged = true;
    ImGui::HelpMarker("Use Gaussian weighting to calculate pixel statistics");

    if (ImGui::Checkbox("Use YCoCg color space", &FeatureYCoCgColorSpace))
        AttribsChanged = true;

    ImGui::HelpMarker("Use YCoCg color space for color clipping.");

    auto ResetStateFeatureMask = [](FEATURE_FLAGS& FeatureFlags, FEATURE_FLAGS Flag, bool State) {
        if (State)
            FeatureFlags |= Flag;
        else
            FeatureFlags &= ~Flag;
    };

    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_BICUBIC_FILTER, FeatureBicubicFiltering);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_GAUSSIAN_WEIGHTING, FeatureGaussWeighting);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_YCOCG_COLOR_SPACE, FeatureYCoCgColorSpace);

    return AttribsChanged;
}

ITextureView* TemporalAntiAliasing::GetAccumulatedFrameSRV(bool IsPrevFrame, Uint32 AccumulationBufferIdx) const
{
    const auto& it = m_AccumulationBuffers.find(AccumulationBufferIdx);
    if (it == m_AccumulationBuffers.end())
    {
        LOG_ERROR_MESSAGE("Accumulation buffer with index ", AccumulationBufferIdx, " is not found.");
        return nullptr;
    }

    const Uint32 BuffIdx = (it->second.CurrentFrameIdx + (IsPrevFrame ? 1 : 0)) & 0x01u;
    return it->second.Resources[AccumulationBufferInfo::RESOURCE_ID_ACCUMULATED_BUFFER0 + BuffIdx].GetTextureSRV();
}

void TemporalAntiAliasing::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs, AccumulationBufferInfo& AccBuff)
{
    auto& RenderTech = m_RenderTech[{RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION, AccBuff.FeatureFlags}];
    if (!RenderTech.IsInitializedPSO())
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
        Macros.Add("TAA_OPTION_GAUSSIAN_WEIGHTING", (AccBuff.FeatureFlags & FEATURE_FLAG_GAUSSIAN_WEIGHTING) != 0);
        Macros.Add("TAA_OPTION_INVERTED_DEPTH", (AccBuff.FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
        Macros.Add("TAA_OPTION_BICUBIC_FILTER", (AccBuff.FeatureFlags & FEATURE_FLAG_BICUBIC_FILTER) != 0);
        Macros.Add("TAA_OPTION_YCOCG_COLOR_SPACE", (AccBuff.FeatureFlags & FEATURE_FLAG_YCOCG_COLOR_SPACE) != 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "TAA_ComputeTemporalAccumulation.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL, Macros);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "TemporalAntiAliasing::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     AccBuff.Resources[AccumulationBufferInfo::RESOURCE_ID_ACCUMULATED_BUFFER0].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbTemporalAntiAliasingAttribs"}.Set(AccBuff.Resources[AccumulationBufferInfo::RESOURCE_ID_CONSTANT_BUFFER].AsBuffer());
    }

    auto& SRB = AccBuff.SRB;
    if (!SRB)
    {
        RenderTech.PSO->CreateShaderResourceBinding(&SRB, true);
    }

    const Uint32  FrameIndex    = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32  CurrBuffIdx   = (FrameIndex + 0) & 0x01;
    const Uint32  PrevBuffIdx   = (FrameIndex + 1) & 0x01;
    ITextureView* PrevBufferSRV = AccBuff.Resources[AccumulationBufferInfo::RESOURCE_ID_ACCUMULATED_BUFFER0 + PrevBuffIdx].GetTextureSRV();
    ITextureView* CurrBufferRTV = AccBuff.Resources[AccumulationBufferInfo::RESOURCE_ID_ACCUMULATED_BUFFER0 + CurrBuffIdx].GetTextureRTV();

    ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_TextureCurrColor"}.Set(RenderAttribs.pColorBufferSRV);
    ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_TexturePrevColor"}.Set(PrevBufferSRV);
    ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(RenderAttribs.pPostFXContext->GetClosestMotionVectors());
    ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_TextureCurrDepth"}.Set(RenderAttribs.pPostFXContext->GetReprojectedDepth());
    ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_TexturePrevDepth"}.Set(RenderAttribs.pPostFXContext->GetPreviousDepth());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, &CurrBufferRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

} // namespace Diligent
