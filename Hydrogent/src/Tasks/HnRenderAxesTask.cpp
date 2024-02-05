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

#include "Tasks/HnRenderAxesTask.hpp"

#include "HnShaderSourceFactory.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"

#include "DebugUtilities.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace HLSL
{

#include "../shaders/HnAxesStructures.fxh"

} // namespace HLSL


namespace USD
{

HnRenderAxesTask::HnRenderAxesTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderAxesTask::~HnRenderAxesTask()
{
}

void HnRenderAxesTask::PreparePSO(const HnRenderPassState& RPState)
{
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

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Axes VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnAxes.vsh";

            pVS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Axes PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnAxes.psh";

            pPS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        }

        GraphicsPipelineStateCreateInfoX PsoCI{"Axes"};
        PsoCI
            .AddShader(pVS)
            .AddShader(pPS)
            .SetDepthFormat(RPState.GetDepthStencilFormat())
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetDepthStencilDesc(DSS_EnableDepthNoWrites)
            .SetBlendDesc(BS_AlphaBlend)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_LINE_LIST);
        PsoCI.PSODesc.ResourceLayout.DefaultVariableType        = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        PsoCI.PSODesc.ResourceLayout.DefaultVariableMergeStages = SHADER_TYPE_VS_PS;
        for (Uint32 i = 0; i < RPState.GetNumRenderTargets(); ++i)
            PsoCI.AddRenderTarget(RPState.GetRenderTargetFormat(i));

        m_PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error
        m_PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs")->Set(RenderDelegate->GetFrameAttribsCB());
        m_PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbConstants")->Set(m_ConstantsCB);
        m_PSO->CreateShaderResourceBinding(&m_SRB, true);
        VERIFY_EXPR(m_SRB);
    }
    catch (const std::runtime_error& err)
    {
        LOG_ERROR_MESSAGE("Failed to axes PSO: ", err.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to axes PSO");
    }
}

void HnRenderAxesTask::Sync(pxr::HdSceneDelegate* Delegate,
                            pxr::HdTaskContext*   TaskCtx,
                            pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnRenderAxesTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
            m_Params         = Params;
            m_ParamsAreDirty = true;
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnRenderAxesTask::Prepare(pxr::HdTaskContext* TaskCtx,
                               pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    if (!m_ConstantsCB)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
        CreateUniformBuffer(RenderDelegate->GetDevice(), sizeof(HLSL::AxesConstants), "Axes constants CB", &m_ConstantsCB, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE);
        VERIFY(m_ConstantsCB, "Failed to create axes constants CB");
    }

    if (m_ParamsAreDirty)
    {
        HLSL::AxesConstants Constants;
        Constants.Transform = m_Params.Transform;

        Constants.AxesColors[0] = m_Params.NegativeXColor;
        Constants.AxesColors[1] = m_Params.PositiveXColor;
        Constants.AxesColors[2] = m_Params.NegativeYColor;
        Constants.AxesColors[3] = m_Params.PositiveYColor;
        Constants.AxesColors[4] = m_Params.NegativeZColor;
        Constants.AxesColors[5] = m_Params.PositiveZColor;

        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
        IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();
        pCtx->UpdateBuffer(m_ConstantsCB, 0, sizeof(Constants), &Constants, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_ParamsAreDirty = false;
    }

    if (std::shared_ptr<HnRenderPassState> RenderPassState = GetRenderPassState(TaskCtx))
    {
        PreparePSO(*RenderPassState);
    }
    else
    {
        UNEXPECTED("Render pass state is not set in the task context");
    }
}

void HnRenderAxesTask::Execute(pxr::HdTaskContext* TaskCtx)
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

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Render Axes"};

    pCtx->SetPipelineState(m_PSO);
    pCtx->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->Draw({12, DRAW_FLAG_VERIFY_ALL});
}

} // namespace USD

} // namespace Diligent
