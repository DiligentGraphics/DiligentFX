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


#include "TemporalAntiAliasing.hpp"

#include "CommonlyUsedStates.h"
#include "GraphicsTypesX.hpp"
#include "RenderStateCache.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

// https://en.wikipedia.org/wiki/Halton_sequence#Implementation_in_pseudocode
static float HaltonSequenc(Uint32 Base, Uint32 Index)
{
    float Result = 0.0;
    float F      = 1.0;
    for (Uint32 Idx = Index; Idx > 0;)
    {
        F      = F / static_cast<float>(Base);
        Result = Result + F * static_cast<float>(Idx % Base);
        Idx    = static_cast<uint32_t>(floorf(static_cast<float>(Idx) / static_cast<float>(Base)));
    }
    return Result;
}

TemporalAntiAliasing::TemporalAntiAliasing(IRenderDevice* pDevice) {}

TemporalAntiAliasing::~TemporalAntiAliasing() = default;

float2 TemporalAntiAliasing::GetJitterOffset() const
{
    if (!m_BackBufferWidth || !m_BackBufferHeight)
        return float2{0.0f, 0.0f};

    const float JitterX = (HaltonSequenc(2u, m_CurrentFrameIdx % 32u + 1) - 0.5f) / (0.5f * static_cast<float>(m_BackBufferWidth));
    const float JitterY = (HaltonSequenc(3u, m_CurrentFrameIdx % 32u + 1) - 0.5f) / (0.5f * static_cast<float>(m_BackBufferHeight));
    return float2{JitterX, JitterY};
}

void TemporalAntiAliasing::PrepareResources(IRenderDevice* pDevice, PostFXContext* pPostFXContext, TEXTURE_FORMAT AccumulatedBufferFormat)
{
    const auto& FrameDesc = pPostFXContext->GetFrameDesc();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height)
        return;

    m_BackBufferWidth  = FrameDesc.Width;
    m_BackBufferHeight = FrameDesc.Height;
    m_LastFrameIdx     = ~0u;

    RenderDeviceWithCache_N Device{pDevice};

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0; TextureIdx <= RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER1; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name      = "TemporalAntiAliasing::AccumulatedBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = AccumulatedBufferFormat;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }

    const auto& SupportedFeatures = pPostFXContext->GetSupportedFeatures();
    if (!SupportedFeatures.ShaderBaseVertexOffset && !m_IndexBuffer)
    {
        static constexpr Uint32 Indices[] = {0, 1, 2, 3, 4, 5};

        BufferDesc Desc{"TemporalAntiAliasing::IndexBuffer", sizeof(Indices), BIND_INDEX_BUFFER, USAGE_IMMUTABLE};

        m_IndexBuffer = Device.CreateBuffer(Desc, BufferData{Indices, sizeof(Indices)});
    }
}

void TemporalAntiAliasing::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pMotionVectorsSRV != nullptr, "RenderAttribs.pMotionVectorsSRV must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_DEPTH, RenderAttribs.pDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS, RenderAttribs.pMotionVectorsSRV->GetTexture());

    ComputeTemporalAccumulation(RenderAttribs);
}

ITextureView* TemporalAntiAliasing::GetAccumulatedFrameSRV() const
{
    const Uint32 CurrFrameIdx = (m_CurrentFrameIdx + 0) & 0x01;
    return m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + CurrFrameIdx].GetTextureSRV();
}

void TemporalAntiAliasing::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION, RenderAttribs.FeatureFlag);
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeTemporalAntiAliasing.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "TemporalAntiAliasing::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, true);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        RenderTech.InitializeSRB(true);
    }

    const Uint32 FrameIndex        = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx      = (FrameIndex + 0) & 0x01;
    const Uint32 PrevFrameIdx      = (FrameIndex + 1) & 0x01;
    const bool   ResetAccumulation = m_LastFrameIdx == ~0u || m_LastFrameIdx + 1 != FrameIndex;
    m_LastFrameIdx                 = FrameIndex;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "TemporalAccumulation"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0 + CurrFrameIdx].GetTextureRTV(),
    };
    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (ResetAccumulation && m_IndexBuffer)
    {
        RenderAttribs.pDeviceContext->SetIndexBuffer(m_IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->DrawIndexed({3, VT_UINT32, DRAW_FLAG_VERIFY_ALL, 1, ResetAccumulation ? 3u : 0u});
    }
    else
    {
        RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1, ResetAccumulation ? 3u : 0u});
    }
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
