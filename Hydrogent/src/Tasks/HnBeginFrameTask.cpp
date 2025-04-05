/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include "Tasks/HnBeginFrameTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnTokens.hpp"
#include "HnRenderBuffer.hpp"
#include "HnCamera.hpp"
#include "HnLight.hpp"
#include "HnRenderParam.hpp"
#include "HnShadowMapManager.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsAccessories.hpp"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"
#include "GLTF_PBR_Renderer.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"

} // namespace HLSL

namespace USD
{

HnBeginFrameTaskParams::RenderTargetFormats::RenderTargetFormats() noexcept
{
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR]   = TEX_FORMAT_RGBA16_FLOAT;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_MESH_ID]       = TEX_FORMAT_R32_FLOAT;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR] = TEX_FORMAT_RG16_FLOAT;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL]        = TEX_FORMAT_RGBA16_FLOAT;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR]    = TEX_FORMAT_RGBA8_UNORM;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL]      = TEX_FORMAT_RG8_UNORM;
    GBuffer[HnFrameRenderTargets::GBUFFER_TARGET_IBL]           = TEX_FORMAT_RGBA16_FLOAT;
    static_assert(HnFrameRenderTargets::GBUFFER_TARGET_COUNT == 7, "Please initialize default render target formats.");
}

HnBeginFrameTask::HnBeginFrameTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
    if (ParamsDelegate == nullptr)
    {
        UNEXPECTED("ParamsDelegate is null");
        return;
    }
    pxr::HdRenderIndex& RenderIndex = ParamsDelegate->GetRenderIndex();

    // Insert empty Bprims for offscreen render targets into the render index.
    // The render targets will be created when Prepare() is called and the
    // dimensions of the final color target are known.
    auto InitBrim = [&](const pxr::TfToken& Name) {
        pxr::SdfPath Id = GetId().AppendChild(Name);
        RenderIndex.InsertBprim(pxr::HdPrimTypeTokens->renderBuffer, ParamsDelegate, Id);
        return Id;
    };

    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR]   = InitBrim(HnRenderResourceTokens->offscreenColorTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MESH_ID]       = InitBrim(HnRenderResourceTokens->meshIdTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR] = InitBrim(HnRenderResourceTokens->motionVectorsTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL]        = InitBrim(HnRenderResourceTokens->normalTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR]    = InitBrim(HnRenderResourceTokens->baseColorTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL]      = InitBrim(HnRenderResourceTokens->materialDataTarget);
    m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_IBL]           = InitBrim(HnRenderResourceTokens->iblTarget);
    static_assert(HnFrameRenderTargets::GBUFFER_TARGET_COUNT == 7, "Please initialize GBuffer BPrims.");

    m_SelectionDepthBufferId     = InitBrim(HnRenderResourceTokens->selectionDepthBuffer);
    m_DepthBufferId[0]           = InitBrim(HnRenderResourceTokens->depthBuffer0);
    m_DepthBufferId[1]           = InitBrim(HnRenderResourceTokens->depthBuffer1);
    m_ClosestSelLocnTargetId[0]  = InitBrim(HnRenderResourceTokens->closestSelectedLocation0Target);
    m_ClosestSelLocnTargetId[1]  = InitBrim(HnRenderResourceTokens->closestSelectedLocation1Target);
    m_JitteredFinalColorTargetId = InitBrim(HnRenderResourceTokens->jitteredFinalColorTarget);
}

HnBeginFrameTask::~HnBeginFrameTask()
{
}

static TEXTURE_FORMAT GetFallbackTextureFormat(TEXTURE_FORMAT Format)
{
    switch (Format)
    {
        case TEX_FORMAT_R16_UNORM: return TEX_FORMAT_R16_FLOAT;
        case TEX_FORMAT_RG16_UNORM: return TEX_FORMAT_RG16_FLOAT;
        case TEX_FORMAT_RGBA16_UNORM: return TEX_FORMAT_RGBA16_FLOAT;
        default: return Format;
    }
}

