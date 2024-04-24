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

#pragma once

#include <unordered_map>
#include <vector>

#include "Tasks/HnTask.hpp"

#include "../../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

struct HnBeginFrameTaskParams;
struct HnRenderRprimsTaskParams;
struct HnPostProcessTaskParams;
struct HnRenderPassParams;
struct HnReadRprimIdTaskParams;
struct HnRenderBoundBoxTaskParams;

/// Task manager implementation in Hydrogent.
class HnTaskManager
{
public:
    using TaskUID                                                    = uint64_t;
    static constexpr TaskUID TaskUID_BeginFrame                      = 0x8362faac57354542;
    static constexpr TaskUID TaskUID_RenderShadows                   = 0x511e003b7a584315;
    static constexpr TaskUID TaskUID_BeginMainPass                   = 0xbdd00156269447a9;
    static constexpr TaskUID TaskUID_RenderRprimsDefaultSelected     = 0x1cdf84fa9ab5423e;
    static constexpr TaskUID TaskUID_RenderRprimsMaskedSelected      = 0xe926da1de43d4f47;
    static constexpr TaskUID TaskUID_CopySelectionDepth              = 0xf3026cea7404c64a;
    static constexpr TaskUID TaskUID_RenderRprimsDefaultUnselected   = 0x287af907f3a740a0;
    static constexpr TaskUID TaskUID_RenderRprimsMaskedUnselected    = 0xf5290fec47594711;
    static constexpr TaskUID TaskUID_RenderRprimsAdditive            = 0x37d45531106c4c52;
    static constexpr TaskUID TaskUID_RenderRprimsTranslucent         = 0xa015c7e45941407e;
    static constexpr TaskUID TaskUID_RenderRprimsAdditiveSelected    = 0x2cb8a35254ec46da;
    static constexpr TaskUID TaskUID_RenderRprimsTranslucentSelected = 0x50a786394d834b4f;
    static constexpr TaskUID TaskUID_RenderEnvMap                    = 0xf646122e1dc74bab;
    static constexpr TaskUID TaskUID_RenderBoundBox                  = 0x1e7e47f37e6445b4;
    static constexpr TaskUID TaskUID_ReadRprimId                     = 0x199572fe7ff144ef;
    static constexpr TaskUID TaskUID_ProcessSelection                = 0x87ef181ec6d4cf83;
    static constexpr TaskUID TaskUID_PostProcess                     = 0x1f5367e65d034500;

    HnTaskManager(pxr::HdRenderIndex& RenderIndex,
                  const pxr::SdfPath& ManagerId);

    ~HnTaskManager();

    const pxr::HdRenderIndex& GetRenderIndex() const { return m_RenderIndex; }
    pxr::HdRenderIndex&       GetRenderIndex() { return m_RenderIndex; }

    const pxr::SdfPath& GetId() const { return m_ManagerId; }

