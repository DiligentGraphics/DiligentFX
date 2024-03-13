/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "Tasks/HnRenderShadowsTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnTokens.hpp"
#include "HnShadowMapManager.hpp"

namespace Diligent
{

namespace USD
{

HnRenderShadowsTask::HnRenderShadowsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderShadowsTask::~HnRenderShadowsTask()
{
}

void HnRenderShadowsTask::Sync(pxr::HdSceneDelegate* Delegate,
                               pxr::HdTaskContext*   TaskCtx,
                               pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnRenderShadowsTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnRenderShadowsTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                  pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;
}

void HnRenderShadowsTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate*         RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*            pDevice        = RenderDelegate->GetDevice();
    IDeviceContext*           pCtx           = RenderDelegate->GetDeviceContext();
    const HnShadowMapManager& ShadowMapMgr   = RenderDelegate->GetShadowMapManager();

    StateTransitionDesc Barrier{ShadowMapMgr.GetAtlasSRV()->GetTexture(), RESOURCE_STATE_UNKNOWN,
                                pDevice->GetDeviceInfo().IsD3DDevice() ? RESOURCE_STATE_SHADER_RESOURCE : RESOURCE_STATE_DEPTH_READ,
                                STATE_TRANSITION_FLAG_UPDATE_STATE};
    pCtx->TransitionResourceStates(1, &Barrier);
}

} // namespace USD

} // namespace Diligent