void HnBeginFrameTask::Sync(pxr::HdSceneDelegate* Delegate,
                            pxr::HdTaskContext*   TaskCtx,
                            pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        if (GetTaskParams(Delegate, m_Params))
        {
            for (const pxr::TfToken& PassName : {HnRenderResourceTokens->renderPass_OpaqueSelected,
                                                 HnRenderResourceTokens->renderPass_OpaqueUnselected,
                                                 HnRenderResourceTokens->renderPass_TransparentAll})
            {
                m_RenderPassStates[PassName].Init(
                    m_Params.Formats.GBuffer.data(),
                    m_Params.Formats.GBuffer.size(),
                    m_Params.Formats.Depth,
                    m_Params.UseReverseDepth);
            }

            m_RenderPassStates[HnRenderResourceTokens->renderPass_TransparentSelected].Init(
                nullptr,
                0,
                m_Params.Formats.Depth,
                m_Params.UseReverseDepth);

            (*TaskCtx)[HnRenderResourceTokens->suspendSuperSampling] = pxr::VtValue{true};
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnBeginFrameTask::PrepareRenderTargets(pxr::HdRenderIndex* RenderIndex,
                                            pxr::HdTaskContext* TaskCtx,
                                            ITextureView*       pFinalColorRTV)
{
    if (pFinalColorRTV == nullptr)
    {
        UNEXPECTED("Final color target RTV is null");
        return;
    }
    const TextureDesc& FinalTargetDesc = pFinalColorRTV->GetTexture()->GetDesc();

    const HnRenderDelegate* RenderDelegate = static_cast<const HnRenderDelegate*>(RenderIndex->GetRenderDelegate());
    const HnRenderParam*    RenderParam    = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());

    m_FrameBufferWidth  = FinalTargetDesc.Width;
    m_FrameBufferHeight = FinalTargetDesc.Height;

    auto UpdateBrim = [&](const pxr::SdfPath& Id, TEXTURE_FORMAT Format, const std::string& Name) -> ITextureView* {
        if (Format == TEX_FORMAT_UNKNOWN)
            return nullptr;

        IRenderDevice* const pDevice = RenderDelegate->GetDevice();
        if (!pDevice->GetTextureFormatInfo(Format).Supported)
        {
            Format = GetFallbackTextureFormat(Format);
        }

        VERIFY_EXPR(!Id.IsEmpty());

        HnRenderBuffer* Renderbuffer = static_cast<HnRenderBuffer*>(RenderIndex->GetBprim(pxr::HdPrimTypeTokens->renderBuffer, Id));
        if (Renderbuffer == nullptr)
        {
            UNEXPECTED("Render buffer is not set at Id ", Id);
            return nullptr;
        }

        if (ITextureView* pView = Renderbuffer->GetTarget())
        {
            const TextureViewDesc& ViewDesc   = pView->GetDesc();
            const TextureDesc&     TargetDesc = pView->GetTexture()->GetDesc();
            if (TargetDesc.GetWidth() == FinalTargetDesc.GetWidth() &&
                TargetDesc.GetHeight() == FinalTargetDesc.GetHeight() &&
                ViewDesc.Format == Format)
                return pView;
        }

        const bool IsDepth = GetTextureFormatAttribs(Format).IsDepthStencil();

        TextureDesc TargetDesc = FinalTargetDesc;
        TargetDesc.Name        = Name.c_str();
        TargetDesc.Format      = Format;
        TargetDesc.BindFlags   = (IsDepth ? BIND_DEPTH_STENCIL : BIND_RENDER_TARGET) | BIND_SHADER_RESOURCE;

        RefCntAutoPtr<ITexture> pTarget;
        pDevice->CreateTexture(TargetDesc, nullptr, &pTarget);
        if (!pTarget)
        {
            UNEXPECTED("Failed to create ", Name, " texture");
            return nullptr;
        }
        LOG_INFO_MESSAGE("HnBeginFrameTask: created ", TargetDesc.GetWidth(), "x", TargetDesc.GetHeight(), " ", Name, " texture");

        ITextureView* const pView = pTarget->GetDefaultView(IsDepth ? TEXTURE_VIEW_DEPTH_STENCIL : TEXTURE_VIEW_RENDER_TARGET);
        VERIFY(pView != nullptr, "Failed to get texture view for target ", Name);

        Renderbuffer->SetTarget(pView);

        return pView;
    };

    m_FrameRenderTargets.FinalColorRTV = pFinalColorRTV;

    for (Uint32 i = 0; i < HnFrameRenderTargets::GBUFFER_TARGET_COUNT; ++i)
    {
        const char* Name                    = HnFrameRenderTargets::GetGBufferTargetName(static_cast<HnFrameRenderTargets::GBUFFER_TARGET>(i));
        m_FrameRenderTargets.GBufferRTVs[i] = UpdateBrim(m_GBufferTargetIds[i], m_Params.Formats.GBuffer[i], Name);
        if (m_FrameRenderTargets.GBufferRTVs[i])
        {
            m_FrameRenderTargets.GBufferSRVs[i] = m_FrameRenderTargets.GBufferRTVs[i]->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            VERIFY_EXPR(m_FrameRenderTargets.GBufferSRVs[i] != nullptr);
        }
        else
        {
            UNEXPECTED("Unable to get GBuffer target from Bprim ", m_GBufferTargetIds[i]);
        }
    }

    m_FrameRenderTargets.SelectionDepthDSV             = UpdateBrim(m_SelectionDepthBufferId, m_Params.Formats.Depth, "Selection depth buffer");
    m_FrameRenderTargets.DepthDSV                      = UpdateBrim(m_DepthBufferId[0], m_Params.Formats.Depth, "Depth buffer 0");
    m_FrameRenderTargets.PrevDepthDSV                  = UpdateBrim(m_DepthBufferId[1], m_Params.Formats.Depth, "Depth buffer 1");
    m_FrameRenderTargets.ClosestSelectedLocationRTV[0] = UpdateBrim(m_ClosestSelLocnTargetId[0], m_Params.Formats.ClosestSelectedLocation, "Closest selected location 0");
    m_FrameRenderTargets.ClosestSelectedLocationRTV[1] = UpdateBrim(m_ClosestSelLocnTargetId[1], m_Params.Formats.ClosestSelectedLocation, "Closest selected location 1");
    m_FrameRenderTargets.JitteredFinalColorRTV         = UpdateBrim(m_JitteredFinalColorTargetId, m_Params.Formats.JitteredColor, "Jittered final color");

    (*TaskCtx)[HnRenderResourceTokens->frameRenderTargets] = pxr::VtValue{&m_FrameRenderTargets};

    // Set render pass render targets

    std::array<float4, HnFrameRenderTargets::GBUFFER_TARGET_COUNT> ClearValues; // No need to zero-initialize
    for (Uint32 i = 0; i < HnFrameRenderTargets::GBUFFER_TARGET_COUNT; ++i)
    {
        if (i == HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR)
        {
            if (RenderParam->GetDebugView() != PBR_Renderer::DebugViewType::SceneDepth)
            {
                // NB: we should clear alpha to one as it accumulates the total transmittance
                ClearValues[i] = float4{m_Params.ClearColor.r, m_Params.ClearColor.g, m_Params.ClearColor.b, 1.0};
            }
            else
            {
                // Clear background to white in scene depth debug view mode
                ClearValues[i] = float4{1};
            }
        }
        else
        {
            ClearValues[i] = float4{0};
        }
    }

    HnRenderPassState& RP_OpaqueSelected      = m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueSelected];
    HnRenderPassState& RP_OpaqueUnselected    = m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueUnselected];
    HnRenderPassState& RP_TransparentAll      = m_RenderPassStates[HnRenderResourceTokens->renderPass_TransparentAll];
    HnRenderPassState& RP_TransparentSelected = m_RenderPassStates[HnRenderResourceTokens->renderPass_TransparentSelected];

    const float DepthClearValue = m_Params.UseReverseDepth ? 0.f : 1.f;

    // We first render selected objects using the selection depth buffer.
    // Selection depth buffer is copied to the main depth buffer by the HnCopySelectionDepthTask.
    RP_OpaqueSelected.Begin(HnFrameRenderTargets::GBUFFER_TARGET_COUNT, m_FrameRenderTargets.GBufferRTVs.data(), m_FrameRenderTargets.SelectionDepthDSV, ClearValues.data(), DepthClearValue, ~0u);
    RP_OpaqueUnselected.Begin(HnFrameRenderTargets::GBUFFER_TARGET_COUNT, m_FrameRenderTargets.GBufferRTVs.data(), m_FrameRenderTargets.DepthDSV);
    RP_TransparentAll.Begin(HnFrameRenderTargets::GBUFFER_TARGET_COUNT, m_FrameRenderTargets.GBufferRTVs.data(), m_FrameRenderTargets.DepthDSV);
    RP_TransparentSelected.Begin(0, nullptr, m_FrameRenderTargets.SelectionDepthDSV);

    for (HnRenderPassState* RPState : {&RP_OpaqueSelected,
                                       &RP_OpaqueUnselected,
                                       &RP_TransparentAll,
                                       &RP_TransparentSelected})
    {
        RPState->SetCamera(m_pCamera);
    }

    // Register render pass states in the task context
    for (auto& it : m_RenderPassStates)
    {
        (*TaskCtx)[it.first] = pxr::VtValue{&it.second};
    }
    (*TaskCtx)[HnRenderResourceTokens->backgroundDepth] = pxr::VtValue{DepthClearValue};
    (*TaskCtx)[HnRenderResourceTokens->useReverseDepth] = pxr::VtValue{m_Params.UseReverseDepth};
}

