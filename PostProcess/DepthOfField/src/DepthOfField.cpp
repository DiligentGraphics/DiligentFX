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


#include "DepthOfField.hpp"
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
#include "Shaders/PostProcess/DepthOfField/public/DepthOfFieldStructures.fxh"
} // namespace HLSL

DepthOfField::DepthOfField(IRenderDevice* pDevice) :
    m_pDOFAttribs{std::make_unique<HLSL::DepthOfFieldAttribs>()}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::DepthOfFieldAttribs), "DepthOfFieldAttribs::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_pDOFAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
}

DepthOfField::~DepthOfField() {}

void DepthOfField::PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{

    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const auto& FrameDesc = pPostFXContext->GetFrameDesc();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height && m_FeatureFlags == FeatureFlags)
        return;

    for (auto& Iter : m_RenderTech)
        Iter.second.SRB.Release();

    m_BackBufferWidth  = FrameDesc.Width;
    m_BackBufferHeight = FrameDesc.Height;

    RenderDeviceWithCache_N Device{pDevice};

    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::CircleOfConfusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Prefiltered";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Bokeh";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_BOKEH_TEXTURE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Postfiltered";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_POSTFILTERED_TEXTURE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Combined";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth;
        Desc.Height    = m_BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R11G11B10_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_COMBINED_TEXTURE, Device.CreateTexture(Desc));
    }
}

void DepthOfField::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDOFAttribs != nullptr, "RenderAttribs.pDOFAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_DEPTH, RenderAttribs.pDepthBufferSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "DepthOfField"};
    if (memcmp(RenderAttribs.pDOFAttribs, m_pDOFAttribs.get(), sizeof(HLSL::DepthOfFieldAttribs)) != 0)
    {
        memcpy(m_pDOFAttribs.get(), RenderAttribs.pDOFAttribs, sizeof(HLSL::DepthOfFieldAttribs));
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::DepthOfFieldAttribs), RenderAttribs.pDOFAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    ComputeCircleOfConfusion(RenderAttribs);
    ComputePrefilteredTexture(RenderAttribs);
    ComputeBokehTexture(RenderAttribs);
    ComputePostfilteredTexture(RenderAttribs);
    ComputeCombinedTexture(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

bool DepthOfField::UpdateUI(HLSL::DepthOfFieldAttribs& Attribs, FEATURE_FLAGS& FeatureFlags)
{
    bool AttribsChanged = false;

    if (ImGui::SliderFloat("Bokeh Radius", &Attribs.BokehRadius, 1.0f, 10.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The intensity of the depth of field effect.");

    if (ImGui::SliderFloat("Focus Distance", &Attribs.FocusDistance, 0.1f, 100.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The distance from the camera at which the depth of field effect is focused.");

    if (ImGui::SliderFloat("Focus Range", &Attribs.FocusRange, 0.1f, 10.0f))
        AttribsChanged = true;
    ImGui::HelpMarker("The range of distances from the focus distance at which the depth of field effect is applied.");

    return AttribsChanged;
}

ITextureView* DepthOfField::GetDepthOfFieldTextureSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].GetTextureSRV();
}

void DepthOfField::ComputeCircleOfConfusion(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "DOF_ComputeCircleOfConfusion.fx", "ComputeCircleOfConfusionPS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "DepthOfField::ComputeCircleOfConfusion",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeCircleOfConfusion"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputePrefilteredTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "DOF_ComputePrefilteredTexture.fx", "ComputePrefilteredTexturePS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "DepthOfField::ComputePrefilteredTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputePrefilteredTexture"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeBokehTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BOKEH_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "DOF_ComputeBokeh.fx", "ComputeBokehPS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoC", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "DepthOfField::ComputeBokehTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBokehTexture"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputePostfilteredTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_POSTFILTERED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "DOF_ComputePostfilteredTexture.fx", "ComputePostfilteredTexturePS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDoF", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDoF", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "DepthOfField::ComputePostfilteredTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_POSTFILTERED_TEXTURE].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputePostfilteredTexture"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_POSTFILTERED_TEXTURE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDoF"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeCombinedTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_COMBINED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedPSO())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "DOF_ComputeCombinedTexture.fx", "ComputeCombinedTexturePS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDoF", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDoF", Sam_LinearClamp);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "DepthOfField::ComputeCombinedTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
    }

    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeCombinedTexture"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDoF"}.Set(m_Resources[RESOURCE_IDENTIFIER_POSTFILTERED_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

DepthOfField::RenderTechnique& DepthOfField::GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags)
{
    auto Iter = m_RenderTech.find({RenderTech, FeatureFlags});
    if (Iter != m_RenderTech.end())
        return Iter->second;

    auto Condition = m_RenderTech.emplace(RenderTechniqueKey{RenderTech, FeatureFlags}, RenderTechnique{});
    return Condition.first->second;
}

} // namespace Diligent
