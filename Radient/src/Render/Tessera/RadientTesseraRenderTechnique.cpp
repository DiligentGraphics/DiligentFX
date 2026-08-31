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

#include "Render/Tessera/RadientTesseraRenderTechnique.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "Core/RadientViewImpl.hpp"

#include "Cast.hpp"
#include "Errors.hpp"

#include <cmath>

namespace Diligent
{

namespace
{

const RadientMaterialDefaultTextures& GetDefaultMaterialTextures(RadientAssetManagerImpl* pAssetManager)
{
    if (pAssetManager == nullptr)
        LOG_ERROR_AND_THROW("Radient Tessera render technique asset manager must not be null");

    return pAssetManager->GetDefaultMaterialTextures();
}

IThreadPool* ValidateThreadPool(IThreadPool* pThreadPool)
{
    if (pThreadPool == nullptr)
        LOG_ERROR_AND_THROW("Radient Tessera render technique thread pool must not be null");

    return pThreadPool;
}

float ValidatePostFXTransitionDuration(float Duration)
{
    if (!std::isfinite(Duration) || Duration < 0.f)
    {
        constexpr float DefaultDuration = 1.f;
        LOG_ERROR_MESSAGE("Radient post-FX transition duration must be finite and non-negative. Using the default value of ", DefaultDuration, " second.");
        return DefaultDuration;
    }

    return Duration;
}

bool GetTextureViewSamplingInfo(ITextureView*               pTextureView,
                                RadientTextureSamplingInfo& SamplingInfo) noexcept
{
    if (pTextureView == nullptr)
        return false;

    ITexture* const pTexture = pTextureView->GetTexture();
    if (pTexture == nullptr)
        return false;

    const TextureDesc& Desc = pTexture->GetDesc();
    SamplingInfo.Width      = Desc.Width;
    SamplingInfo.Height     = Desc.Height;
    SamplingInfo.MipLevels  = Desc.MipLevels;
    return SamplingInfo.MipLevels != 0;
}

} // namespace

RadientTesseraRenderTechnique::RadientTesseraRenderTechnique(IThreadPool*               pThreadPool,
                                                             RadientAssetManagerImpl*   pAssetManager,
                                                             const RadientRendererDesc& Desc) :
    m_pThreadPool{ValidateThreadPool(pThreadPool)},
    m_pAssetManager{pAssetManager},
    m_GeometryRenderer{Desc.MaterialTextureSlotCount,
                       GetDefaultMaterialTextures(pAssetManager),
                       Desc.MultiDrawBatchSize},
    m_EnableAsyncPipelineCompilation{Desc.EnableAsyncPipelineCompilation == True},
    m_PostFXTransitionDuration{ValidatePostFXTransitionDuration(Desc.PostFXTransitionDuration)}
{}

RADIENT_STATUS RadientTesseraRenderTechnique::SyncScene(const IRadientScene& Scene)
{
    SceneRenderState& SceneState = GetOrCreateSceneRenderState(Scene);

    const RadientTesseraMaterialResolveContext MaterialResolveContext{
        *m_pThreadPool,
        m_GeometryRenderer.GetMaterialCache(),
    };
    const RADIENT_STATUS Status = SceneState.DrawableCache.SyncScene(Scene, MaterialResolveContext);
    if (RADIENT_FAILED(Status))
        return Status;

    // Keep rendering ready content, but report that renderer-specific mesh or
    // material work is still in flight so explicit synchronization points can
    // wait without relying on a fixed number of warm-up frames.
    return SceneState.DrawableCache.HasPendingRenderables() ? RADIENT_STATUS_PENDING : Status;
}

RADIENT_STATUS RadientTesseraRenderTechnique::PrepareFrame(const RadientRenderContext& Context)
{
    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.pRenderTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ViewRenderState& ViewState = GetOrCreateViewRenderState(pView);

    RADIENT_STATUS FrameStatus = ViewState.FrameTargets.Prepare(Context.pDevice, *ViewDesc.pRenderTarget);
    if (RADIENT_FAILED(FrameStatus) || Context.pDevice == nullptr || Context.pContext == nullptr)
        return FrameStatus;

    SceneRenderState* const pSceneState = FindSceneRenderState(ViewDesc.pScene, false);
    if (pSceneState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    // Structured-buffer matrices follow the same backend-specific packing as
    // PBR_Renderer::WriteSkinningData(). Every changed skin writes one half of
    // its allocation before the geometry renderer uploads the joint buffer.
    const bool PackJointMatricesRowMajor = Context.pDevice->GetDeviceInfo().IsWebGPUDevice();

    const RADIENT_STATUS SkinningStatus =
        pSceneState->DrawableCache.PrepareSkinningData(PackJointMatricesRowMajor);
    if (RADIENT_FAILED(SkinningStatus))
        return SkinningStatus;

    // NO_CHANGE only describes whether CPU skin palettes were rebuilt. It does
    // not affect whether the rest of the frame resources are ready.
    if (SkinningStatus != RADIENT_STATUS_NO_CHANGE)
        FrameStatus = CombineDependencyStatus(FrameStatus, SkinningStatus);

    const RADIENT_STATUS GeometryRendererStatus =
        m_GeometryRenderer.Prepare(Context.pDevice,
                                   Context.pContext,
                                   m_pAssetManager->GetResourceManager());
    if (RADIENT_FAILED(GeometryRendererStatus))
        return GeometryRendererStatus;
    FrameStatus = CombineDependencyStatus(FrameStatus, GeometryRendererStatus);

    RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
    if (pPBRRenderer == nullptr || pPBRRenderer->GetFrameAttribsCB() == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const bool HasDrawables = !pSceneState->DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    if (HasDrawables || HasSkybox)
    {
        const RADIENT_STATUS ViewStatus = pView->Prepare(*pPBRRenderer, Context.pContext);
        if (RADIENT_FAILED(ViewStatus))
            return ViewStatus;
        FrameStatus = CombineDependencyStatus(FrameStatus, ViewStatus);
    }

    const RADIENT_STATUS GeometryPassStatus =
        pSceneState->GeometryPass.Prepare(m_GeometryRenderer,
                                          Context.pDevice,
                                          Context.pContext,
                                          pSceneState->DrawableCache,
                                          ViewState.FrameTargets,
                                          RadientPBRRenderer::GetDebugViewType(ViewDesc.DebugVisualization),
                                          ViewDesc.EnableIBL);
    if (RADIENT_FAILED(GeometryPassStatus))
        return GeometryPassStatus;
    FrameStatus = CombineDependencyStatus(FrameStatus, GeometryPassStatus);

    const RADIENT_STATUS SkyboxStatus =
        m_SkyboxPass.Prepare(m_GeometryRenderer, Context.pDevice, ViewState.FrameTargets);
    if (RADIENT_FAILED(SkyboxStatus))
        return SkyboxStatus;
    FrameStatus = CombineDependencyStatus(FrameStatus, SkyboxStatus);

    RadientTesseraPostProcessPipeline::PrepareInfo PostProcessInfo{ViewState.FrameTargets, ViewDesc};
    PostProcessInfo.pDevice                = Context.pDevice;
    PostProcessInfo.pContext               = Context.pContext;
    PostProcessInfo.FrameIndex             = ViewState.FrameHistory.GetFrameIndex();
    PostProcessInfo.pFrameAttribsCB        = pPBRRenderer->GetFrameAttribsCB();
    PostProcessInfo.pPreintegratedGGXSRV   = pPBRRenderer->GetPreintegratedGGX_SRV();
    const RADIENT_STATUS PostProcessStatus = ViewState.PostProcessPipeline.Prepare(PostProcessInfo);
    return CombineDependencyStatus(FrameStatus, PostProcessStatus);
}

RADIENT_STATUS RadientTesseraRenderTechnique::BeginFrame(const RadientRenderContext& Context)
{
    m_pFrameSRB.Release();
    m_pActiveViewState  = nullptr;
    m_pActiveSceneState = nullptr;
    m_FrameActive       = false;

    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ViewRenderState* const pViewState = FindViewRenderState(pView, false);
    if (pViewState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const RadientViewDesc&  ViewDesc    = pView->GetDesc();
    SceneRenderState* const pSceneState = FindSceneRenderState(ViewDesc.pScene, false);
    if (pSceneState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const bool HasDrawables = !pSceneState->DrawableCache.GetDrawLists().IsEmpty();

    pViewState->FrameTargets.ClearGBuffer(Context.pContext, ViewDesc.ClearColor);

    if (HasDrawables)
    {
        RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
        if (pPBRRenderer == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const RadientTesseraBufferSuballocator& JointBuffer = m_GeometryRenderer.GetJointBuffer();

        m_pFrameSRB = pPBRRenderer->GetOrCreateFrameSRB(
            pView->GetIBLResources(),
            JointBuffer.GetBuffer(),
            JointBuffer.GetVersion());
        if (m_pFrameSRB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
    }

    const RadientTesseraGeometryFrameAttribs GeometryFrameAttribs{
        ViewDesc.Environment,
        ViewDesc.pScene,
        ViewDesc.Camera,
        pView->GetPrefilteredEnvMapSRV(),
        pViewState->PostProcessPipeline.GetCameraJitter(),
        RadientPBRRenderer::GetDebugViewType(ViewDesc.DebugVisualization),
    };

    const RADIENT_STATUS Status = m_GeometryRenderer.BeginFrame(
        Context.pDevice,
        Context.pContext,
        pSceneState->DrawableCache.GetLightList(),
        m_pAssetManager->GetResourceManager(),
        GeometryFrameAttribs,
        pViewState->FrameTargets,
        pViewState->FrameHistory);

    m_FrameActive = !RADIENT_FAILED(Status);
    if (m_FrameActive)
    {
        m_pActiveViewState  = pViewState;
        m_pActiveSceneState = pSceneState;
    }
    else
        m_pFrameSRB.Release();

    return Status;
}

RADIENT_STATUS RadientTesseraRenderTechnique::Render(const RadientRenderContext& Context)
{
    const RADIENT_STATUS ValidationStatus = ValidateActiveFrameContext(Context, "Render");
    if (RADIENT_FAILED(ValidationStatus))
        return ValidationStatus;

    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    VERIFY_EXPR(pView != nullptr);

    ViewRenderState&       ViewState    = *m_pActiveViewState;
    SceneRenderState&      SceneState   = *m_pActiveSceneState;
    const RadientViewDesc& ViewDesc     = pView->GetDesc();
    const bool             HasDrawables = !SceneState.DrawableCache.GetDrawLists().IsEmpty();
    const bool             HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    RADIENT_STATUS         FrameStatus  = RADIENT_STATUS_OK;

    const auto ExecuteGeometryPass = [&](PBR_Renderer::ALPHA_MODE AlphaMode) {
        const RADIENT_STATUS Status = SceneState.GeometryPass.Execute(
            m_GeometryRenderer,
            Context.pDevice,
            Context.pContext,
            m_pFrameSRB,
            AlphaMode,
            SceneState.DrawableCache,
            ViewState.FrameTargets,
            ViewState.FrameHistory);

        FrameStatus = CombineDependencyStatus(FrameStatus, Status);
    };

    if (m_FrameActive)
    {
        if (HasDrawables)
        {
            ExecuteGeometryPass(PBR_Renderer::ALPHA_MODE_OPAQUE);
            ExecuteGeometryPass(PBR_Renderer::ALPHA_MODE_MASK);
        }

        if (HasSkybox)
        {
            ITextureView*              pSkyboxSRV = nullptr;
            RadientTextureSamplingInfo SamplingInfo;
            bool                       SphereMapRow0IsNegativeY = false;
            Float32                    Yaw                      = 0.f;
            switch (ViewDesc.Skybox.Source)
            {
                case RADIENT_SKYBOX_SOURCE_ENVIRONMENT:
                    pSkyboxSRV = RadientAssetManagerImpl::GetTextureSRV(ViewDesc.Environment.pEnvironmentMap);
                    RadientTextureAssetManager::GetTextureSamplingInfo(ViewDesc.Environment.pEnvironmentMap, SamplingInfo);
                    SphereMapRow0IsNegativeY = ViewDesc.Environment.SphereMapRow0IsNegativeY;
                    Yaw                      = ViewDesc.Environment.Yaw;
                    break;

                case RADIENT_SKYBOX_SOURCE_TEXTURE:
                    pSkyboxSRV = RadientAssetManagerImpl::GetTextureSRV(ViewDesc.Skybox.pTexture);
                    RadientTextureAssetManager::GetTextureSamplingInfo(ViewDesc.Skybox.pTexture, SamplingInfo);
                    SphereMapRow0IsNegativeY = ViewDesc.Skybox.SphereMapRow0IsNegativeY;
                    Yaw                      = ViewDesc.Skybox.Yaw;
                    break;

                case RADIENT_SKYBOX_SOURCE_IRRADIANCE:
                    pSkyboxSRV = pView->GetIrradianceCubeSRV();
                    GetTextureViewSamplingInfo(pSkyboxSRV, SamplingInfo);
                    Yaw = ViewDesc.Environment.Yaw;
                    break;

                case RADIENT_SKYBOX_SOURCE_PREFILTERED_ENVIRONMENT:
                    pSkyboxSRV = pView->GetPrefilteredEnvMapSRV();
                    GetTextureViewSamplingInfo(pSkyboxSRV, SamplingInfo);
                    Yaw = ViewDesc.Environment.Yaw;
                    break;

                default:
                    UNEXPECTED("Unexpected Radient skybox source");
                    break;
            }

            if (pSkyboxSRV != nullptr && SamplingInfo.MipLevels != 0)
            {
                const RADIENT_STATUS Status = m_SkyboxPass.Execute(Context.pContext,
                                                                   ViewDesc.Skybox,
                                                                   pSkyboxSRV,
                                                                   SamplingInfo,
                                                                   SphereMapRow0IsNegativeY,
                                                                   Yaw,
                                                                   ViewState.FrameTargets);

                FrameStatus = CombineDependencyStatus(FrameStatus, Status);
            }
        }

        if (HasDrawables)
            ExecuteGeometryPass(PBR_Renderer::ALPHA_MODE_BLEND);
    }

    const RADIENT_STATUS PostProcessStatus = ViewState.PostProcessPipeline.Execute(
        Context.pDevice,
        Context.pContext,
        ViewState.FrameTargets,
        !ViewState.FrameHistory.HasCameraHistory());
    return CombineDependencyStatus(FrameStatus, PostProcessStatus);
}

RADIENT_STATUS RadientTesseraRenderTechnique::EndFrame(const RadientRenderContext& Context)
{
    const RADIENT_STATUS ValidationStatus = ValidateActiveFrameContext(Context, "EndFrame");

    if (m_FrameActive)
    {
        VERIFY_EXPR(m_pActiveViewState != nullptr);
        if (m_pActiveViewState != nullptr)
        {
            m_GeometryRenderer.EndFrame(m_pActiveViewState->FrameHistory);
            m_pActiveViewState->FrameTargets.CommitFrame();
        }
    }

    m_pActiveViewState  = nullptr;
    m_pActiveSceneState = nullptr;
    m_FrameActive       = false;
    m_pFrameSRB.Release();

    return ValidationStatus;
}

RADIENT_STATUS RadientTesseraRenderTechnique::ValidateActiveFrameContext(
    const RadientRenderContext& Context,
    const Char*                 Operation) const
{
    if (!m_FrameActive || m_pActiveViewState == nullptr || m_pActiveSceneState == nullptr)
    {
        LOG_ERROR_MESSAGE("RadientTesseraRenderTechnique::", Operation, " requires an active frame");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    RefCntAutoPtr<IRadientView> pActiveView = m_pActiveViewState->WeakView.Lock();
    if (Context.Attribs.pView == nullptr || pActiveView.RawPtr() != Context.Attribs.pView)
    {
        LOG_ERROR_MESSAGE("RadientTesseraRenderTechnique::", Operation,
                          " view does not match the view passed to BeginFrame");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    RadientViewImpl* const pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    VERIFY_EXPR(pView != nullptr);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientScene> pActiveScene = m_pActiveSceneState->WeakScene.Lock();
    if (pActiveScene.RawPtr() != pView->GetDesc().pScene)
    {
        LOG_ERROR_MESSAGE("RadientTesseraRenderTechnique::", Operation,
                          " scene does not match the scene passed to BeginFrame");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    return RADIENT_STATUS_OK;
}

RadientTesseraRenderTechnique::SceneRenderState* RadientTesseraRenderTechnique::FindSceneRenderState(
    const IRadientScene* pScene,
    bool                 PruneExpired)
{
    if (pScene == nullptr)
        return nullptr;

    SceneRenderState* pResult = nullptr;
    for (auto It = m_SceneRenderStates.begin(); It != m_SceneRenderStates.end();)
    {
        RefCntAutoPtr<IRadientScene> pCachedScene = (*It)->WeakScene.Lock();
        if (pCachedScene == nullptr && PruneExpired)
        {
            It = m_SceneRenderStates.erase(It);
        }
        else
        {
            if (pCachedScene == pScene)
            {
                if (!PruneExpired)
                    return It->get();

                pResult = It->get();
            }

            ++It;
        }
    }

    return pResult;
}

RadientTesseraRenderTechnique::SceneRenderState& RadientTesseraRenderTechnique::GetOrCreateSceneRenderState(
    const IRadientScene& Scene)
{
    // SyncScene calls this once per update, making it a natural point to
    // release renderer state for scenes that are no longer alive.
    if (SceneRenderState* pState = FindSceneRenderState(&Scene, true))
        return *pState;

    m_SceneRenderStates.push_back(
        std::make_unique<SceneRenderState>(const_cast<IRadientScene*>(&Scene),
                                           m_EnableAsyncPipelineCompilation,
                                           m_GeometryRenderer.GetJointBuffer()));
    return *m_SceneRenderStates.back();
}

RadientTesseraRenderTechnique::ViewRenderState* RadientTesseraRenderTechnique::FindViewRenderState(IRadientView* pView,
                                                                                                   bool          PruneExpired)
{
    ViewRenderState* pResult = nullptr;
    for (auto It = m_ViewRenderStates.begin(); It != m_ViewRenderStates.end();)
    {
        RefCntAutoPtr<IRadientView> pCachedView = (*It)->WeakView.Lock();
        if (pCachedView == nullptr && PruneExpired)
        {
            It = m_ViewRenderStates.erase(It);
        }
        else
        {
            if (pCachedView == pView)
            {
                if (!PruneExpired)
                    return It->get();

                pResult = It->get();
            }

            ++It;
        }
    }

    return pResult;
}

RadientTesseraRenderTechnique::ViewRenderState& RadientTesseraRenderTechnique::GetOrCreateViewRenderState(IRadientView* pView)
{
    // PrepareFrame calls this once per frame, making it a natural point to
    // release resources that belonged to views destroyed since the last frame.
    if (ViewRenderState* pState = FindViewRenderState(pView, true))
        return *pState;

    m_ViewRenderStates.push_back(std::make_unique<ViewRenderState>(pView, m_PostFXTransitionDuration));
    return *m_ViewRenderStates.back();
}

} // namespace Diligent