void HnBeginFrameTask::Prepare(pxr::HdTaskContext* TaskCtx,
                               pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate());
    HnRenderParam*      pRenderParam   = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam());
    const USD_Renderer& Renderer       = *RenderDelegate->GetUSDRenderer();
    if (pRenderParam == nullptr)
    {
        UNEXPECTED("Render param is null");
        return;
    }

    // Mark dirty RPrims that were not synced in the change tracker.
    // Note: we need to mark the prims dirty after the sync finishes, because OpenUSD marks
    //       all prims clean after the sync. There may be a better place to do this, but
    //       it's not clear where that would be.
    pRenderParam->CommitDirtyRPrims(m_RenderIndex->GetChangeTracker());

    m_CurrFrameTime = m_FrameTimer.GetElapsedTime();

    Uint32 FrameNumber = 0;
    pRenderParam->SetElapsedTime(static_cast<float>(m_CurrFrameTime - pRenderParam->GetFrameTime()));
    pRenderParam->SetFrameTime(m_CurrFrameTime);
    pRenderParam->SetFrameNumber(pRenderParam->GetFrameNumber() + 1);
    FrameNumber = pRenderParam->GetFrameNumber();

    if (FrameNumber > 1)
    {
        std::swap(m_DepthBufferId[0], m_DepthBufferId[1]);
    }

    (*TaskCtx)[HnRenderResourceTokens->finalColorTarget] = pxr::VtValue{m_Params.FinalColorTargetId};

    (*TaskCtx)[HnRenderResourceTokens->offscreenColorTarget] = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR]};
    (*TaskCtx)[HnRenderResourceTokens->meshIdTarget]         = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MESH_ID]};
    (*TaskCtx)[HnRenderResourceTokens->normalTarget]         = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_NORMAL]};
    (*TaskCtx)[HnRenderResourceTokens->motionVectorsTarget]  = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR]};
    (*TaskCtx)[HnRenderResourceTokens->baseColorTarget]      = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR]};
    (*TaskCtx)[HnRenderResourceTokens->materialDataTarget]   = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL]};
    (*TaskCtx)[HnRenderResourceTokens->iblTarget]            = pxr::VtValue{m_GBufferTargetIds[HnFrameRenderTargets::GBUFFER_TARGET_IBL]};
    static_assert(HnFrameRenderTargets::GBUFFER_TARGET_COUNT == 7, "Please initialize all GBuffer targets.");

    (*TaskCtx)[HnRenderResourceTokens->depthBuffer]                    = pxr::VtValue{m_DepthBufferId[0]};
    (*TaskCtx)[HnRenderResourceTokens->selectionDepthBuffer]           = pxr::VtValue{m_SelectionDepthBufferId};
    (*TaskCtx)[HnRenderResourceTokens->closestSelectedLocation0Target] = pxr::VtValue{m_ClosestSelLocnTargetId[0]};
    (*TaskCtx)[HnRenderResourceTokens->closestSelectedLocation1Target] = pxr::VtValue{m_ClosestSelLocnTargetId[1]};
    (*TaskCtx)[HnRenderResourceTokens->jitteredFinalColorTarget]       = pxr::VtValue{m_JitteredFinalColorTargetId};

    bool ResetTAA = false;
    if (!m_Params.CameraId.IsEmpty())
    {
        m_pCamera = static_cast<const HnCamera*>(m_RenderIndex->GetSprim(pxr::HdPrimTypeTokens->camera, m_Params.CameraId));
        if (m_pCamera == nullptr)
        {
            LOG_ERROR_MESSAGE("Camera is not set at Id ", m_Params.CameraId);
        }
        (*TaskCtx)[HnRenderResourceTokens->camera] = pxr::VtValue{static_cast<const HnCamera*>(m_pCamera)};
    }
    else
    {
        LOG_ERROR_MESSAGE("Camera Id is empty");
    }

    (*TaskCtx)[HnRenderResourceTokens->taaReset] = pxr::VtValue{ResetTAA};

    if (ITextureView* pFinalColorRTV = GetRenderBufferTarget(*RenderIndex, m_Params.FinalColorTargetId))
    {
        PrepareRenderTargets(RenderIndex, TaskCtx, pFinalColorRTV);
    }
    else
    {
        UNEXPECTED("Unable to get final color target from Bprim ", m_Params.FinalColorTargetId);
    }

    if (const HnShadowMapManager* ShadowMapMgr = RenderDelegate->GetShadowMapManager())
    {
        // Assign indices to shadow casting lights

        const Uint32 NumShadowCastingLights = Renderer.GetSettings().EnableShadows ? Renderer.GetSettings().MaxShadowCastingLightCount : 0;
        const auto&  Lights                 = RenderDelegate->GetLights();

        Uint32 ShadowCastingLightIdx = 0;
        for (HnLight* Light : Lights)
        {
            if (Light->ShadowsEnabled() && Light->IsVisible() && ShadowCastingLightIdx < NumShadowCastingLights)
            {
                Light->SetFrameAttribsIndex(ShadowCastingLightIdx++);
            }
            else
            {
                Light->SetFrameAttribsIndex(-1);
            }
        }
    }
}

