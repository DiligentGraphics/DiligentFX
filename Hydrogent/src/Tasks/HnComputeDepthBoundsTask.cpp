/*
 *  Copyright 2025 Diligent Graphics LLC
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

#include "Tasks/HnComputeDepthBoundsTask.hpp"
#include "HnRenderDelegate.hpp"
#include "ScopedDebugGroup.hpp"
#include "HnRenderParam.hpp"
#include "DepthRangeCalculator.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnTokens.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/Common/private/ComputeDepthRangeStructs.fxh"
} // namespace HLSL

namespace USD
{

HnComputeDepthBoundsTask::HnComputeDepthBoundsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnComputeDepthBoundsTask::~HnComputeDepthBoundsTask()
{
}

void HnComputeDepthBoundsTask::Sync(pxr::HdSceneDelegate* Delegate,
                                    pxr::HdTaskContext*   TaskCtx,
                                    pxr::HdDirtyBits*     DirtyBits)
{
    *DirtyBits = pxr::HdChangeTracker::Clean;
}

bool HnComputeDepthBoundsTask::IsActive(pxr::HdRenderIndex& RenderIndex) const
{
    pxr::HdRenderDelegate* RenderDelegate = RenderIndex.GetRenderDelegate();
    const HnRenderParam*   RenderParam    = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    const HN_GEOMETRY_MODE GeometryMode   = RenderParam->GetGeometryMode();
    const HN_VIEW_MODE     ViewMode       = RenderParam->GetViewMode();

    // Only run this task when scene depth view mode is enabled
    return GeometryMode == HN_GEOMETRY_MODE_SOLID && ViewMode == HN_VIEW_MODE_SCENE_DEPTH;
}

void HnComputeDepthBoundsTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                       pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    m_FrameTargets = GetFrameRenderTargets(TaskCtx);
    if (m_FrameTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }

    if (m_FrameTargets->Version != m_FrameRenderTargetsVersion)
    {
        m_FrameRenderTargetsVersion = m_FrameTargets->Version;
        m_ComputeDepthRangeSRBs     = {};
    }

    HnRenderDelegate*    RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate());
    const HnRenderParam* RenderParam    = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());

    if (!m_DepthRangeCalculator)
    {
        DepthRangeCalculator::CreateInfo DepthRangeCalcCI;
        DepthRangeCalcCI.pDevice            = RenderDelegate->GetDevice();
        DepthRangeCalcCI.pStateCache        = RenderDelegate->GetRenderStateCache();
        DepthRangeCalcCI.PackMatrixRowMajor = RenderDelegate->GetUSDRenderer()->GetSettings().PackMatrixRowMajor;
        DepthRangeCalcCI.AsyncShaders       = RenderParam->GetConfig().AsyncShaderCompilation;

        try
        {
            m_DepthRangeCalculator = std::make_unique<DepthRangeCalculator>(DepthRangeCalcCI);
        }
        catch (const std::exception& e)
        {
            UNEXPECTED("Failed to create DepthRangeCalculator: ", e.what());
        }
        catch (...)
        {
            UNEXPECTED("Failed to create DepthRangeCalculator");
        }
    }
}

void HnComputeDepthBoundsTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (!m_DepthRangeCalculator)
    {
        UNEXPECTED("DepthRangeCalculator is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (!m_DepthRangeCalculator->IsReady())
    {
        return;
    }

    ITexture* pDepth = m_FrameTargets->DepthDSV != nullptr ? m_FrameTargets->DepthDSV->GetTexture() : nullptr;
    if (pDepth == nullptr)
    {
        UNEXPECTED("Depth stencil view is null");
        return;
    }

    ITextureView* pDepthSRV = pDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pDepthSRV == nullptr)
    {
        UNEXPECTED("Depth SRV is null");
        return;
    }

    HnRenderDelegate*    RenderDelegate  = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const HnRenderParam* RenderParam     = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    IDeviceContext*      pCtx            = RenderDelegate->GetDeviceContext();
    IBuffer*             pFrameAttribsCB = RenderDelegate->GetFrameAttribsCB();

    RefCntAutoPtr<IShaderResourceBinding>& ComputeDepthRangeSRB = m_ComputeDepthRangeSRBs[RenderParam->GetFrameNumber() & 0x01];
    if (!ComputeDepthRangeSRB)
    {
        ComputeDepthRangeSRB = m_DepthRangeCalculator->CreateSRB(pDepthSRV, pFrameAttribsCB);
    }

    ScopedDebugGroup DebugGroup{pCtx, "Compute Depth Bounds"};

    const TextureDesc& DepthDesc = pDepth->GetDesc();

    DepthRangeCalculator::ComputeRangeAttribs Attribs;
    Attribs.pContext = pCtx;
    Attribs.pSRB     = ComputeDepthRangeSRB;
    Attribs.Width    = DepthDesc.Width;
    Attribs.Height   = DepthDesc.Height;
    m_DepthRangeCalculator->ComputeRange(Attribs);

    // Copy the depth range to the frame attributes constant buffer
    if (IBuffer* pDepthRangeBuffer = m_DepthRangeCalculator->GetDepthRangeBuffer())
    {
        pCtx->CopyBuffer(pDepthRangeBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                         pFrameAttribsCB, offsetof(HLSL::CameraAttribs, fSceneNearZ), sizeof(HLSL::DepthRangeI), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        StateTransitionDesc Barrier{pFrameAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
        pCtx->TransitionResourceStates(1, &Barrier);
    }
    else
    {
        UNEXPECTED("Depth range buffer is null");
    }
}

} // namespace USD

} // namespace Diligent
