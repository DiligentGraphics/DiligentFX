/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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

// https://www.shadertoy.com/view/wdKXDK
static std::vector<float2> GenerateKernelPoints(Int32 RingCount, Int32 RingDensity)
{
    // The number of samples is calculated by the formula of the sum of arithmetic progression
    Int32               SampleCount = 1 + RingDensity * (RingCount - 1) * RingCount / 2;
    std::vector<float2> Kernel{};
    Kernel.reserve(SampleCount);

    float RadiusInc = 1.0f / ((static_cast<float>(RingCount) - 1.0f));
    for (Int32 i = RingCount - 1; i >= 0; --i)
    {
        Int32 PointCount = std::max(RingDensity * i, 1);
        float Radius     = static_cast<float>(i) * RadiusInc;

        float ThetaInc = 2.0f * PI_F / static_cast<float>(PointCount);
        float Offset   = 0.1f * static_cast<float>(i);

        for (Int32 j = 0; j < PointCount; ++j)
        {
            float  Theta    = Offset + static_cast<float>(j) * ThetaInc;
            float2 Position = Radius * float2(cos(Theta), sin(Theta));
            Kernel.push_back(Position);
        }
    }

    return Kernel;
}

static std::vector<float> GenerateGaussKernel(Int32 Radius, float Sigma)
{
    std::vector<float> Kernel{};
    Kernel.reserve(2 * Radius + 1);

    float Sum = 0.0f;
    for (Int32 i = -Radius; i <= Radius; ++i)
    {
        float Value = exp(-static_cast<float>(i * i) / (2.0f * Sigma * Sigma));
        Kernel.push_back(Value);
        Sum += Value;
    }

    for (float& Value : Kernel)
        Value /= Sum;

    return Kernel;
}

DepthOfField::DepthOfField(IRenderDevice* pDevice, const CreateInfo& CI) :
    m_pDOFAttribs{std::make_unique<HLSL::DepthOfFieldAttribs>()},
    m_Settings{CI}
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RenderDeviceWithCache_N Device{pDevice};

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(Device, sizeof(HLSL::DepthOfFieldAttribs), "DepthOfFieldAttribs::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_pDOFAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);

    {
        auto KernelData = GenerateKernelPoints(m_pDOFAttribs->BokehKernelRingCount, m_pDOFAttribs->BokehKernelRingDensity);
        KernelData.resize(128, float2(0.0, 0.0));

        TextureSubResData ResourceData;
        ResourceData.pData  = KernelData.data();
        ResourceData.Stride = sizeof(float2) * KernelData.size();

        TextureData TexData;
        TexData.pSubResources   = &ResourceData;
        TexData.NumSubresources = 1;

        TextureDesc Desc;
        Desc.Name      = "DepthOfField::LargeBokehKernel";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = static_cast<Uint32>(KernelData.size());
        Desc.Height    = 1;
        Desc.Format    = TEX_FORMAT_RG32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_BOKEH_LARGE_KERNEL_TEXTURE, Device.CreateTexture(Desc, &TexData));
    }

    {
        auto KernelData = GenerateKernelPoints(DOF_BOKEH_KERNEL_SMALL_RING_COUNT, DOF_BOKEH_KERNEL_SMALL_RING_DENSITY);

        TextureSubResData ResourceData;
        ResourceData.pData  = KernelData.data();
        ResourceData.Stride = sizeof(float2) * KernelData.size();

        TextureData TexData;
        TexData.pSubResources   = &ResourceData;
        TexData.NumSubresources = 1;

        TextureDesc Desc;
        Desc.Name      = "DepthOfField::SmallBokehKernel";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = static_cast<Uint32>(KernelData.size());
        Desc.Height    = 1;
        Desc.Format    = TEX_FORMAT_RG32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_BOKEH_SMALL_KERNEL_TEXTURE, Device.CreateTexture(Desc, &TexData));
    }

    {
        auto KernelData = GenerateGaussKernel(DOF_GAUSS_KERNEL_RADIUS, DOF_GAUSS_KERNEL_SIGMA);

        TextureSubResData ResourceData;
        ResourceData.pData  = KernelData.data();
        ResourceData.Stride = sizeof(float) * KernelData.size();

        TextureData TexData;
        TexData.pSubResources   = &ResourceData;
        TexData.NumSubresources = 1;

        TextureDesc Desc;
        Desc.Name      = "DepthOfField::GaussKernel";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = static_cast<Uint32>(KernelData.size());
        Desc.Height    = 1;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_GAUSS_KERNEL_TEXTURE, Device.CreateTexture(Desc, &TexData));
    }
}

