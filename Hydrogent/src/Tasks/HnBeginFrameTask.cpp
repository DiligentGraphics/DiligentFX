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

static void UpdateRenderPassState(const HnBeginFrameTaskParams& Params,
                                  const TEXTURE_FORMAT*         RTVFormats,
                                  Uint32                        NumRTVs,
                                  TEXTURE_FORMAT                DSVFormat,
                                  HnRenderPassState&            RPState)
{
    RPState.SetNumRenderTargets(NumRTVs);
    for (Uint32 i = 0; i < NumRTVs; ++i)
        RPState.SetRenderTargetFormat(i, RTVFormats[i]);
    RPState.SetDepthStencilFormat(DSVFormat);

    RPState.SetDepthBias(Params.State.DepthBias, Params.State.SlopeScaledDepthBias);
    RPState.SetDepthFunc(Params.State.DepthFunc);
    RPState.SetDepthBiasEnabled(Params.State.DepthBiasEnabled);
    RPState.SetEnableDepthTest(Params.State.DepthTestEnabled);
    RPState.SetEnableDepthClamp(Params.State.DepthClampEnabled);

    RPState.SetCullStyle(Params.State.CullStyle);

    RPState.SetStencil(Params.State.StencilFunc, Params.State.StencilRef, Params.State.StencilMask,
                       Params.State.StencilFailOp, Params.State.StencilZFailOp, Params.State.StencilZPassOp);

    RPState.SetFrontFaceCCW(Params.State.FrontFaceCCW);
}

void HnBeginFrameTask::Sync(pxr::HdSceneDelegate* Delegate,
                            pxr::HdTaskContext*   TaskCtx,
                            pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        if (GetTaskParams(Delegate, m_Params))
        {
            UpdateRenderPassState(m_Params,
                                  m_Params.Formats.GBuffer.data(),
                                  m_Params.Formats.GBuffer.size(),
                                  m_Params.Formats.Depth,
                                  m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueSelected]);

            UpdateRenderPassState(m_Params,
                                  m_Params.Formats.GBuffer.data(),
                                  m_Params.Formats.GBuffer.size(),
                                  m_Params.Formats.Depth,
                                  m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll]);

            UpdateRenderPassState(m_Params,
                                  nullptr,
                                  0,
                                  m_Params.Formats.Depth,
                                  m_RenderPassStates[HnRenderResourceTokens->renderPass_TransparentSelected]);

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
    const auto& FinalTargetDesc = pFinalColorRTV->GetTexture()->GetDesc();

    m_FrameBufferWidth  = FinalTargetDesc.Width;
    m_FrameBufferHeight = FinalTargetDesc.Height;

    auto UpdateBrim = [&](const pxr::SdfPath& Id, TEXTURE_FORMAT Format, const std::string& Name) -> ITextureView* {
        if (Format == TEX_FORMAT_UNKNOWN)
            return nullptr;

        VERIFY_EXPR(!Id.IsEmpty());

        HnRenderBuffer* Renderbuffer = static_cast<HnRenderBuffer*>(RenderIndex->GetBprim(pxr::HdPrimTypeTokens->renderBuffer, Id));
        if (Renderbuffer == nullptr)
        {
            UNEXPECTED("Render buffer is not set at Id ", Id);
            return nullptr;
        }

        if (auto* pView = Renderbuffer->GetTarget())
        {
            const auto& ViewDesc   = pView->GetDesc();
            const auto& TargetDesc = pView->GetTexture()->GetDesc();
            if (TargetDesc.GetWidth() == FinalTargetDesc.GetWidth() &&
                TargetDesc.GetHeight() == FinalTargetDesc.GetHeight() &&
                ViewDesc.Format == Format)
                return pView;
        }

        const bool  IsDepth = GetTextureFormatAttribs(Format).IsDepthStencil();
        auto* const pDevice = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate())->GetDevice();

        auto TargetDesc      = FinalTargetDesc;
        TargetDesc.Name      = Name.c_str();
        TargetDesc.Format    = Format;
        TargetDesc.BindFlags = (IsDepth ? BIND_DEPTH_STENCIL : BIND_RENDER_TARGET) | BIND_SHADER_RESOURCE;

        RefCntAutoPtr<ITexture> pTarget;
        pDevice->CreateTexture(TargetDesc, nullptr, &pTarget);
        if (!pTarget)
        {
            UNEXPECTED("Failed to create ", Name, " texture");
            return nullptr;
        }
        LOG_INFO_MESSAGE("HnBeginFrameTask: created ", TargetDesc.GetWidth(), "x", TargetDesc.GetHeight(), " ", Name, " texture");

        auto* const pView = pTarget->GetDefaultView(IsDepth ? TEXTURE_VIEW_DEPTH_STENCIL : TEXTURE_VIEW_RENDER_TARGET);
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
        // NB: we should clear alpha to zero to get correct alpha in the final image!
        ClearValues[i] = (i == HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR) ?
            float4{m_Params.ClearColor.r, m_Params.ClearColor.g, m_Params.ClearColor.b, 0} :
            float4{0};
    }

    HnRenderPassState& RP_OpaqueSelected                  = m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueSelected];
    HnRenderPassState& RP_OpaqueUnselected_TransparentAll = m_RenderPassStates[HnRenderResourceTokens->renderPass_OpaqueUnselected_TransparentAll];
    HnRenderPassState& RP_TransparentSelected             = m_RenderPassStates[HnRenderResourceTokens->renderPass_TransparentSelected];

    // We first render selected objects using the selection depth buffer.
    // Selection depth buffer is copied to the main depth buffer by the HnCopySelectionDepthTask.
    RP_OpaqueSelected.Begin(HnFrameRenderTargets::GBUFFER_TARGET_COUNT, m_FrameRenderTargets.GBufferRTVs.data(), m_FrameRenderTargets.SelectionDepthDSV, ClearValues.data(), m_Params.ClearDepth, ~0u);
    RP_OpaqueUnselected_TransparentAll.Begin(HnFrameRenderTargets::GBUFFER_TARGET_COUNT, m_FrameRenderTargets.GBufferRTVs.data(), m_FrameRenderTargets.DepthDSV);
    RP_TransparentSelected.Begin(0, nullptr, m_FrameRenderTargets.SelectionDepthDSV);

    for (HnRenderPassState* RPState : {&RP_OpaqueSelected, &RP_OpaqueUnselected_TransparentAll, &RP_TransparentSelected})
    {
        RPState->SetCamera(m_pCamera);
    }

    // Register render pass states in the task context
    for (auto& it : m_RenderPassStates)
    {
        (*TaskCtx)[it.first] = pxr::VtValue{&it.second};
    }
}

