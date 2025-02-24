/*
 *  Copyright 2025 Diligent Graphics LLC
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

#include "Tasks/HnBeginOITPassTask.hpp"
#include "HnRenderDelegate.hpp"
#include "ScopedDebugGroup.hpp"
#include "HnTokens.hpp"
#include "HnRenderPassState.hpp"

namespace Diligent
{

namespace USD
{

HnBeginOITPassTask::HnBeginOITPassTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnBeginOITPassTask::~HnBeginOITPassTask()
{
}

void HnBeginOITPassTask::Sync(pxr::HdSceneDelegate* Delegate,
                              pxr::HdTaskContext*   TaskCtx,
                              pxr::HdDirtyBits*     DirtyBits)
{
    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnBeginOITPassTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                 pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;
}

void HnBeginOITPassTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Begin OIT pass"};

    if (HnRenderPassState* RP_OITLayers = GetRenderPassState(TaskCtx, HnRenderResourceTokens->renderPass_OITLayers))
    {
        RP_OITLayers->SetFrameAttribsSRB(RenderDelegate->GetOITPassFrameAttribsSRB());
        RP_OITLayers->Commit(pCtx);
    }
    else
    {
        UNEXPECTED("OIT layers render pass state is not set in the task context");
    }

    if (HnRenderPassState* RP_OpaqueUnselected_TransparentAll = GetRenderPassState(TaskCtx, HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll))
    {
        // Make sure opaque render pass is committed next time it is used.
        RP_OpaqueUnselected_TransparentAll->Restart();
    }
    else
    {
        UNEXPECTED("OpaqueUnselected_TransparentAll render pass state is not set in the task context");
    }
}

} // namespace USD

} // namespace Diligent
