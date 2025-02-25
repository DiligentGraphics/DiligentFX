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

#include "Tasks/HnBeginOITPassTask.hpp"
#include "HnRenderDelegate.hpp"
#include "ScopedDebugGroup.hpp"
#include "HnTokens.hpp"
#include "HnRenderPassState.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnCamera.hpp"
#include "HnRenderParam.hpp"

namespace Diligent
{

namespace USD
{

HnBeginOITPassTask::HnBeginOITPassTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnBeginOITPassTask::~HnBeginOITPassTask()
{
}

void HnBeginOITPassTask::Sync(pxr::HdSceneDelegate* Delegate,
                              pxr::HdTaskContext*   TaskCtx,
                              pxr::HdDirtyBits*     DirtyBits)
{
    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnBeginOITPassTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                 pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    HnFrameRenderTargets* FrameTargets = GetFrameRenderTargets(TaskCtx);
    if (FrameTargets == nullptr)
    {
        UNEXPECTED("Framebuffer targets are null");
        return;
    }
    m_FrameTargets = FrameTargets;

    ITextureView* pColorRTV = FrameTargets->GBufferRTVs[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR];
    if (pColorRTV == nullptr)
    {
        UNEXPECTED("Scene color target is null");
        return;
    }

    const TextureDesc& ColorTargetDesc = pColorRTV->GetTexture()->GetDesc();
    if (FrameTargets->OIT)
    {
        const TextureDesc& OITDesc = FrameTargets->OIT.Tail->GetDesc();
        if (OITDesc.Width != ColorTargetDesc.Width || OITDesc.Height != ColorTargetDesc.Height)
        {
            FrameTargets->OIT = {};
            m_ClearLayersSRB.Release();
        }
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate());

    const USD_Renderer& Renderer = *RenderDelegate->GetUSDRenderer();
    VERIFY_EXPR(Renderer.GetSettings().OITLayerCount > 0);

    if (!FrameTargets->OIT)
    {
        FrameTargets->OIT = Renderer.CreateOITResources(ColorTargetDesc.Width, ColorTargetDesc.Height);
        // Mark OIT resources dirty to make render delegate recreate transparent pass frame attribs SRB.
        // We will set the OIT resources in the SBR in Execute().
        static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam())->MakeAttribDirty(HnRenderParam::GlobalAttrib::OITResources);
    }

    bool UseReverseDepth = false;
    GetTaskContextData(TaskCtx, HnRenderResourceTokens->useReverseDepth, UseReverseDepth);

    const TEXTURE_FORMAT DepthFormat = FrameTargets->DepthDSV->GetTexture()->GetDesc().Format;
    if (m_RenderPassState.GetDepthStencilFormat() != DepthFormat ||
        m_RenderPassState.GetUseReverseDepth() != UseReverseDepth)
    {
        const TEXTURE_FORMAT OITRTVFormats[] = {USD_Renderer::OITTailFmt};
        m_RenderPassState.Init(
            OITRTVFormats,
            _countof(OITRTVFormats),
            DepthFormat,
            UseReverseDepth);
    }

    ITextureView* OITRTVs[] = {FrameTargets->OIT.Tail->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    const float4  TailClearValue{
        0, // Layer counter
        0, // Unused
        0, // Unused
        1, // Total tail transmittance
    };
    m_RenderPassState.Begin(_countof(OITRTVs), OITRTVs, FrameTargets->DepthDSV, &TailClearValue, 0, 0x01u);

    const HnCamera* pCamera = nullptr;
    if (GetTaskContextData(TaskCtx, HnRenderResourceTokens->camera, pCamera))
    {
        m_RenderPassState.SetCamera(pCamera);
    }

    (*TaskCtx)[HnRenderResourceTokens->renderPass_OITLayers] = pxr::VtValue{&m_RenderPassState};
}


void HnBeginOITPassTask::BindOITResources(HnRenderDelegate* RenderDelegate)
{
    VERIFY_EXPR(m_FrameTargets != nullptr);
    if (!m_FrameTargets->OIT)
    {
        UNEXPECTED("OIT resources are not initialized. This likely indicates that Prepare() has not been called.");
        return;
    }

    const USD_Renderer& Renderer = *RenderDelegate->GetUSDRenderer();
    if (IShaderResourceBinding* pFrameAttribsSRB = RenderDelegate->GetFrameAttribsSRB(HnRenderDelegate::FrameAttribsSRBType::Transparent))
    {
        Renderer.SetOITResources(pFrameAttribsSRB, m_FrameTargets->OIT);
    }
    else
    {
        UNEXPECTED("Main pass frame attribs SRB is null");
    }

    IDeviceContext* pCtx = RenderDelegate->GetDeviceContext();

    StateTransitionDesc Barriers[] =
        {
            {m_FrameTargets->OIT.Layers, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {m_FrameTargets->OIT.Tail, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
}

void HnBeginOITPassTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }
    if (m_FrameTargets == nullptr)
    {
        UNEXPECTED("Frame targets are null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    HnRenderParam* RenderParam = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam());
    if (m_BoundOITResourcesVersion != RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::OITResources))
    {
        BindOITResources(RenderDelegate);
        m_BoundOITResourcesVersion = RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::OITResources);
    }

    ScopedDebugGroup DebugGroup{pCtx, "Begin OIT pass"};

    const USD_Renderer& Renderer = *RenderDelegate->GetUSDRenderer();
    if (!m_ClearLayersSRB)
    {
        Renderer.CreateClearOITLayersSRB(RenderDelegate->GetFrameAttribsCB(), m_FrameTargets->OIT.Layers, &m_ClearLayersSRB);
    }
    const TextureDesc& OITTailDesc = m_FrameTargets->OIT.Tail->GetDesc();
    Renderer.ClearOITLayers(pCtx, m_ClearLayersSRB, OITTailDesc.Width, OITTailDesc.Height);

    IShaderResourceBinding* pFrameAttribsSRB = RenderDelegate->GetFrameAttribsSRB(HnRenderDelegate::FrameAttribsSRBType::OITLayers);
    m_RenderPassState.SetFrameAttribsSRB(pFrameAttribsSRB);
    m_RenderPassState.Commit(pCtx);
}

} // namespace USD

} // namespace Diligent
