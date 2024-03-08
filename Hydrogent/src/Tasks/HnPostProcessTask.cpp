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
#include "ScreenSpaceAmbientOcclusion.hpp"
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
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"

} // namespace HLSL

namespace USD
{

HnPostProcessTask::HnPostProcessTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id},
    m_PostProcessTech{*this},
    m_CopyFrameTech{*this}
{
}

HnPostProcessTask::~HnPostProcessTask()
{
}


void HnPostProcessTask::PostProcessingTechnique::PreparePRS()
{
    if (PRS)
    {
        return;
    }

    HnRenderDelegate*  RenderDelegate = static_cast<HnRenderDelegate*>(PPTask.m_RenderIndex->GetRenderDelegate());
    IRenderDevice*     pDevice        = RenderDelegate->GetDevice();
    IRenderStateCache* pStateCache    = RenderDelegate->GetRenderStateCache();

    PipelineResourceSignatureDescX PRSDesc{"HnPostProcessTask: Post-processing PRS"};
    PRSDesc
        .SetUseCombinedTextureSamplers(true)
        .AddResource(SHADER_TYPE_PIXEL, "cbPostProcessAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddResource(SHADER_TYPE_PIXEL, "cbFrameAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddResource(SHADER_TYPE_PIXEL, "g_PreintegratedGGX", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_PreintegratedGGX", Sam_LinearClamp);

    PRSDesc
        .AddResource(SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_Depth", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_SelectionDepth", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_ClosestSelectedLocation", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_SSR", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_SSAO", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_SpecularIBL", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_Normal", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_MaterialData", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_BaseColor", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    PRS = RenderDeviceWithCache_N{pDevice, pStateCache}.CreatePipelineResourceSignature(PRSDesc);
    VERIFY_EXPR(PRS);

    USD_Renderer& USDRender = *RenderDelegate->GetUSDRenderer();

    VERIFY_EXPR(PPTask.m_PostProcessAttribsCB);
    ShaderResourceVariableX{PRS, SHADER_TYPE_PIXEL, "cbPostProcessAttribs"}.Set(PPTask.m_PostProcessAttribsCB);

    VERIFY_EXPR(USDRender.GetPreintegratedGGX_SRV());
    ShaderResourceVariableX{PRS, SHADER_TYPE_PIXEL, "g_PreintegratedGGX"}.Set(USDRender.GetPreintegratedGGX_SRV());

    VERIFY_EXPR(RenderDelegate->GetFrameAttribsCB());
    ShaderResourceVariableX{PRS, SHADER_TYPE_PIXEL, "cbFrameAttribs"}.Set(RenderDelegate->GetFrameAttribsCB());
}

static GraphicsPipelineStateCreateInfoX& CreateShaders(RenderDeviceWithCache_E&          Device,
                                                       const char*                       PSFilePath,
                                                       const char*                       PSName,
                                                       const ShaderMacroArray&           Macros,
                                                       GraphicsPipelineStateCreateInfoX& PsoCI)
{
    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Macros         = Macros;

    auto pHnFxCompoundSourceFactory     = HnShaderSourceFactory::CreateHnFxCompoundFactory();
    ShaderCI.pShaderSourceStreamFactory = pHnFxCompoundSourceFactory;

    {
        ShaderCI.Desc       = {"Full-screen Triangle VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "FullScreenTriangleVS";
        ShaderCI.FilePath   = "FullScreenTriangleVS.fx";

        auto pVS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        PsoCI.AddShader(pVS);
    }

    {
        ShaderCI.Desc       = {PSName, SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = PSFilePath;

        auto pPS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        PsoCI.AddShader(pPS);
    }

    return PsoCI;
}

void HnPostProcessTask::PostProcessingTechnique::PreparePSO(TEXTURE_FORMAT RTVFormat)
{
    bool IsDirty = PSO && PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat;

    if (ConvertOutputToSRGB != (!PPTask.m_UseTAA && PPTask.m_Params.ConvertOutputToSRGB))
    {
        ConvertOutputToSRGB = !PPTask.m_UseTAA && PPTask.m_Params.ConvertOutputToSRGB;
        IsDirty             = true;
    }

    if (ToneMappingMode != PPTask.m_Params.ToneMappingMode)
    {
        ToneMappingMode = PPTask.m_Params.ToneMappingMode;
        IsDirty         = true;
    }

    if (IsDirty)
        PSO.Release();

    if (PSO)
        return;

    try
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(PPTask.m_RenderIndex->GetRenderDelegate());

        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", ConvertOutputToSRGB);
        Macros.Add("TONE_MAPPING_MODE", ToneMappingMode);

        GraphicsPipelineStateCreateInfoX PsoCI{"Post process"};
        CreateShaders(Device, "HnPostProcess.psh", "Post-process PS", Macros, PsoCI)
            .AddRenderTarget(RTVFormat)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .AddSignature(PRS)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error

        IsDirty = false;
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

void HnPostProcessTask::PostProcessingTechnique::PrepareSRB(ITextureView* pClosestSelectedLocationSRV)
{
    const auto& FBTargets = PPTask.m_FBTargets;

    for (Uint32 i = 0; i < HnFramebufferTargets::GBUFFER_TARGET_COUNT; ++i)
    {
        if (FBTargets->GBufferSRVs[i] == nullptr)
        {
            UNEXPECTED("G-buffer SRV ", i, " is null");
            return;
        }
    }

    ITextureView* pDepthSRV = FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pDepthSRV == nullptr)
    {
        UNEXPECTED("Depth SRV is null");
        return;
    }

    ITextureView* pSelectionDepthSRV = FBTargets->SelectionDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionDepthSRV == nullptr)
    {
        UNEXPECTED("Selection depth SRV is null");
        return;
    }

    ITextureView* pSSR = PPTask.m_SSR->GetSSRRadianceSRV();
    VERIFY_EXPR(pSSR != nullptr);

    ITextureView* pSSAO = PPTask.m_SSAO->GetAmbientOcclusionSRV();
    VERIFY_EXPR(pSSAO != nullptr);

    const HnRenderParam* pRenderParam = static_cast<const HnRenderParam*>(PPTask.m_RenderIndex->GetRenderDelegate()->GetRenderParam());
    VERIFY_EXPR(pRenderParam != nullptr);
    size_t ResIdx     = pRenderParam != nullptr ? pRenderParam->GetFrameNumber() % Resources.size() : 0;
    auto&  SRB        = Resources[ResIdx].SRB;
    auto&  ShaderVars = Resources[ResIdx].Vars;

    ITextureView* pOffscreenColorSRV = FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_SCENE_COLOR];
    ITextureView* pSpecularIblSRV    = FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_IBL];
    ITextureView* pNormalSRV         = FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NORMAL];
    ITextureView* pMaterialSRV       = FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MATERIAL];
    ITextureView* pBaseColorSRV      = FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_BASE_COLOR];
    if (SRB)
    {
        auto VarValueChanged = [](const ShaderResourceVariableX& Var, IDeviceObject* pValue) {
            return Var && Var.Get() != pValue;
        };

        if (VarValueChanged(ShaderVars.Color, pOffscreenColorSRV) ||
            VarValueChanged(ShaderVars.Depth, pDepthSRV) ||
            VarValueChanged(ShaderVars.SelectionDepth, pSelectionDepthSRV) ||
            VarValueChanged(ShaderVars.ClosestSelectedLocation, pClosestSelectedLocationSRV) ||
            VarValueChanged(ShaderVars.SSR, pSSR) ||
            VarValueChanged(ShaderVars.SSAO, pSSAO) ||
            VarValueChanged(ShaderVars.SpecularIBL, pSpecularIblSRV) ||
            VarValueChanged(ShaderVars.Normal, pNormalSRV) ||
            VarValueChanged(ShaderVars.Material, pMaterialSRV) ||
            VarValueChanged(ShaderVars.BaseColor, pBaseColorSRV))
        {
            Resources = {};
            CurrSRB   = nullptr;
        }
    }

    if (!SRB)
    {
        PRS->CreateShaderResourceBinding(&SRB, true);
        VERIFY_EXPR(SRB);

        auto SetVar = [&SRB](ShaderResourceVariableX& Var, const char* Name, IDeviceObject* pValue) {
            Var = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, Name};
            Var.Set(pValue);
        };

        // clang-format off
        SetVar(ShaderVars.Color,                   "g_ColorBuffer",             pOffscreenColorSRV);
        SetVar(ShaderVars.Depth,                   "g_Depth",                   pDepthSRV);
        SetVar(ShaderVars.SelectionDepth,          "g_SelectionDepth",          pSelectionDepthSRV);
        SetVar(ShaderVars.ClosestSelectedLocation, "g_ClosestSelectedLocation", pClosestSelectedLocationSRV);
        SetVar(ShaderVars.SSR,                     "g_SSR",                     pSSR);
        SetVar(ShaderVars.SSAO,                    "g_SSAO",                    pSSAO);
        SetVar(ShaderVars.SpecularIBL,             "g_SpecularIBL",             pSpecularIblSRV);
        SetVar(ShaderVars.Normal,                  "g_Normal",                  pNormalSRV);
        SetVar(ShaderVars.Material,                "g_MaterialData",            pMaterialSRV);
        SetVar(ShaderVars.BaseColor,               "g_BaseColor",               pBaseColorSRV);
        // clang-format on
    }

    CurrSRB = SRB;
}



