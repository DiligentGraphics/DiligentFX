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

#include "Tasks/HnReadRprimIdTask.hpp"

#include "HnRenderDelegate.hpp"
#include "HnTokens.hpp"

#include "DebugUtilities.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace USD
{

HnReadRprimIdTask::HnReadRprimIdTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnReadRprimIdTask::~HnReadRprimIdTask()
{
}

void HnReadRprimIdTask::Sync(pxr::HdSceneDelegate* Delegate,
                             pxr::HdTaskContext*   TaskCtx,
                             pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        GetTaskParams(Delegate, m_Params);
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnReadRprimIdTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    if (!m_MeshIdReadBackQueue)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
        m_MeshIdReadBackQueue            = std::make_unique<MeshIdReadBackQueueType>(RenderDelegate->GetDevice());
    }
}

void HnReadRprimIdTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    m_MeshIndex = InvalidMeshIndex;

    if (!m_Params.IsEnabled)
        return;

    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }
    if (m_MeshIdReadBackQueue == nullptr)
    {
        UNEXPECTED("Mesh ID readback queue is null.");
        return;
    }

    // Render target Bprims are initialized by the HnSetupRenderingTask.

    ITextureView* pMeshIdRTV = GetRenderBufferTarget(*m_RenderIndex, TaskCtx, HnRenderResourceTokens->meshIdTarget);
    if (pMeshIdRTV == nullptr)
    {
        UNEXPECTED("Mesh Id RTV is not set in the task context");
        return;
    }

    auto* const pMeshIdTexture = pMeshIdRTV->GetTexture();
    const auto& MeshIdRTVDesc  = pMeshIdTexture->GetDesc();
    if (m_Params.LocationX >= MeshIdRTVDesc.GetWidth() ||
        m_Params.LocationY >= MeshIdRTVDesc.GetHeight())
    {
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*    pDevice        = RenderDelegate->GetDevice();
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Read RPrim Id"};

    while (auto pStagingTex = m_MeshIdReadBackQueue->GetFirstCompleted())
    {
        {
            // We waited for the fence, so the texture data should be available.
            // However, mapping the texture on AMD with the MAP_FLAG_DO_NOT_WAIT flag
            // still returns null.
            MAP_FLAGS MapFlags = pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11 ?
                MAP_FLAG_NONE :
                MAP_FLAG_DO_NOT_WAIT;

            MappedTextureSubresource MappedData;
            pCtx->MapTextureSubresource(pStagingTex, 0, 0, MAP_READ, MapFlags, nullptr, MappedData);
            if (MappedData.pData != nullptr)
            {
                float fMeshIndex = *static_cast<const float*>(MappedData.pData);
                m_MeshIndex      = fMeshIndex >= 0.f ? static_cast<Uint32>(fMeshIndex) : 0u;
                pCtx->UnmapTextureSubresource(pStagingTex, 0, 0);
            }
            else
            {
                UNEXPECTED("Mapped data pointer is null");
            }
        }
        m_MeshIdReadBackQueue->Recycle(std::move(pStagingTex));
    }

    auto pStagingTex = m_MeshIdReadBackQueue->GetRecycled();
    if (!pStagingTex)
    {
        TextureDesc StagingTexDesc;
        StagingTexDesc.Name           = "Mesh ID staging tex";
        StagingTexDesc.Usage          = USAGE_STAGING;
        StagingTexDesc.Type           = RESOURCE_DIM_TEX_2D;
        StagingTexDesc.BindFlags      = BIND_NONE;
        StagingTexDesc.Format         = MeshIdRTVDesc.Format;
        StagingTexDesc.Width          = 1;
        StagingTexDesc.Height         = 1;
        StagingTexDesc.MipLevels      = 1;
        StagingTexDesc.CPUAccessFlags = CPU_ACCESS_READ;

        pDevice->CreateTexture(StagingTexDesc, nullptr, &pStagingTex);
        VERIFY_EXPR(pStagingTex);
    }

    // Unbind render targets from the device context since Diligent will have to do this anyway
    // and will print a warning if the targets are still bound.
    pCtx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    CopyTextureAttribs CopyAttribs;
    CopyAttribs.pSrcTexture = pMeshIdTexture;
    CopyAttribs.pDstTexture = pStagingTex;
    Box SrcBox{m_Params.LocationX, m_Params.LocationX + 1, m_Params.LocationY, m_Params.LocationY + 1};
    CopyAttribs.pSrcBox                  = &SrcBox;
    CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    pCtx->CopyTexture(CopyAttribs);
    m_MeshIdReadBackQueue->Enqueue(pCtx, std::move(pStagingTex));
}

} // namespace USD

} // namespace Diligent