DepthOfField::~DepthOfField() {}

void DepthOfField::PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");
    DEV_CHECK_ERR(pPostFXContext != nullptr, "pPostFXContext must not be null");

    const PostFXContext::FrameDesc& FrameDesc = pPostFXContext->GetFrameDesc();

    m_CurrentFrameIdx = FrameDesc.Index;

    if (m_BackBufferWidth == FrameDesc.Width && m_BackBufferHeight == FrameDesc.Height && m_FeatureFlags == FeatureFlags)
        return;

    for (auto& Iter : m_RenderTech)
        Iter.second.SRB.Release();

    m_BackBufferWidth  = FrameDesc.Width;
    m_BackBufferHeight = FrameDesc.Height;
    m_FeatureFlags     = FeatureFlags;

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

    if (FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING)
    {
        for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0; TextureIdx <= RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE1; ++TextureIdx)
        {
            TextureDesc Desc;
            Desc.Name      = "DepthOfField::TemporalCircleOfConfusion";
            Desc.Type      = RESOURCE_DIM_TEX_2D;
            Desc.Width     = m_BackBufferWidth;
            Desc.Height    = m_BackBufferHeight;
            Desc.Format    = TEX_FORMAT_R16_FLOAT;
            Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

            RefCntAutoPtr<ITexture> pTexture = Device.CreateTexture(Desc);

            float ClearColor[] = {0.0, 0.0, 0.0, 0.0};
            pPostFXContext->ClearRenderTarget({nullptr, nullptr, pDeviceContext}, pTexture, ClearColor);
            m_Resources.Insert(TextureIdx, pTexture);
        }
    }

    TEXTURE_FORMAT TextureCoCFormat = TEX_FORMAT_R16_FLOAT;
    if (pDevice->GetTextureFormatInfo(TEX_FORMAT_R16_UNORM).Supported)
        TextureCoCFormat = TEX_FORMAT_R16_UNORM;

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0; TextureIdx <= RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::DilationCircleOfConfusion";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth >> (TextureIdx - RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0);
        Desc.Height    = m_BackBufferHeight >> (TextureIdx - RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0);
        Desc.Format    = TextureCoCFormat;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }

    // We use this texture like intermediate texture for blurring dilation CoC texture
    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::DilationCircleOfConfusionIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth >> (RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP - RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0);
        Desc.Height    = m_BackBufferHeight >> (RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP - RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0);
        Desc.Format    = TextureCoCFormat;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_INTERMEDIATE, Device.CreateTexture(Desc));
    }


    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0; TextureIdx <= RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Prefiltered";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_BOKEH_TEXTURE0; TextureIdx <= RESOURCE_IDENTIFIER_BOKEH_TEXTURE1; ++TextureIdx)
    {
        TextureDesc Desc;
        Desc.Name      = "DepthOfField::Bokeh";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_BackBufferWidth / 2;
        Desc.Height    = m_BackBufferHeight / 2;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
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

    bool AllPSOsReady = PrepareShadersAndPSO(RenderAttribs, m_FeatureFlags) && RenderAttribs.pPostFXContext->IsPSOsReady();
    UpdateConstantBuffers(RenderAttribs, !AllPSOsReady);
    if (AllPSOsReady)
    {
        ComputeCircleOfConfusion(RenderAttribs);
        ComputeTemporalCircleOfConfusion(RenderAttribs);
        ComputeSeparatedCircleOfConfusion(RenderAttribs);
        ComputeDilationCircleOfConfusion(RenderAttribs);
        ComputeCircleOfConfusionBlurX(RenderAttribs);
        ComputeCircleOfConfusionBlurY(RenderAttribs);
        ComputePrefilteredTexture(RenderAttribs);
        ComputeBokehFirstPass(RenderAttribs);
        ComputeBokehSecondPass(RenderAttribs);
        ComputePostFilteredTexture(RenderAttribs);
        ComputeCombinedTexture(RenderAttribs);
    }
    else
    {
        ComputePlaceholderTexture(RenderAttribs);
    }

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

bool DepthOfField::UpdateUI(HLSL::DepthOfFieldAttribs& Attribs, FEATURE_FLAGS& FeatureFlags)
{
    bool ActiveTemporalSmoothing = (FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING) != 0;
    bool ActiveKarisInverse      = (FeatureFlags & FEATURE_FLAG_ENABLE_KARIS_INVERSE) != 0;

    bool AttribsChanged = false;

    if (ImGui::SliderFloat("CoC Limit factor", &Attribs.MaxCircleOfConfusion, 0.005f, 0.02f))
        AttribsChanged = true;
    ImGui::HelpMarker("The intensity of the depth of field effect.");

    {
        ImGui::ScopedDisabler Disabler{!ActiveTemporalSmoothing};
        if (ImGui::SliderFloat("Temporal Stability Factor", &Attribs.TemporalStabilityFactor, 0.0f, 1.0f))
            AttribsChanged = true;
        ImGui::HelpMarker("This parameter is used to control the stability of the temporal accumulation of the CoC.");
    }

    if (ImGui::SliderInt("Bokeh Kernel Ring Count", &Attribs.BokehKernelRingCount, 2, 5))
        AttribsChanged = true;
    ImGui::HelpMarker("The number of rings in the Octaweb kernel.");

    if (ImGui::SliderInt("Bokeh Kernel Ring Density", &Attribs.BokehKernelRingDensity, 2, 7))
        AttribsChanged = true;
    ImGui::HelpMarker("The number of samples within each ring of the Octaweb kernel.");

    if (ImGui::Checkbox("Temporal Smoothing", &ActiveTemporalSmoothing))
        AttribsChanged = true;
    ImGui::HelpMarker("Enable temporal accumulation for CoC");

    if (ImGui::Checkbox("Karis inverse", &ActiveKarisInverse))
        AttribsChanged = true;
    ImGui::HelpMarker("Increases the intensity of bokeh circles but may affect temporal stability.");

    auto ResetStateFeatureMask = [](FEATURE_FLAGS& FeatureFlags, FEATURE_FLAGS Flag, bool State) {
        if (State)
            FeatureFlags |= Flag;
        else
            FeatureFlags &= ~Flag;
    };

    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING, ActiveTemporalSmoothing);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_ENABLE_KARIS_INVERSE, ActiveKarisInverse);
    return AttribsChanged;
}

