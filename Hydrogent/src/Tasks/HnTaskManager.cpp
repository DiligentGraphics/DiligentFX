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

#include "Tasks/HnTaskManager.hpp"

#include <atomic>
#include <array>

#include "Tasks/HnBeginFrameTask.hpp"
#include "Tasks/HnRenderShadowsTask.hpp"
#include "Tasks/HnBeginMainPassTask.hpp"
#include "Tasks/HnRenderRprimsTask.hpp"
#include "Tasks/HnCopySelectionDepthTask.hpp"
#include "Tasks/HnRenderEnvMapTask.hpp"
#include "Tasks/HnRenderBoundBoxTask.hpp"
#include "Tasks/HnReadRprimIdTask.hpp"
#include "Tasks/HnPostProcessTask.hpp"
#include "Tasks/HnProcessSelectionTask.hpp"
#include "HnTokens.hpp"
#include "HashUtils.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPass.hpp"

namespace Diligent
{

namespace USD
{

namespace
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnTaskManagerTokens,

    (beginFrameTask)
    (renderShadowsTask)
    (beginMainPassTask)
    (copySelectionDepthTask)
    (renderEnvMapTask)
    (renderBoundBoxTask)
    (readRprimIdTask)
    (processSelectionTask)
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
    CreateBeginFrameTask();
    if (static_cast<const HnRenderDelegate*>(RenderIndex.GetRenderDelegate())->GetShadowMapManager())
    {
        CreateRenderShadowsTask();
    }
    CreateBeginMainPassTask();

    // Opaque selected RPrims -> {GBuffer + SelectionDepth}
    CreateRenderRprimsTask(HnMaterialTagTokens->defaultTag,
                           TaskUID_RenderRprimsDefaultSelected,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueSelected,
                               HnRenderPassParams::SelectionType::Selected,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });
    CreateRenderRprimsTask(HnMaterialTagTokens->masked,
                           TaskUID_RenderRprimsMaskedSelected,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueSelected,
                               HnRenderPassParams::SelectionType::Selected,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });

    CreateCopySelectionDepthTask();

    // Opaque unselected RPrims and all transparent RPrims -> {GBuffer + MainDepth}
    CreateRenderRprimsTask(HnMaterialTagTokens->defaultTag,
                           TaskUID_RenderRprimsDefaultUnselected,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll,
                               HnRenderPassParams::SelectionType::Unselected,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });
    CreateRenderRprimsTask(HnMaterialTagTokens->masked,
                           TaskUID_RenderRprimsMaskedUnselected,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll,
                               HnRenderPassParams::SelectionType::Unselected,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });
    CreateRenderEnvMapTask(HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll);
    CreateRenderBoundBoxTask(HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll);
    CreateRenderRprimsTask(HnMaterialTagTokens->additive,
                           TaskUID_RenderRprimsAdditive,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll,
                               HnRenderPassParams::SelectionType::All,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });
    CreateRenderRprimsTask(HnMaterialTagTokens->translucent,
                           TaskUID_RenderRprimsTranslucent,
                           {
                               HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll,
                               HnRenderPassParams::SelectionType::All,
                               USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS,
                           });

    // Transparent selected RPrims  -> {0 + SelectionDepth}
    CreateRenderRprimsTask(HnMaterialTagTokens->additive,
                           TaskUID_RenderRprimsAdditiveSelected,
                           {
                               HnRenderResourceTokens->renderPass_TransparentSelected,
                               HnRenderPassParams::SelectionType::Selected,
                               USD_Renderer::USD_PSO_FLAG_NONE,
                           });
    CreateRenderRprimsTask(HnMaterialTagTokens->translucent,
                           TaskUID_RenderRprimsTranslucentSelected,
                           {
                               HnRenderResourceTokens->renderPass_TransparentSelected,
                               HnRenderPassParams::SelectionType::Selected,
                               USD_Renderer::USD_PSO_FLAG_NONE,
                           });

    CreateReadRprimIdTask();
    CreateProcessSelectionTask();
    CreatePostProcessTask();
}

HnTaskManager::~HnTaskManager()
{
    // Remove all tasks from the render index
    for (const auto& it : m_TaskInfo)
    {
        m_RenderIndex.RemoveTask(it.second.Id);
    }
    m_TaskInfo.clear();
}

