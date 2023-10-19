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

#pragma once

#include <memory>
#include <array>

#include "Tasks/HnTask.hpp"

namespace Diligent
{

namespace USD
{

struct HnRenderTaskParams;
struct HnPostProcessTaskParams;

/// Task controller implementation in Hydrogent.
class HnTaskController
{
public:
    HnTaskController(pxr::HdRenderIndex& RenderIndex,
                     const pxr::SdfPath& ControllerId);

    ~HnTaskController();

    const pxr::HdRenderIndex& GetRenderIndex() const { return m_RenderIndex; }
    pxr::HdRenderIndex&       GetRenderIndex() { return m_RenderIndex; }

    const pxr::SdfPath& GetControllerId() const { return m_ControllerId; }

    const pxr::HdTaskSharedPtrVector GetRenderingTasks() const;

    /// Sets new collection for the render tasks.
    void SetCollection(const pxr::HdRprimCollection& Collection);

    /// Sets new params for the render tasks.
    void SetRenderParams(const HnRenderTaskParams& Params);

    /// Sets new params for the post-process task.
    void SetPostProcessParams(const HnPostProcessTaskParams& Params);

    /// Sets new render tags for the render tasks.
    void SetRenderTags(const pxr::TfTokenVector& RenderTags);

private:
    pxr::SdfPath GetRenderTaskId(const pxr::TfToken& MaterialTag) const;
    pxr::SdfPath CreateRenderTask(const pxr::TfToken& MaterialTag);
    pxr::SdfPath CreatePostProcessTask();

private:
    pxr::HdRenderIndex& m_RenderIndex;
    const pxr::SdfPath  m_ControllerId;

    // Custom delegate to pass parameters to the render tasks.
    class TaskParamsDelegate;
    std::unique_ptr<TaskParamsDelegate> m_ParamsDelegate;

    pxr::SdfPathVector m_RenderTaskIds;

    enum TASK_ID
    {
        TASK_ID_POST_PROCESS,
        TASK_ID_COUNT
    };
    std::array<pxr::SdfPath, TASK_ID_COUNT> m_TaskIds;
};

} // namespace USD

} // namespace Diligent
