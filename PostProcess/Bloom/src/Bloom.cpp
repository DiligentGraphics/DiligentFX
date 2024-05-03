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


#include "Bloom.hpp"
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

#include <numeric>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
} // namespace HLSL

static constexpr SamplerDesc Sam_LinearBoarder{
    FILTER_TYPE_LINEAR,
    FILTER_TYPE_LINEAR,
    FILTER_TYPE_POINT,
    TEXTURE_ADDRESS_BORDER,
    TEXTURE_ADDRESS_BORDER,
    TEXTURE_ADDRESS_BORDER,
};

Bloom::Bloom(IRenderDevice* pDevice) :
    m_BloomAttribs{std::make_unique<HLSL::BloomAttribs>()}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::BloomAttribs), "Bloom::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_BloomAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}

Bloom::~Bloom() {}

void Bloom::PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
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


    Uint32 HalfWidth    = m_BackBufferWidth / 2u;
    Uint32 HalfHeight   = m_BackBufferHeight / 2u;
    Uint32 TextureCount = ComputeMipLevelsCount(HalfWidth, HalfHeight);

    RenderDeviceWithCache_N Device{pDevice};

    m_UpsampledTextures.clear();
    for (Uint32 TextureIdx = 0; TextureIdx < TextureCount; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "Bloom::UpsampledTexture";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = std::max(HalfWidth >> TextureIdx, 1u);
        Desc.Height    = std::max(HalfHeight >> TextureIdx, 1u);
        Desc.Format    = TEX_FORMAT_R11G11B10_FLOAT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_UpsampledTextures.push_back(Device.CreateTexture(Desc));
    }

    m_DownsampledTextures.clear();
    for (Uint32 TextureIdx = 0; TextureIdx < TextureCount; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "Bloom::DownsampledTexture";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = std::max(HalfWidth >> TextureIdx, 1u);
        Desc.Height    = std::max(HalfHeight >> TextureIdx, 1u);
        Desc.Format    = TEX_FORMAT_R11G11B10_FLOAT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_DownsampledTextures.push_back(Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "Bloom::OutputTexture";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R11G11B10_FLOAT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OUTPUT_COLOR, Device.CreateTexture(Desc));
    }

    if (!SupportedFeatures.ShaderBaseVertexOffset && !m_Resources[RESOURCE_IDENTIFIER_INDEX_BUFFER])
    {
        static constexpr Uint32 Indices[] = {0, 1, 2, 3, 4, 5};

        BufferDesc Desc{"Bloom::IndexBuffer", sizeof(Indices), BIND_INDEX_BUFFER, USAGE_IMMUTABLE};
        m_Resources.Insert(RESOURCE_IDENTIFIER_INDEX_BUFFER, Device.CreateBuffer(Desc, BufferData{Indices, sizeof(Indices)}));
    }
}

Int32 Bloom::ComputeMipCount(Uint32 Width, Uint32 Height, float Radius)
{
    Uint32 MaxMipCount = ComputeMipLevelsCount(Width, Height);
    return static_cast<Int32>(Radius * static_cast<float>(MaxMipCount));
}

