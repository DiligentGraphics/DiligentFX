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

#include "RadientTesseraRenderTechnique.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Core/RadientViewImpl.hpp"

#include "Cast.hpp"
#include "Errors.hpp"

namespace Diligent
{

RadientTesseraRenderTechnique::RadientTesseraRenderTechnique(RadientAssetManagerImpl*   pAssetManager,
                                                             const RadientRendererDesc& Desc) :
    m_pAssetManager{pAssetManager},
    m_ForwardPass{Desc.EnableAsyncPipelineCompilation == True}
{
    if (m_pAssetManager == nullptr)
        LOG_ERROR_AND_THROW("Radient Tessera render technique asset manager must not be null");
}

RADIENT_STATUS RadientTesseraRenderTechnique::SyncScene(const IRadientScene& Scene)
{
    return m_DrawableCache.SyncScene(Scene);
}

RADIENT_STATUS RadientTesseraRenderTechnique::PrepareFrame(const RadientRenderContext& Context)
{
    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.pRenderTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RADIENT_STATUS Status = m_FrameTargets.Prepare(Context.pDevice, *ViewDesc.pRenderTarget);
    if (RADIENT_FAILED(Status) || Context.pDevice == nullptr || Context.pContext == nullptr)
        return Status;

    Status = m_GeometryRenderer.Prepare(Context.pDevice, Context.pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    const bool HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    if (HasDrawables || HasSkybox)
    {
        RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
        if (pPBRRenderer == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        Status = pView->Prepare(*pPBRRenderer, Context.pContext);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    Status = m_ForwardPass.Prepare(m_GeometryRenderer,
                                   Context.pDevice,
                                   Context.pContext,
                                   m_pAssetManager->GetResourceManager(),
                                   m_DrawableCache,
                                   m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_SkyboxPass.Prepare(m_GeometryRenderer, Context.pDevice, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    return m_PostProcessPipeline.Prepare(Context.pDevice, Context.pContext, m_FrameTargets);
}

RADIENT_STATUS RadientTesseraRenderTechnique::BeginFrame(const RadientRenderContext& Context)
{
    m_pFrameSRB.Release();
    m_FrameActive = false;

    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientViewDesc& ViewDesc     = pView->GetDesc();
    const bool             HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool             HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    if (!HasDrawables && !HasSkybox)
        return RADIENT_STATUS_OK;

    if (HasDrawables)
    {
        RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
        if (pPBRRenderer == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        m_pFrameSRB = pPBRRenderer->GetOrCreateFrameSRB(pView->GetIBLResources());
        if (m_pFrameSRB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
    }

    const RadientGeometryFrameAttribs GeometryFrameAttribs{
        ViewDesc.Environment,
        ViewDesc.pScene,
        ViewDesc.Camera,
        pView->GetPrefilteredEnvMapSRV(),
    };

    const RADIENT_STATUS Status = m_GeometryRenderer.BeginFrame(
        Context.pDevice,
        Context.pContext,
        m_DrawableCache.GetLightList(),
        m_pAssetManager->GetResourceManager(),
        GeometryFrameAttribs,
        m_FrameTargets);

    m_FrameActive = !RADIENT_FAILED(Status);
    if (RADIENT_FAILED(Status))
        m_pFrameSRB.Release();

    return Status;
}

RADIENT_STATUS RadientTesseraRenderTechnique::Render(const RadientRenderContext& Context)
{
    RadientViewImpl* pView = ClassPtrCast<RadientViewImpl>(Context.Attribs.pView);
    if (pView == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

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
                m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE),
                m_DrawableCache,
                m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;

            Status = m_ForwardPass.Execute(
                m_GeometryRenderer,
                Context.pDevice,
                Context.pContext,
                m_pFrameSRB,
                m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK),
                m_DrawableCache,
                m_FrameTargets);
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
                                                                   m_FrameTargets);
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
                m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND),
                m_DrawableCache,
                m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;
        }
    }

    return m_PostProcessPipeline.Execute(Context.pContext, m_FrameTargets);
}

void RadientTesseraRenderTechnique::EndFrame(const RadientRenderContext&)
{
    if (m_FrameActive)
        m_GeometryRenderer.EndFrame();

    m_FrameActive = false;
    m_pFrameSRB.Release();
}

} // namespace Diligent
