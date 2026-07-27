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

#include "Render/RadientRenderPipeline.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Core/RadientViewImpl.hpp"
#include "Scene/RadientSceneImpl.hpp"

#include "Cast.hpp"
#include "Errors.hpp"

namespace Diligent
{

RadientRenderPipeline::RadientRenderPipeline(IRadientBackend*           pBackend,
                                             RadientAssetManagerImpl*   pAssetManager,
                                             const RadientRendererDesc& Desc) :
    m_pBackend{pBackend},
    m_pAssetManager{pAssetManager},
    m_ForwardPass{Desc.EnableAsyncPipelineCompilation == True}
{
    if (m_pBackend == nullptr)
        LOG_ERROR_AND_THROW("Radient render pipeline backend must not be null");
    if (m_pAssetManager == nullptr)
        LOG_ERROR_AND_THROW("Radient render pipeline asset manager must not be null");
}

RadientRenderPipeline::~RadientRenderPipeline()
{
}

RADIENT_STATUS RadientRenderPipeline::Update(const RadientRenderAttribs& Attribs)
{
    RadientViewImpl* pViewImpl = ClassPtrCast<RadientViewImpl>(Attribs.pView);
    if (pViewImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientViewDesc& ViewDesc = pViewImpl->GetDesc();
    if (ViewDesc.pScene == nullptr || ViewDesc.pRenderTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(ViewDesc.pScene);
    if (pSceneImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    IRenderDevice*  pDevice  = m_pBackend->GetNativeDevice();
    IDeviceContext* pContext = Attribs.pDeviceContext != nullptr ?
        Attribs.pDeviceContext :
        m_pBackend->GetNativeImmediateContext();

    const RADIENT_STATUS TargetStatus = m_FrameTargets.Prepare(pDevice, *ViewDesc.pRenderTarget);
    if (RADIENT_FAILED(TargetStatus))
        return TargetStatus;

    // Remote execution and headless local tests use the same public renderer object.
    // The concrete command serialization/GPU execution will be plugged in behind this pipeline.
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    RADIENT_STATUS Status = m_pAssetManager->UpdateGPUResources(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    const RADIENT_STATUS SyncStatus = m_DrawableCache.SyncScene(*ViewDesc.pScene);
    if (RADIENT_FAILED(SyncStatus))
        return SyncStatus;
    pSceneImpl->ClearPendingRenderChanges();

    Status = m_GeometryRenderer.Prepare(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    const bool HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;
    if (HasDrawables || HasSkybox)
    {
        RadientPBRRenderer* const pPBRRenderer = m_GeometryRenderer.GetRenderer();
        if (pPBRRenderer == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        Status = pViewImpl->Prepare(*pPBRRenderer, pContext);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    Status = m_ForwardPass.Prepare(m_GeometryRenderer, pDevice, pContext,
                                   m_pAssetManager->GetResourceManager(),
                                   m_DrawableCache, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_SkyboxPass.Prepare(m_GeometryRenderer, pDevice, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_PostProcessPipeline.Prepare(pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientRenderPipeline::Render(const RadientRenderAttribs& Attribs)
{
    RadientViewImpl* pViewImpl = ClassPtrCast<RadientViewImpl>(Attribs.pView);
    if (pViewImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientViewDesc& ViewDesc = pViewImpl->GetDesc();
    if (ViewDesc.pScene == nullptr || ViewDesc.pRenderTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    IRenderDevice*  pDevice  = m_pBackend->GetNativeDevice();
    IDeviceContext* pContext = Attribs.pDeviceContext != nullptr ?
        Attribs.pDeviceContext :
        m_pBackend->GetNativeImmediateContext();

    // Remote execution and headless local tests use the same public renderer object.
    // The concrete command serialization/GPU execution will be plugged in behind this pipeline.
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    const bool HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;

    if (HasDrawables || HasSkybox)
    {
        RefCntAutoPtr<IShaderResourceBinding> pFrameSRB;
        if (HasDrawables)
        {
            RadientPBRRenderer* const pPBRRenderer    = m_GeometryRenderer.GetRenderer();
            IBuffer* const            pFrameAttribsCB = m_GeometryRenderer.GetFrameAttribsCB();
            if (pPBRRenderer == nullptr || pFrameAttribsCB == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            pFrameSRB = pPBRRenderer->GetOrCreateFrameSRB(pViewImpl->GetIBLResources(), pFrameAttribsCB);
            if (pFrameSRB == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }

        const RadientGeometryFrameAttribs GeometryFrameAttribs{
            ViewDesc.Environment,
            ViewDesc.pScene,
            ViewDesc.Camera,
            pViewImpl->GetPrefilteredEnvMapSRV(),
        };

        Status = m_GeometryRenderer.BeginFrame(pDevice,
                                               pContext,
                                               m_DrawableCache.GetLightList(),
                                               m_pAssetManager->GetResourceManager(),
                                               GeometryFrameAttribs,
                                               m_FrameTargets);
        if (RADIENT_FAILED(Status))
            return Status;

        if (HasDrawables)
        {
            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           pFrameSRB,
                                           m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE),
                                           m_DrawableCache,
                                           m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;

            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           pFrameSRB,
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

            ITextureView* pSkyboxSRV = pSkyboxTexture != nullptr ? RadientAssetManagerImpl::GetTextureSRV(pSkyboxTexture) : nullptr;
            if (pSkyboxSRV != nullptr)
            {
                Status = m_SkyboxPass.Execute(pContext,
                                              ViewDesc.Skybox,
                                              pSkyboxSRV,
                                              m_FrameTargets);
                if (RADIENT_FAILED(Status))
                    return Status;
            }
        }

        if (HasDrawables)
        {
            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           pFrameSRB,
                                           m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND),
                                           m_DrawableCache,
                                           m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        m_GeometryRenderer.EndFrame();
    }

    return m_PostProcessPipeline.Execute(pContext, m_FrameTargets);
}

} // namespace Diligent
