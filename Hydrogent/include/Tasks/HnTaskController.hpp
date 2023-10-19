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
#include <unordered_map>

#include "Tasks/HnTask.hpp"

#include "DebugUtilities.hpp"

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
    using TaskUID                                      = uint64_t;
    static constexpr TaskUID TaskUID_RenderDefault     = 0x287af907f3a740a0;
    static constexpr TaskUID TaskUID_RenderMasked      = 0xf5290fec47594711;
    static constexpr TaskUID TaskUID_RenderAdditive    = 0x37d45531106c4c52;
    static constexpr TaskUID TaskUID_RenderTranslucent = 0xa015c7e45941407e;
    static constexpr TaskUID TaskUID_PostProcess       = 0x1f5367e65d034500;

    HnTaskController(pxr::HdRenderIndex& RenderIndex,
                     const pxr::SdfPath& ControllerId);

    ~HnTaskController();

    const pxr::HdRenderIndex& GetRenderIndex() const { return m_RenderIndex; }
    pxr::HdRenderIndex&       GetRenderIndex() { return m_RenderIndex; }

    const pxr::SdfPath& GetControllerId() const { return m_ControllerId; }

    const pxr::HdTaskSharedPtrVector GetTasks(const std::vector<TaskUID>* TaskOrder = nullptr) const;

    /// Sets new collection for the render tasks.
    void SetCollection(const pxr::HdRprimCollection& Collection);

    /// Sets new params for the render tasks.
    void SetRenderParams(const HnRenderTaskParams& Params);

    /// Sets new params for the post-process task.
    void SetPostProcessParams(const HnPostProcessTaskParams& Params);

    /// Sets new render tags for the render tasks.
    void SetRenderTags(const pxr::TfTokenVector& RenderTags);

    /// Sets the parameter value
    void SetParameter(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey, pxr::VtValue Value);

    template <typename TaskType>
    void CreateTask(const pxr::SdfPath& TaskId,
                    TaskUID             UID);

    pxr::HdTaskSharedPtr GetTask(TaskUID UID) const;

    void RemoveTask(TaskUID UID);

private:
    pxr::SdfPath GetRenderTaskId(const pxr::TfToken& MaterialTag) const;

    void CreateRenderTask(const pxr::TfToken& MaterialTag, TaskUID UID);
    void CreatePostProcessTask();

private:
    pxr::HdRenderIndex& m_RenderIndex;
    const pxr::SdfPath  m_ControllerId;

    // Custom delegate to pass parameters to the render tasks.
    class TaskParamsDelegate;
    std::unique_ptr<TaskParamsDelegate> m_ParamsDelegate;

    std::unordered_map<TaskUID, pxr::SdfPath> m_TaskIds;

    std::vector<TaskUID> m_DefaultTaskOrder;
};

template <typename TaskType>
void HnTaskController::CreateTask(const pxr::SdfPath& TaskId,
                                  TaskUID             UID)
{
    m_RenderIndex.InsertTask<HnRenderTask>(m_ParamsDelegate.get(), TaskId);
    auto it_inserted = m_TaskIds.emplace(UID, TaskId);
    VERIFY(!it_inserted.second, "Task with UID ", UID, " already exists: ", it_inserted.first->second.GetText());
}

} // namespace USD

} // namespace Diligent
