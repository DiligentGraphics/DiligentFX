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

#include "HnRendererImpl.hpp"
#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "HnTokens.hpp"
#include "Tasks/HnTaskManager.hpp"
#include "Tasks/HnSetupRenderingTask.hpp"
#include "Tasks/HnPostProcessTask.hpp"

#include "EngineMemory.h"
#include "USD_Renderer.hpp"
#include "HnRenderBuffer.hpp"

#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/renderPass.h"

namespace Diligent
{

namespace USD
{

void CreateHnRenderer(IRenderDevice* pDevice, IDeviceContext* pContext, const HnRendererCreateInfo& CI, IHnRenderer** ppRenderer)
{
    auto* pRenderer = NEW_RC_OBJ(GetRawAllocator(), "HnRenderer instance", HnRendererImpl)(pDevice, pContext, CI);
    pRenderer->QueryInterface(IID_HnRenderer, reinterpret_cast<IObject**>(ppRenderer));
}

HnRendererImpl::HnRendererImpl(IReferenceCounters*         pRefCounters,
                               IRenderDevice*              pDevice,
                               IDeviceContext*             pContext,
                               const HnRendererCreateInfo& CI) :
    TBase{pRefCounters},
    m_Device{pDevice},
    m_Context{pContext},
    m_CameraAttribsCB{CI.pCameraAttribsCB},
    m_LightAttribsCB{CI.pLightAttribsCB},
    m_ConvertOutputToSRGB{CI.ConvertOutputToSRGB}
{
}

HnRendererImpl::~HnRendererImpl()
{
    DestroyStageResources();
}

void HnRendererImpl::DestroyStageResources()
{
    m_TaskManager.reset();
    m_ImagingDelegate.reset();
    m_RenderIndex.reset();
    m_RenderDelegate.reset();
    m_Stage.Reset();
}

void HnRendererImpl::LoadUSDStage(pxr::UsdStageRefPtr& Stage)
{
    DestroyStageResources();

    m_Stage = Stage;

    m_RenderDelegate = HnRenderDelegate::Create({m_Device, m_Context, nullptr, m_CameraAttribsCB, m_LightAttribsCB});
    m_RenderIndex.reset(pxr::HdRenderIndex::New(m_RenderDelegate.get(), pxr::HdDriverVector{}));

    const pxr::SdfPath SceneDelegateId = pxr::SdfPath::AbsoluteRootPath();

    m_ImagingDelegate = std::make_unique<pxr::UsdImagingDelegate>(m_RenderIndex.get(), SceneDelegateId);
    m_ImagingDelegate->Populate(m_Stage->GetPseudoRoot());

    const pxr::SdfPath TaskManagerId = SceneDelegateId.AppendChild(pxr::TfToken{"_HnTaskManager_"});
    m_TaskManager                    = std::make_unique<HnTaskManager>(*m_RenderIndex, TaskManagerId);

    m_FinalColorTargetId = SceneDelegateId.AppendChild(pxr::TfToken{"_HnFinalColorTarget_"});
    m_RenderIndex->InsertBprim(pxr::HdPrimTypeTokens->renderBuffer, m_ImagingDelegate.get(), m_FinalColorTargetId);

    {
        HnPostProcessTaskParams Params;
        Params.ConvertOutputToSRGB = m_ConvertOutputToSRGB;
        Params.ToneMappingMode     = 4; // Uncharted2
        m_TaskManager->SetTaskParams(HnTaskManager::TaskUID_PostProcess, Params);
    }
}

void HnRendererImpl::SetParams(const HnRenderParams& Params)
{
    m_RenderParams        = Params;
    m_RenderParamsChanged = true;
}

void HnRendererImpl::Update()
{
    if (!m_RenderDelegate || !m_ImagingDelegate)
        return;

    if (m_RenderParamsChanged)
    {
        HnSetupRenderingTaskParams Params;
        Params.ColorFormat        = ColorBufferFormat;
        Params.MeshIdFormat       = MeshIdFormat;
        Params.DepthFormat        = DepthFormat;
        Params.RenderMode         = m_RenderParams.RenderMode;
        Params.FrontFaceCCW       = m_RenderParams.FrontFaceCCW;
        Params.DebugView          = m_RenderParams.DebugView;
        Params.OcclusionStrength  = m_RenderParams.OcclusionStrength;
        Params.EmissionScale      = m_RenderParams.EmissionScale;
        Params.IBLScale           = m_RenderParams.IBLScale;
        Params.Transform          = m_RenderParams.Transform;
        Params.FinalColorTargetId = m_FinalColorTargetId;
        m_TaskManager->SetTaskParams(HnTaskManager::TaskUID_SetupRendering, Params);

        m_RenderParamsChanged = false;
    }

    m_ImagingDelegate->ApplyPendingUpdates();
}

void HnRendererImpl::Draw(IDeviceContext* pCtx, const HnDrawAttribs& Attribs)
{
    if (!m_RenderDelegate)
        return;

    auto* FinalColorTarget = static_cast<HnRenderBuffer*>(m_RenderIndex->GetBprim(pxr::HdPrimTypeTokens->renderBuffer, m_FinalColorTargetId));
    VERIFY_EXPR(FinalColorTarget != nullptr);
    FinalColorTarget->SetTarget(Attribs.pDstRTV);

    pxr::HdTaskSharedPtrVector tasks = m_TaskManager->GetTasks();
    m_Engine.Execute(&m_ImagingDelegate->GetRenderIndex(), &tasks);

    FinalColorTarget->ReleaseTarget();
}

void HnRendererImpl::SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV)
{
    m_RenderDelegate->GetUSDRenderer()->PrecomputeCubemaps(pCtx, pEnvironmentMapSRV);
}

const pxr::SdfPath* HnRendererImpl::QueryPrimId(IDeviceContext* pCtx, Uint32 X, Uint32 Y)
{
    return nullptr;
#if 0
    if (!m_MeshIdTexture)
        return nullptr;

    Uint32 MeshUid = ~0u;


    static const pxr::SdfPath EmptyPath;
    if (MeshUid == ~0u)
        return nullptr;
    else
        return MeshUid != 0 ? m_RenderDelegate->GetMeshPrimId(MeshUid) : &EmptyPath;
#endif
}

} // namespace USD

} // namespace Diligent
