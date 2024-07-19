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

#include "SuperResolution.hpp"
#include "CommonlyUsedStates.h"
#include "imgui.h"
#include "RenderStateCache.hpp"
#include "ScopedDebugGroup.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/SuperResolution/public/SuperResolutionStructures.fxh"
} // namespace HLSL

SuperResolution::SuperResolution(IRenderDevice* pDevice, const CreateInfo& CI) :
    m_SuperResolutionAttribs{std::make_unique<HLSL::SuperResolutionAttribs>()},
    m_Settings{CI}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::SuperResolutionAttribs), "SuperResolution::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_SuperResolutionAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}
SuperResolution::~SuperResolution() = default;

void SuperResolution::PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc = pPostFXContext->GetFrameDesc();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.OutputWidth && m_BackBufferHeight == FrameDesc.OutputHeight && m_FeatureFlags == FeatureFlags)
        return;

    for (auto& Iter : m_RenderTech)
        Iter.second.SRB.Release();

    m_BackBufferWidth  = FrameDesc.OutputWidth;
    m_BackBufferHeight = FrameDesc.OutputHeight;
    m_FeatureFlags     = FeatureFlags;

    RenderDeviceWithCache_N Device{pDevice};

    // We use sRGB space to reduce color banding artifacts
    {
        TextureDesc Desc;
        Desc.Name      = "SuperResolution::TextureEAU";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA8_UNORM_SRGB;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources.Insert(RESOURCE_IDENTIFIER_EAU, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "SuperResolution::TextureCAS";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA8_UNORM_SRGB;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources.Insert(RESOURCE_IDENTIFIER_CAS, Device.CreateTexture(Desc));
    }
}

void SuperResolution::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pFSRAttribs != nullptr, "RenderAttribs.pFSRAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "SuperResolution"};

    bool AllPSOsReady = PrepareShadersAndPSO(RenderAttribs, m_FeatureFlags);
    UpdateConstantBuffer(RenderAttribs);
    if (AllPSOsReady)
    {
        ComputeEdgeAdaptiveUpsampling(RenderAttribs);
        ComputeContrastAdaptiveSharpening(RenderAttribs);
    }
    else
    {
        ComputePlaceholderTexture(RenderAttribs);
    }

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

bool SuperResolution::UpdateUI(HLSL::SuperResolutionAttribs& Attribs, FEATURE_FLAGS& FeatureFlags)
{
    bool bAttribsChanged = false;

    bAttribsChanged |= ImGui::SliderFloat("Sharpness", &Attribs.Sharpening, 0.0f, 1.0f);
    bAttribsChanged |= ImGui::SliderFloat("Resolution Scale", &Attribs.ResolutionScale, 0.5f, 1.0f);

    return bAttribsChanged;
}

ITextureView* SuperResolution::GetUpsampledTextureSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_CAS].GetTextureSRV();
}

SuperResolution::RenderTechnique& SuperResolution::GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags)
{
    auto Iter = m_RenderTech.find({RenderTech, FeatureFlags});
    if (Iter != m_RenderTech.end())
        return Iter->second;

    auto Condition = m_RenderTech.emplace(RenderTechniqueKey{RenderTech, FeatureFlags}, RenderTechnique{});
    return Condition.first->second;
}

bool SuperResolution::PrepareShadersAndPSO(const RenderAttributes& RenderAttribs, FEATURE_FLAGS FeatureFlags)
{
    bool AllPSOsReady = true;

    const SHADER_COMPILE_FLAGS ShaderFlags = RenderAttribs.pPostFXContext->GetShaderCompileFlags(m_Settings.EnableAsyncCreation);
    const PSO_CREATE_FLAGS     PSOFlags    = m_Settings.EnableAsyncCreation ? PSO_CREATE_FLAG_ASYNCHRONOUS : PSO_CREATE_FLAG_NONE;

    {
        auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_EDGE_ADAPTIVE_UPSAMPLING, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX, {}, ShaderFlags);
            const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FSR_EdgeAdaptiveUpsampling.fx", "ComputeEdgeAdaptiveUpsamplingPS", SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbFSRAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureSource", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureSource", Sam_LinearClamp);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "SuperResolution::ComputeEdgeAdaptiveUpsampling",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_EAU].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CONTRAST_ADAPTIVE_SHARPENING, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX, {}, ShaderFlags);
            const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FSR_ContrastAdaptiveSharpening.fx", "ComputeContrastAdaptiveSharpeningPS", SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbFSRAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureSource", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "SuperResolution::ContrastAdaptiveSharpening",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CAS].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }
    return AllPSOsReady;
}

void SuperResolution::UpdateConstantBuffer(const RenderAttributes& RenderAttribs)
{
    if (memcmp(RenderAttribs.pFSRAttribs, m_SuperResolutionAttribs.get(), sizeof(HLSL::SuperResolutionAttribs)) != 0)
    {
        memcpy(m_SuperResolutionAttribs.get(), RenderAttribs.pFSRAttribs, sizeof(HLSL::SuperResolutionAttribs));
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::SuperResolutionAttribs), RenderAttribs.pFSRAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void SuperResolution::ComputeEdgeAdaptiveUpsampling(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_EDGE_ADAPTIVE_UPSAMPLING, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbFSRAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "EdgeAdaptiveUpsampling"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_EAU].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureSource"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void SuperResolution::ComputeContrastAdaptiveSharpening(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CONTRAST_ADAPTIVE_SHARPENING, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbFSRAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ContrastAdaptiveSharpening"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_CAS].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureSource"}.Set(m_Resources[RESOURCE_IDENTIFIER_EAU].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void SuperResolution::ComputePlaceholderTexture(const RenderAttributes& RenderAttribs)
{
    PostFXContext::TextureOperationAttribs CopyTextureAttribs;
    CopyTextureAttribs.pDevice        = RenderAttribs.pDevice;
    CopyTextureAttribs.pDeviceContext = RenderAttribs.pDeviceContext;
    CopyTextureAttribs.pStateCache    = RenderAttribs.pStateCache;
    RenderAttribs.pPostFXContext->CopyTextureColor(CopyTextureAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV(), m_Resources[RESOURCE_IDENTIFIER_CAS].GetTextureRTV());
}

} // namespace Diligent
