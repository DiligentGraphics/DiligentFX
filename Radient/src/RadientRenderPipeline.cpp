/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "RadientRenderPipeline.hpp"

namespace Diligent
{

RadientRenderPipeline::RadientRenderPipeline(IRadientBackend*         pBackend,
                                             RadientAssetManagerImpl* pAssetManager) :
    m_pBackend{pBackend},
    m_ResourceCache{
        pAssetManager,
        pBackend != nullptr ? pBackend->GetNativeDevice() : nullptr}
{
}

RadientRenderPipeline::~RadientRenderPipeline()
{
}

void RadientRenderPipeline::PrepareDrawList(IRenderDevice*  pDevice,
                                            IDeviceContext* pContext)
{
    m_PreparedDrawList.clear();

    const RadientDrawList& DrawList = m_SceneCache.GetDrawList();
    if (DrawList.IsEmpty())
        return;

    m_PreparedDrawList.reserve(DrawList.GetItemCount());
    for (const RadientDrawItem& DrawItem : DrawList.GetItems())
    {
        if (const RadientRenderMesh* pMesh = m_ResourceCache.ResolveMesh(DrawItem.Mesh.Mesh, pDevice, pContext))
            m_PreparedDrawList.push_back({&DrawItem, pMesh});
    }
}

RADIENT_STATUS RadientRenderPipeline::Render(const RadientRenderAttribs& Attribs)
{
    if (m_pBackend == nullptr ||
        Attribs.pScene == nullptr ||
        Attribs.pRenderTarget == nullptr)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RADIENT_STATUS SyncStatus = m_SceneCache.SyncScene(*Attribs.pScene);
    if (RADIENT_FAILED(SyncStatus))
        return SyncStatus;

    IRenderDevice*  pDevice  = m_pBackend->GetNativeDevice();
    IDeviceContext* pContext = Attribs.pDeviceContext != nullptr ?
        Attribs.pDeviceContext :
        m_pBackend->GetNativeImmediateContext();

    const RADIENT_STATUS TargetStatus = m_FrameTargets.Prepare(pDevice, *Attribs.pRenderTarget);
    if (RADIENT_FAILED(TargetStatus))
        return TargetStatus;

    // Remote execution and headless local tests use the same public renderer object.
    // The concrete command serialization/GPU execution will be plugged in behind this pipeline.
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    RADIENT_STATUS Status = m_ResourceCache.Prepare(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_ForwardPass.Prepare(pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_PostProcessPipeline.Prepare(pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    PrepareDrawList(pDevice, pContext);

    Status = m_ForwardPass.Execute(pDevice,
                                   pContext,
                                   m_PreparedDrawList,
                                   m_SceneCache.GetLightList(),
                                   m_ResourceCache.GetResourceManager(),
                                   Attribs,
                                   m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    return m_PostProcessPipeline.Execute(pContext, m_FrameTargets);
}

} // namespace Diligent