void HnPostProcessTask::CopyFrameTechnique::PreparePRS()
{
    if (PRS)
    {
        return;
    }

    HnRenderDelegate*  RenderDelegate = static_cast<HnRenderDelegate*>(PPTask.m_RenderIndex->GetRenderDelegate());
    IRenderDevice*     pDevice        = RenderDelegate->GetDevice();
    IRenderStateCache* pStateCache    = RenderDelegate->GetRenderStateCache();

    PipelineResourceSignatureDescX PRSDesc{"HnPostProcessTask: Tone mapping PRS"};
    PRSDesc
        .SetUseCombinedTextureSamplers(true)
        .AddResource(SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    PRS = RenderDeviceWithCache_N{pDevice, pStateCache}.CreatePipelineResourceSignature(PRSDesc);
    VERIFY_EXPR(PRS);
}

void HnPostProcessTask::CopyFrameTechnique::PreparePSO(TEXTURE_FORMAT RTVFormat)
{
    bool IsDirty = PSO && PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat;

    if (ConvertOutputToSRGB != PPTask.m_Params.ConvertOutputToSRGB)
    {
        ConvertOutputToSRGB = PPTask.m_Params.ConvertOutputToSRGB;
        IsDirty             = true;
    }

    if (IsDirty)
        PSO.Release();

    if (PSO)
        return;

    try
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(PPTask.m_RenderIndex->GetRenderDelegate());

        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", ConvertOutputToSRGB);

        GraphicsPipelineStateCreateInfoX PsoCI{"Copy Frame"};
        CreateShaders(Device, "HnCopyFrame.psh", "PostCopy Frame PS", Macros, PsoCI)
            .AddRenderTarget(RTVFormat)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .AddSignature(PRS)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error

        IsDirty = false;
    }
    catch (const std::runtime_error& err)
    {
        LOG_ERROR_MESSAGE("Failed to create tone mapping PSO: ", err.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create tone mapping PSO");
    }
}

void HnPostProcessTask::CopyFrameTechnique::PrepareSRB()
{
    VERIFY_EXPR(PPTask.m_TAA);
    ITexture* pAccumulatedFrame = PPTask.m_TAA->GetAccumulatedFrameSRV()->GetTexture();
    if (pAccumulatedFrame == nullptr)
    {
        UNEXPECTED("Accumulated frame is null");
        return;
    }

    ITextureView* pAccumulatedFrameSRV = pAccumulatedFrame->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (SRB)
    {
        if (ShaderVars.Color && ShaderVars.Color.Get() != pAccumulatedFrameSRV)
        {
            SRB.Release();
            ShaderVars = {};
        }
    }

    if (!SRB)
    {
        PRS->CreateShaderResourceBinding(&SRB, true);
        VERIFY_EXPR(SRB);

        ShaderVars.Color = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_ColorBuffer"};
        ShaderVars.Color.Set(pAccumulatedFrameSRV);
    }
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

    HnRenderDelegate*  RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*     pDevice        = RenderDelegate->GetDevice();
    IDeviceContext*    pCtx           = RenderDelegate->GetDeviceContext();
    IRenderStateCache* pStateCache    = RenderDelegate->GetRenderStateCache();
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

    if (!m_SSAO)
    {
        m_SSAO = std::make_unique<ScreenSpaceAmbientOcclusion>(pDevice);
    }

    if (!m_TAA)
    {
        m_TAA = std::make_unique<TemporalAntiAliasing>(pDevice);
    }

    const HnRenderParam* pRenderParam   = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    const TextureDesc&   FinalColorDesc = m_FinalColorRTV->GetTexture()->GetDesc();

    float SSRScale  = 0;
    float SSAOScale = 0;
    if (pRenderParam->GetDebugView() == PBR_Renderer::DebugViewType::None && pRenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID)
    {
        SSRScale  = m_Params.SSRScale;
        SSAOScale = m_Params.SSAOScale;
    }
    if (SSRScale != m_SSRScale)
    {
        m_SSRScale       = SSRScale;
        m_AttribsCBDirty = true;
    }
    m_UseSSR = m_SSRScale > 0;

    if (SSAOScale != m_SSAOScale)
    {
        m_SSAOScale      = SSAOScale;
        m_AttribsCBDirty = true;
    }
    m_UseSSAO = m_SSAOScale > 0;

    m_UseTAA = m_Params.EnableTAA &&
        pRenderParam->GetDebugView() == PBR_Renderer::DebugViewType::None &&
        pRenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID;

    m_PostFXContext->PrepareResources({pRenderParam->GetFrameNumber(), FinalColorDesc.Width, FinalColorDesc.Height});

    constexpr auto SSAOFeatureFlags =
        ScreenSpaceAmbientOcclusion::FEATURE_FLAG_GUIDED_FILTER |
        ScreenSpaceAmbientOcclusion::FEATURE_FLAG_HALF_PRECISION_DEPTH;
    m_SSAO->PrepareResources(pDevice, m_PostFXContext.get(), SSAOFeatureFlags);

    constexpr auto SSRFeatureFlags =
        ScreenSpaceReflection::FEATURE_FLAGS::FEATURE_FLAG_NONE;
    m_SSR->PrepareResources(pDevice, m_PostFXContext.get(), SSRFeatureFlags);

    constexpr auto TAAFeatureFlag =
        TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER |
        TemporalAntiAliasing::FEATURE_FLAG_GAUSSIAN_WEIGHTING;
    m_TAA->PrepareResources(pDevice, m_PostFXContext.get(), TAAFeatureFlag);

    m_PostProcessTech.PreparePRS();
    m_PostProcessTech.PreparePSO((m_UseTAA ? m_FBTargets->JitteredFinalColorRTV : m_FinalColorRTV)->GetDesc().Format);
    m_PostProcessTech.PrepareSRB(ClosestSelectedLocationSRV);

    if (m_UseTAA)
    {
        m_CopyFrameTech.PreparePRS();
        m_CopyFrameTech.PreparePSO(m_FinalColorRTV->GetDesc().Format);
        m_CopyFrameTech.PrepareSRB();
    }

    if (!m_VectorFieldRenderer)
    {
        CreateVectorFieldRenderer(m_FinalColorRTV->GetDesc().Format);
    }

    // Check if one of the previous tasks forced TAA reset.
    if (!m_ResetTAA)
    {
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->taaReset, m_ResetTAA);
    }

    // These parameters will be used by HnBeginFrameTask::Execute() to set
    // projection matrix and mip bias.
    (*TaskCtx)[HnRenderResourceTokens->useTaa]           = pxr::VtValue{m_UseTAA};
    (*TaskCtx)[HnRenderResourceTokens->taaJitterOffsets] = pxr::VtValue{m_UseTAA && !m_ResetTAA ? m_TAA->GetJitterOffset() : float2{0, 0}};
}

void HnPostProcessTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (m_PostProcessTech.PSO == nullptr || m_PostProcessTech.CurrSRB == nullptr)
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
    IRenderStateCache*   pStateCache    = RenderDelegate->GetRenderStateCache();
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

    if (m_UseSSR || m_UseSSAO)
    {
        PostFXContext::RenderAttributes PostFXAttribs{pDevice, pStateCache, pCtx};

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
    }

    if (m_UseSSR)
    {
        HLSL::ScreenSpaceReflectionAttribs SSRAttribs{};
        SSRAttribs.MaxTraversalIntersections = 64;
        SSRAttribs.RoughnessChannel          = 0;
        SSRAttribs.IsRoughnessPerceptual     = true;
        SSRAttribs.RoughnessThreshold        = 0.4f;

        ScreenSpaceReflection::RenderAttributes SSRRenderAttribs{pDevice, pStateCache, pCtx};
        SSRRenderAttribs.pPostFXContext     = m_PostFXContext.get();
        SSRRenderAttribs.pColorBufferSRV    = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_SCENE_COLOR];
        SSRRenderAttribs.pDepthBufferSRV    = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSRRenderAttribs.pNormalBufferSRV   = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NORMAL];
        SSRRenderAttribs.pMaterialBufferSRV = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MATERIAL];
        SSRRenderAttribs.pMotionVectorsSRV  = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MOTION_VECTOR];
        SSRRenderAttribs.pSSRAttribs        = &SSRAttribs;
        m_SSR->Execute(SSRRenderAttribs);
    }

    if (m_UseSSAO)
    {
        HLSL::ScreenSpaceAmbientOcclusionAttribs SSAOSettings{};
        SSAOSettings.EffectRadius = m_Params.SSAORadius;

        ScreenSpaceAmbientOcclusion::RenderAttributes SSAORenderAttribs{};
        SSAORenderAttribs.pDevice             = pDevice;
        SSAORenderAttribs.pDeviceContext      = pCtx;
        SSAORenderAttribs.pPostFXContext      = m_PostFXContext.get();
        SSAORenderAttribs.pCurrDepthBufferSRV = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSAORenderAttribs.pPrevDepthBufferSRV = m_FBTargets->PrevDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSAORenderAttribs.pNormalBufferSRV    = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_NORMAL];
        SSAORenderAttribs.pMotionVectorsSRV   = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MOTION_VECTOR];
        SSAORenderAttribs.pSSAOAttribs        = &SSAOSettings;
        m_SSAO->Execute(SSAORenderAttribs);
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
            ShaderAttribs.SSAOScale                        = m_SSAOScale;
            pCtx->UpdateBuffer(m_PostProcessAttribsCB, 0, sizeof(HLSL::PostProcessAttribs), &ShaderAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        pCtx->SetPipelineState(m_PostProcessTech.PSO);
        pCtx->CommitShaderResources(m_PostProcessTech.CurrSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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

        bool CameraTransformDirty = false;
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->cameraTransformDirty, CameraTransformDirty);

        SuperSamplingFactors CurrSSFactors{
            (pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshMaterial) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshTransform) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshVisibility) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Material) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Light)),
            m_UseSSR,
            m_UseSSAO,
        };

        // Skip rejection if no geometry has changed and the camera transform is not dirty.
        // This will effectively result in full temporal supersampling for static scenes.
        TAASettings.SkipRejection  = (CurrSSFactors == m_LastSuperSamplingFactors) && !CameraTransformDirty && !m_AttribsCBDirty;
        m_LastSuperSamplingFactors = CurrSSFactors;

        TemporalAntiAliasing::RenderAttributes TAARenderAttribs{pDevice, pStateCache, pCtx};
        TAARenderAttribs.pPostFXContext      = m_PostFXContext.get();
        TAARenderAttribs.pColorBufferSRV     = m_FBTargets->JitteredFinalColorRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pCurrDepthBufferSRV = m_FBTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pPrevDepthBufferSRV = m_FBTargets->PrevDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pMotionVectorsSRV   = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MOTION_VECTOR];
        TAARenderAttribs.pTAAAttribs         = &TAASettings;
        m_TAA->Execute(TAARenderAttribs);


        ScopedDebugGroup DebugGroup{pCtx, "Copy frame"};
        ITextureView*    pRTVs[] = {m_FinalColorRTV};
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->SetPipelineState(m_CopyFrameTech.PSO);
        pCtx->CommitShaderResources(m_CopyFrameTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
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

        Attribs.pVectorField = m_FBTargets->GBufferSRVs[HnFramebufferTargets::GBUFFER_TARGET_MOTION_VECTOR];
        m_VectorFieldRenderer->Render(Attribs);
    }

    // Checked to set TAASettings.SkipRejection
    m_AttribsCBDirty = false;
}

} // namespace USD

} // namespace Diligent
