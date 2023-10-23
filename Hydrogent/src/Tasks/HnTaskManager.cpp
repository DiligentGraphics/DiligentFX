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

#include "Tasks/HnTaskManager.hpp"

#include <atomic>
#include <array>

#include "Tasks/HnSetupRenderingTask.hpp"
#include "Tasks/HnRenderRprimsTask.hpp"
#include "Tasks/HnRenderEnvMapTask.hpp"
#include "Tasks/HnReadRprimIdTask.hpp"
#include "Tasks/HnPostProcessTask.hpp"
#include "HnTokens.hpp"
#include "HashUtils.hpp"
#include "HnRenderDelegate.hpp"

namespace Diligent
{

namespace USD
{

namespace
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnTaskManagerTokens,

    (setupRendering)
    (renderEnvMapTask)
    (readRprimIdTask)
    (postProcessTask)

    (renderBufferDescriptor)
    (renderTags)
);
// clang-format on

} // namespace


HnTaskManager::TaskParamsDelegate::ParamKey::ParamKey(const pxr::SdfPath& _Path, const pxr::TfToken& _ValueKey) :
    Path{_Path},
    ValueKey{_ValueKey},
    Hash{ComputeHash(pxr::SdfPath::Hash{}(_Path), pxr::TfToken::HashFunctor{}(_ValueKey))}
{}

HnTaskManager::TaskParamsDelegate::TaskParamsDelegate(pxr::HdRenderIndex& Index,
                                                      const pxr::SdfPath& Id) :
    pxr::HdSceneDelegate{&Index, Id}
{}

HnTaskManager::TaskParamsDelegate::~TaskParamsDelegate()
{
}

bool HnTaskManager::TaskParamsDelegate::HasParameter(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey) const
{
    return m_ParamsCache.find({Id, ValueKey}) != m_ParamsCache.end();
}

pxr::VtValue HnTaskManager::TaskParamsDelegate::Get(const pxr::SdfPath& Id, const pxr::TfToken& ValueKey)
{
    auto it = m_ParamsCache.find({Id, ValueKey});
    return it != m_ParamsCache.end() ? it->second : pxr::VtValue{};
}

pxr::GfMatrix4d HnTaskManager::TaskParamsDelegate::GetTransform(const pxr::SdfPath& Id)
{
    auto it = m_ParamsCache.find({Id, pxr::HdTokens->transform});
    return it != m_ParamsCache.end() ? it->second.Get<pxr::GfMatrix4d>() : pxr::GfMatrix4d{1.0};
}

pxr::VtValue HnTaskManager::TaskParamsDelegate::GetLightParamValue(const pxr::SdfPath& Id,
                                                                   const pxr::TfToken& ParamName)
{
    return Get(Id, ParamName);
}

bool HnTaskManager::TaskParamsDelegate::IsEnabled(const pxr::TfToken& Option) const
{
    return HdSceneDelegate::IsEnabled(Option);
}

pxr::HdRenderBufferDescriptor HnTaskManager::TaskParamsDelegate::GetRenderBufferDescriptor(const pxr::SdfPath& Id)
{
    if (HasParameter(Id, HnTaskManagerTokens->renderBufferDescriptor))
    {
        return GetParameter<pxr::HdRenderBufferDescriptor>(Id, HnTaskManagerTokens->renderBufferDescriptor);
    }
    return pxr::HdRenderBufferDescriptor{};
}

pxr::TfTokenVector HnTaskManager::TaskParamsDelegate::GetTaskRenderTags(const pxr::SdfPath& TaskId)
{
    if (HasParameter(TaskId, HnTaskManagerTokens->renderTags))
    {
        return GetParameter<pxr::TfTokenVector>(TaskId, HnTaskManagerTokens->renderTags);
    }
    return pxr::TfTokenVector{};
}