ITextureView* DepthOfField::GetDepthOfFieldTextureSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].GetTextureSRV();
}

bool DepthOfField::PrepareShadersAndPSO(const RenderAttributes& RenderAttribs, FEATURE_FLAGS FeatureFlags)
{
    bool AllPSOsReady = true;

    const SHADER_COMPILE_FLAGS ShaderFlags = RenderAttribs.pPostFXContext->GetShaderCompileFlags(m_Settings.EnableAsyncCreation);
    const PSO_CREATE_FLAGS     PSOFlags    = m_Settings.EnableAsyncCreation ? PSO_CREATE_FLAG_ASYNCHRONOUS : PSO_CREATE_FLAG_NONE;

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeCircleOfConfusion.fx", "ComputeCircleOfConfusionPS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC, SHADER_VARIABLE_FLAG_UNFILTERABLE_FLOAT_TEXTURE_WEBGPU);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeCircleOfConfusion",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_TEMPORAL, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeTemporalCircleOfConfusion.fx", "ComputeTemporalCircleOfConfusionPS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevCoC", Sam_LinearClamp)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureCurrCoC", Sam_PointClamp);


            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeTemporalCircleOfConfusion",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_SEPARATED, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeSeparatedCircleOfConfusion.fx", "ComputeSeparatedCoCPS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeSeparatedCircleOfConfusion",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_DILATION, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeDilationCircleOfConfusion.fx", "ComputeDilationCoCPS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);


            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeDilationCircleOfConfusion",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_X, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            ShaderMacroHelper Macros;
            Macros.Add("DOF_CIRCLE_OF_CONFUSION_BLUR_TYPE", DOF_CIRCLE_OF_CONFUSION_BLUR_X);

            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeBlurredCircleOfConfusion.fx", "ComputeBlurredCoCPS",
                SHADER_TYPE_PIXEL, Macros, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureGaussKernel", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeCircleOfConfusionBlurX",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_INTERMEDIATE].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_Y, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            ShaderMacroHelper Macros;
            Macros.Add("DOF_CIRCLE_OF_CONFUSION_BLUR_TYPE", DOF_CIRCLE_OF_CONFUSION_BLUR_Y);

            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeBlurredCircleOfConfusion.fx", "ComputeBlurredCoCPS",
                SHADER_TYPE_PIXEL, Macros, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureGaussKernel", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeCircleOfConfusionBlurY",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_TEXTURE, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputePrefilteredTexture.fx", "ComputePrefilteredTexturePS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDilationCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDilationCoC", Sam_LinearClamp);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputePrefilteredTexture",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].AsTexture()->GetDesc().Format,
                                         m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BOKEH_FIRST_PASS, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            ShaderMacroHelper Macros;
            Macros.Add("DOF_OPTION_KARIS_INVERSE", (m_FeatureFlags & FEATURE_FLAG_ENABLE_KARIS_INVERSE) != 0);

            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeBokehFirstPass.fx", "ComputeBokehPS",
                SHADER_TYPE_PIXEL, Macros, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureBokehKernel", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", Sam_LinearClamp)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", Sam_LinearClamp);

            if (m_FeatureFlags & FEATURE_FLAG_ENABLE_KARIS_INVERSE)
            {
                ResourceLayout
                    .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                    .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureRadiance", Sam_LinearClamp);
            }

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeBokehFirstPass",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].AsTexture()->GetDesc().Format,
                                         m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE1].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BOKEH_SECOND_PASS, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeBokehSecondPass.fx", "ComputeBokehPS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureBokehKernel", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", Sam_LinearClamp)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", Sam_LinearClamp);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeBokehSecondPass",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].AsTexture()->GetDesc().Format,
                                         m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_POST_FILTERED_TEXTURE, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputePostfilteredTexture.fx", "ComputePostfilteredTexturePS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCNear", Sam_LinearClamp)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureColorCoCFar", Sam_LinearClamp);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputePostFilteredTexture",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].AsTexture()->GetDesc().Format,
                                         m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    {
        RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_COMBINED_TEXTURE, FeatureFlags);
        if (!RenderTech.IsInitializedPSO())
        {
            RefCntAutoPtr<IShader> VS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
                SHADER_TYPE_VERTEX, {}, ShaderFlags);

            RefCntAutoPtr<IShader> PS = PostFXRenderTechnique::CreateShader(
                RenderAttribs.pDevice, RenderAttribs.pStateCache,
                "DOF_ComputeCombinedTexture.fx", "ComputeCombinedTexturePS",
                SHADER_TYPE_PIXEL, {}, ShaderFlags);

            PipelineResourceLayoutDescX ResourceLayout;
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCoC", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDoFNearPlane", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDoFFarPlane", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDoFNearPlane", Sam_LinearClamp)
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDoFFarPlane", Sam_LinearClamp);

            RenderTech.InitializePSO(RenderAttribs.pDevice,
                                     RenderAttribs.pStateCache, "DepthOfField::ComputeCombinedTexture",
                                     VS, PS, ResourceLayout,
                                     {
                                         m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].AsTexture()->GetDesc().Format,
                                     },
                                     TEX_FORMAT_UNKNOWN,
                                     DSS_DisableDepth, BS_Default, false, PSOFlags);
        }
        if (AllPSOsReady && !RenderTech.IsReady())
            AllPSOsReady = false;
    }

    return AllPSOsReady;
}