    /// Returns the list of tasks that can be passed to the Hydra engine for execution.
    ///
    /// \param [in] TaskOrder - Optional task order. If not specified, the following default order is used:
    ///                         - BeginFrame
    ///                             * Prepares render targets and other frame resources
    ///                         - BeginMainPass
    ///                             * Binds the Color and Mesh Id render targes and the the selection depth buffer
    ///                         - RenderShadows
    ///                         - RenderRprimsDefaultSelected
    ///                             * Renders only selected Rprims with the default material tag
    ///                         - RenderRprimsMaskedSelected
    ///                             * Renders only selected Rprims with the masked material tag
    /// 						- CopySelectionDepth
    ///                             * Copies the selection depth buffer to the main depth buffer
    ///                             * Binds the Color and Mesh Id render targes and the main depth buffer
    ///                         - RenderRprimsDefaultUnselected
    ///                             * Renders only unselected Rprims with the default material tag
    ///                         - RenderRprimsMaskedUnselected
    ///                             * Renders only unselected Rprims with the masked material tag
    ///                         - RenderEnvMap
    ///                         - RenderBoundBox
    ///                         - RenderRprimsAdditive
    ///                             * Renders all Rprims with additive material tag
    ///                         - RenderRprimsTranslucent
    ///                             * Renders all Rprims with translucent material tag
    ///                         - RenderRprimsAdditiveSelected
    ///                             * Renders only selected Rprims with the additive material tag (depth only)
    ///                         - RenderRprimsTranslucentSelected
    ///                             * Renders only selected Rprims with the translucent material tag (depth only)
    ///                         - ReadRprimId
    ///                         - ProcessSelection
    ///                             * Generates the closest selected location texture using the Jump-Flood algorithm
    ///                         - PostProcess
    ///
    ///     | Task                            |  Selected Rprims | Unselected Rprims | Color  |  Mesh ID  | G-Buffer |  Selection Detph | Main Depth |
    ///     |---------------------------------|------------------|-------------------|--------|-----------|----------|------------------|------------|
    ///     | BeginFrame                      |                  |                   |        |           |          |                  |            |
    ///     | BeginMainPass                   |                  |                   |        |           |          |                  |            |
    ///     | RenderShadows                   |                  |                   |        |           |          |                  |            |
    ///     | RenderRprimsDefaultSelected     |       V          |                   |   V    |     V     |    V     |        V         |            |
    ///     | RenderRprimsMaskedSelected      |       V          |                   |   V    |     V     |    V     |        V         |            |
    ///     | CopySelectionDepth              |                  |                   |        |           |          |        V---copy--|---->V      |
    ///     | RenderRprimsDefaultUnselected   |                  |         V         |   V    |     V     |    V     |                  |     V      |
    ///     | RenderRprimsMaskedUnselected    |                  |         V         |   V    |     V     |    V     |                  |     V      |
    ///     | RenderEnvMap                    |                  |                   |   V    |           |          |                  |            |
    ///     | RenderBoundBox                  |                  |                   |   V    |           |          |                  |            |
    ///     | RenderRprimsAdditive            |       V          |         V         |   V    |     V     |    V     |                  |     V      |
    ///     | RenderRprimsTranslucent         |       V          |         V         |   V    |     V     |    V     |                  |     V      |
    ///     | RenderRprimsAdditiveSelected    |       V          |                   |        |           |          |        V         |            |
    ///     | RenderRprimsTranslucentSelected |       V          |                   |        |           |          |        V         |            |
    ///     | ReadRprimId                     |                  |                   |        |           |          |                  |            |
    ///     | ProcessSelection                |                  |                   |        |           |          |                  |            |
    ///     | PostProcess                     |                  |                   |        |           |          |                  |            |
    ///
    /// \return The list of tasks that can be passed to pxr::HdEngine::Execute.
    ///
    /// \remarks Only enabled tasks are returned.
    const pxr::HdTaskSharedPtrVector GetTasks(const std::vector<TaskUID>* TaskOrder = nullptr) const;

    /// Sets new collection for the render tasks.
    void SetCollection(const pxr::HdRprimCollection& Collection);

    /// Sets new render tags for the render tasks.
    void SetRenderTags(const pxr::TfTokenVector& RenderTags);

    /// Sets the parameter value
    void SetParameter(const pxr::SdfPath& TaskId, const pxr::TfToken& ValueKey, pxr::VtValue Value);

    /// Sets the parameter value
    template <typename ParamterType>
    void SetParameter(const pxr::SdfPath& TaskId, const pxr::TfToken& ValueKey, ParamterType&& Value);

    /// Sets the parameter value
    template <typename ParamterType>
    void SetParameter(const pxr::TfToken& TaskName, const pxr::TfToken& ValueKey, ParamterType&& Value);

    /// Creates a new render task.
    /// \param [in] TaskId - The task ID that will be used to register the task in the render index.
    /// \param [in] UID    - The task UID that will be used to identify the task in the task manager.
    /// \param [in] Params - The task parameters that will be associated with the task using the task ID.
    /// \param [in] Enabled - Whether the task is enabled.
    template <typename TaskType, typename TaskParamsType>
    void CreateTask(const pxr::SdfPath& TaskId,
                    TaskUID             UID,
                    TaskParamsType&&    Params,
                    bool                Enabled = true);

    /// Creates a new render task.
    ///
    /// This is method is similar to the one above, but it automatically appends the task ID
    /// as child of the manager ID.
    template <typename TaskType, typename TaskParamsType>
    void CreateTask(const pxr::TfToken& TaskName,
                    TaskUID             UID,
                    TaskParamsType&&    Params,
                    bool                Enabled = true);

    template <typename TaskParamsType>
    bool SetTaskParams(TaskUID          UID,
                       TaskParamsType&& Params);

    template <typename TaskParamsType>
    bool SetTaskParams(const pxr::SdfPath& Id,
                       TaskParamsType&&    Params);

