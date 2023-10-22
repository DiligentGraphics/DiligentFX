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

#include "Tasks/HnSetupRenderingTask.hpp"
#include "HnRenderPassState.hpp"
#include "HnTokens.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

HnSetupRenderingTask::HnSetupRenderingTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id},
    m_RenderPassState{std::make_shared<HnRenderPassState>()}
{
}

HnSetupRenderingTask::~HnSetupRenderingTask()
{
}

void HnSetupRenderingTask::UpdateRenderPassState(const HnSetupRenderingTaskParams& Params)
{
    m_RenderPassState->SetRenderMode(Params.RenderMode);
    m_RenderPassState->SetDebugView(Params.DebugView);
    m_RenderPassState->SetOcclusionStrength(Params.OcclusionStrength);
    m_RenderPassState->SetEmissionScale(Params.EmissionScale);
    m_RenderPassState->SetIBLScale(Params.IBLScale);
    m_RenderPassState->SetTransform(Params.Transform);

    VERIFY_EXPR(Params.ColorFormat != TEX_FORMAT_UNKNOWN);
    m_RenderPassState->SetNumRenderTargets(Params.MeshIdFormat != TEX_FORMAT_UNKNOWN ? 2 : 1);
    m_RenderPassState->SetRenderTargetFormat(0, Params.ColorFormat);
    m_RenderPassState->SetRenderTargetFormat(1, Params.MeshIdFormat);
    m_RenderPassState->SetDepthStencilFormat(Params.DepthFormat);

    m_RenderPassState->SetDepthBias(Params.DepthBias, Params.SlopeScaledDepthBias);
    m_RenderPassState->SetDepthFunc(Params.DepthFunc);
    m_RenderPassState->SetDepthBiasEnabled(Params.DepthBiasEnabled);
    m_RenderPassState->SetEnableDepthTest(Params.DepthTestEnabled);
    m_RenderPassState->SetEnableDepthClamp(Params.DepthClampEnabled);

    m_RenderPassState->SetCullStyle(Params.CullStyle);

    m_RenderPassState->SetStencil(Params.StencilFunc, Params.StencilRef, Params.StencilMask,
                                  Params.StencilFailOp, Params.StencilZFailOp, Params.StencilZPassOp);

    m_RenderPassState->SetFrontFaceCCW(Params.FrontFaceCCW);
}

void HnSetupRenderingTask::Sync(pxr::HdSceneDelegate* Delegate,
                                pxr::HdTaskContext*   TaskCtx,
                                pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        pxr::VtValue ParamsValue = Delegate->Get(GetId(), pxr::HdTokens->params);
        if (ParamsValue.IsHolding<HnSetupRenderingTaskParams>())
        {
            HnSetupRenderingTaskParams Params = ParamsValue.UncheckedGet<HnSetupRenderingTaskParams>();
            UpdateRenderPassState(Params);
        }
        else
        {
            UNEXPECTED("Unknown task parameters type: ", ParamsValue.GetTypeName());
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnSetupRenderingTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                   pxr::HdRenderIndex* RenderIndex)
{
    (*TaskCtx)[HnTokens->renderPassState] = pxr::VtValue{m_RenderPassState};
}

void HnSetupRenderingTask::Execute(pxr::HdTaskContext* TaskCtx)
{
}

} // namespace USD

} // namespace Diligent
