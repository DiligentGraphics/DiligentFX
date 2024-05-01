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
#include "HnFrameRenderTargets.hpp"
#include "HnRenderParam.hpp"

#include "DebugUtilities.hpp"
#include "TextureView.h"
#include "RenderStateCache.hpp"
#include "ShaderMacroHelper.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "VectorFieldRenderer.hpp"
#include "ToneMapping.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "../shaders/HnPostProcessStructures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"

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

    {
        const bool _ConvertOutputToSRGB = !PPTask.m_UseTAA && PPTask.m_Params.ConvertOutputToSRGB;
        if (ConvertOutputToSRGB != _ConvertOutputToSRGB)
        {
            ConvertOutputToSRGB = _ConvertOutputToSRGB;
            IsDirty             = true;
        }
    }

    {
        const int _ToneMappingMode = !PPTask.m_UseTAA ? PPTask.m_Params.ToneMappingMode : 0;
        if (ToneMappingMode != _ToneMappingMode)
        {
            ToneMappingMode = _ToneMappingMode;
            IsDirty         = true;
        }
    }

    {
        const CoordinateGridRenderer::FEATURE_FLAGS _GridFeatureFlags = !PPTask.m_UseTAA ? PPTask.m_Params.GridFeatureFlags : CoordinateGridRenderer::FEATURE_FLAG_NONE;
        if (GridFeatureFlags != _GridFeatureFlags)
        {
            GridFeatureFlags = _GridFeatureFlags;
            IsDirty          = true;
        }
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
        if (GridFeatureFlags != CoordinateGridRenderer::FEATURE_FLAG_NONE)
        {
            Macros.Add("ENABLE_GRID", 1);
            CoordinateGridRenderer::AddShaderMacros(GridFeatureFlags, Macros);
        }

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

void HnPostProcessTask::PostProcessingTechnique::PrepareSRB(ITextureView* pClosestSelectedLocationSRV, Uint32 FrameIdx)
{
    const auto* FrameTargets = PPTask.m_FrameTargets;

    for (Uint32 i = 0; i < HnFrameRenderTargets::GBUFFER_TARGET_COUNT; ++i)
    {
        if (FrameTargets->GBufferSRVs[i] == nullptr)
        {
            UNEXPECTED("G-buffer SRV ", i, " is null");
            return;
        }
    }

    ITextureView* pDepthSRV = FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pDepthSRV == nullptr)
    {
        UNEXPECTED("Depth SRV is null");
        return;
    }

    ITextureView* pSelectionDepthSRV = FrameTargets->SelectionDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionDepthSRV == nullptr)
    {
        UNEXPECTED("Selection depth SRV is null");
        return;
    }

    ITextureView* pSSR = PPTask.m_SSR->GetSSRRadianceSRV();
    VERIFY_EXPR(pSSR != nullptr);

    ITextureView* pSSAO = PPTask.m_SSAO->GetAmbientOcclusionSRV();
    VERIFY_EXPR(pSSAO != nullptr);

    size_t ResIdx     = FrameIdx % Resources.size();
    auto&  SRB        = Resources[ResIdx].SRB;
    auto&  ShaderVars = Resources[ResIdx].Vars;

    ITextureView* pOffscreenColorSRV = FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR];
    ITextureView* pSpecularIblSRV    = FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_IBL];
    ITextureView* pNormalSRV         = FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL];
    ITextureView* pMaterialSRV       = FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL];
    ITextureView* pBaseColorSRV      = FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR];
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
        .AddResource(SHADER_TYPE_PIXEL, "cbPostProcessAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddResource(SHADER_TYPE_PIXEL, "cbFrameAttribs", SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddResource(SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        .AddResource(SHADER_TYPE_PIXEL, "g_Depth", SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    PRS = RenderDeviceWithCache_N{pDevice, pStateCache}.CreatePipelineResourceSignature(PRSDesc);
    VERIFY_EXPR(PRS);

    VERIFY_EXPR(PPTask.m_PostProcessAttribsCB);
    ShaderResourceVariableX{PRS, SHADER_TYPE_PIXEL, "cbPostProcessAttribs"}.Set(PPTask.m_PostProcessAttribsCB);

    VERIFY_EXPR(RenderDelegate->GetFrameAttribsCB());
    ShaderResourceVariableX{PRS, SHADER_TYPE_PIXEL, "cbFrameAttribs"}.Set(RenderDelegate->GetFrameAttribsCB());
}