void HnBeginFrameTask::UpdateFrameConstants(IDeviceContext* pCtx,
                                            IBuffer*        pFrameAttrbisCB,
                                            bool            UseTAA,
                                            const float2&   Jitter,
                                            bool&           CameraTransformDirty,
                                            bool&           LoadingAnimationActive)
{
    HnRenderDelegate*    RenderDelegate     = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const HnRenderParam* RenderParam        = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    IRenderDevice*       pDevice            = RenderDelegate->GetDevice();
    const USD_Renderer&  Renderer           = *RenderDelegate->GetUSDRenderer();
    const int            MaxLightCount      = Renderer.GetSettings().MaxLightCount;
    const bool           PackMatrixRowMajor = Renderer.GetSettings().PackMatrixRowMajor;

    const Uint32 NumShadowCastingLights = Renderer.GetSettings().EnableShadows ? Renderer.GetSettings().MaxShadowCastingLightCount : 0;
    const Uint32 FrameAttribsDataSize   = pFrameAttrbisCB->GetDesc().Size;
    VERIFY(FrameAttribsDataSize == RenderDelegate->GetShadowPassFrameAttribsOffset(NumShadowCastingLights), "Frame attributes buffer size mismatch");
    m_FrameAttribsData.resize(FrameAttribsDataSize);
    //
    // ||                   Main Pass                  ||        Shadow Pass 1       ||  ...  ||       Shadow Pass N        ||
    // || Camera|PrevCamera|Renderer|Lights|ShadowMaps || Camera|PrevCamera|Renderer ||  ...  || Camera|PrevCamera|Renderer ||
    //

    // Write shadow casting light attributes first to initialize shadow cating light indices
    if (const HnShadowMapManager* ShadowMapMgr = RenderDelegate->GetShadowMapManager())
    {
        const TextureDesc& ShadowAtlasDesc = ShadowMapMgr->GetAtlasDesc();

        const auto& Lights = RenderDelegate->GetLights();
        for (const HnLight* Light : Lights)
        {
            const Int32 ShadowCastingLightIdx = Light->GetFrameAttribsIndex();
            VERIFY_EXPR((ShadowCastingLightIdx < 0) == (!Light->ShadowsEnabled() || !Light->IsVisible() || ShadowCastingLightIdx >= static_cast<Int32>(NumShadowCastingLights)));
            if (ShadowCastingLightIdx < 0)
                continue;

            HLSL::PBRFrameAttribs* ShadowAttribs = reinterpret_cast<HLSL::PBRFrameAttribs*>(&m_FrameAttribsData[RenderDelegate->GetShadowPassFrameAttribsOffset(ShadowCastingLightIdx)]);
            HLSL::CameraAttribs&   CamAttribs    = ShadowAttribs->Camera;

            const float4x4& ProjMatrix = Light->GetViewProjMatrix();
            const float4x4& ViewMatrix = Light->GetViewMatrix();
            const float4x4  ViewProj   = Light->GetViewProjMatrix();

            VERIFY_EXPR(ShadowAtlasDesc.Width > 0 && ShadowAtlasDesc.Height > 0);
            CamAttribs.f4ViewportSize = float4{
                static_cast<float>(ShadowAtlasDesc.Width),
                static_cast<float>(ShadowAtlasDesc.Height),
                1.f / static_cast<float>(ShadowAtlasDesc.Width),
                1.f / static_cast<float>(ShadowAtlasDesc.Height),
            };
            CamAttribs.fHandness = 1.f;

            WriteShaderMatrix(&CamAttribs.mView, ViewMatrix, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mProj, ProjMatrix, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewProj, ViewProj, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewInv, ViewMatrix.Inverse(), !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mProjInv, ProjMatrix.Inverse(), !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewProjInv, ViewProj.Inverse(), !PackMatrixRowMajor);
            CamAttribs.f4Position = float4{0, 0, 0, 1};
            CamAttribs.f2Jitter   = float2{0, 0};

            memset(&ShadowAttribs->Renderer, 0, sizeof(HLSL::PBRRendererShaderParameters));
        }
    }

    // Write main pass frame attributes
    HnLight* DomeLight = nullptr;
    {
#if defined(PBR_MAX_LIGHTS)
#    error PBR_MAX_LIGHTS is defined. The logic below will not work correctly.
#endif
        HLSL::PBRFrameAttribs*  FrameAttribs = reinterpret_cast<HLSL::PBRFrameAttribs*>(m_FrameAttribsData.data());
        HLSL::CameraAttribs&    PrevCamera   = FrameAttribs->PrevCamera;
        HLSL::CameraAttribs&    CamAttribs   = FrameAttribs->Camera;
        HLSL::PBRLightAttribs*  Lights       = reinterpret_cast<HLSL::PBRLightAttribs*>(FrameAttribs + 1);
        HLSL::PBRShadowMapInfo* ShadowMaps   = reinterpret_cast<HLSL::PBRShadowMapInfo*>(Lights + MaxLightCount);

        PrevCamera = CamAttribs;
        if (m_pCamera != nullptr)
        {
            const float4x4  ProjMatrix  = m_pCamera->GetProjectionMatrix(m_Params.UseReverseDepth, Jitter);
            const float4x4& ViewMatrix  = m_pCamera->GetViewMatrix();
            const float4x4& WorldMatrix = m_pCamera->GetWorldMatrix();
            const float4x4  ViewProj    = ViewMatrix * ProjMatrix;

            VERIFY_EXPR(m_FrameBufferWidth > 0 && m_FrameBufferHeight > 0);
            CamAttribs.f4ViewportSize = float4{
                static_cast<float>(m_FrameBufferWidth),
                static_cast<float>(m_FrameBufferHeight),
                1.f / static_cast<float>(m_FrameBufferWidth),
                1.f / static_cast<float>(m_FrameBufferHeight),
            };
            CamAttribs.fHandness = ViewMatrix.Determinant() > 0 ? 1.f : -1.f;

            WriteShaderMatrix(&CamAttribs.mView, ViewMatrix, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mProj, ProjMatrix, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewProj, ViewProj, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewInv, WorldMatrix, !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mProjInv, ProjMatrix.Inverse(), !PackMatrixRowMajor);
            WriteShaderMatrix(&CamAttribs.mViewProjInv, ViewProj.Inverse(), !PackMatrixRowMajor);
            CamAttribs.f4Position = float4{float3::MakeVector(WorldMatrix[3]), 1};
            CamAttribs.f2Jitter   = Jitter;
            CamAttribs.fFStop     = m_pCamera->GetFStop();

            const HnRenderParam::Configuration& RenderConfig  = RenderParam->GetConfig();
            const float                         MetersPerUnit = RenderConfig.MetersPerUnit;

            // USD camera properties are measured in scene units, but Diligent camera expects them in world units.
            CamAttribs.fFocusDistance = m_pCamera->GetFocusDistance() * MetersPerUnit;

            // Diligent sensor properties and focal length are measured in millimeters.
            const float MillimetersPerUnit = MetersPerUnit * 1000.f;

            // Note that by an odd convention, lens and filmback properties are measured in tenths of a scene unit rather than "raw" scene units.
            // https://openusd.org/dev/api/class_usd_geom_camera.html#UsdGeom_CameraUnits
            // So, for example after
            //      UsdCamera.GetFocalLengthAttr().Set(30.f)
            // Reading the attribute will return same value:
            //      float focalLength;
            //      UsdCamera.GetFocalLengthAttr().Get(&focalLength); // focalLength == 30
            // However
            //      focalLength = SceneDelegate->GetCameraParamValue(id, HdCameraTokens->focalLength).Get<float>(); //  focalLength == 3
            //
            // Since HnCamera gets its properties from SceneDelegate, the units are already scaled to scene units.
            // We only need to convert them to world units.

            CamAttribs.fSensorWidth  = m_pCamera->GetHorizontalAperture() * MillimetersPerUnit;
            CamAttribs.fSensorHeight = m_pCamera->GetVerticalAperture() * MillimetersPerUnit;
            CamAttribs.fFocalLength  = m_pCamera->GetFocalLength() * MillimetersPerUnit;
            CamAttribs.fExposure     = m_pCamera->GetExposure();

            float fNearPlaneZ, fFarPlaneZ;
            ProjMatrix.GetNearFarClipPlanes(fNearPlaneZ, fFarPlaneZ, pDevice->GetDeviceInfo().NDC.MinZ == -1);
            VERIFY_EXPR((!m_Params.UseReverseDepth && (fNearPlaneZ <= fFarPlaneZ)) ||
                        (m_Params.UseReverseDepth && (fNearPlaneZ >= fFarPlaneZ)));
            CamAttribs.SetClipPlanes(fNearPlaneZ, fFarPlaneZ);

            if (CamAttribs.mView != PrevCamera.mView)
            {
                CameraTransformDirty = true;
            }
            else
            {
                float4x4 PrevProj;
                WriteShaderMatrix(&PrevProj, m_pCamera->GetProjectionMatrix(m_Params.UseReverseDepth, PrevCamera.f2Jitter), !PackMatrixRowMajor);
                if (PrevProj != PrevCamera.mProj)
                {
                    CameraTransformDirty = true;
                }
            }

            if (PrevCamera.f4ViewportSize.x == 0)
            {
                // First frame
                PrevCamera           = CamAttribs;
                CameraTransformDirty = true;
            }
        }
        else
        {
            UNEXPECTED("Camera is null. It should've been set in Prepare()");
        }

        int LightCount = 0;
        for (HnLight* Light : RenderDelegate->GetLights())
        {
            if (!Light->IsVisible())
                continue;

            if (Light->GetTypeId() == pxr::HdPrimTypeTokens->domeLight)
            {
                // Only use the first dome light
                if (DomeLight == nullptr)
                    DomeLight = Light;

                continue;
            }

            if (Light->GetParams().Type == GLTF::Light::TYPE::UNKNOWN)
                continue;

            GLTF_PBR_Renderer::PBRLightShaderAttribsData LightAttribs{
                &Light->GetParams(),
                &Light->GetPosition(),
                &Light->GetDirection(),
            };

            const int ShadowMapIndex = Light->GetFrameAttribsIndex();
            if (Light->ShadowsEnabled() && ShadowMapIndex >= 0)
            {
                if (const HLSL::PBRShadowMapInfo* pShadowMapInfo = Light->GetShadowMapShaderInfo())
                {
                    HLSL::PBRShadowMapInfo& DstShadowMap = ShadowMaps[ShadowMapIndex];
                    WriteShaderMatrix(&DstShadowMap.WorldToLightProjSpace, pShadowMapInfo->WorldToLightProjSpace, !PackMatrixRowMajor);
                    DstShadowMap.UVScale        = pShadowMapInfo->UVScale;
                    DstShadowMap.UVBias         = pShadowMapInfo->UVBias;
                    DstShadowMap.ShadowMapSlice = pShadowMapInfo->ShadowMapSlice;
                }
                else
                {
                    UNEXPECTED("Shadow map info is null");
                }
                LightAttribs.ShadowMapIndex = ShadowMapIndex;
            }

            GLTF_PBR_Renderer::WritePBRLightShaderAttribs(LightAttribs, Lights + LightCount);

            ++LightCount;
            if (LightCount >= MaxLightCount)
            {
                break;
            }
        }

        {
            HLSL::PBRRendererShaderParameters& RendererParams = FrameAttribs->Renderer;
            RenderDelegate->GetUSDRenderer()->SetInternalShaderParameters(RendererParams);

            RendererParams.LightCount = LightCount;

            RendererParams.OcclusionStrength = m_Params.Renderer.OcclusionStrength;
            RendererParams.EmissionScale     = m_Params.Renderer.EmissionScale;

            RendererParams.IBLScale = DomeLight != nullptr ?
                DomeLight->GetParams().Color * DomeLight->GetParams().Intensity * m_Params.Renderer.IBLScale :
                float4{0};

            RendererParams.UnshadedColor  = m_Params.Renderer.UnshadedColor;
            RendererParams.HighlightColor = float4{0, 0, 0, 0};
            RendererParams.PointSize      = m_Params.Renderer.PointSize;

            RendererParams.MipBias = UseTAA ? -0.5 : 0.0;

            // Tone mapping is performed in the post-processing pass
            RendererParams.AverageLogLum = 0.3f;
            RendererParams.MiddleGray    = HLSL::ToneMappingAttribs{}.fMiddleGray;
            RendererParams.WhitePoint    = HLSL::ToneMappingAttribs{}.fWhitePoint;

            RendererParams.Time = static_cast<float>(m_CurrFrameTime);

            {
                float LoadingAnimationFactor = m_FallBackPsoUseStartTime > 0 ? 1.0 : 0.0;
                if (m_FallBackPsoUseStartTime > 0 && m_FallBackPsoUseEndTime > m_FallBackPsoUseStartTime)
                {
                    float FallbackDuration   = static_cast<float>(m_FallBackPsoUseEndTime - m_FallBackPsoUseStartTime);
                    float TransitionDuration = std::min(m_Params.Renderer.LoadingAnimationTransitionDuration, FallbackDuration * 0.5f);
                    LoadingAnimationFactor   = TransitionDuration > 0.f ?
                        static_cast<float>(m_CurrFrameTime - m_FallBackPsoUseEndTime) / TransitionDuration :
                        1.f;
                    LoadingAnimationFactor = std::max(1.f - LoadingAnimationFactor, 0.f);
                    if (LoadingAnimationFactor == 0)
                    {
                        // Transition is over
                        m_FallBackPsoUseStartTime = -1;
                    }
                }

                RendererParams.LoadingAnimation.Factor     = LoadingAnimationFactor;
                RendererParams.LoadingAnimation.Color0     = m_Params.Renderer.LoadingAnimationColor0;
                RendererParams.LoadingAnimation.Color1     = m_Params.Renderer.LoadingAnimationColor1;
                RendererParams.LoadingAnimation.WorldScale = m_Params.Renderer.LoadingAnimationWorldScale;
                RendererParams.LoadingAnimation.Speed      = m_Params.Renderer.LoadingAnimationSpeed;

                LoadingAnimationActive = LoadingAnimationFactor > 0;
            }
        }
    }

    pCtx->UpdateBuffer(pFrameAttrbisCB, 0, m_FrameAttribsData.size(), m_FrameAttribsData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    std::vector<StateTransitionDesc> Barriers =
        {
            {pFrameAttrbisCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
    if (DomeLight == nullptr)
    {
        // Since there is no dome light, IBL cube maps may still be in RTV state after they were cleared during the initialization.
        Barriers.emplace_back(Renderer.GetIrradianceCubeSRV()->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE);
        Barriers.emplace_back(Renderer.GetPrefilteredEnvMapSRV()->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE);
    }
    pCtx->TransitionResourceStates(static_cast<Uint32>(Barriers.size()), Barriers.data());
}

void HnBeginFrameTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    HnRenderParam*    RenderParam    = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Begin Frame"};

    // NB: we can't move the buffer update to Prepare() because we need TAA parameters
    //     that are set by HnPostProcessTask::Prepare().
    if (IBuffer* pFrameAttribsCB = RenderDelegate->GetFrameAttribsCB())
    {
        float2 JitterOffsets{0, 0};
        bool   UseTAA = false;
        // Set by HnPostProcessTask::Prepare()
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->taaJitterOffsets, JitterOffsets);
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->useTaa, UseTAA);

        bool FallBackPsoInUse = false;
        if (GetTaskContextData(TaskCtx, HnRenderResourceTokens->fallBackPsoInUse, FallBackPsoInUse, /*Required = */ false))
        {
            if (FallBackPsoInUse)
            {
                if (m_FallBackPsoUseStartTime < 0)
                {
                    // Fallback PSO is in use for the first time
                    m_FallBackPsoUseStartTime = m_CurrFrameTime;
                    m_FallBackPsoUseEndTime   = -1;
                }
            }
            else if (m_FallBackPsoUseEndTime < 0)
            {
                // First frame after fallback PSO was used
                m_FallBackPsoUseEndTime = m_CurrFrameTime;
            }
        }
        // Reset the fallBackPsoInUse flag.
        // HnRenderRprimsTask::Execute sets it to true if the fallback PSO was used.
        (*TaskCtx)[HnRenderResourceTokens->fallBackPsoInUse] = pxr::VtValue{false};

        bool CameraTransformDirty   = false;
        bool LoadingAnimationActive = false;
        UpdateFrameConstants(pCtx, pFrameAttribsCB, UseTAA, JitterOffsets, CameraTransformDirty, LoadingAnimationActive);
        (*TaskCtx)[HnRenderResourceTokens->frameShaderAttribs] = pxr::VtValue{reinterpret_cast<HLSL::PBRFrameAttribs*>(m_FrameAttribsData.data())};
        // Will be used by HnPostProcessTask::Execute()
        (*TaskCtx)[HnRenderResourceTokens->cameraTransformDirty] = pxr::VtValue{CameraTransformDirty};
        if (LoadingAnimationActive)
        {
            // Disable temporal AA while loading animation is active
            (*TaskCtx)[HnRenderResourceTokens->suspendSuperSampling] = pxr::VtValue{true};
        }
        RenderParam->SetLoadingAnimationActive(LoadingAnimationActive);
    }
    else
    {
        UNEXPECTED("Frame attribs constant buffer is null");
    }
}

} // namespace USD

} // namespace Diligent