void Bloom::ComputePrefilteredTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "Bloom_ComputePrefilteredTexture.fx", "ComputePrefilteredTexturePS", SHADER_TYPE_PIXEL);

        const bool BorderSamplingModeSupported = RenderAttribs.pDevice->GetAdapterInfo().Sampler.BorderSamplingModeSupported;

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbBloomAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureInput", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureInput", BorderSamplingModeSupported ? Sam_LinearBoarder : Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "Bloom::ComputePrefilteredTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_DownsampledTextures[0]->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbBloomAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputePrefilteredTexture"};

    ITextureView* pRTVs[] = {
        m_DownsampledTextures[0]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
    };

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureInput"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void Bloom::ComputeDownsampledTextures(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_DOWNSAMPLED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "Bloom_ComputeDownsampledTexture.fx", "ComputeDownsampledTexturePS", SHADER_TYPE_PIXEL);

        const bool BorderSamplingModeSupported = RenderAttribs.pDevice->GetAdapterInfo().Sampler.BorderSamplingModeSupported;

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureInput", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureInput", BorderSamplingModeSupported ? Sam_LinearBoarder : Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "Bloom::ComputeDownsampledTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_DownsampledTextures[0]->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeDownsampledTexture"};

    Int32                   MipCount = ComputeMipCount(m_DownsampledTextures[0]->GetDesc().Width, m_DownsampledTextures[0]->GetDesc().Height, m_BloomAttribs->Radius);
    ShaderResourceVariableX TextureInputSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureInput"};
    for (Int32 TextureIdx = 1; TextureIdx < MipCount; TextureIdx++)
    {
        ITextureView* pRTVs[] = {
            m_DownsampledTextures[TextureIdx]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
        };

        TextureInputSV.Set(m_DownsampledTextures[TextureIdx - 1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
        RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    }
}

void Bloom::ComputeUpsampledTextures(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_UPSAMPLED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "Bloom_ComputeUpsampledTexture.fx", "ComputeUpsampledTexturePS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbBloomAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureInput", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDownsampled", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureInput", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDownsampled", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "Bloom::ComputeUpsampledTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_UpsampledTextures[0]->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbBloomAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeUpsampledTexture"};

    {
        ShaderResourceVariableX TextureInputSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureInput"};
        ShaderResourceVariableX TextureDowsampledSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDownsampled"};

        Int32 MipCount = ComputeMipCount(m_DownsampledTextures[0]->GetDesc().Width, m_DownsampledTextures[0]->GetDesc().Height, m_BloomAttribs->Radius) - 1;
        for (Int32 TextureIdx = MipCount; TextureIdx > 0; TextureIdx--)
        {
            ITextureView* pRTVs[] = {
                m_UpsampledTextures[TextureIdx - 1]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            };

            TextureInputSV.Set(m_DownsampledTextures[TextureIdx]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            TextureDowsampledSV.Set(TextureIdx != MipCount ? m_UpsampledTextures[TextureIdx]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : m_DownsampledTextures[TextureIdx]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

            RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
            RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
            RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        }
    }

    {
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureInput"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDownsampled"}.Set(m_UpsampledTextures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        ITextureView* pRTVs[] = {
            m_Resources[RESOURCE_IDENTIFIER_OUTPUT_COLOR].GetTextureRTV(),
        };

        RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
        RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (m_Resources[RESOURCE_IDENTIFIER_INDEX_BUFFER])
        {
            RenderAttribs.pDeviceContext->SetIndexBuffer(m_Resources[RESOURCE_IDENTIFIER_INDEX_BUFFER].AsBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            RenderAttribs.pDeviceContext->DrawIndexed({3, VT_UINT32, DRAW_FLAG_VERIFY_ALL, 1, 3});
        }
        else
        {
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1, 3});
        }

        RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    }
}

void Bloom::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pBloomAttribs != nullptr, "RenderAttribs.pBloomAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "Bloom"};
    if (memcmp(RenderAttribs.pBloomAttribs, m_BloomAttribs.get(), sizeof(HLSL::BloomAttribs)) != 0)
    {
        memcpy(m_BloomAttribs.get(), RenderAttribs.pBloomAttribs, sizeof(HLSL::BloomAttribs));
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::BloomAttribs), RenderAttribs.pBloomAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    ComputePrefilteredTexture(RenderAttribs);
    ComputeDownsampledTextures(RenderAttribs);
    ComputeUpsampledTextures(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

Bloom::RenderTechnique& Bloom::GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags)
{
    auto Iter = m_RenderTech.find({RenderTech, FeatureFlags});
    if (Iter != m_RenderTech.end())
        return Iter->second;

    auto Condition = m_RenderTech.emplace(RenderTechniqueKey{RenderTech, FeatureFlags}, RenderTechnique{});
    return Condition.first->second;
}

ITextureView* Bloom::GetBloomTextureSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_OUTPUT_COLOR].GetTextureSRV();
}

bool Bloom::UpdateUI(HLSL::BloomAttribs& Attribs, FEATURE_FLAGS& FeatureFlags)
{
    bool AttribsChanged = false;

    if (ImGui::SliderFloat("Intensity", &Attribs.Intensity, 0.0f, 1.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The intensity of the bloom effect.");

    if (ImGui::SliderFloat("Radius", &Attribs.Radius, 0.3f, 0.85f))
        AttribsChanged = true;
    ImGui::HelpMarker("This variable controls the size of the bloom effect. A larger radius will result in a larger area of the image being affected by the bloom effect.");

    if (ImGui::SliderFloat("Threshold", &Attribs.Threshold, 0.0f, 10.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("This value determines the minimum brightness required for a pixel to contribute to the bloom effect.");

    if (ImGui::SliderFloat("Soft Threshold", &Attribs.SoftTreshold, 0.0f, 1.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("This value determines the softness of the threshold. A higher value will result in a softer threshold.");

    return AttribsChanged;
}

} // namespace Diligent
