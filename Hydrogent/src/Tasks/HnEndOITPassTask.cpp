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

#include "Tasks/HnEndOITPassTask.hpp"
#include "HnRenderDelegate.hpp"
#include "ScopedDebugGroup.hpp"
#include "HnTokens.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnRenderParam.hpp"

namespace Diligent
{

namespace USD
{

HnEndOITPassTask::HnEndOITPassTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnEndOITPassTask::~HnEndOITPassTask()
{
}

void HnEndOITPassTask::Sync(pxr::HdSceneDelegate* Delegate,
                            pxr::HdTaskContext*   TaskCtx,
                            pxr::HdDirtyBits*     DirtyBits)
{
    *DirtyBits = pxr::HdChangeTracker::Clean;
}

bool HnEndOITPassTask::IsActive(pxr::HdRenderIndex& RenderIndex) const
{
    pxr::HdRenderDelegate* RenderDelegate = RenderIndex.GetRenderDelegate();
    const HnRenderParam*   RenderParam    = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    const HN_RENDER_MODE   RenderMode     = RenderParam->GetRenderMode();

    return RenderMode == HN_RENDER_MODE_SOLID;
}

void HnEndOITPassTask::Prepare(pxr::HdTaskContext* TaskCtx,
                               pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate());
    const USD_Renderer& Renderer       = *RenderDelegate->GetUSDRenderer();

    HnFrameRenderTargets* FrameTargets = GetFrameRenderTargets(TaskCtx);
    if (FrameTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }

    if (!m_ApplyOITAttenuationPSO)
    {
        ITextureView* pColorRTV     = FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR];
        ITextureView* pBaseColorRTV = FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR];
        ITextureView* pIblRTV       = FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_IBL];
        if (pColorRTV == nullptr || pBaseColorRTV == nullptr || pIblRTV == nullptr)
        {
            UNEXPECTED("Scene color, base color or IBL target is null");
            return;
        }

        const TextureViewDesc& ColorDesc     = pColorRTV->GetDesc();
        const TextureViewDesc& BaseColorDesc = pBaseColorRTV->GetDesc();
        const TextureViewDesc& IblDesc       = pIblRTV->GetDesc();
        const TEXTURE_FORMAT   RTVFormats[]  = {ColorDesc.Format, BaseColorDesc.Format, IblDesc.Format};

        Renderer.CreateApplyOITAttenuationPSO(RTVFormats, _countof(RTVFormats), ~0u, TEX_FORMAT_UNKNOWN, &m_ApplyOITAttenuationPSO);
        VERIFY_EXPR(m_ApplyOITAttenuationPSO);
    }

    HnRenderParam* RenderParam = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam());
    VERIFY_EXPR(RenderParam->GetRenderMode() == HN_RENDER_MODE_SOLID);
    if (m_OITResourcesVersion != RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::OITResources))
    {
        m_ApplyOITAttenuationSRB.Release();
        Renderer.CreateApplyOITAttenuationSRB(RenderDelegate->GetFrameAttribsCB(), FrameTargets->OIT.Layers, FrameTargets->OIT.Tail, &m_ApplyOITAttenuationSRB);
        VERIFY_EXPR(m_ApplyOITAttenuationSRB);
        m_OITResourcesVersion = RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::OITResources);
    }
}

void HnEndOITPassTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const USD_Renderer& Renderer       = *RenderDelegate->GetUSDRenderer();
    IDeviceContext*     pCtx           = RenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "End OIT pass"};

    const HnFrameRenderTargets* FrameTargets = GetFrameRenderTargets(TaskCtx);
    if (FrameTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }

    ITextureView* ppRTVs[] = {
        FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR],
        FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR],
        FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_IBL],
    };
    if (ppRTVs[0] == nullptr || ppRTVs[1] == nullptr || ppRTVs[2] == nullptr)
    {
        UNEXPECTED("Scene color, base color or IBL target is null");
        return;
    }

    pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const StateTransitionDesc Barriers[] =
        {
            {FrameTargets->OIT.Layers, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {FrameTargets->OIT.Tail, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

    Renderer.ApplyOITAttenuation(pCtx, m_ApplyOITAttenuationPSO, m_ApplyOITAttenuationSRB);
}

} // namespace USD

} // namespace Diligent
