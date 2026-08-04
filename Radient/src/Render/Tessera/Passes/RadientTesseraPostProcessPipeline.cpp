/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "Render/Tessera/Passes/RadientTesseraPostProcessPipeline.hpp"

#include "Render/RadientPostFXParameters.hpp"

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ShaderMacroHelper.hpp"
#include "CommonlyUsedStates.h"

#include <vector>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/Radient/public/RadientPostProcessStructures.fxh"
} // namespace HLSL

namespace
{

TEXTURE_FORMAT GetTextureViewFormat(ITextureView* pView)
{
    if (pView == nullptr)
        return TEX_FORMAT_UNKNOWN;

    const TextureViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.Format != TEX_FORMAT_UNKNOWN)
        return ViewDesc.Format;

    ITexture* pTexture = pView->GetTexture();
    return pTexture != nullptr ? pTexture->GetDesc().Format : TEX_FORMAT_UNKNOWN;
}

bool RequiresOutputSRGBConversion(TEXTURE_FORMAT Format)
{
    return Format == TEX_FORMAT_RGBA8_UNORM ||
        Format == TEX_FORMAT_BGRA8_UNORM;
}

} // namespace

RADIENT_STATUS RadientTesseraPostProcessPipeline::Prepare(IRenderDevice*                   pDevice,
                                                          IDeviceContext*                  pContext,
                                                          const RadientFrameRenderTargets& Targets,
                                                          const RadientToneMappingDesc&    ToneMapping,
                                                          const RadientSSAODesc&           SSAO,
                                                          const RadientSSRDesc&            SSR,
                                                          Uint32                           FrameIndex,
                                                          IBuffer*                         pFrameAttribsCB,
                                                          ITextureView*                    pPreintegratedGGXSRV)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* const pSceneColorSRV  = Targets.GetSceneColorSRV();
    ITextureView* const pOutputColorRTV = Targets.GetOutputColorRTV();
    if (pSceneColorSRV == nullptr || pOutputColorRTV == nullptr || pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const TEXTURE_FORMAT OutputFormat = GetTextureViewFormat(pOutputColorRTV);
    if (OutputFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const bool SSAOEnabled   = SSAO.Enabled != False;
    const bool SSREnabled    = SSR.Enabled != False;
    const bool PostFXEnabled = SSAOEnabled || SSREnabled;

    ITextureView* pSSAOSRV = nullptr;
    ITextureView* pSSRSRV  = nullptr;
    if (PostFXEnabled)
    {
        if (Targets.GetDepthSRV() == nullptr ||
            Targets.GetPreviousDepthSRV() == nullptr ||
            Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR) == nullptr ||
            Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_NORMAL) == nullptr)
        {
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        if (m_pPostFXContext == nullptr)
        {
            PostFXContext::CreateInfo CI;
            CI.PackMatrixRowMajor = true;
            m_pPostFXContext      = std::make_unique<PostFXContext>(pDevice, CI);
        }

        const RadientExtent2D& Size = Targets.GetSize();
        const PostFXContext::FEATURE_FLAGS PostFXFlags = Targets.GetUseReverseDepth() ?
            PostFXContext::FEATURE_FLAG_REVERSED_DEPTH :
            PostFXContext::FEATURE_FLAG_NONE;
        m_pPostFXContext->PrepareResources(pDevice, {FrameIndex, Size.Width, Size.Height}, PostFXFlags);

        if (SSAOEnabled)
        {
            if (m_pSSAO == nullptr)
                m_pSSAO = std::make_unique<ScreenSpaceAmbientOcclusion>(pDevice, ScreenSpaceAmbientOcclusion::CreateInfo{});

            m_pSSAO->PrepareResources(pDevice,
                                      pContext,
                                      m_pPostFXContext.get(),
                                      ScreenSpaceAmbientOcclusion::FEATURE_FLAG_NONE);
            pSSAOSRV = m_pSSAO->GetAmbientOcclusionSRV();
            if (pSSAOSRV == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }

        if (SSREnabled)
        {
            if (pPreintegratedGGXSRV == nullptr ||
                Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR) == nullptr ||
                Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MATERIAL) == nullptr ||
                Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_IBL) == nullptr)
            {
                return RADIENT_STATUS_INVALID_OPERATION;
            }

            if (m_pSSR == nullptr)
                m_pSSR = std::make_unique<ScreenSpaceReflection>(pDevice, ScreenSpaceReflection::CreateInfo{});

            m_pSSR->PrepareResources(pDevice,
                                     pContext,
                                     m_pPostFXContext.get(),
                                     ScreenSpaceReflection::FEATURE_FLAG_NONE);
            pSSRSRV = m_pSSR->GetSSRRadianceSRV();
            if (pSSRSRV == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }
    }

    const HLSL::ToneMappingAttribs ToneMappingAttribs  = RadientPostFX::MakeToneMappingAttribs(ToneMapping);
    const bool                     ConvertOutputToSRGB = RequiresOutputSRGBConversion(OutputFormat);
    if (SSAOEnabled &&
        (!m_SSAOEnabled || m_TargetVersion != Targets.GetVersion() || m_SSAO != SSAO))
    {
        m_ResetSSAO = true;
    }

    if (m_pPSO == nullptr ||
        m_OutputFormat != OutputFormat ||
        m_ToneMappingMode != ToneMappingAttribs.iToneMappingMode ||
        m_ConvertOutputToSRGB != ConvertOutputToSRGB ||
        m_SSAOEnabled != SSAOEnabled ||
        m_SSREnabled != SSREnabled ||
        (SSREnabled && m_pPreintegratedGGXSRV != pPreintegratedGGXSRV) ||
        m_pFrameAttribsCB != pFrameAttribsCB)
    {
        const RADIENT_STATUS Status = CreatePipelineState(pDevice,
                                                          OutputFormat,
                                                          ToneMappingAttribs.iToneMappingMode,
                                                          ConvertOutputToSRGB,
                                                          SSAOEnabled,
                                                          SSREnabled,
                                                          pFrameAttribsCB,
                                                          pPreintegratedGGXSRV);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    const bool ToneMappingEnabled = ToneMappingAttribs.iToneMappingMode > TONE_MAPPING_MODE_NONE;
    if (ToneMappingEnabled)
    {
        if (m_pToneMappingAttribsCB == nullptr)
        {
            CreateUniformBuffer(pDevice,
                                sizeof(HLSL::RadientPostProcessAttribs),
                                "Radient post-process attribs buffer",
                                m_pToneMappingAttribsCB.GetAddressOfEmpty(),
                                USAGE_DYNAMIC);
            if (m_pToneMappingAttribsCB == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            IShaderResourceVariable* const pAttribsVar =
                m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbRadientPostProcessAttribs");
            if (pAttribsVar == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
            pAttribsVar->Set(m_pToneMappingAttribsCB);
        }

        MapHelper<HLSL::RadientPostProcessAttribs> Attribs{pContext,
                                                           m_pToneMappingAttribsCB,
                                                           MAP_WRITE,
                                                           MAP_FLAG_DISCARD};
        if (!Attribs)
            return RADIENT_STATUS_INVALID_OPERATION;

        Attribs->ToneMapping = ToneMappingAttribs;
        // Hydrogent currently uses a fixed luminance value. Automatic
        // luminance reduction will replace this value in a later stage.
        Attribs->AverageLogLum = 0.3f;
    }

    if (m_pSRB == nullptr ||
        m_TargetVersion != Targets.GetVersion() ||
        m_pBoundSSRSRV != pSSRSRV ||
        m_pBoundSSAOSRV != pSSAOSRV)
    {
        m_pSRB.Release();
        m_pPSO->CreateShaderResourceBinding(m_pSRB.GetAddressOfEmpty(), true);
        if (m_pSRB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        IShaderResourceVariable* const pColorVar =
            m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer");
        if (pColorVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        pColorVar->Set(pSceneColorSRV);

        if (SSAOEnabled)
        {
            IShaderResourceVariable* const pSSAOVar =
                m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SSAO");
            if (pSSAOVar == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            pSSAOVar->Set(pSSAOSRV);
        }

        if (SSREnabled)
        {
            struct TextureBinding
            {
                const Char*   Name;
                ITextureView* pView;
            };
            const TextureBinding TextureBindings[] = {
                {"g_SSR", pSSRSRV},
                {"g_SpecularIBL", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_IBL)},
                {"g_Normal", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_NORMAL)},
                {"g_MaterialData", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MATERIAL)},
                {"g_BaseColor", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR)},
            };
            for (const TextureBinding& Binding : TextureBindings)
            {
                IShaderResourceVariable* const pVariable =
                    m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, Binding.Name);
                if (pVariable == nullptr || Binding.pView == nullptr)
                    return RADIENT_STATUS_INVALID_OPERATION;

                pVariable->Set(Binding.pView);
            }
        }

        m_TargetVersion = Targets.GetVersion();
        m_pBoundSSRSRV  = pSSRSRV;
        m_pBoundSSAOSRV = pSSAOSRV;
    }

    m_SSAO = SSAO;
    m_SSR  = SSR;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::Execute(IRenderDevice*                   pDevice,
                                                          IDeviceContext*                  pContext,
                                                          const RadientFrameRenderTargets& Targets,
                                                          bool                             ResetTemporalHistory)
{
    if (pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_SSAOEnabled || m_SSREnabled)
    {
        if (pDevice == nullptr || m_pPostFXContext == nullptr || m_pFrameAttribsCB == nullptr ||
            (m_SSAOEnabled && m_pSSAO == nullptr) ||
            (m_SSREnabled && m_pSSR == nullptr))
        {
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        ITextureView* const pDepthSRV         = Targets.GetDepthSRV();
        ITextureView* const pPreviousDepthSRV = ResetTemporalHistory ?
            pDepthSRV :
            Targets.GetPreviousDepthSRV();
        ITextureView* const pMotionVectorsSRV =
            Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR);
        ITextureView* const pNormalSRV =
            Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_NORMAL);
        if (pDepthSRV == nullptr || pPreviousDepthSRV == nullptr ||
            pMotionVectorsSRV == nullptr || pNormalSRV == nullptr)
        {
            return RADIENT_STATUS_OUT_OF_DATE;
        }

        pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        PostFXContext::RenderAttributes PostFXAttribs;
        PostFXAttribs.pDevice             = pDevice;
        PostFXAttribs.pDeviceContext      = pContext;
        PostFXAttribs.pCurrDepthBufferSRV = pDepthSRV;
        PostFXAttribs.pPrevDepthBufferSRV = pPreviousDepthSRV;
        PostFXAttribs.pMotionVectorsSRV   = pMotionVectorsSRV;
        PostFXAttribs.pCameraAttribsCB    = m_pFrameAttribsCB;
        m_pPostFXContext->Execute(PostFXAttribs);

        if (m_SSREnabled)
        {
            HLSL::ScreenSpaceReflectionAttribs SSRAttribs = RadientPostFX::MakeSSRAttribs(m_SSR);

            ScreenSpaceReflection::RenderAttributes SSRRenderAttribs;
            SSRRenderAttribs.pDevice            = pDevice;
            SSRRenderAttribs.pDeviceContext     = pContext;
            SSRRenderAttribs.pPostFXContext     = m_pPostFXContext.get();
            SSRRenderAttribs.pColorBufferSRV    = Targets.GetSceneColorSRV();
            SSRRenderAttribs.pDepthBufferSRV    = pDepthSRV;
            SSRRenderAttribs.pNormalBufferSRV   = pNormalSRV;
            SSRRenderAttribs.pMaterialBufferSRV = Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MATERIAL);
            SSRRenderAttribs.pMotionVectorsSRV  = pMotionVectorsSRV;
            SSRRenderAttribs.pSSRAttribs        = &SSRAttribs;
            m_pSSR->Execute(SSRRenderAttribs);
        }

        if (m_SSAOEnabled)
        {
            HLSL::ScreenSpaceAmbientOcclusionAttribs SSAOAttribs = RadientPostFX::MakeSSAOAttribs(m_SSAO);
            SSAOAttribs.ResetAccumulation                        = ResetTemporalHistory || m_ResetSSAO;

            ScreenSpaceAmbientOcclusion::RenderAttributes SSAORenderAttribs;
            SSAORenderAttribs.pDevice          = pDevice;
            SSAORenderAttribs.pDeviceContext   = pContext;
            SSAORenderAttribs.pPostFXContext   = m_pPostFXContext.get();
            SSAORenderAttribs.pDepthBufferSRV  = pDepthSRV;
            SSAORenderAttribs.pNormalBufferSRV = pNormalSRV;
            SSAORenderAttribs.pSSAOAttribs     = &SSAOAttribs;
            m_pSSAO->Execute(SSAORenderAttribs);
            m_ResetSSAO = false;
        }
    }

    ITextureView* pOutputColorRTV = Targets.GetOutputColorRTV();
    if (pOutputColorRTV == nullptr || m_pPSO == nullptr || m_pSRB == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    pContext->SetRenderTargets(1, &pOutputColorRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->SetPipelineState(m_pPSO);
    pContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::CreatePipelineState(IRenderDevice* pDevice,
                                                                      TEXTURE_FORMAT OutputFormat,
                                                                      Int32          ToneMappingMode,
                                                                      bool           ConvertOutputToSRGB,
                                                                      bool           SSAOEnabled,
                                                                      bool           SSREnabled,
                                                                      IBuffer*       pFrameAttribsCB,
                                                                      ITextureView*  pPreintegratedGGXSRV)
{
    m_pSRB.Release();
    m_pPSO.Release();
    m_pToneMappingAttribsCB.Release();
    m_pFrameAttribsCB.Release();
    m_pPreintegratedGGXSRV.Release();
    m_TargetVersion = ~Uint32{0};
    m_pBoundSSRSRV  = nullptr;
    m_pBoundSSAOSRV = nullptr;

    ShaderMacroHelper Macros;
    Macros.Add("TONE_MAPPING_MODE", ToneMappingMode);
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", ConvertOutputToSRGB);
    Macros.Add("ENABLE_SSAO", SSAOEnabled);
    Macros.Add("ENABLE_SSR", SSREnabled);

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    RefCntAutoPtr<IShader> pVS;
    ShaderCI.Desc       = {"Radient full-screen triangle VS", SHADER_TYPE_VERTEX, true};
    ShaderCI.EntryPoint = "FullScreenTriangleVS";
    ShaderCI.FilePath   = "FullScreenTriangleVS.fx";
    ShaderCI.Macros     = {};
    pDevice->CreateShader(ShaderCI, pVS.GetAddressOfEmpty());

    RefCntAutoPtr<IShader> pPS;
    ShaderCI.Desc       = {"Radient tone mapping PS", SHADER_TYPE_PIXEL, true};
    ShaderCI.EntryPoint = "main";
    ShaderCI.FilePath   = "RadientToneMapping.psh";
    ShaderCI.Macros     = Macros;
    pDevice->CreateShader(ShaderCI, pPS.GetAddressOfEmpty());
    if (pVS == nullptr || pPS == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const bool                              ToneMappingEnabled   = ToneMappingMode > TONE_MAPPING_MODE_NONE;
    const bool                              FrameAttribsRequired = ToneMappingEnabled || SSREnabled;
    std::vector<ShaderResourceVariableDesc> Variables;
    Variables.push_back({SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
    if (SSAOEnabled)
        Variables.push_back({SHADER_TYPE_PIXEL, "g_SSAO", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
    if (SSREnabled)
    {
        Variables.push_back({SHADER_TYPE_PIXEL, "g_SSR", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        Variables.push_back({SHADER_TYPE_PIXEL, "g_SpecularIBL", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        Variables.push_back({SHADER_TYPE_PIXEL, "g_Normal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        Variables.push_back({SHADER_TYPE_PIXEL, "g_MaterialData", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        Variables.push_back({SHADER_TYPE_PIXEL, "g_BaseColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        Variables.push_back({SHADER_TYPE_PIXEL, "g_PreintegratedGGX", SHADER_RESOURCE_VARIABLE_TYPE_STATIC});
    }
    if (ToneMappingEnabled)
        Variables.push_back({SHADER_TYPE_PIXEL, "cbRadientPostProcessAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC});
    if (FrameAttribsRequired)
        Variables.push_back({SHADER_TYPE_PIXEL, "cbFrameAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC});

    ImmutableSamplerDesc PreintegratedGGXSampler;
    if (SSREnabled)
    {
        PreintegratedGGXSampler.ShaderStages         = SHADER_TYPE_PIXEL;
        PreintegratedGGXSampler.SamplerOrTextureName = "g_PreintegratedGGX";
        PreintegratedGGXSampler.Desc                 = Sam_LinearClamp;
    }

    GraphicsPipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name                                       = "Radient tone mapping PSO";
    PSOCI.PSODesc.PipelineType                               = PIPELINE_TYPE_GRAPHICS;
    PSOCI.PSODesc.ResourceLayout.Variables                   = Variables.data();
    PSOCI.PSODesc.ResourceLayout.NumVariables                = static_cast<Uint32>(Variables.size());
    PSOCI.PSODesc.ResourceLayout.ImmutableSamplers           = SSREnabled ? &PreintegratedGGXSampler : nullptr;
    PSOCI.PSODesc.ResourceLayout.NumImmutableSamplers        = SSREnabled ? 1u : 0u;
    PSOCI.GraphicsPipeline.NumRenderTargets                  = 1;
    PSOCI.GraphicsPipeline.RTVFormats[0]                     = OutputFormat;
    PSOCI.GraphicsPipeline.PrimitiveTopology                 = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCI.GraphicsPipeline.RasterizerDesc.CullMode           = CULL_MODE_NONE;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthEnable      = False;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
    PSOCI.pVS                                                = pVS;
    PSOCI.pPS                                                = pPS;
    pDevice->CreateGraphicsPipelineState(PSOCI, m_pPSO.GetAddressOfEmpty());
    if (m_pPSO == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (FrameAttribsRequired)
    {
        IShaderResourceVariable* const pFrameAttribsVar =
            m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbFrameAttribs");
        if (pFrameAttribsVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pFrameAttribsVar->Set(pFrameAttribsCB);
    }

    if (SSREnabled)
    {
        IShaderResourceVariable* const pPreintegratedGGXVar =
            m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX");
        if (pPreintegratedGGXVar == nullptr || pPreintegratedGGXSRV == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pPreintegratedGGXVar->Set(pPreintegratedGGXSRV);
    }

    m_pFrameAttribsCB          = pFrameAttribsCB;
    m_pPreintegratedGGXSRV     = pPreintegratedGGXSRV;
    m_OutputFormat             = OutputFormat;
    m_ToneMappingMode          = ToneMappingMode;
    m_ConvertOutputToSRGB      = ConvertOutputToSRGB;
    m_SSAOEnabled              = SSAOEnabled;
    m_SSREnabled               = SSREnabled;
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
