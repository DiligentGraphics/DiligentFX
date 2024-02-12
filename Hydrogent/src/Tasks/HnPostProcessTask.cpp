/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "Tasks/HnPostProcessTask.hpp"

#include "HnTokens.hpp"
#include "HnShaderSourceFactory.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnRenderParam.hpp"

#include "DebugUtilities.hpp"
#include "TextureView.h"
#include "RenderStateCache.hpp"
#include "ShaderMacroHelper.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "VectorFieldRenderer.hpp"
#include "ScreenSpaceReflection.hpp"
#include "TemporalAntiAliasing.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "../shaders/HnPostProcessStructures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"

} // namespace HLSL

namespace USD
{

HnPostProcessTask::HnPostProcessTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnPostProcessTask::~HnPostProcessTask()
{
}

void HnPostProcessTask::PostProcessingTechnique::PreparePSO(const HnPostProcessTask& PPTask, HnRenderDelegate* RenderDelegate, TEXTURE_FORMAT RTVFormat)
{
    if (PSO && PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat)
        PSOIsDirty = true;

    if (PSOIsDirty)
        PSO.Release();

    if (PSO)
        return;

    try
    {
        USD_Renderer& USDRender = *RenderDelegate->GetUSDRenderer();

        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        auto pHnFxCompoundSourceFactory     = HnShaderSourceFactory::CreateHnFxCompoundFactory();
        ShaderCI.pShaderSourceStreamFactory = pHnFxCompoundSourceFactory;

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", PPTask.m_Params.ConvertOutputToSRGB);
        Macros.Add("TONE_MAPPING_MODE", PPTask.m_Params.ToneMappingMode);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Full-screen Triangle VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "FullScreenTriangleVS";
            ShaderCI.FilePath   = "FullScreenTriangleVS.fx";

            pVS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Post process PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnPostProcess.psh";

            pPS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        }

        PipelineResourceLayoutDescX ResourceLauout;
        ResourceLauout
            .SetDefaultVariableType(SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
            .AddVariable(SHADER_TYPE_PIXEL, "cbPostProcessAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_PreintegratedGGX", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_PreintegratedGGX", Sam_LinearClamp)
            .AddVariable(SHADER_TYPE_PIXEL, "cbFrameAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        GraphicsPipelineStateCreateInfoX PsoCI{"Post process"};
        PsoCI
            .AddRenderTarget(RTVFormat)
            .AddShader(pVS)
            .AddShader(pPS)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetResourceLayout(ResourceLauout)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error
        ShaderResourceVariableX{PSO, SHADER_TYPE_PIXEL, "cbPostProcessAttribs"}.Set(PPTask.m_PostProcessAttribsCB);
        ShaderResourceVariableX{PSO, SHADER_TYPE_PIXEL, "g_PreintegratedGGX"}.Set(USDRender.GetPreintegratedGGX_SRV());
        ShaderResourceVariableX{PSO, SHADER_TYPE_PIXEL, "cbFrameAttribs"}.Set(RenderDelegate->GetFrameAttribsCB());

        PSOIsDirty = false;
    }
    catch (const std::runtime_error& err)
    {
        LOG_ERROR_MESSAGE("Failed to create post process PSO: ", err.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create post process PSO");
    }
}

void HnPostProcessTask::PostProcessingTechnique::CreateSRB()
{
    SRB.Release();
    PSO->CreateShaderResourceBinding(&SRB, true);
    VERIFY_EXPR(SRB);
    ShaderVars.Color                   = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_ColorBuffer"};
    ShaderVars.Depth                   = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_Depth"};
    ShaderVars.SelectionDepth          = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_SelectionDepth"};
    ShaderVars.ClosestSelectedLocation = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_ClosestSelectedLocation"};
    ShaderVars.SSR                     = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_SSR"};
    ShaderVars.SpecularIBL             = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_SpecularIBL"};
    ShaderVars.Normal                  = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_Normal"};
    ShaderVars.Material                = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_MaterialData"};
    ShaderVars.BaseColor               = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_BaseColor"};
}

void HnPostProcessTask::CreateVectorFieldRenderer(TEXTURE_FORMAT RTVFormat)
{
    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

    VectorFieldRenderer::CreateInfo CI;
    CI.pDevice          = RenderDelegate->GetDevice();
    CI.pStateCache      = RenderDelegate->GetRenderStateCache();
    CI.NumRenderTargets = 1;
    CI.RTVFormats[0]    = RTVFormat;

    m_VectorFieldRenderer = std::make_unique<VectorFieldRenderer>(CI);
}

void HnPostProcessTask::Sync(pxr::HdSceneDelegate* Delegate,
                             pxr::HdTaskContext*   TaskCtx,
                             pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnPostProcessTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
            if (m_Params.ConvertOutputToSRGB != Params.ConvertOutputToSRGB ||
                m_Params.ToneMappingMode != Params.ToneMappingMode ||
                m_Params.EnableTAA != Params.EnableTAA)
            {
                // In OpenGL we can't release PSO in Worker thread
                m_PostProcessTech.PSOIsDirty = true;
            }

            m_Params         = Params;
            m_AttribsCBDirty = true;
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnPostProcessTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    // Final color Bprim is initialized by the HnSetupRenderingTask.
    m_FinalColorRTV = GetRenderBufferTarget(*RenderIndex, TaskCtx, HnRenderResourceTokens->finalColorTarget);
    if (m_FinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is not set in the task context");
        return;
    }

    ITextureView* ClosestSelectedLocationSRV = nullptr;
    if (auto* ClosestSelectedLocationRTV = GetRenderBufferTarget(*RenderIndex, TaskCtx, HnRenderResourceTokens->closestSelectedLocationFinalTarget))
    {
        ClosestSelectedLocationSRV = ClosestSelectedLocationRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        if (ClosestSelectedLocationSRV == nullptr)
        {
            UNEXPECTED("Closest selected location SRV is null");
            return;
        }
    }
    else
    {
        UNEXPECTED("Closest selected location target RTV is not set in the task context");
    }

    std::shared_ptr<HnRenderPassState> RPState = GetRenderPassState(TaskCtx);
    if (RPState == nullptr)
    {
        UNEXPECTED("Render pass state is not set in the task context");
        return;
    }

    if (m_ClearDepth != RPState->GetClearDepth())
    {
        m_ClearDepth     = RPState->GetClearDepth();
        m_AttribsCBDirty = true;
    }

    const HnFramebufferTargets& FBTargets = RPState->GetFramebufferTargets();
    if (!FBTargets)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }
    m_FBTargets = &FBTargets;

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*    pDevice        = RenderDelegate->GetDevice();
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();
    if (!m_PostProcessAttribsCB)
    {
        CreateUniformBuffer(pDevice, sizeof(HLSL::PostProcessAttribs), "Post process attribs CB", &m_PostProcessAttribsCB, USAGE_DEFAULT);
        VERIFY(m_PostProcessAttribsCB, "Failed to create post process attribs CB");
    }

    if (!m_PostFXContext)
    {
        m_PostFXContext = std::make_unique<PostFXContext>(pDevice);
    }

    if (!m_SSR)
    {
        m_SSR = std::make_unique<ScreenSpaceReflection>(pDevice);
    }

    if (!m_TAA)
    {
        m_TAA = std::make_unique<TemporalAntiAliasing>(pDevice);
    }

    const HnRenderParam* pRenderParam   = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    const TextureDesc&   FinalColorDesc = m_FinalColorRTV->GetTexture()->GetDesc();

    const float SSRScale =
        (pRenderParam->GetDebugView() == PBR_Renderer::DebugViewType::None && pRenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID) ?
        m_Params.SSRScale :
        0;
    if (SSRScale != m_SSRScale)
    {
        m_SSRScale       = SSRScale;
        m_AttribsCBDirty = true;
    }

    m_PostFXContext->PrepareResources({pRenderParam->GetFrameNumber(), FinalColorDesc.Width, FinalColorDesc.Height});
    m_SSR->PrepareResources(pDevice, m_PostFXContext.get());
    m_TAA->PrepareResources(pDevice, m_PostFXContext.get(), {m_FinalColorRTV->GetDesc().Format});

    m_UseTAA = m_Params.EnableTAA &&
        pRenderParam->GetDebugView() == PBR_Renderer::DebugViewType::None &&
        pRenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID;

    m_PostProcessTech.PreparePSO(*this, RenderDelegate, (m_UseTAA ? m_FBTargets->JitteredFinalColorRTV : m_FinalColorRTV)->GetDesc().Format);

    PrepareSRB(ClosestSelectedLocationSRV);

    if (!m_VectorFieldRenderer)
    {
        CreateVectorFieldRenderer(m_FinalColorRTV->GetDesc().Format);
    }

    // Check if one of the previous tasks forced TAA reset:
    // - HnBeginFrameTask::Execute() forces TAA reset if the camera has been moved.
    // - HnPostProcessTask::Execute() forces TAA reset if the selected prim has changed.
    // - m_ResetTAA can be set manually by the application to force TAA reset.
    if (!m_ResetTAA)
    {
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->taaReset, m_ResetTAA);
    }

    // The jitter will be used by HnBeginFrameTask::Execute() to set projection matrix.
    (*TaskCtx)[HnRenderResourceTokens->taaJitterOffsets] = pxr::VtValue{m_UseTAA && !m_ResetTAA ? m_TAA->GetJitterOffset() : float2{0, 0}};
}

void HnPostProcessTask::PrepareSRB(ITextureView* pClosestSelectedLocationSRV)
{
    if (!m_PostProcessTech.PSO)
    {
        UNEXPECTED("PSO is null");
        return;
    }

    for (Uint32 i = 0; i < HnFramebufferTargets::GBUFFER_TARGET_COUNT; ++i)
    {
        if (m_FBTargets->GBufferSRVs[i] == nullptr)
        {
            UNEXPECTED("G-buffer SRV ", i, " is null");
            return;
        }
    }

    ITextureView* pDepthSRV = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pDepthSRV == nullptr)
    {
        UNEXPECTED("Depth SRV is null");
        return;
    }
    ITextureView* pSelectionDepthSRV = m_FBTargets->SelectionDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionDepthSRV == nullptr)
    {
        UNEXPECTED("Selection depth SRV is null");
        return;
    }

