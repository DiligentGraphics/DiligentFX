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

#include "Tasks/HnSetupSelectionDepthTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "ScopedDebugGroup.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

HnSetupSelectionDepthTask::HnSetupSelectionDepthTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnSetupSelectionDepthTask::~HnSetupSelectionDepthTask()
{
}

void HnSetupSelectionDepthTask::Sync(pxr::HdSceneDelegate* Delegate,
                                     pxr::HdTaskContext*   TaskCtx,
                                     pxr::HdDirtyBits*     DirtyBits)
{
    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnSetupSelectionDepthTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                        pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;
}

void HnSetupSelectionDepthTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is not initialized");
        return;
    }

    std::shared_ptr<HnRenderPassState> RenderPassState = GetRenderPassState(TaskCtx);
    if (!RenderPassState)
    {
        UNEXPECTED("Render pass state is not set in the task context");
        return;
    }
    const HnFramebufferTargets& Targets = RenderPassState->GetFramebufferTargets();
    if (Targets.SelectionDepthDSV == nullptr)
    {
        UNEXPECTED("Selection depth buffer is not set in the render pass state");
        return;
    }

    HnRenderDelegate* pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx            = pRenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Set up Selection Depth"};

    pCtx->SetRenderTargets(0, nullptr, Targets.SelectionDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

} // namespace USD

} // namespace Diligent
