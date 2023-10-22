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
            (void)Params;
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