    ITextureView* pSSR = m_SSR->GetSSRRadianceSRV();
    VERIFY_EXPR(pSSR != nullptr);

    ITextureView* pOffscreenColorSRV = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_SCENE_COLOR];
    ITextureView* pSpecularIblSRV    = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_IBL];
    ITextureView* pNormalSRV         = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NORMAL];
    ITextureView* pMaterialSRV       = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MATERIAL];
    ITextureView* pBaseColorSRV      = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_BASE_COLOR];
    if (m_PostProcessTech.SRB && m_PostProcessTech.ShaderVars)
    {
        if ((m_PostProcessTech.ShaderVars.Color && m_PostProcessTech.ShaderVars.Color.Get() != pOffscreenColorSRV) ||
            (m_PostProcessTech.ShaderVars.Depth && m_PostProcessTech.ShaderVars.Depth.Get() != pDepthSRV) ||
            (m_PostProcessTech.ShaderVars.SelectionDepth && m_PostProcessTech.ShaderVars.SelectionDepth.Get() != pSelectionDepthSRV) ||
            (m_PostProcessTech.ShaderVars.ClosestSelectedLocation && m_PostProcessTech.ShaderVars.ClosestSelectedLocation.Get() != pClosestSelectedLocationSRV) ||
            (m_PostProcessTech.ShaderVars.SSR && m_PostProcessTech.ShaderVars.SSR.Get() != pSSR) ||
            (m_PostProcessTech.ShaderVars.SpecularIBL && m_PostProcessTech.ShaderVars.SpecularIBL.Get() != pSpecularIblSRV) ||
            (m_PostProcessTech.ShaderVars.Normal && m_PostProcessTech.ShaderVars.Normal.Get() != pNormalSRV) ||
            (m_PostProcessTech.ShaderVars.Material && m_PostProcessTech.ShaderVars.Material.Get() != pMaterialSRV) ||
            (m_PostProcessTech.ShaderVars.BaseColor && m_PostProcessTech.ShaderVars.BaseColor.Get() != pBaseColorSRV))
        {
            m_PostProcessTech.SRB.Release();
            m_PostProcessTech.ShaderVars = {};
        }
    }

    if (!m_PostProcessTech.SRB)
    {
        m_PostProcessTech.CreateSRB();

        VERIFY_EXPR(m_PostProcessTech.ShaderVars);

        m_PostProcessTech.ShaderVars.Color.Set(pOffscreenColorSRV);
        m_PostProcessTech.ShaderVars.Depth.Set(pDepthSRV);
        m_PostProcessTech.ShaderVars.SelectionDepth.Set(pSelectionDepthSRV);
        m_PostProcessTech.ShaderVars.ClosestSelectedLocation.Set(pClosestSelectedLocationSRV);
        m_PostProcessTech.ShaderVars.SSR.Set(pSSR);
        m_PostProcessTech.ShaderVars.SpecularIBL.Set(pSpecularIblSRV);
        m_PostProcessTech.ShaderVars.Normal.Set(pNormalSRV);
        m_PostProcessTech.ShaderVars.Material.Set(pMaterialSRV);
        m_PostProcessTech.ShaderVars.BaseColor.Set(pBaseColorSRV);
    }
}

void HnPostProcessTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (!m_PostProcessTech.PSO || !m_PostProcessTech.SRB)
    {
        UNEXPECTED("Render technique is not initialized");
        return;
    }

    if (m_FinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (m_FBTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate*    RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*       pDevice        = RenderDelegate->GetDevice();
    IDeviceContext*      pCtx           = RenderDelegate->GetDeviceContext();
    const HnRenderParam* pRenderParam   = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    VERIFY_EXPR(pRenderParam != nullptr);

    {
        std::array<StateTransitionDesc, HnFramebufferTargets::GBUFFER_TARGET_COUNT> Barriers{};
        for (Uint32 i = 0; i < HnFramebufferTargets::GBUFFER_TARGET_COUNT; ++i)
        {
            Barriers[i] = {m_FBTargets->GBufferSRVs[i]->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
        }
        pCtx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->TransitionResourceStates(static_cast<Uint32>(Barriers.size()), Barriers.data());
    }

    const TextureDesc& FinalColorDesc = m_FinalColorRTV->GetTexture()->GetDesc();

    if (m_SSRScale > 0)
    {
        PostFXContext::RenderAttributes PostFXAttribs{};
        PostFXAttribs.pDevice        = pDevice;
        PostFXAttribs.pDeviceContext = pCtx;

        pxr::VtValue PBRFrameAttribsVal = (*TaskCtx)[HnRenderResourceTokens->frameShaderAttribs];
        if (PBRFrameAttribsVal.IsHolding<HLSL::PBRFrameAttribs*>())
        {
            const HLSL::PBRFrameAttribs* pPBRFrameAttibs = PBRFrameAttribsVal.UncheckedGet<HLSL::PBRFrameAttribs*>();

            PostFXAttribs.pCurrCamera = &pPBRFrameAttibs->Camera;
            PostFXAttribs.pPrevCamera = &pPBRFrameAttibs->PrevCamera;
        }
        else
        {
            UNEXPECTED("PBR frame attribs are not set in the task context");
        }
        m_PostFXContext->Execute(PostFXAttribs);

        HLSL::ScreenSpaceReflectionAttribs SSRAttribs{};
        SSRAttribs.RoughnessChannel      = 0;
        SSRAttribs.DepthBufferThickness  = 0.015f * 10.0f;
        SSRAttribs.IsRoughnessPerceptual = true;

        ScreenSpaceReflection::RenderAttributes SSRRenderAttribs{};
        SSRRenderAttribs.pDevice            = pDevice;
        SSRRenderAttribs.pDeviceContext     = pCtx;
        SSRRenderAttribs.pPostFXContext     = m_PostFXContext.get();
        SSRRenderAttribs.pColorBufferSRV    = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_SCENE_COLOR];
        SSRRenderAttribs.pDepthBufferSRV    = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSRRenderAttribs.pNormalBufferSRV   = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NORMAL];
        SSRRenderAttribs.pMaterialBufferSRV = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MATERIAL];
        SSRRenderAttribs.pMotionVectorsSRV  = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NOTION_VECTOR];
        SSRRenderAttribs.pSSRAttribs        = &SSRAttribs;
        m_SSR->Execute(SSRRenderAttribs);
    }

    {
        ScopedDebugGroup DebugGroup{pCtx, "Post Processing"};

        ITextureView* pRTVs[] = {m_UseTAA ? m_FBTargets->JitteredFinalColorRTV : m_FinalColorRTV};
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (m_AttribsCBDirty)
        {
            HLSL::PostProcessAttribs ShaderAttribs;
            ShaderAttribs.SelectionOutlineColor            = m_Params.SelectionColor;
            ShaderAttribs.NonselectionDesaturationFactor   = m_Params.NonselectionDesaturationFactor;
            ShaderAttribs.ToneMapping.iToneMappingMode     = m_Params.ToneMappingMode;
            ShaderAttribs.ToneMapping.bAutoExposure        = 0;
            ShaderAttribs.ToneMapping.fMiddleGray          = m_Params.MiddleGray;
            ShaderAttribs.ToneMapping.bLightAdaptation     = 0;
            ShaderAttribs.ToneMapping.fWhitePoint          = m_Params.WhitePoint;
            ShaderAttribs.ToneMapping.fLuminanceSaturation = m_Params.LuminanceSaturation;
            ShaderAttribs.AverageLogLum                    = m_Params.AverageLogLum;
            ShaderAttribs.ClearDepth                       = m_ClearDepth;
            ShaderAttribs.SelectionOutlineWidth            = m_Params.SelectionOutlineWidth;
            ShaderAttribs.SSRScale                         = m_SSRScale;
            pCtx->UpdateBuffer(m_PostProcessAttribsCB, 0, sizeof(HLSL::PostProcessAttribs), &ShaderAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_AttribsCBDirty = false;
        }
        pCtx->SetPipelineState(m_PostProcessTech.PSO);
        pCtx->CommitShaderResources(m_PostProcessTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
    }

    if (m_UseTAA)
    {
        HLSL::TemporalAntiAliasingAttribs TAASettings{};

        if (m_ResetTAA)
        {
            TAASettings.ResetAccumulation = m_ResetTAA;
            m_ResetTAA                    = false;
        }

        TemporalAntiAliasing::RenderAttributes TAARenderAttribs{};
        TAARenderAttribs.pDevice           = pDevice;
        TAARenderAttribs.pDeviceContext    = pCtx;
        TAARenderAttribs.pPostFXContext    = m_PostFXContext.get();
        TAARenderAttribs.pColorBufferSRV   = m_FBTargets->JitteredFinalColorRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pDepthBufferSRV   = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pMotionVectorsSRV = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NOTION_VECTOR];
        TAARenderAttribs.FeatureFlag       = TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER;
        TAARenderAttribs.pTAAAttribs       = &TAASettings;
        m_TAA->Execute(TAARenderAttribs);

        ScopedDebugGroup DebugGroup{pCtx, "Copy accumulated buffer"};
        pCtx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        CopyTextureAttribs CopyAttribs{
            m_TAA->GetAccumulatedFrameSRV()->GetTexture(),
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
            m_FinalColorRTV->GetTexture(),
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        };
        pCtx->CopyTexture(CopyAttribs);
    }

    if (m_VectorFieldRenderer && pRenderParam->GetDebugView() == PBR_Renderer::DebugViewType::MotionVectors)
    {
        ScopedDebugGroup DebugGroup{pCtx, "Motion Vector Field"};

        VectorFieldRenderer::RenderAttribs Attribs;
        Attribs.pContext = pCtx;
        Attribs.GridSize = {FinalColorDesc.Width / 20, FinalColorDesc.Height / 20};
        // Render motion vectors in the opposite direction
        Attribs.Scale               = float2{-0.02f} / std::max(static_cast<float>(pRenderParam->GetElapsedTime()), 0.001f);
        Attribs.StartColor          = float4{1};
        Attribs.EndColor            = float4{0.5, 0.5, 0.5, 1.0};
        Attribs.ConvertOutputToSRGB = m_Params.ConvertOutputToSRGB;

        Attribs.pVectorField = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NOTION_VECTOR];
        m_VectorFieldRenderer->Render(Attribs);
    }
}

} // namespace USD

} // namespace Diligent
