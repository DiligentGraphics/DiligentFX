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

#include "DebugUtilities.hpp"
#include "TextureView.h"
#include "RenderStateCache.hpp"
#include "ShaderMacroHelper.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.h"

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
    if (m_PSO)
    {
        if (m_PsoIsDirty || m_PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat)
        {
            m_PSO.Release();
            m_SRB.Release();
        }
    }

    if (m_PSO)
        return;

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

    RenderDeviceWithCache_N Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

    IShaderSourceInputStreamFactory* ppShaderSourceFactories[] =
        {
            &HnShaderSourceFactory::GetInstance(),
            &DiligentFXShaderSourceStreamFactory::GetInstance(),
        };
    CompoundShaderSourceFactoryCreateInfo          CompoundSourceFactoryCI{ppShaderSourceFactories, _countof(ppShaderSourceFactories)};
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pCompoundSourceFactory;
    CreateCompoundShaderSourceFactory(CompoundSourceFactoryCI, &pCompoundSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pCompoundSourceFactory;

    ShaderMacroHelper Macros;
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", m_Params.ConvertOutputToSRGB);
    Macros.Add("TONE_MAPPING_MODE", m_Params.ToneMappingMode);
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"Post process VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "HnPostProcess.vsh";

        pVS = Device.CreateShader(ShaderCI);
        if (!pVS)
        {
            UNEXPECTED("Failed to create post process VS");
            return;
        }
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"Post process PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "HnPostProcess.psh";

        pPS = Device.CreateShader(ShaderCI);
        if (!pPS)
        {
            UNEXPECTED("Failed to create post process PS");
            return;
        }
    }

    PipelineResourceLayoutDescX ResourceLauout;
    ResourceLauout
        .AddVariable(SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddVariable(SHADER_TYPE_PIXEL, "g_Selection", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    GraphicsPipelineStateCreateInfoX PsoCI{"Post process"};
    PsoCI
        .AddRenderTarget(RTVFormat)
        .AddShader(pVS)
        .AddShader(pPS)
        .SetDepthStencilDesc(DSS_DisableDepth)
        .SetRasterizerDesc(RS_SolidFillNoCull)
        .SetResourceLayout(ResourceLauout)
        .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    m_PSO = Device.CreateGraphicsPipelineState(PsoCI);
    if (!m_PSO)
    {
        UNEXPECTED("Failed to create post process PSO");
        return;
    }
    m_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPostProcessAttribs")->Set(m_PostProcessAttribsCB);

    m_SRB.Release();
    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    VERIFY_EXPR(m_SRB);

    m_PsoIsDirty = false;
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
    ITextureView* pFinalColorRTV = GetRenderBufferTarget(*RenderIndex, TaskCtx, HnRenderResourceTokens->finalColorTarget);
    if (pFinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is not set in the task context");
        return;
    }

    if (!m_PostProcessAttribsCB)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
        CreateUniformBuffer(RenderDelegate->GetDevice(), sizeof(HLSL::PostProcessAttribs), "Post process attribs CB", &m_PostProcessAttribsCB);
        VERIFY(m_PostProcessAttribsCB, "Failed to create post process attribs CB");
    }

    PreparePSO(pFinalColorRTV->GetDesc().Format);
}

void HnPostProcessTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    // Render target Bprims are initialized by the HnSetupRenderingTask.

    ITextureView* pFinalColorRTV = GetRenderBufferTarget(*m_RenderIndex, TaskCtx, HnRenderResourceTokens->finalColorTarget);
    if (pFinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is not set in the task context");
        return;
    }
    ITextureView* pOffscreenColorRTV = GetRenderBufferTarget(*m_RenderIndex, TaskCtx, HnRenderResourceTokens->offscreenColorTarget);
    if (pOffscreenColorRTV == nullptr)
    {
        UNEXPECTED("Offscreen color RTV is not set in the task context");
        return;
    }
    ITextureView* pSelectionRTV = GetRenderBufferTarget(*m_RenderIndex, TaskCtx, HnRenderResourceTokens->selectionTarget);
    if (pSelectionRTV == nullptr)
    {
        UNEXPECTED("Mesh Id RTV is not set in the task context");
        return;
    }
    ITextureView* pOffscreenColorSRV = pOffscreenColorRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pOffscreenColorSRV == nullptr)
    {
        UNEXPECTED("Offscreen color SRV is null");
        return;
    }
    ITextureView* pSelectionSRV = pSelectionRTV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionSRV == nullptr)
    {
        UNEXPECTED("Mesh Id SRV is null");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ITextureView* pRTVs[] = {pFinalColorRTV};
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
    }
    pCtx->SetPipelineState(m_PSO);
    m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer")->Set(pOffscreenColorSRV);
    m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Selection")->Set(pSelectionSRV);
    pCtx->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
}

} // namespace USD

} // namespace Diligent
