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

#include <unordered_map>
#include <vector>

#include "Tasks/HnTask.hpp"

#include "../../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

struct HnSetupRenderingTaskParams;
struct HnPostProcessTaskParams;
struct HnRenderRprimsTaskParams;

/// Task manager implementation in Hydrogent.
class HnTaskManager
{
public:
    using TaskUID                                            = uint64_t;
    static constexpr TaskUID TaskUID_SetupRendering          = 0x8362faac57354542;
    static constexpr TaskUID TaskUID_RenderRprimsDefault     = 0x287af907f3a740a0;
    static constexpr TaskUID TaskUID_RenderRprimsMasked      = 0xf5290fec47594711;
    static constexpr TaskUID TaskUID_RenderRprimsAdditive    = 0x37d45531106c4c52;
    static constexpr TaskUID TaskUID_RenderRprimsTranslucent = 0xa015c7e45941407e;
    static constexpr TaskUID TaskUID_RenderEnvMap            = 0xf646122e1dc74bab;
    static constexpr TaskUID TaskUID_ReadRprimId             = 0x199572fe7ff144ef;
    static constexpr TaskUID TaskUID_PostProcess             = 0x1f5367e65d034500;

    HnTaskManager(pxr::HdRenderIndex& RenderIndex,
                  const pxr::SdfPath& ManagerId);

    ~HnTaskManager();

    const pxr::HdRenderIndex& GetRenderIndex() const { return m_RenderIndex; }
    pxr::HdRenderIndex&       GetRenderIndex() { return m_RenderIndex; }

    const pxr::SdfPath& GetId() const { return m_ManagerId; }

    /// Returns the list of tasks that can be passed to the Hydra engine for execution.
    ///
    /// \param [in] TaskOrder - Optional task order. If not specified, the default order is used:
    ///                         - SetupRendering
    ///                         - RenderRprimsDefault
    ///                         - RenderRprimsMasked
    ///                         - RenderEnvMap
    ///                         - RenderRprimsAdditive
    ///                         - RenderRprimsTranslucent
    ///                         - ReadRprimId
    ///                         - PostProcess
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
    void CreateTask(const pxr::TfToken& TaskId,
                    TaskUID             UID,
                    TaskParamsType&&    Params,
                    bool                Enabled = true);

    template <typename TaskParamsType>
    bool SetTaskParams(TaskUID          UID,
                       TaskParamsType&& Params);

    template <typename TaskParamsType>
    bool SetTaskParams(const pxr::SdfPath& Id,
                       TaskParamsType&&    Params);

    void SetRenderRprimParams(const HnRenderRprimsTaskParams& Params);

    void EnableTask(TaskUID UID, bool Enable);
    bool IsTaskEnabled(TaskUID UID) const;

    pxr::HdTaskSharedPtr GetTask(TaskUID UID) const;

    void RemoveTask(TaskUID UID);

    /// Returns the Id of the selected Rprim:
    /// - if no selected Rprim data is available, returns nullptr.
    /// - if no Rprim is selected, returns empty path.
    /// - if an Rprim is selected, returns the Sdf Path of the selected Rprim.
    const pxr::SdfPath* GetSelectedRprimId() const;

private:
    pxr::SdfPath GetRenderRprimsTaskId(const pxr::TfToken& MaterialTag) const;

    void CreateSetupRenderingTask();
    void CreateRenderRprimsTask(const pxr::TfToken& MaterialTag, TaskUID UID);
    void CreateRenderEnvMapTask();
    void CreateReadRprimIdTask();
    void CreatePostProcessTask();

private:
    pxr::HdRenderIndex& m_RenderIndex;
    const pxr::SdfPath  m_ManagerId;

    // Custom delegate to pass parameters to the render tasks.
    class HnTaskManager::TaskParamsDelegate final : public pxr::HdSceneDelegate
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

            VERIFY(it->second.IsHolding<T>(), "Unexpected parameter type");
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
    m_ParamsDelegate.SetParameter(TaskId, pxr::HdTokens->params, std::forward<TaskParamsType>(Params));
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
void HnTaskManager::CreateTask(const pxr::TfToken& TaskId,
                               TaskUID             UID,
                               TaskParamsType&&    Params,
                               bool                Enabled)
{
    CreateTask<TaskType>(pxr::SdfPath{GetId().AppendChild(TaskId)}, UID, std::forward<TaskParamsType>(Params), Enabled);
}

template <typename TaskParamsType>
bool HnTaskManager::SetTaskParams(const pxr::SdfPath& TaskId,
                                  TaskParamsType&&    Params)
{
    auto OldParams = m_ParamsDelegate.GetParameter<std::remove_reference<TaskParamsType>::type>(TaskId, pxr::HdTokens->params);
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
