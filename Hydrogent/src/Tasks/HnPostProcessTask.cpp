/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "DebugUtilities.hpp"
#include "TextureView.h"
#include "RenderStateCache.hpp"
#include "ShaderMacroHelper.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"

namespace Diligent
{

namespace USD
{

namespace HLSL
{

namespace
{
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "../shaders/HnPostProcessStructures.fxh"
} // namespace

} // namespace HLSL

HnPostProcessTask::HnPostProcessTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnPostProcessTask::~HnPostProcessTask()
{
}

void HnPostProcessTask::PreparePSO(TEXTURE_FORMAT RTVFormat)
{
    if (m_PsoIsDirty || (m_PSO && m_PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat))
        m_PSO.Release();

    if (m_PSO)
        return;

    try
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        auto pHnFxCompoundSourceFactory     = HnShaderSourceFactory::CreateHnFxCompoundFactory();
        ShaderCI.pShaderSourceStreamFactory = pHnFxCompoundSourceFactory;

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", m_Params.ConvertOutputToSRGB);
        Macros.Add("TONE_MAPPING_MODE", m_Params.ToneMappingMode);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Post process VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnPostProcess.vsh";

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
            .AddVariable(SHADER_TYPE_PIXEL, "cbPostProcessAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        GraphicsPipelineStateCreateInfoX PsoCI{"Post process"};
        PsoCI
            .AddRenderTarget(RTVFormat)
            .AddShader(pVS)
            .AddShader(pPS)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetResourceLayout(ResourceLauout)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        m_PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error
        m_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPostProcessAttribs")->Set(m_PostProcessAttribsCB);

        m_PsoIsDirty = false;
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
                m_Params.ToneMappingMode != Params.ToneMappingMode)
            {
                // In OpenGL we can't release PSO in Worker thread
                m_PsoIsDirty = true;
            }

            m_Params = Params;
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

    if (!m_PostProcessAttribsCB)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
        CreateUniformBuffer(RenderDelegate->GetDevice(), sizeof(HLSL::PostProcessAttribs), "Post process attribs CB", &m_PostProcessAttribsCB);
        VERIFY(m_PostProcessAttribsCB, "Failed to create post process attribs CB");
    }

    PreparePSO(m_FinalColorRTV->GetDesc().Format);
    if (std::shared_ptr<HnRenderPassState> RenderPassState = GetRenderPassState(TaskCtx))
    {
        m_ClearDepth = RenderPassState->GetClearDepth();
        PrepareSRB(*RenderPassState, ClosestSelectedLocationSRV);
    }
    else
    {
        UNEXPECTED("Render pass state is not set in the task context");
    }
}

void HnPostProcessTask::PrepareSRB(const HnRenderPassState& RPState, ITextureView* pClosestSelectedLocationSRV)
{
    if (!m_PSO)
    {
        UNEXPECTED("PSO is null");
        return;
    }

    const auto& FBTargets = RPState.GetFramebufferTargets();
    if (!FBTargets)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }

    ITextureView* pOffscreenColorSRV = FBTargets.OffscreenColorRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pOffscreenColorSRV == nullptr)
    {
        UNEXPECTED("Offscreen color SRV is null");
        return;
    }
    ITextureView* pDepthSRV = FBTargets.DepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pDepthSRV == nullptr)
    {
        UNEXPECTED("Depth SRV is null");
        return;
    }
    ITextureView* pSelectionDepthSRV = FBTargets.SelectionDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionDepthSRV == nullptr)
    {
        UNEXPECTED("Selection depth SRV is null");
        return;
    }

    if (m_SRB && m_ShaderVars)
    {
        if (m_ShaderVars.Color->Get() != pOffscreenColorSRV ||
            m_ShaderVars.Depth->Get() != pDepthSRV ||
            m_ShaderVars.SelectionDepth->Get() != pSelectionDepthSRV ||
            m_ShaderVars.ClosestSelectedLocation->Get() != pClosestSelectedLocationSRV)
        {
            m_SRB.Release();
            m_ShaderVars = {};
        }
    }

    if (!m_SRB)
    {
        m_PSO->CreateShaderResourceBinding(&m_SRB, true);
        VERIFY_EXPR(m_SRB);
        m_ShaderVars.Color                   = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer");
        m_ShaderVars.Depth                   = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Depth");
        m_ShaderVars.SelectionDepth          = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SelectionDepth");
        m_ShaderVars.ClosestSelectedLocation = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ClosestSelectedLocation");
        VERIFY_EXPR(m_ShaderVars);

        if (m_ShaderVars.Color != nullptr)
            m_ShaderVars.Color->Set(pOffscreenColorSRV);
        if (m_ShaderVars.Depth != nullptr)
            m_ShaderVars.Depth->Set(pDepthSRV);
        if (m_ShaderVars.SelectionDepth != nullptr)
            m_ShaderVars.SelectionDepth->Set(pSelectionDepthSRV);
        if (m_ShaderVars.ClosestSelectedLocation != nullptr)
            m_ShaderVars.ClosestSelectedLocation->Set(pClosestSelectedLocationSRV);
    }
}

void HnPostProcessTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (!m_PSO || !m_SRB)
    {
        UNEXPECTED("Render technique is not initialized");
        return;
    }

    if (m_FinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ITextureView* pRTVs[] = {m_FinalColorRTV};
    pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<HLSL::PostProcessAttribs> pDstShaderAttribs{pCtx, m_PostProcessAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        pDstShaderAttribs->SelectionOutlineColor          = m_Params.SelectionColor;
        pDstShaderAttribs->NonselectionDesaturationFactor = m_Params.NonselectionDesaturationFactor;

        pDstShaderAttribs->ToneMapping.iToneMappingMode     = m_Params.ToneMappingMode;
        pDstShaderAttribs->ToneMapping.bAutoExposure        = 0;
        pDstShaderAttribs->ToneMapping.fMiddleGray          = m_Params.MiddleGray;
        pDstShaderAttribs->ToneMapping.bLightAdaptation     = 0;
        pDstShaderAttribs->ToneMapping.fWhitePoint          = m_Params.WhitePoint;
        pDstShaderAttribs->ToneMapping.fLuminanceSaturation = m_Params.LuminanceSaturation;
        pDstShaderAttribs->AverageLogLum                    = m_Params.AverageLogLum;
        pDstShaderAttribs->ClearDepth                       = m_ClearDepth;
        pDstShaderAttribs->SelectionOutlineWidth            = m_Params.SelectionOutlineWidth;
    }
    pCtx->SetPipelineState(m_PSO);
    pCtx->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
}

} // namespace USD

} // namespace Diligent