void DepthOfField::UpdateConstantBuffers(const RenderAttributes& RenderAttribs, bool ResetTimer)
{
    if (ResetTimer)
        m_FrameTimer.Restart();

    float Alpha = std::min(std::max(m_FrameTimer.GetElapsedTimef() * RenderAttribs.pPostFXContext->GetInterpolationSpeed(), 0.0f), 1.0f);

    if (RenderAttribs.pDOFAttribs->BokehKernelRingCount != m_pDOFAttribs->BokehKernelRingCount || RenderAttribs.pDOFAttribs->BokehKernelRingDensity != m_pDOFAttribs->BokehKernelRingDensity)
    {
        auto KernelData = GenerateKernelPoints(RenderAttribs.pDOFAttribs->BokehKernelRingCount, RenderAttribs.pDOFAttribs->BokehKernelRingDensity);

        TextureSubResData ResourceData;
        ResourceData.pData  = KernelData.data();
        ResourceData.Stride = sizeof(float2) * KernelData.size();

        Box Region{0u, static_cast<Uint32>(KernelData.size()), 0u, 1};
        RenderAttribs.pDeviceContext->UpdateTexture(m_Resources[RESOURCE_IDENTIFIER_BOKEH_LARGE_KERNEL_TEXTURE].AsTexture(), 0, 0, Region, ResourceData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    bool UpdateRequired = m_pDOFAttribs->AlphaInterpolation != Alpha || memcmp(RenderAttribs.pDOFAttribs, m_pDOFAttribs.get(), sizeof(HLSL::DepthOfFieldAttribs)) != 0;
    if (UpdateRequired)
    {
        memcpy(m_pDOFAttribs.get(), RenderAttribs.pDOFAttribs, sizeof(HLSL::DepthOfFieldAttribs));
        m_pDOFAttribs->AlphaInterpolation = Alpha;
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::DepthOfFieldAttribs), m_pDOFAttribs.get(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void DepthOfField::ComputeCircleOfConfusion(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION, m_FeatureFlags);

    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

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

void DepthOfField::ComputeTemporalCircleOfConfusion(const RenderAttributes& RenderAttribs)
{
    if (!(m_FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING))
        return;

    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_TEMPORAL, m_FeatureFlags);

    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeTemporalCircleOfConfusion"};

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;
    const Uint32 PrevFrameIdx = (FrameIndex + 1) & 0x01;

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0 + CurrFrameIdx].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(RenderAttribs.pPostFXContext->GetClosestMotionVectors());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeSeparatedCircleOfConfusion(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_SEPARATED, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(true);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeSeparatedCircleOfConfusion"};

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    ITextureView* pRTVs[] = {m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0].GetTextureRTV()};

    if (m_FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING)
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0 + CurrFrameIdx].GetTextureSRV());
    else
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeDilationCircleOfConfusion(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_DILATION, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeHierarchicalCoC"};

    for (Uint32 TextureMip = RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0; TextureMip < RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP; ++TextureMip)
    {
        ITextureView* pRTVs[] = {m_Resources[TextureMip + 1].GetTextureRTV()};

        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureLastMip"}.Set(m_Resources[TextureMip].GetTextureSRV());

        RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
        RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void DepthOfField::ComputeCircleOfConfusionBlurX(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_X, m_FeatureFlags);

    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_TextureGaussKernel"}.Set(m_Resources[RESOURCE_IDENTIFIER_GAUSS_KERNEL_TEXTURE].GetTextureSRV());
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeCircleOfConfusionBlurX"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_INTERMEDIATE].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeCircleOfConfusionBlurY(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_Y, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_TextureGaussKernel"}.Set(m_Resources[RESOURCE_IDENTIFIER_GAUSS_KERNEL_TEXTURE].GetTextureSRV());
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeCircleOfConfusionBlurY"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_INTERMEDIATE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputePrefilteredTexture(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_PREFILTERED_TEXTURE, m_FeatureFlags);

    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputePrefilteredTexture"};

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDilationCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP].GetTextureSRV());

    if (m_FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING)
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0 + CurrFrameIdx].GetTextureSRV());
    else
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeBokehFirstPass(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BOKEH_FIRST_PASS, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_TextureBokehKernel"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_LARGE_KERNEL_TEXTURE].GetTextureSRV());
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBokehFirstPass"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE1].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCNear"}.Set(m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCFar"}.Set(m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRadiance"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeBokehSecondPass(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_BOKEH_SECOND_PASS, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_TextureBokehKernel"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_SMALL_KERNEL_TEXTURE].GetTextureSRV());
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBokehSecondPass"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCNear"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCFar"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE1].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputePostFilteredTexture(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_POST_FILTERED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
        RenderTech.InitializeSRB(false);

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputePostFilteredTexture"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE1].GetTextureRTV()};

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCNear"}.Set(m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColorCoCFar"}.Set(m_Resources[RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputeCombinedTexture(const RenderAttributes& RenderAttribs)
{
    RenderTechnique& RenderTech = GetRenderTechnique(RENDER_TECH_COMPUTE_COMBINED_TEXTURE, m_FeatureFlags);
    if (!RenderTech.IsInitializedSRB())
    {
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbDepthOfFieldAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeCombinedTexture"};

    const Uint32 FrameIndex   = RenderAttribs.pPostFXContext->GetFrameDesc().Index;
    const Uint32 CurrFrameIdx = (FrameIndex + 0) & 0x01;

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].GetTextureRTV()};

    if (m_FeatureFlags & FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING)
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0 + CurrFrameIdx].GetTextureSRV());
    else
        ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCoC"}.Set(m_Resources[RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE].GetTextureSRV());

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureColor"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDoFNearPlane"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE0].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDoFFarPlane"}.Set(m_Resources[RESOURCE_IDENTIFIER_BOKEH_TEXTURE1].GetTextureSRV());

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void DepthOfField::ComputePlaceholderTexture(const RenderAttributes& RenderAttribs)
{
    PostFXContext::TextureOperationAttribs CopyTextureAttribs;
    CopyTextureAttribs.pDevice        = RenderAttribs.pDevice;
    CopyTextureAttribs.pDeviceContext = RenderAttribs.pDeviceContext;
    CopyTextureAttribs.pStateCache    = RenderAttribs.pStateCache;
    RenderAttribs.pPostFXContext->CopyTextureColor(CopyTextureAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV(), m_Resources[RESOURCE_IDENTIFIER_COMBINED_TEXTURE].GetTextureRTV());
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