void HnBeginFrameTask::Prepare(pxr::HdTaskContext* TaskCtx,
                               pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex->GetRenderDelegate());
    const USD_Renderer& Renderer       = *RenderDelegate->GetUSDRenderer();

    Uint32 FrameNumber = 0;
    if (HnRenderParam* pRenderParam = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam()))
    {
        double CurrFrameTime = m_FrameTimer.GetElapsedTime();
        pRenderParam->SetElapsedTime(static_cast<float>(CurrFrameTime - pRenderParam->GetFrameTime()));
        pRenderParam->SetFrameTime(CurrFrameTime);
        pRenderParam->SetFrameNumber(pRenderParam->GetFrameNumber() + 1);
        FrameNumber = pRenderParam->GetFrameNumber();
    }
    else
    {
        UNEXPECTED("Render param is null");
    }

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
    (*TaskCtx)[HnRenderResourceTokens->backgroundDepth]                = pxr::VtValue{m_Params.ClearDepth};

    bool ResetTAA = false;
    if (!m_Params.CameraId.IsEmpty())
    {
        m_pCamera = static_cast<const HnCamera*>(m_RenderIndex->GetSprim(pxr::HdPrimTypeTokens->camera, m_Params.CameraId));
        if (m_pCamera == nullptr)
        {
            LOG_ERROR_MESSAGE("Camera is not set at Id ", m_Params.CameraId);
        }
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
                                            bool&           CameraTransformDirty)
{
    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IRenderDevice*      pDevice        = RenderDelegate->GetDevice();
    const USD_Renderer& Renderer       = *RenderDelegate->GetUSDRenderer();
    const int           MaxLightCount  = Renderer.GetSettings().MaxLightCount;

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

            CamAttribs.mViewT        = ViewMatrix.Transpose();
            CamAttribs.mProjT        = ProjMatrix.Transpose();
            CamAttribs.mViewProjT    = ViewProj.Transpose();
            CamAttribs.mViewInvT     = ViewMatrix.Inverse().Transpose();
            CamAttribs.mProjInvT     = ProjMatrix.Inverse().Transpose();
            CamAttribs.mViewProjInvT = ViewProj.Inverse().Transpose();
            CamAttribs.f4Position    = float4{0, 0, 0, 1};
            CamAttribs.f2Jitter      = float2{0, 0};

            memset(&ShadowAttribs->Renderer, 0, sizeof(HLSL::PBRRendererShaderParameters));
        }
    }

    // Write main pass frame attributes
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
            float4x4 ProjMatrix = m_pCamera->GetProjectionMatrix();
            ProjMatrix[2][0]    = Jitter.x;
            ProjMatrix[2][1]    = Jitter.y;

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

            CamAttribs.mViewT        = ViewMatrix.Transpose();
            CamAttribs.mProjT        = ProjMatrix.Transpose();
            CamAttribs.mViewProjT    = ViewProj.Transpose();
            CamAttribs.mViewInvT     = WorldMatrix.Transpose();
            CamAttribs.mProjInvT     = ProjMatrix.Inverse().Transpose();
            CamAttribs.mViewProjInvT = ViewProj.Inverse().Transpose();
            CamAttribs.f4Position    = float4{float3::MakeVector(WorldMatrix[3]), 1};
            CamAttribs.f2Jitter      = Jitter;

            ProjMatrix.GetNearFarClipPlanes(CamAttribs.fNearPlaneZ, CamAttribs.fFarPlaneZ, pDevice->GetDeviceInfo().NDC.MinZ == -1);

            if (CamAttribs.mViewT != PrevCamera.mViewT)
            {
                CameraTransformDirty = true;
            }
            else
            {
                float4x4 PrevProjT = PrevCamera.mProjT;
                PrevProjT[0][2]    = Jitter.x;
                PrevProjT[1][2]    = Jitter.y;
                if (PrevProjT != CamAttribs.mProjT)
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
            if (!Light->IsVisible() || Light->GetParams().Type == GLTF::Light::TYPE::UNKNOWN)
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
                    ShadowMaps[ShadowMapIndex] = *pShadowMapInfo;
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
            RendererParams.IBLScale          = m_Params.Renderer.IBLScale;

            RendererParams.UnshadedColor  = m_Params.Renderer.UnshadedColor;
            RendererParams.HighlightColor = float4{0, 0, 0, 0};
            RendererParams.PointSize      = m_Params.Renderer.PointSize;

            RendererParams.MipBias = UseTAA ? -0.5 : 0.0;

            // Tone mapping is performed in the post-processing pass
            RendererParams.AverageLogLum = 0.3f;
            RendererParams.MiddleGray    = 0.18f;
            RendererParams.WhitePoint    = 3.0f;
        }
    }

    pCtx->UpdateBuffer(pFrameAttrbisCB, 0, m_FrameAttribsData.size(), m_FrameAttribsData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    StateTransitionDesc Barriers[] =
        {
            {pFrameAttrbisCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
}

void HnBeginFrameTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx           = RenderDelegate->GetDeviceContext();

    // NB: we can't move the buffer update to Prepare() because we need TAA parameters
    //     that are set by HnPostProcessTask::Prepare().
    if (IBuffer* pFrameAttribsCB = RenderDelegate->GetFrameAttribsCB())
    {
        float2 JitterOffsets{0, 0};
        bool   UseTAA = false;
        // Set by HnPostProcessTask::Prepare()
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->taaJitterOffsets, JitterOffsets);
        GetTaskContextData(TaskCtx, HnRenderResourceTokens->useTaa, UseTAA);

        bool CameraTransformDirty = false;
        UpdateFrameConstants(pCtx, pFrameAttribsCB, UseTAA, JitterOffsets, CameraTransformDirty);
        (*TaskCtx)[HnRenderResourceTokens->frameShaderAttribs] = pxr::VtValue{reinterpret_cast<HLSL::PBRFrameAttribs*>(m_FrameAttribsData.data())};
        // Will be used by HnPostProcessTask::Execute()
        (*TaskCtx)[HnRenderResourceTokens->cameraTransformDirty] = pxr::VtValue{CameraTransformDirty};
    }
    else
    {
        UNEXPECTED("Frame attribs constant buffer is null");
    }
}

} // namespace USD

} // namespace Diligent