    void SetFrameParams(const HnBeginFrameTaskParams& Params);
    void SetRenderRprimParams(const HnRenderRprimsTaskParams& Params);
    void SetPostProcessParams(const HnPostProcessTaskParams& Params);
    void SetReadRprimIdParams(const HnReadRprimIdTaskParams& Params);
    void SetRenderBoundBoxParams(const HnRenderBoundBoxTaskParams& Params);

    void EnableTask(TaskUID UID, bool Enable);
    bool IsTaskEnabled(TaskUID UID) const;

    pxr::HdTaskSharedPtr GetTask(TaskUID UID) const;

    template <typename TaskType>
    TaskType* GetTask(TaskUID UID) const;

    void RemoveTask(TaskUID UID);

    /// Returns the Id of the selected Rprim:
    /// - if no selected Rprim data is available, returns nullptr.
    /// - if no Rprim is selected, returns empty path.
    /// - if an Rprim is selected, returns the Sdf Path of the selected Rprim.
    const pxr::SdfPath* GetSelectedRPrimId() const;

    /// Enables or disables the tasks associated with the specified material tag.
    void EnableMaterial(const pxr::TfToken& MaterialTag, bool Enable);

    /// Enables or disables environment map rendering.
    void EnableEnvironmentMap(bool Enable);

    /// Returns true if environment map rendering is enabled.
    bool IsEnvironmentMapEnabled() const;

    /// Returns true if the tasks associated with the specified material tag are enabled.
    bool IsMaterialEnabled(const pxr::TfToken& MaterialTag) const;

    /// Enables or disables the rendering of the selected Rprim's bounding box.
    void EnableSelectedPrimBoundBox(bool Enable);

    /// Returns true if the rendering of the selected Rprim's bounding box is enabled.
    bool IsSelectedPrimBoundBoxEnabled() const;

    /// Resets temporal anti-aliasing.
    void ResetTAA();

    /// Suspends temporal super-sampling.
    void SuspededSuperSampling();

private:
    pxr::SdfPath GetTaskId(const pxr::TfToken& TaskName) const;
    pxr::SdfPath GetRenderRprimsTaskId(const pxr::TfToken& MaterialTag, const HnRenderPassParams& RenderPassParams) const;

    void CreateBeginFrameTask();
    void CreateBeginMainPassTask();
    void CreateRenderShadowsTask();
    void CreateRenderRprimsTask(const pxr::TfToken& MaterialTag, TaskUID UID, const HnRenderPassParams& RenderPassParams);
    void CreateRenderEnvMapTask(const pxr::TfToken& RenderPassName);
    void CreateRenderBoundBoxTask(const pxr::TfToken& RenderPassName);
    void CreateReadRprimIdTask();
    void CreateCopySelectionDepthTask();
    void CreateProcessSelectionTask();
    void CreatePostProcessTask();

private:
    pxr::HdRenderIndex& m_RenderIndex;
    const pxr::SdfPath  m_ManagerId;

    // Custom delegate to pass parameters to the render tasks.
    class TaskParamsDelegate final : public pxr::HdSceneDelegate
    {
    public:
        TaskParamsDelegate(pxr::HdRenderIndex& Index,
                           const pxr::SdfPath& Id);
        ~TaskParamsDelegate() override final;

        template <typename T>
        void SetParameter(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey, T&& Value)
        {
            m_ParamsCache[{Id, ValueKey}] = std::forward<T>(Value);
        }

        template <typename T>
        T GetParameter(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey) const
        {
            auto it = m_ParamsCache.find({Id, ValueKey});
            if (it == m_ParamsCache.end())
            {
                UNEXPECTED("Parameter ", ValueKey, " is not set for ", Id);
                return {};
            }

            VERIFY(it->second.IsHolding<T>(), "Unexpected parameter type: ", it->second.GetTypeName());
            return it->second.Get<T>();
        }

        virtual bool HasParameter(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey) const;

        virtual pxr::VtValue Get(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey) override final;

        virtual pxr::GfMatrix4d GetTransform(const pxr::SdfPath& Id) override final;

        virtual pxr::VtValue GetLightParamValue(const pxr::SdfPath& Id, const pxr::TfToken& ParamName) override final;

        virtual bool IsEnabled(const pxr::TfToken& Option) const override final;