pxr::HdTaskSharedPtr HnTaskManager::GetTask(TaskUID UID) const
{
    auto it = m_TaskInfo.find(UID);
    return it != m_TaskInfo.end() ?
        m_RenderIndex.GetTask(it->second.Id) :
        nullptr;
}

void HnTaskManager::RemoveTask(TaskUID UID)
{
    auto it = m_TaskInfo.find(UID);
    if (it == m_TaskInfo.end())
        return;

    m_RenderIndex.RemoveTask(it->second.Id);
    m_TaskInfo.erase(it);
}

void HnTaskManager::SetParameter(const pxr::SdfPath& TaskId, const TfToken& ValueKey, pxr::VtValue Value)
{
    m_ParamsDelegate.SetParameter(TaskId, ValueKey, std::move(Value));
}

void HnTaskManager::CreateBeginFrameTask()
{
    HnBeginFrameTaskParams TaskParams;
    CreateTask<HnBeginFrameTask>(HnTaskManagerTokens->beginFrameTask, TaskUID_BeginFrame, TaskParams);
}

void HnTaskManager::CreateRenderShadowsTask()
{
    const USD_Renderer& Renderer = *static_cast<const HnRenderDelegate*>(GetRenderIndex().GetRenderDelegate())->GetUSDRenderer();

    HnRenderShadowsTaskParams TaskParams;
    TaskParams.State.DepthBiasEnabled     = true;
    TaskParams.State.SlopeScaledDepthBias = Renderer.GetSettings().PCFKernelSize * 0.5f + 0.5f;
    CreateTask<HnRenderShadowsTask>(HnTaskManagerTokens->renderShadowsTask, TaskUID_RenderShadows, TaskParams);

    // Only render shadows from default material tfor now
    pxr::HdRprimCollection Collection{
        pxr::HdTokens->geometry,
        pxr::HdReprSelector{pxr::HdReprTokens->hull},
        false, // forcedRepr
        HnMaterialTagTokens->defaultTag,
    };
    Collection.SetRootPath(pxr::SdfPath::AbsoluteRootPath());

    pxr::TfTokenVector RenderTags = {pxr::HdRenderTagTokens->geometry};

    HnRenderPassParams RPParams{
        HnRenderResourceTokens->renderPass_Shadow,
        HnRenderPassParams::SelectionType::All,
        USD_Renderer::USD_PSO_FLAG_NONE,
    };

    SetParameter(HnTaskManagerTokens->renderShadowsTask, pxr::HdTokens->collection, Collection);
    SetParameter(HnTaskManagerTokens->renderShadowsTask, pxr::HdTokens->renderTags, RenderTags);
    SetParameter(HnTaskManagerTokens->renderShadowsTask, HnTokens->renderPassParams, RPParams);
}

void HnTaskManager::CreateBeginMainPassTask()
{
    HnBeginMainPassTaskParams TaskParams;
    CreateTask<HnBeginMainPassTask>(HnTaskManagerTokens->beginMainPassTask, TaskUID_BeginMainPass, TaskParams);
}

pxr::SdfPath HnTaskManager::GetTaskId(const pxr::TfToken& TaskName) const
{
    return GetId().AppendChild(TaskName);
}

pxr::SdfPath HnTaskManager::GetRenderRprimsTaskId(const pxr::TfToken& MaterialTag, const HnRenderPassParams& RenderPassParams) const
{
    std::string Id = std::string{"RenderRprimsTask_"} + MaterialTag.GetString();
    std::replace(Id.begin(), Id.end(), ':', '_');
    switch (RenderPassParams.Selection)
    {
        case HnRenderPassParams::SelectionType::All:
            Id += "_All";
            break;

        case HnRenderPassParams::SelectionType::Unselected:
            Id += "_Unselected";
            break;

        case HnRenderPassParams::SelectionType::Selected:
            Id += "_Selected";
            break;

        default:
            UNEXPECTED("Unknown selection type");
    }
    return GetTaskId(TfToken{Id});
}

