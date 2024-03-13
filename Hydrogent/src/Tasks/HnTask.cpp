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

#include "Tasks/HnTask.hpp"
#include "HnTokens.hpp"
#include "HnRenderBuffer.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnRenderPassState.hpp"

namespace Diligent
{

namespace USD
{

HnTask::HnTask(const pxr::SdfPath& Id) :
    pxr::HdTask{Id}
{
}

HnFrameRenderTargets* HnTask::GetFrameRenderTargets(pxr::HdTaskContext* TaskCtx) const
{
    HnFrameRenderTargets* RenderTargets = nullptr;
    _GetTaskContextData(TaskCtx, HnRenderResourceTokens->frameRenderTargets, &RenderTargets);
    return RenderTargets;
}

HnRenderPassState* HnTask::GetRenderPassState(pxr::HdTaskContext* TaskCtx, const pxr::TfToken& Name) const
{
    VERIFY(!Name.IsEmpty(), "Render pass name must not be empty");
    HnRenderPassState* RenderPassState = nullptr;
    _GetTaskContextData(TaskCtx, Name, &RenderPassState);
    return RenderPassState;
}

ITextureView* HnTask::GetRenderBufferTarget(pxr::HdRenderIndex& RenderIndex, const pxr::SdfPath& RenderBufferId)
{
    VERIFY(!RenderBufferId.IsEmpty(), "Render buffer Id must not be empty");
    HnRenderBuffer* RenderBuffer = static_cast<HnRenderBuffer*>(RenderIndex.GetBprim(pxr::HdPrimTypeTokens->renderBuffer, RenderBufferId));
    if (RenderBuffer == nullptr)
    {
        UNEXPECTED("Render buffer Bprim is not set at Id ", RenderBufferId);
        return nullptr;
    }

    return RenderBuffer->GetTarget();
}

ITextureView* HnTask::GetRenderBufferTarget(pxr::HdRenderIndex& RenderIndex, pxr::HdTaskContext* TaskCtx, const pxr::TfToken& Name) const
{
    VERIFY(!Name.IsEmpty(), "Parameter name must not be empty");
    pxr::SdfPath RenderBufferId;
    if (GetTaskContextData(TaskCtx, Name, RenderBufferId))
    {
        return GetRenderBufferTarget(RenderIndex, RenderBufferId);
    }
    else
    {
        return nullptr;
    }
}

} // namespace USD

} // namespace Diligent