HnTaskManager::HnTaskManager(pxr::HdRenderIndex& RenderIndex,
                             const pxr::SdfPath& ManagerId) :
    m_RenderIndex{RenderIndex},
    m_ManagerId{ManagerId},
    m_ParamsDelegate{RenderIndex, ManagerId}
{
    // Task creation order defines the default task order
    CreateSetupRenderingTask();
    CreateRenderRprimsTask(HnMaterialTagTokens->defaultTag, TaskUID_RenderRprimsDefault);
    CreateRenderRprimsTask(HnMaterialTagTokens->masked, TaskUID_RenderRprimsMasked);
    CreateRenderEnvMapTask();
    CreateRenderRprimsTask(HnMaterialTagTokens->additive, TaskUID_RenderRprimsAdditive);
    CreateRenderRprimsTask(HnMaterialTagTokens->translucent, TaskUID_RenderRprimsTranslucent);
    CreateReadRprimIdTask();
    CreatePostProcessTask();
}

HnTaskManager::~HnTaskManager()
{
    // Remove all tasks from the render index
    for (const auto& it : m_TaskUIDs)
    {
        m_RenderIndex.RemoveTask(it.second);
    }
    m_TaskUIDs.clear();
}

pxr::HdTaskSharedPtr HnTaskManager::GetTask(TaskUID UID) const
{
    auto it = m_TaskUIDs.find(UID);
    return it != m_TaskUIDs.end() ?
        m_RenderIndex.GetTask(it->second) :
        nullptr;
}

void HnTaskManager::RemoveTask(TaskUID UID)
{
    auto it = m_TaskUIDs.find(UID);
    if (it == m_TaskUIDs.end())
        return;

    m_RenderIndex.RemoveTask(it->second);
    m_TaskUIDs.erase(it);
}

void HnTaskManager::SetParameter(const pxr::SdfPath& TaskId, const TfToken& ValueKey, pxr::VtValue Value)
{
    m_ParamsDelegate.SetParameter(TaskId, ValueKey, std::move(Value));
}

void HnTaskManager::CreateSetupRenderingTask()
{
    HnSetupRenderingTaskParams TaskParams;
    CreateTask<HnSetupRenderingTask>(HnTaskManagerTokens->setupRendering, TaskUID_SetupRendering, TaskParams);
}

pxr::SdfPath HnTaskManager::GetRenderRprimsTaskId(const pxr::TfToken& MaterialTag) const
{
    std::string Id = std::string{"RenderRprimsTask_"} + MaterialTag.GetString();
    std::replace(Id.begin(), Id.end(), ':', '_');
    return GetId().AppendChild(TfToken{Id});
}

void HnTaskManager::CreateRenderRprimsTask(const pxr::TfToken& MaterialTag, TaskUID UID)
{
    HnRenderRprimsTaskParams TaskParams;
    pxr::SdfPath             RenderRprimsTaskId = GetRenderRprimsTaskId(MaterialTag);
    // Note that we pass the delegate to the scene index. This delegate will be passed
    // to the task's Sync() method.
    CreateTask<HnRenderRprimsTask>(RenderRprimsTaskId, UID, TaskParams);


    pxr::HdRprimCollection Collection{
        pxr::HdTokens->geometry,
        pxr::HdReprSelector{pxr::HdReprTokens->hull},
        false, // forcedRepr
        MaterialTag,
    };
    Collection.SetRootPath(pxr::SdfPath::AbsoluteRootPath());

    pxr::TfTokenVector RenderTags = {pxr::HdRenderTagTokens->geometry};

    m_ParamsDelegate.SetParameter(RenderRprimsTaskId, pxr::HdTokens->params, TaskParams);
    m_ParamsDelegate.SetParameter(RenderRprimsTaskId, pxr::HdTokens->collection, Collection);
    m_ParamsDelegate.SetParameter(RenderRprimsTaskId, pxr::HdTokens->renderTags, RenderTags);

    m_RenderTaskIds.emplace_back(RenderRprimsTaskId);
}