void HnTaskManager::CreateRenderRprimsTask(const pxr::TfToken& MaterialTag, TaskUID UID, const HnRenderPassParams& RenderPassParams)
{
    HnRenderRprimsTaskParams TaskParams;
    pxr::SdfPath             RenderRprimsTaskId = GetRenderRprimsTaskId(MaterialTag, RenderPassParams);
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

    m_ParamsDelegate.SetParameter(RenderRprimsTaskId, HnTokens->renderPassParams, RenderPassParams);

    m_RenderTaskIds.emplace_back(RenderRprimsTaskId);
}

void HnTaskManager::SetFrameParams(const HnBeginFrameTaskParams& Params)
{
    SetTaskParams(TaskUID_BeginFrame, Params);
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

void HnTaskManager::CreateCopySelectionDepthTask()
{
    HnCopySelectionDepthTaskParams TaskParams;
    CreateTask<HnCopySelectionDepthTask>(HnTaskManagerTokens->copySelectionDepthTask, TaskUID_CopySelectionDepth, TaskParams);
}

void HnTaskManager::CreateProcessSelectionTask()
{
    HnProcessSelectionTaskParams TaskParams;
    TaskParams.MaximumDistance = HnPostProcessTaskParams{}.SelectionOutlineWidth;
    CreateTask<HnProcessSelectionTask>(HnTaskManagerTokens->processSelectionTask, TaskUID_ProcessSelection, TaskParams);
}

void HnTaskManager::CreateRenderEnvMapTask(const pxr::TfToken& RenderPassName)
{
    HnRenderEnvMapTaskParams TaskParams;
    CreateTask<HnRenderEnvMapTask>(HnTaskManagerTokens->renderEnvMapTask, TaskUID_RenderEnvMap, TaskParams);

    SetParameter(HnTaskManagerTokens->renderEnvMapTask, HnTokens->renderPassName, RenderPassName);
}

void HnTaskManager::CreateRenderBoundBoxTask(const pxr::TfToken& RenderPassName)
{
    HnRenderBoundBoxTaskParams TaskParams;
    CreateTask<HnRenderBoundBoxTask>(HnTaskManagerTokens->renderBoundBoxTask, TaskUID_RenderBoundBox, TaskParams);

    SetParameter(HnTaskManagerTokens->renderBoundBoxTask, HnTokens->renderPassName, RenderPassName);
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
    Tasks.reserve(TaskOrder->size());
    for (auto UID : *TaskOrder)
    {
        auto it = m_TaskInfo.find(UID);
        if (it == m_TaskInfo.end())
            continue;

        if (!it->second.Enabled)
            continue;

        Tasks.push_back(m_RenderIndex.GetTask(it->second.Id));
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

const pxr::SdfPath* HnTaskManager::GetSelectedRPrimId() const
{
    pxr::HdTaskSharedPtr pReadRprimIdTask = GetTask(TaskUID_ReadRprimId);
    if (!pReadRprimIdTask)
        return nullptr;

    HnReadRprimIdTask& ReadRprimIdTask = static_cast<HnReadRprimIdTask&>(*pReadRprimIdTask);
    const Uint32       MeshIdx         = ReadRprimIdTask.GetMeshIndex();
    if (MeshIdx == HnReadRprimIdTask::InvalidMeshIndex)
    {
        // Data is not yet available
        return nullptr;
    }
    else
    {
        const pxr::SdfPath*       rPRimId = static_cast<const HnRenderDelegate*>(GetRenderIndex().GetRenderDelegate())->GetRPrimId(MeshIdx);
        static const pxr::SdfPath EmptyPath;
        return rPRimId != nullptr ?
            rPRimId :
            &EmptyPath; // Return empty path to indicate that the data is available, but no RPrim is not selected
    }
}

void HnTaskManager::EnableTask(TaskUID UID, bool Enable)
{
    auto it = m_TaskInfo.find(UID);
    if (it == m_TaskInfo.end())
        return;

    it->second.Enabled = Enable;
}

bool HnTaskManager::IsTaskEnabled(TaskUID UID) const
{
    auto it = m_TaskInfo.find(UID);
    return it != m_TaskInfo.end() ? it->second.Enabled : false;
}

void HnTaskManager::EnableMaterial(const pxr::TfToken& MaterialTag, bool Enable)
{
    if (MaterialTag == HnMaterialTagTokens->defaultTag)
    {
        EnableTask(TaskUID_RenderRprimsDefaultSelected, Enable);
        EnableTask(TaskUID_RenderRprimsDefaultUnselected, Enable);
        EnableTask(TaskUID_RenderShadows, Enable);
    }
    else if (MaterialTag == HnMaterialTagTokens->masked)
    {
        EnableTask(TaskUID_RenderRprimsMaskedSelected, Enable);
        EnableTask(TaskUID_RenderRprimsMaskedUnselected, Enable);
    }
    else if (MaterialTag == HnMaterialTagTokens->additive)
    {
        EnableTask(TaskUID_RenderRprimsAdditive, Enable);
        EnableTask(TaskUID_RenderRprimsAdditiveSelected, Enable);
    }
    else if (MaterialTag == HnMaterialTagTokens->translucent)
    {
        EnableTask(TaskUID_RenderRprimsTranslucent, Enable);
        EnableTask(TaskUID_RenderRprimsTranslucentSelected, Enable);
    }
    else
    {
        UNEXPECTED("Unknown material tag ", MaterialTag);
    }
    SuspededSuperSampling();
}

bool HnTaskManager::IsMaterialEnabled(const pxr::TfToken& MaterialTag) const
{
    if (MaterialTag == HnMaterialTagTokens->defaultTag)
    {
        return IsTaskEnabled(TaskUID_RenderRprimsDefaultUnselected);
    }
    else if (MaterialTag == HnMaterialTagTokens->masked)
    {
        return IsTaskEnabled(TaskUID_RenderRprimsMaskedUnselected);
    }
    else if (MaterialTag == HnMaterialTagTokens->additive)
    {
        return IsTaskEnabled(TaskUID_RenderRprimsAdditive);
    }
    else if (MaterialTag == HnMaterialTagTokens->translucent)
    {
        return IsTaskEnabled(TaskUID_RenderRprimsTranslucent);
    }
    else
    {
        UNEXPECTED("Unknown material tag ", MaterialTag);
        return false;
    }
}

void HnTaskManager::SetReadRprimIdParams(const HnReadRprimIdTaskParams& Params)
{
    SetTaskParams(TaskUID_ReadRprimId, Params);
}

void HnTaskManager::SetRenderBoundBoxParams(const HnRenderBoundBoxTaskParams& Params)
{
    SetTaskParams(TaskUID_RenderBoundBox, Params);
}

void HnTaskManager::SetPostProcessParams(const HnPostProcessTaskParams& Params)
{
    SetTaskParams(TaskUID_PostProcess, Params);

    auto process_selection_task_it = m_TaskInfo.find(TaskUID{TaskUID_ProcessSelection});
    if (process_selection_task_it != m_TaskInfo.end())
    {
        HnProcessSelectionTaskParams ProcessSelectionParams = m_ParamsDelegate.GetParameter<HnProcessSelectionTaskParams>(process_selection_task_it->second.Id, pxr::HdTokens->params);
        if (ProcessSelectionParams.MaximumDistance != Params.SelectionOutlineWidth)
        {
            ProcessSelectionParams.MaximumDistance = Params.SelectionOutlineWidth;
            SetTaskParams(TaskUID_ProcessSelection, ProcessSelectionParams);
        }
    }
}

void HnTaskManager::EnableEnvironmentMap(bool Enable)
{
    EnableTask(TaskUID_RenderEnvMap, Enable);
    SuspededSuperSampling();
}

bool HnTaskManager::IsEnvironmentMapEnabled() const
{
    return IsTaskEnabled(TaskUID_RenderEnvMap);
}

void HnTaskManager::EnableSelectedPrimBoundBox(bool Enable)
{
    EnableTask(TaskUID_RenderBoundBox, Enable);
    SuspededSuperSampling();
}

bool HnTaskManager::IsSelectedPrimBoundBoxEnabled() const
{
    return IsTaskEnabled(TaskUID_RenderBoundBox);
}

void HnTaskManager::ResetTAA()
{
    if (HnPostProcessTask* Task = GetTask<HnPostProcessTask>(TaskUID{TaskUID_PostProcess}))
    {
        Task->ResetTAA();
    }
}

void HnTaskManager::SuspededSuperSampling()
{
    if (HnPostProcessTask* Task = GetTask<HnPostProcessTask>(TaskUID{TaskUID_PostProcess}))
    {
        Task->SuspendSuperSampling();
    }
}

} // namespace USD

} // namespace Diligent
