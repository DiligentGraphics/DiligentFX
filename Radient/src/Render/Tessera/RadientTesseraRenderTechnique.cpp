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
#include "Core/RadientViewImpl.hpp"

#include "Cast.hpp"
#include "Errors.hpp"

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

} // namespace

RadientTesseraRenderTechnique::RadientTesseraRenderTechnique(IThreadPool*               pThreadPool,
                                                             RadientAssetManagerImpl*   pAssetManager,
                                                             const RadientRendererDesc& Desc) :
    m_pThreadPool{ValidateThreadPool(pThreadPool)},
    m_pAssetManager{pAssetManager},
    m_GeometryRenderer{Desc.MaterialTextureSlotCount,
                       GetDefaultMaterialTextures(pAssetManager),
                       Desc.MultiDrawBatchSize},
    m_ForwardPass{Desc.EnableAsyncPipelineCompilation == True}
{}

RADIENT_STATUS RadientTesseraRenderTechnique::SyncScene(const IRadientScene& Scene)
{
    const RadientTesseraMaterialResolveContext MaterialResolveContext{
        *m_pThreadPool,
        m_GeometryRenderer.GetMaterialCache(),
    };
    return m_DrawableCache.SyncScene(Scene, &MaterialResolveContext);
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

    RADIENT_STATUS Status = ViewState.FrameTargets.Prepare(Context.pDevice, *ViewDesc.pRenderTarget);
    if (RADIENT_FAILED(Status) || Context.pDevice == nullptr || Context.pContext == nullptr)
        return Status;

    Status = m_GeometryRenderer.Prepare(Context.pDevice,
                                        Context.pContext,
                                        m_pAssetManager->GetResourceManager());
    if (RADIENT_FAILED(Status))
        return Status;

    RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
    if (pPBRRenderer == nullptr || pPBRRenderer->GetFrameAttribsCB() == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const bool HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    if (HasDrawables || HasSkybox)
    {
        Status = pView->Prepare(*pPBRRenderer, Context.pContext);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    Status = m_ForwardPass.Prepare(m_GeometryRenderer,
                                   Context.pDevice,
                                   Context.pContext,
                                   m_DrawableCache,
                                   ViewState.FrameTargets,
                                   RadientPBRRenderer::GetDebugViewType(ViewDesc.DebugVisualization));
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_SkyboxPass.Prepare(m_GeometryRenderer, Context.pDevice, ViewState.FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientTesseraPostProcessPipeline::PrepareInfo PostProcessInfo{ViewState.FrameTargets, ViewDesc};
    PostProcessInfo.pDevice              = Context.pDevice;
    PostProcessInfo.pContext             = Context.pContext;
    PostProcessInfo.FrameIndex           = ViewState.FrameHistory.GetFrameIndex();
    PostProcessInfo.pFrameAttribsCB      = pPBRRenderer->GetFrameAttribsCB();
    PostProcessInfo.pPreintegratedGGXSRV = pPBRRenderer->GetPreintegratedGGX_SRV();
    return ViewState.PostProcessPipeline.Prepare(PostProcessInfo);
}

RADIENT_STATUS RadientTesseraRenderTechnique::BeginFrame(const RadientRenderContext& Context)
{
    m_pFrameSRB.Release();
    m_pActiveViewState = nullptr;
    m_FrameActive      = false;

    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ViewRenderState* const pViewState = FindViewRenderState(pView, false);
    if (pViewState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    pViewState->FrameTargets.ClearGBuffer(Context.pContext);

    const RadientViewDesc& ViewDesc     = pView->GetDesc();
    const bool             HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();

    if (HasDrawables)
    {
        RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
        if (pPBRRenderer == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        m_pFrameSRB = pPBRRenderer->GetOrCreateFrameSRB(pView->GetIBLResources());
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
        m_DrawableCache.GetLightList(),
        m_pAssetManager->GetResourceManager(),
        GeometryFrameAttribs,
        pViewState->FrameTargets,
        pViewState->FrameHistory);

    m_FrameActive = !RADIENT_FAILED(Status);
    if (m_FrameActive)
        m_pActiveViewState = pViewState;
    else
        m_pFrameSRB.Release();

    return Status;
}

RADIENT_STATUS RadientTesseraRenderTechnique::Render(const RadientRenderContext& Context)
{
    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ViewRenderState* const pViewState = FindViewRenderState(pView, false);
    if (pViewState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const RadientViewDesc& ViewDesc     = pView->GetDesc();
    const bool             HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool             HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;

    if (m_FrameActive)
    {
        if (HasDrawables)
        {
            RADIENT_STATUS Status = m_ForwardPass.Execute(
                m_GeometryRenderer,
                Context.pDevice,
                Context.pContext,
                m_pFrameSRB,
                GLTF::Material::ALPHA_MODE_OPAQUE,
                m_DrawableCache,
                pViewState->FrameTargets,
                pViewState->FrameHistory);
            if (RADIENT_FAILED(Status))
                return Status;

            Status = m_ForwardPass.Execute(
                m_GeometryRenderer,
                Context.pDevice,
                Context.pContext,
                m_pFrameSRB,
                GLTF::Material::ALPHA_MODE_MASK,
                m_DrawableCache,
                pViewState->FrameTargets,
                pViewState->FrameHistory);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        if (HasSkybox)
        {
            IRadientTextureAsset* pSkyboxTexture = nullptr;
            switch (ViewDesc.Skybox.Source)
            {
                case RADIENT_SKYBOX_SOURCE_ENVIRONMENT:
                    pSkyboxTexture = ViewDesc.Environment.pEnvironmentMap;
                    break;

                case RADIENT_SKYBOX_SOURCE_TEXTURE:
                    pSkyboxTexture = ViewDesc.Skybox.pTexture;
                    break;

                default:
                    UNEXPECTED("Unexpected Radient skybox source");
                    break;
            }

            ITextureView* const pSkyboxSRV = pSkyboxTexture != nullptr ?
                RadientAssetManagerImpl::GetTextureSRV(pSkyboxTexture) :
                nullptr;
            if (pSkyboxSRV != nullptr)
            {
                const RADIENT_STATUS Status = m_SkyboxPass.Execute(Context.pContext,
                                                                   ViewDesc.Skybox,
                                                                   pSkyboxSRV,
                                                                   pViewState->FrameTargets);
                if (RADIENT_FAILED(Status))
                    return Status;
            }
        }

        if (HasDrawables)
        {
            const RADIENT_STATUS Status = m_ForwardPass.Execute(
                m_GeometryRenderer,
                Context.pDevice,
                Context.pContext,
                m_pFrameSRB,
                GLTF::Material::ALPHA_MODE_BLEND,
                m_DrawableCache,
                pViewState->FrameTargets,
                pViewState->FrameHistory);
            if (RADIENT_FAILED(Status))
                return Status;
        }
    }

    return pViewState->PostProcessPipeline.Execute(Context.pDevice,
                                                   Context.pContext,
                                                   pViewState->FrameTargets,
                                                   !pViewState->FrameHistory.HasCameraHistory());
}

void RadientTesseraRenderTechnique::EndFrame(const RadientRenderContext&)
{
    if (m_FrameActive)
    {
        VERIFY_EXPR(m_pActiveViewState != nullptr);
        if (m_pActiveViewState != nullptr)
        {
            m_GeometryRenderer.EndFrame(m_pActiveViewState->FrameHistory);
            m_pActiveViewState->FrameTargets.CommitFrame();
        }
    }

    m_pActiveViewState = nullptr;
    m_FrameActive      = false;
    m_pFrameSRB.Release();
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

    m_ViewRenderStates.push_back(std::make_unique<ViewRenderState>(pView));
    return *m_ViewRenderStates.back();
}

} // namespace Diligent