void HnTaskManager::SetRenderRprimParams(const HnRenderRprimsTaskParams& Params)
{
    for (const auto& TaskId : m_RenderTaskIds)
    {
        SetTaskParams(TaskId, Params);
    }
}

void HnTaskManager::CreatePostProcessTask()
{
    HnPostProcessTaskParams TaskParams;
    CreateTask<HnPostProcessTask>(HnTaskManagerTokens->postProcessTask, TaskUID_PostProcess, TaskParams);
}

void HnTaskManager::CreateRenderEnvMapTask()
{
    HnRenderEnvMapTaskParams TaskParams;
    CreateTask<HnRenderEnvMapTask>(HnTaskManagerTokens->renderEnvMapTask, TaskUID_RenderEnvMap, TaskParams);
}

void HnTaskManager::CreateReadRprimIdTask()
{
    HnReadRprimIdTaskParams TaskParams;
    CreateTask<HnReadRprimIdTask>(HnTaskManagerTokens->readRprimIdTask, TaskUID_ReadRprimId, TaskParams);
}

const pxr::HdTaskSharedPtrVector HnTaskManager::GetTasks(const std::vector<TaskUID>* TaskOrder) const
{
    if (TaskOrder == nullptr)
        TaskOrder = &m_DefaultTaskOrder;

    pxr::HdTaskSharedPtrVector Tasks;
    for (auto UID : *TaskOrder)
    {
        auto it = m_TaskUIDs.find(UID);
        if (it == m_TaskUIDs.end())
            continue;

        Tasks.push_back(m_RenderIndex.GetTask(it->second));
    }

    return Tasks;
}

void HnTaskManager::SetCollection(const pxr::HdRprimCollection& Collection)
{
    pxr::HdRprimCollection NewCollection = Collection;
    for (const auto& TaskId : m_RenderTaskIds)
    {
        pxr::HdRprimCollection OldCollection = m_ParamsDelegate.GetParameter<pxr::HdRprimCollection>(TaskId, pxr::HdTokens->collection);

        const pxr::TfToken& OldMaterialTag = OldCollection.GetMaterialTag();
        NewCollection.SetMaterialTag(OldMaterialTag);

        if (OldCollection == NewCollection)
            continue;

        m_ParamsDelegate.SetParameter(TaskId, pxr::HdTokens->collection, NewCollection);
        m_RenderIndex.GetChangeTracker().MarkTaskDirty(TaskId, pxr::HdChangeTracker::DirtyCollection);
    }
}

void HnTaskManager::SetRenderTags(const pxr::TfTokenVector& RenderTags)
{
    for (const auto& TaskId : m_RenderTaskIds)
    {
        pxr::TfTokenVector OldRenderTags = m_ParamsDelegate.GetParameter<pxr::TfTokenVector>(TaskId, pxr::HdTokens->renderTags);
        if (OldRenderTags == RenderTags)
            continue;

        m_ParamsDelegate.SetParameter(TaskId, pxr::HdTokens->renderTags, RenderTags);
        m_RenderIndex.GetChangeTracker().MarkTaskDirty(TaskId, pxr::HdChangeTracker::DirtyRenderTags);
    }
}

const pxr::SdfPath* HnTaskManager::GetSelectedRprimId() const
{
    pxr::HdTaskSharedPtr pReadRprimIdTask = GetTask(TaskUID_ReadRprimId);
    if (!pReadRprimIdTask)
        return nullptr;

    HnReadRprimIdTask& ReadRprimIdTask = static_cast<HnReadRprimIdTask&>(*pReadRprimIdTask);
    const Uint32       MeshIdx         = ReadRprimIdTask.GetMeshIndex();
    if (MeshIdx == HnReadRprimIdTask::InvalidMeshIndex)
    {
        static const pxr::SdfPath EmptyPath;
        return &EmptyPath;
    }
    else
    {
        return static_cast<const HnRenderDelegate*>(GetRenderIndex().GetRenderDelegate())->GetMeshPrimId(MeshIdx);
    }
}

} // namespace USD

} // namespace Diligent
