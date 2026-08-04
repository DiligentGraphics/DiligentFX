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

RADIENT_STATUS RadientTesseraPostProcessPipeline::Prepare(const PrepareInfo& Info)
{
    IRenderDevice* const                   pDevice                 = Info.pDevice;
    IDeviceContext* const                  pContext                = Info.pContext;
    const RadientFrameRenderTargets&       Targets                 = Info.Targets;
    const RadientToneMappingDesc&          ToneMapping             = Info.View.ToneMapping;
    const RadientBloomDesc&                BloomDesc               = Info.View.Bloom;
    const RadientSSAODesc&                 SSAO                    = Info.View.SSAO;
    const RadientSSRDesc&                  SSR                     = Info.View.SSR;
    const RadientDepthOfFieldDesc&         DepthOfFieldDesc        = Info.View.DepthOfField;
    const Uint32                           FrameIndex              = Info.FrameIndex;
    IBuffer* const                         pFrameAttribsCB         = Info.pFrameAttribsCB;
    ITextureView* const                    pPreintegratedGGXSRV    = Info.pPreintegratedGGXSRV;

    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* const pSceneColorSRV  = Targets.GetSceneColorSRV();
    ITextureView* const pOutputColorRTV = Targets.GetOutputColorRTV();
    if (pSceneColorSRV == nullptr || pOutputColorRTV == nullptr || pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const TEXTURE_FORMAT OutputFormat = GetTextureViewFormat(pOutputColorRTV);
    if (OutputFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const bool SSAOEnabled          = SSAO.Enabled != False;
    const bool SSREnabled           = SSR.Enabled != False;
    const bool DepthOfFieldEnabled  = DepthOfFieldDesc.Enabled != False;
    const bool BloomEnabled         = BloomDesc.Enabled != False;
    const bool ScreenEffectsEnabled = SSAOEnabled || SSREnabled;
    const bool ColorEffectsEnabled  = DepthOfFieldEnabled || BloomEnabled;
    const bool PostFXEnabled        = ScreenEffectsEnabled || ColorEffectsEnabled;

    ITextureView* pSSAOSRV         = nullptr;
    ITextureView* pSSRSRV          = nullptr;
    ITextureView* pDepthOfFieldSRV = nullptr;
    ITextureView* pBloomSRV        = nullptr;
    if (PostFXEnabled)
    {
        if (Targets.GetDepthSRV() == nullptr ||
            Targets.GetPreviousDepthSRV() == nullptr ||
            Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR) == nullptr)
        {
            return RADIENT_STATUS_INVALID_OPERATION;
        }
        if (ScreenEffectsEnabled &&
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

        const RadientExtent2D&             Size        = Targets.GetSize();
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

        if (DepthOfFieldEnabled)
        {
            if (m_pDepthOfField == nullptr)
                m_pDepthOfField = std::make_unique<DepthOfField>(pDevice, DepthOfField::CreateInfo{});

            m_pDepthOfField->PrepareResources(pDevice,
                                              pContext,
                                              m_pPostFXContext.get(),
                                              RadientPostFX::GetDepthOfFieldFeatureFlags(DepthOfFieldDesc));
            pDepthOfFieldSRV = m_pDepthOfField->GetDepthOfFieldTextureSRV();
            if (pDepthOfFieldSRV == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }

        if (BloomEnabled)
        {
            if (m_pBloom == nullptr)
                m_pBloom = std::make_unique<Bloom>(pDevice, Bloom::CreateInfo{});

            m_pBloom->PrepareResources(pDevice,
                                       pContext,
                                       m_pPostFXContext.get(),
                                       Bloom::FEATURE_FLAG_NONE);
            pBloomSRV = m_pBloom->GetBloomTextureSRV();
            if (pBloomSRV == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }
    }

    const HLSL::ToneMappingAttribs ToneMappingAttribs  = RadientPostFX::MakeToneMappingAttribs(ToneMapping);
    const bool                     ConvertOutputToSRGB = RequiresOutputSRGBConversion(OutputFormat);
    if (SSAOEnabled &&
        (!m_SSAOEnabled || m_FinalPass.TargetVersion != Targets.GetVersion() || m_SSAO != SSAO))
    {
        m_ResetSSAO = true;
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

    m_CompositionRequired          = ScreenEffectsEnabled && ColorEffectsEnabled;
    ITextureView* pCurrentColorSRV = pSceneColorSRV;
    if (m_CompositionRequired)
    {
        if (m_pComposedColor == nullptr || m_ComposedColorVersion != Targets.GetVersion())
        {
            TextureDesc Desc;
            Desc.Name      = "Radient composed HDR color";
            Desc.Type      = RESOURCE_DIM_TEX_2D;
            Desc.Width     = Targets.GetSize().Width;
            Desc.Height    = Targets.GetSize().Height;
            Desc.Format    = GetTextureViewFormat(pSceneColorSRV);
            Desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

            RefCntAutoPtr<ITexture> pComposedColor;
            pDevice->CreateTexture(Desc, nullptr, pComposedColor.GetAddressOfEmpty());
            if (pComposedColor == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            m_pComposedColor       = std::move(pComposedColor);
            m_ComposedColorVersion = Targets.GetVersion();
        }

        ITextureView* const pComposedColorSRV = m_pComposedColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        if (pComposedColorSRV == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        ColorPassPrepareInfo PrepareInfo{Targets};
        PrepareInfo.Pipeline.OutputFormat         = GetTextureViewFormat(pComposedColorSRV);
        PrepareInfo.Pipeline.ToneMappingMode      = TONE_MAPPING_MODE_NONE;
        PrepareInfo.Pipeline.ConvertOutputToSRGB  = false;
        PrepareInfo.Pipeline.SSAOEnabled          = SSAOEnabled;
        PrepareInfo.Pipeline.SSREnabled           = SSREnabled;
        PrepareInfo.Pipeline.pFrameAttribsCB      = pFrameAttribsCB;
        PrepareInfo.Pipeline.pPreintegratedGGXSRV = pPreintegratedGGXSRV;
        PrepareInfo.pColorSRV                     = pSceneColorSRV;
        PrepareInfo.pSSAOSRV                      = pSSAOSRV;
        PrepareInfo.pSSRSRV                       = pSSRSRV;

        const RADIENT_STATUS Status = PrepareColorPass(pDevice, m_CompositionPass, PrepareInfo);
        if (RADIENT_FAILED(Status))
            return Status;

        pCurrentColorSRV = pComposedColorSRV;
    }
    else
    {
        m_CompositionPass = {};
    }

    if (DepthOfFieldEnabled)
        pCurrentColorSRV = pDepthOfFieldSRV;
    if (BloomEnabled)
        pCurrentColorSRV = pBloomSRV;

    ColorPassPrepareInfo FinalPassInfo{Targets};
    FinalPassInfo.Pipeline.OutputFormat         = OutputFormat;
    FinalPassInfo.Pipeline.ToneMappingMode      = ToneMappingAttribs.iToneMappingMode;
    FinalPassInfo.Pipeline.ConvertOutputToSRGB  = ConvertOutputToSRGB;
    FinalPassInfo.Pipeline.SSAOEnabled          = SSAOEnabled && !m_CompositionRequired;
    FinalPassInfo.Pipeline.SSREnabled           = SSREnabled && !m_CompositionRequired;
    FinalPassInfo.Pipeline.pFrameAttribsCB      = pFrameAttribsCB;
    FinalPassInfo.Pipeline.pPreintegratedGGXSRV = pPreintegratedGGXSRV;
    FinalPassInfo.pColorSRV                     = pCurrentColorSRV;
    FinalPassInfo.pSSAOSRV                      = m_CompositionRequired ? nullptr : pSSAOSRV;
    FinalPassInfo.pSSRSRV                       = m_CompositionRequired ? nullptr : pSSRSRV;

    const RADIENT_STATUS FinalPassStatus = PrepareColorPass(pDevice, m_FinalPass, FinalPassInfo);
    if (RADIENT_FAILED(FinalPassStatus))
        return FinalPassStatus;

    m_pFrameAttribsCB      = pFrameAttribsCB;
    m_pPreintegratedGGXSRV = pPreintegratedGGXSRV;
    m_Bloom                = BloomDesc;
    m_SSAO                 = SSAO;
    m_SSR                  = SSR;
    m_DepthOfField         = DepthOfFieldDesc;
    m_SSAOEnabled          = SSAOEnabled;
    m_SSREnabled           = SSREnabled;
    m_DepthOfFieldEnabled  = DepthOfFieldEnabled;
    m_BloomEnabled         = BloomEnabled;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::Execute(IRenderDevice*                   pDevice,
                                                          IDeviceContext*                  pContext,
                                                          const RadientFrameRenderTargets& Targets,
                                                          bool                             ResetTemporalHistory)
{
    if (pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_SSAOEnabled || m_SSREnabled || m_DepthOfFieldEnabled || m_BloomEnabled)
    {
        if (pDevice == nullptr || m_pPostFXContext == nullptr || m_pFrameAttribsCB == nullptr ||
            (m_SSAOEnabled && m_pSSAO == nullptr) ||
            (m_SSREnabled && m_pSSR == nullptr) ||
            (m_DepthOfFieldEnabled && m_pDepthOfField == nullptr) ||
            (m_BloomEnabled && m_pBloom == nullptr))
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
            pMotionVectorsSRV == nullptr ||
            ((m_SSAOEnabled || m_SSREnabled) && pNormalSRV == nullptr))
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

        ITextureView* pCurrentColorSRV = Targets.GetSceneColorSRV();
        if (m_CompositionRequired)
        {
            ITextureView* const  pComposedColorRTV = m_pComposedColor != nullptr ?
                 m_pComposedColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) :
                 nullptr;
            const RADIENT_STATUS Status            = ExecuteColorPass(pContext, m_CompositionPass, pComposedColorRTV);
            if (RADIENT_FAILED(Status))
                return Status;

            pCurrentColorSRV = m_pComposedColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }

        if (m_DepthOfFieldEnabled)
        {
            if (pCurrentColorSRV == nullptr)
                return RADIENT_STATUS_OUT_OF_DATE;

            HLSL::DepthOfFieldAttribs DepthOfFieldAttribs =
                RadientPostFX::MakeDepthOfFieldAttribs(m_DepthOfField);

            DepthOfField::RenderAttributes DepthOfFieldRenderAttribs;
            DepthOfFieldRenderAttribs.pDevice         = pDevice;
            DepthOfFieldRenderAttribs.pDeviceContext  = pContext;
            DepthOfFieldRenderAttribs.pPostFXContext  = m_pPostFXContext.get();
            DepthOfFieldRenderAttribs.pColorBufferSRV = pCurrentColorSRV;
            DepthOfFieldRenderAttribs.pDepthBufferSRV = pDepthSRV;
            DepthOfFieldRenderAttribs.pDOFAttribs     = &DepthOfFieldAttribs;
            m_pDepthOfField->Execute(DepthOfFieldRenderAttribs);

            pCurrentColorSRV = m_pDepthOfField->GetDepthOfFieldTextureSRV();
            if (pCurrentColorSRV == nullptr)
                return RADIENT_STATUS_OUT_OF_DATE;
        }

        if (m_BloomEnabled)
        {
            if (pCurrentColorSRV == nullptr)
                return RADIENT_STATUS_OUT_OF_DATE;

            HLSL::BloomAttribs BloomAttribs = RadientPostFX::MakeBloomAttribs(m_Bloom);

            Bloom::RenderAttributes BloomRenderAttribs;
            BloomRenderAttribs.pDevice         = pDevice;
            BloomRenderAttribs.pDeviceContext  = pContext;
            BloomRenderAttribs.pPostFXContext  = m_pPostFXContext.get();
            BloomRenderAttribs.pColorBufferSRV = pCurrentColorSRV;
            BloomRenderAttribs.pBloomAttribs   = &BloomAttribs;
            m_pBloom->Execute(BloomRenderAttribs);

            pCurrentColorSRV = m_pBloom->GetBloomTextureSRV();
            if (pCurrentColorSRV == nullptr)
                return RADIENT_STATUS_OUT_OF_DATE;
        }

        if (pCurrentColorSRV != m_FinalPass.pBoundColorSRV)
            return RADIENT_STATUS_OUT_OF_DATE;
    }

    return ExecuteColorPass(pContext, m_FinalPass, Targets.GetOutputColorRTV());
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::PrepareColorPass(IRenderDevice*              pDevice,
                                                                   ColorPass&                  Pass,
                                                                   const ColorPassPrepareInfo& PrepareInfo)
{
    const ColorPassPipelineDesc&     Pipeline = PrepareInfo.Pipeline;
    const RadientFrameRenderTargets& Targets  = PrepareInfo.Targets;

    if (PrepareInfo.pColorSRV == nullptr || Pipeline.OutputFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (Pass.pPSO == nullptr || Pass.Pipeline != Pipeline)
    {
        const RADIENT_STATUS Status = CreatePipelineState(pDevice, Pass, Pipeline);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (Pass.pSRB == nullptr ||
        Pass.TargetVersion != Targets.GetVersion() ||
        Pass.pBoundColorSRV != PrepareInfo.pColorSRV ||
        Pass.pBoundSSRSRV != PrepareInfo.pSSRSRV ||
        Pass.pBoundSSAOSRV != PrepareInfo.pSSAOSRV)
    {
        Pass.pSRB.Release();
        Pass.pPSO->CreateShaderResourceBinding(Pass.pSRB.GetAddressOfEmpty(), true);
        if (Pass.pSRB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        IShaderResourceVariable* const pColorVar =
            Pass.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer");
        if (pColorVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pColorVar->Set(PrepareInfo.pColorSRV);

        if (Pipeline.SSAOEnabled)
        {
            IShaderResourceVariable* const pSSAOVar =
                Pass.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SSAO");
            if (pSSAOVar == nullptr || PrepareInfo.pSSAOSRV == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
            pSSAOVar->Set(PrepareInfo.pSSAOSRV);
        }

        if (Pipeline.SSREnabled)
        {
            struct TextureBinding
            {
                const Char*   Name;
                ITextureView* pView;
            };
            const TextureBinding TextureBindings[] = {
                {"g_SSR", PrepareInfo.pSSRSRV},
                {"g_SpecularIBL", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_IBL)},
                {"g_Normal", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_NORMAL)},
                {"g_MaterialData", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_MATERIAL)},
                {"g_BaseColor", Targets.GetGBufferSRV(RadientFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR)},
            };
            for (const TextureBinding& Binding : TextureBindings)
            {
                IShaderResourceVariable* const pVariable =
                    Pass.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, Binding.Name);
                if (pVariable == nullptr || Binding.pView == nullptr)
                    return RADIENT_STATUS_INVALID_OPERATION;
                pVariable->Set(Binding.pView);
            }
        }

        Pass.TargetVersion  = Targets.GetVersion();
        Pass.pBoundColorSRV = PrepareInfo.pColorSRV;
        Pass.pBoundSSRSRV   = PrepareInfo.pSSRSRV;
        Pass.pBoundSSAOSRV  = PrepareInfo.pSSAOSRV;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::CreatePipelineState(IRenderDevice*               pDevice,
                                                                      ColorPass&                   Pass,
                                                                      const ColorPassPipelineDesc& Desc)
{
    Pass = {};

    ShaderMacroHelper Macros;
    Macros.Add("TONE_MAPPING_MODE", Desc.ToneMappingMode);
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", Desc.ConvertOutputToSRGB);
    Macros.Add("ENABLE_SSAO", Desc.SSAOEnabled);
    Macros.Add("ENABLE_SSR", Desc.SSREnabled);

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

    const bool                              ToneMappingEnabled   = Desc.ToneMappingMode > TONE_MAPPING_MODE_NONE;
    const bool                              FrameAttribsRequired = ToneMappingEnabled || Desc.SSREnabled;
    std::vector<ShaderResourceVariableDesc> Variables;
    Variables.push_back({SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
    if (Desc.SSAOEnabled)
        Variables.push_back({SHADER_TYPE_PIXEL, "g_SSAO", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
    if (Desc.SSREnabled)
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
    if (Desc.SSREnabled)
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
    PSOCI.PSODesc.ResourceLayout.ImmutableSamplers           = Desc.SSREnabled ? &PreintegratedGGXSampler : nullptr;
    PSOCI.PSODesc.ResourceLayout.NumImmutableSamplers        = Desc.SSREnabled ? 1u : 0u;
    PSOCI.GraphicsPipeline.NumRenderTargets                  = 1;
    PSOCI.GraphicsPipeline.RTVFormats[0]                     = Desc.OutputFormat;
    PSOCI.GraphicsPipeline.PrimitiveTopology                 = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCI.GraphicsPipeline.RasterizerDesc.CullMode           = CULL_MODE_NONE;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthEnable      = False;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
    PSOCI.pVS                                                = pVS;
    PSOCI.pPS                                                = pPS;
    pDevice->CreateGraphicsPipelineState(PSOCI, Pass.pPSO.GetAddressOfEmpty());
    if (Pass.pPSO == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (FrameAttribsRequired)
    {
        IShaderResourceVariable* const pFrameAttribsVar =
            Pass.pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbFrameAttribs");
        if (pFrameAttribsVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pFrameAttribsVar->Set(Desc.pFrameAttribsCB);
    }

    if (ToneMappingEnabled)
    {
        IShaderResourceVariable* const pPostProcessAttribsVar =
            Pass.pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbRadientPostProcessAttribs");
        if (pPostProcessAttribsVar == nullptr || m_pToneMappingAttribsCB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pPostProcessAttribsVar->Set(m_pToneMappingAttribsCB);
    }

    if (Desc.SSREnabled)
    {
        IShaderResourceVariable* const pPreintegratedGGXVar =
            Pass.pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX");
        if (pPreintegratedGGXVar == nullptr || Desc.pPreintegratedGGXSRV == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pPreintegratedGGXVar->Set(Desc.pPreintegratedGGXSRV);
    }

    Pass.Pipeline = Desc;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::ExecuteColorPass(IDeviceContext*  pContext,
                                                                   const ColorPass& Pass,
                                                                   ITextureView*    pOutputRTV)
{
    if (pContext == nullptr)
        return RADIENT_STATUS_OK;
    if (pOutputRTV == nullptr || Pass.pPSO == nullptr || Pass.pSRB == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    pContext->SetRenderTargets(1, &pOutputRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->SetPipelineState(Pass.pPSO);
    pContext->CommitShaderResources(Pass.pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