void HnPostProcessTask::CopyFrameTechnique::PreparePSO(TEXTURE_FORMAT RTVFormat)
{
    bool IsDirty = PSO && PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat;

    if (ConvertOutputToSRGB != PPTask.m_Params.ConvertOutputToSRGB)
    {
        ConvertOutputToSRGB = PPTask.m_Params.ConvertOutputToSRGB;
        IsDirty             = true;
    }

    if (ToneMappingMode != PPTask.m_Params.ToneMappingMode)
    {
        ToneMappingMode = PPTask.m_Params.ToneMappingMode;
        IsDirty         = true;
    }

    {
        const CoordinateGridRenderer::FEATURE_FLAGS _GridFeatureFlags = PPTask.m_UseTAA ? PPTask.m_Params.GridFeatureFlags : CoordinateGridRenderer::FEATURE_FLAG_NONE;
        if (GridFeatureFlags != _GridFeatureFlags)
        {
            GridFeatureFlags = _GridFeatureFlags;
            IsDirty          = true;
        }
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
        if (GridFeatureFlags != CoordinateGridRenderer::FEATURE_FLAG_NONE)
        {
            Macros.Add("ENABLE_GRID", 1);
            CoordinateGridRenderer::AddShaderMacros(GridFeatureFlags, Macros);
        }

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

void HnPostProcessTask::CopyFrameTechnique::PrepareSRB(Uint32 FrameIdx)
{
    VERIFY_EXPR(PPTask.m_TAA);
    ITexture* pAccumulatedFrame = nullptr;
    if (PPTask.m_UseBloom)
        pAccumulatedFrame = PPTask.m_Bloom->GetBloomTextureSRV()->GetTexture();
    else if (PPTask.m_UseDOF)
        pAccumulatedFrame = PPTask.m_DOF->GetDepthOfFieldTextureSRV()->GetTexture();
    else
        pAccumulatedFrame = PPTask.m_TAA->GetAccumulatedFrameSRV()->GetTexture();
    if (pAccumulatedFrame == nullptr)
    {
        UNEXPECTED("Accumulated frame is null");
        return;
    }

    size_t ResIdx     = FrameIdx % Resources.size();
    auto&  SRB        = Resources[ResIdx].SRB;
    auto&  ShaderVars = Resources[ResIdx].Vars;

    ITextureView* pAccumulatedFrameSRV = pAccumulatedFrame->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* pDepthSRV            = PPTask.m_FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (SRB)
    {
        if ((ShaderVars.Color && ShaderVars.Color.Get() != pAccumulatedFrameSRV) ||
            (ShaderVars.Depth && ShaderVars.Depth.Get() != pDepthSRV))
        {
            SRB.Release();
            ShaderVars = {};
            CurrSRB    = nullptr;
        }
    }

    if (!SRB)
    {
        PRS->CreateShaderResourceBinding(&SRB, true);
        VERIFY_EXPR(SRB);

        ShaderVars.Color = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_ColorBuffer"};
        ShaderVars.Color.Set(pAccumulatedFrameSRV);

        ShaderVars.Depth = ShaderResourceVariableX{SRB, SHADER_TYPE_PIXEL, "g_Depth"};
        ShaderVars.Depth.Set(pDepthSRV);
    }

    CurrSRB = SRB;
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

    float BackgroundDepth = 1.f;
    if (!GetTaskContextData(TaskCtx, HnRenderResourceTokens->backgroundDepth, BackgroundDepth))
    {
        UNEXPECTED("Background depth is not set in the task context");
    }

    if (m_BackgroundDepth != BackgroundDepth)
    {
        m_BackgroundDepth = BackgroundDepth;
        m_AttribsCBDirty  = true;
    }

    const HnFrameRenderTargets* FrameTargets = GetFrameRenderTargets(TaskCtx);
    if (FrameTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }
    m_FrameTargets = FrameTargets;

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

    if (!m_SSAO)
    {
        m_SSAO = std::make_unique<ScreenSpaceAmbientOcclusion>(pDevice);
    }

    if (!m_TAA)
    {
        m_TAA = std::make_unique<TemporalAntiAliasing>(pDevice);
    }

    if (!m_DOF)
    {
        m_DOF = std::make_unique<DepthOfField>(pDevice);
    }

    if (!m_Bloom)
    {
        m_Bloom = std::make_unique<Bloom>(pDevice);
    }

    const HnRenderParam* pRenderParam   = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    const TextureDesc&   FinalColorDesc = m_FinalColorRTV->GetTexture()->GetDesc();

    const PBR_Renderer::DebugViewType DebugView = pRenderParam->GetDebugView();

    const bool EnablePostProcessing =
        (DebugView == PBR_Renderer::DebugViewType::None || DebugView == PBR_Renderer::DebugViewType::WhiteBaseColor) &&
        (pRenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID);

    float SSRScale  = 0;
    float SSAOScale = 0;
    if (EnablePostProcessing)
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
    m_UseSSAO  = m_SSAOScale > 0;
    m_UseTAA   = m_Params.EnableTAA && EnablePostProcessing;
    m_UseBloom = m_Params.EnableBloom && EnablePostProcessing && m_UseTAA;
    m_UseDOF   = m_Params.EnableDOF && EnablePostProcessing && m_UseTAA;

    m_PostFXContext->PrepareResources(pDevice, {pRenderParam->GetFrameNumber(), FinalColorDesc.Width, FinalColorDesc.Height}, PostFXContext::FEATURE_FLAG_NONE);

    m_SSAO->PrepareResources(pDevice, pCtx, m_PostFXContext.get(), m_Params.SSAOFeatureFlags);
    m_SSR->PrepareResources(pDevice, pCtx, m_PostFXContext.get(), m_Params.SSRFeatureFlags);
    m_TAA->PrepareResources(pDevice, pCtx, m_PostFXContext.get(), m_Params.TAAFeatureFlags);
    if (m_UseBloom)
    {
        m_Bloom->PrepareResources(pDevice, pCtx, m_PostFXContext.get(), m_Params.BloomFeatureFlags);
    }

    if (m_UseDOF)
    {
        m_DOF->PrepareResources(pDevice, pCtx, m_PostFXContext.get(), m_Params.DOFFeatureFlags);
    }

    m_PostProcessTech.PreparePRS();
    m_PostProcessTech.PreparePSO((m_UseTAA ? m_FrameTargets->JitteredFinalColorRTV : m_FinalColorRTV)->GetDesc().Format);
    m_PostProcessTech.PrepareSRB(ClosestSelectedLocationSRV, pRenderParam->GetFrameNumber());

    if (m_UseTAA)
    {
        m_CopyFrameTech.PreparePRS();
        m_CopyFrameTech.PreparePSO(m_FinalColorRTV->GetDesc().Format);
        m_CopyFrameTech.PrepareSRB(pRenderParam->GetFrameNumber());

        {
            auto it = TaskCtx->find(HnRenderResourceTokens->suspendSuperSampling);
            if (it != TaskCtx->end())
            {
                if (it->second.GetWithDefault<bool>(false))
                {
                    m_LastSuperSamplingFactors.Version = ~0u;
                }
            }
        }

        static_assert(static_cast<int>(HnRenderParam::GlobalAttrib::Count) == 7, "Please update the code below to handle the new attribute, if necessary.");
        SuperSamplingFactors CurrSSFactors{
            (pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshMaterial) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshTransform) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshVisibility) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Material) +
             pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Light)),
            m_UseSSR,
            m_UseSSAO,
            pRenderParam->GetUseShadows(),
            pRenderParam->GetDebugView(),
            pRenderParam->GetRenderMode(),
        };

        if (CurrSSFactors != m_LastSuperSamplingFactors || m_AttribsCBDirty)
            SuspendSuperSampling();

        m_LastSuperSamplingFactors = CurrSSFactors;

        (*TaskCtx)[HnRenderResourceTokens->suspendSuperSampling] = pxr::VtValue{false};
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

    if (m_FrameTargets == nullptr)
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
        std::array<StateTransitionDesc, HnFrameRenderTargets::GBUFFER_TARGET_COUNT> Barriers{};
        for (Uint32 i = 0; i < HnFrameRenderTargets::GBUFFER_TARGET_COUNT; ++i)
        {
            Barriers[i] = {m_FrameTargets->GBufferSRVs[i]->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
        }
        pCtx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->TransitionResourceStates(static_cast<Uint32>(Barriers.size()), Barriers.data());
    }

    const TextureDesc& FinalColorDesc = m_FinalColorRTV->GetTexture()->GetDesc();

    if (m_UseSSR || m_UseSSAO || m_UseTAA)
    {
        PostFXContext::RenderAttributes PostFXAttribs{pDevice, pStateCache, pCtx};

        PostFXAttribs.pCurrDepthBufferSRV = m_FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        PostFXAttribs.pPrevDepthBufferSRV = m_FrameTargets->PrevDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        PostFXAttribs.pMotionVectorsSRV   = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR];

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
        ScreenSpaceReflection::RenderAttributes SSRRenderAttribs{pDevice, pStateCache, pCtx};
        SSRRenderAttribs.pPostFXContext     = m_PostFXContext.get();
        SSRRenderAttribs.pColorBufferSRV    = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR];
        SSRRenderAttribs.pDepthBufferSRV    = m_FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSRRenderAttribs.pNormalBufferSRV   = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL];
        SSRRenderAttribs.pMaterialBufferSRV = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL];
        SSRRenderAttribs.pMotionVectorsSRV  = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR];
        SSRRenderAttribs.pSSRAttribs        = &m_Params.SSR;
        m_SSR->Execute(SSRRenderAttribs);
    }

    if (m_UseSSAO)
    {
        ScreenSpaceAmbientOcclusion::RenderAttributes SSAORenderAttribs{pDevice, pStateCache, pCtx};
        SSAORenderAttribs.pPostFXContext   = m_PostFXContext.get();
        SSAORenderAttribs.pDepthBufferSRV  = m_FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        SSAORenderAttribs.pNormalBufferSRV = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL];
        SSAORenderAttribs.pSSAOAttribs     = &m_Params.SSAO;
        m_SSAO->Execute(SSAORenderAttribs);
    }

    {
        ScopedDebugGroup DebugGroup{pCtx, "Post Processing"};

        ITextureView* pRTVs[] = {m_UseTAA ? m_FrameTargets->JitteredFinalColorRTV : m_FinalColorRTV};
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (m_AttribsCBDirty)
        {
            const bool ReverseToneMapSelectionColors = m_UseTAA && m_Params.ToneMappingMode != 0;

            const float3 SelectionColorHDR = ReverseToneMapSelectionColors ?
                ReverseExpToneMap(m_Params.SelectionColor, m_Params.MiddleGray, m_Params.AverageLogLum) :
                m_Params.SelectionColor;

            const float3 OccludedSelectionColorHDR = ReverseToneMapSelectionColors ?
                ReverseExpToneMap(m_Params.OccludedSelectionColor, m_Params.MiddleGray, m_Params.AverageLogLum) :
                m_Params.OccludedSelectionColor;

            HLSL::PostProcessAttribs ShaderAttribs;
            ShaderAttribs.SelectionOutlineColor            = float4{SelectionColorHDR, m_Params.SelectionColor.a};
            ShaderAttribs.OccludedSelectionOutlineColor    = float4{OccludedSelectionColorHDR, m_Params.OccludedSelectionColor.a};
            ShaderAttribs.NonselectionDesaturationFactor   = m_Params.NonselectionDesaturationFactor;
            ShaderAttribs.ToneMapping.iToneMappingMode     = m_Params.ToneMappingMode;
            ShaderAttribs.ToneMapping.bAutoExposure        = 0;
            ShaderAttribs.ToneMapping.fMiddleGray          = m_Params.MiddleGray;
            ShaderAttribs.ToneMapping.bLightAdaptation     = 0;
            ShaderAttribs.ToneMapping.fWhitePoint          = m_Params.WhitePoint;
            ShaderAttribs.ToneMapping.fLuminanceSaturation = m_Params.LuminanceSaturation;

            ShaderAttribs.CoordinateGrid = m_Params.Grid;

            ShaderAttribs.AverageLogLum         = m_Params.AverageLogLum;
            ShaderAttribs.ClearDepth            = m_BackgroundDepth;
            ShaderAttribs.SelectionOutlineWidth = m_Params.SelectionOutlineWidth;
            ShaderAttribs.SSRScale              = m_SSRScale;
            ShaderAttribs.SSAOScale             = m_SSAOScale;
            pCtx->UpdateBuffer(m_PostProcessAttribsCB, 0, sizeof(HLSL::PostProcessAttribs), &ShaderAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        pCtx->SetPipelineState(m_PostProcessTech.PSO);
        pCtx->CommitShaderResources(m_PostProcessTech.CurrSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
    }

    if (m_UseTAA)
    {
        HLSL::TemporalAntiAliasingAttribs TAASettings = m_Params.TAA;

        if (m_ResetTAA)
        {
            TAASettings.ResetAccumulation = m_ResetTAA;
            m_ResetTAA                    = false;
        }

        // cameraTransformDirty is set by HnBeginFrameTask::Execute().
        bool CameraTransformDirty = false;
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->cameraTransformDirty, CameraTransformDirty);

        // Skip rejection if no geometry has changed and the camera transform is not dirty.
        // This will effectively result in full temporal supersampling for static scenes.
        TAASettings.SkipRejection = (m_SuperSamplingSuspensionFrame == 0) && !CameraTransformDirty;
        if (m_SuperSamplingSuspensionFrame > 0)
            --m_SuperSamplingSuspensionFrame;

        TemporalAntiAliasing::RenderAttributes TAARenderAttribs{pDevice, pStateCache, pCtx};
        TAARenderAttribs.pPostFXContext  = m_PostFXContext.get();
        TAARenderAttribs.pColorBufferSRV = m_FrameTargets->JitteredFinalColorRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        TAARenderAttribs.pTAAAttribs     = &TAASettings;
        m_TAA->Execute(TAARenderAttribs);

        ITextureView* pFrameSRV = m_TAA->GetAccumulatedFrameSRV();

        if (m_UseDOF)
        {
            DepthOfField::RenderAttributes DOFRenderAttribs{pDevice, pStateCache, pCtx};
            DOFRenderAttribs.pPostFXContext  = m_PostFXContext.get();
            DOFRenderAttribs.pColorBufferSRV = pFrameSRV;
            DOFRenderAttribs.pDepthBufferSRV = m_FrameTargets->DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            DOFRenderAttribs.pDOFAttribs     = &m_Params.DOF;
            m_DOF->Execute(DOFRenderAttribs);

            pFrameSRV = m_DOF->GetDepthOfFieldTextureSRV();
        }

        if (m_UseBloom)
        {
            Bloom::RenderAttributes BloomRenderAttribs{pDevice, pStateCache, pCtx};
            BloomRenderAttribs.pPostFXContext  = m_PostFXContext.get();
            BloomRenderAttribs.pColorBufferSRV = pFrameSRV;
            BloomRenderAttribs.pBloomAttribs   = &m_Params.Bloom;
            m_Bloom->Execute(BloomRenderAttribs);
        }

        ScopedDebugGroup DebugGroup{pCtx, "Copy frame"};
        ITextureView*    pRTVs[] = {m_FinalColorRTV};
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->SetPipelineState(m_CopyFrameTech.PSO);
        pCtx->CommitShaderResources(m_CopyFrameTech.CurrSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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

        Attribs.pVectorField = m_FrameTargets->GBufferSRVs[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR];
        m_VectorFieldRenderer->Render(Attribs);
    }

    // Checked to set TAASettings.SkipRejection
    m_AttribsCBDirty = false;
}

void HnPostProcessTask::SuspendSuperSampling()
{
    m_SuperSamplingSuspensionFrame = m_Params.SuperSamplingSuspensionFrames;
}

} // namespace USD

} // namespace Diligent