        virtual pxr::HdRenderBufferDescriptor GetRenderBufferDescriptor(const pxr::SdfPath& Id) override final;

        virtual pxr::TfTokenVector GetTaskRenderTags(const pxr::SdfPath& TaskId) override final;

    private:
        struct ParamKey
        {
            const pxr::SdfPath Path;
            const pxr::TfToken ValueKey;
            const size_t       Hash;

            ParamKey(const pxr::SdfPath& _Path, const pxr::TfToken& _ValueKey);

            bool operator==(const ParamKey& rhs) const
            {
                return Hash && rhs.Hash && Path == rhs.Path && ValueKey == rhs.ValueKey;
            }

            struct Hasher
            {
                size_t operator()(const ParamKey& Key) const
                {
                    return Key.Hash;
                }
            };
        };

        std::unordered_map<ParamKey, pxr::VtValue, ParamKey::Hasher> m_ParamsCache;
    };
    TaskParamsDelegate m_ParamsDelegate;

    struct TaskInfo
    {
        pxr::SdfPath Id;
        bool         Enabled = false;
    };
    std::unordered_map<TaskUID, TaskInfo> m_TaskInfo;

    std::vector<TaskUID>      m_DefaultTaskOrder;
    std::vector<pxr::SdfPath> m_RenderTaskIds;
};

template <typename ParamterType>
void HnTaskManager::SetParameter(const pxr::SdfPath& TaskId, const pxr::TfToken& ValueKey, ParamterType&& Value)
{
    m_ParamsDelegate.SetParameter(TaskId, ValueKey, std::forward<ParamterType>(Value));
}

template <typename ParamterType>
void HnTaskManager::SetParameter(const pxr::TfToken& TaskName, const pxr::TfToken& ValueKey, ParamterType&& Value)
{
    m_ParamsDelegate.SetParameter(GetTaskId(TaskName), ValueKey, std::forward<ParamterType>(Value));
}

template <typename TaskType, typename TaskParamsType>
void HnTaskManager::CreateTask(const pxr::SdfPath& TaskId,
                               TaskUID             UID,
                               TaskParamsType&&    Params,
                               bool                Enabled)
{
    m_RenderIndex.InsertTask<TaskType>(&m_ParamsDelegate, TaskId);
    auto it_inserted = m_TaskInfo.emplace(UID, TaskInfo{TaskId, Enabled});
    VERIFY(it_inserted.second, "Task with UID ", UID, " already exists: ", it_inserted.first->second.Id.GetText());

    m_ParamsDelegate.SetParameter(TaskId, pxr::HdTokens->params, std::forward<TaskParamsType>(Params));
    m_DefaultTaskOrder.emplace_back(UID);
}

template <typename TaskType, typename TaskParamsType>
void HnTaskManager::CreateTask(const pxr::TfToken& TaskName,
                               TaskUID             UID,
                               TaskParamsType&&    Params,
                               bool                Enabled)
{
    CreateTask<TaskType>(GetTaskId(TaskName), UID, std::forward<TaskParamsType>(Params), Enabled);
}

template <typename TaskType>
TaskType* HnTaskManager::GetTask(TaskUID UID) const
{
    pxr::HdTaskSharedPtr Task = GetTask(UID);
    return Task ? static_cast<TaskType*>(Task.get()) : nullptr;
}

template <typename TaskParamsType>
bool HnTaskManager::SetTaskParams(const pxr::SdfPath& TaskId,
                                  TaskParamsType&&    Params)
{
    auto OldParams = m_ParamsDelegate.GetParameter<typename std::remove_reference<TaskParamsType>::type>(TaskId, pxr::HdTokens->params);
    if (OldParams == Params)
        return false;

    m_ParamsDelegate.SetParameter(TaskId, pxr::HdTokens->params, std::forward<TaskParamsType>(Params));
    m_RenderIndex.GetChangeTracker().MarkTaskDirty(TaskId, pxr::HdChangeTracker::DirtyParams);

    return true;
}

template <typename TaskParamsType>
bool HnTaskManager::SetTaskParams(TaskUID          UID,
                                  TaskParamsType&& Params)
{
    const auto it = m_TaskInfo.find(UID);
    if (it == m_TaskInfo.end())
        return false;

    return SetTaskParams(it->second.Id, std::forward<TaskParamsType>(Params));
}

} // namespace USD

} // namespace Diligent
