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

#include "RadientRenderPipeline.hpp"

#include "RadientAssetManagerImpl.hpp"

namespace Diligent
{

RadientRenderPipeline::RadientRenderPipeline(IRadientBackend*           pBackend,
                                             RadientAssetManagerImpl*   pAssetManager,
                                             const RadientRendererDesc& Desc) :
    m_pBackend{pBackend},
    m_pAssetManager{pAssetManager},
    m_ForwardPass{Desc.EnableAsyncPipelineCompilation == True}
{
}

RadientRenderPipeline::~RadientRenderPipeline()
{
}

RADIENT_STATUS RadientRenderPipeline::Render(const RadientRenderAttribs& Attribs)
{
    if (m_pBackend == nullptr || Attribs.pView == nullptr)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RadientViewDesc& ViewDesc = Attribs.pView->GetDesc();
    if (ViewDesc.pScene == nullptr || ViewDesc.pRenderTarget == nullptr)
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

    RADIENT_STATUS Status = m_pAssetManager != nullptr ?
        m_pAssetManager->UpdateGPUResources(pDevice, pContext) :
        RADIENT_STATUS_INVALID_OPERATION;
    if (RADIENT_FAILED(Status))
        return Status;

    const RADIENT_STATUS SyncStatus = m_DrawableCache.SyncScene(*ViewDesc.pScene, m_pAssetManager);
    if (RADIENT_FAILED(SyncStatus))
        return SyncStatus;

    Status = m_GeometryRenderer.Prepare(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_ForwardPass.Prepare(m_GeometryRenderer, pDevice, pContext, m_DrawableCache, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_SkyboxPass.Prepare(m_GeometryRenderer, pDevice, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_PostProcessPipeline.Prepare(pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    const bool HasDrawables = !m_DrawableCache.GetDrawLists().IsEmpty();
    const bool HasSkybox    = ViewDesc.Skybox.Source != RADIENT_SKYBOX_SOURCE_NONE;

    if (HasDrawables || HasSkybox)
    {
        Status = m_GeometryRenderer.BeginFrame(pDevice,
                                               pContext,
                                               m_DrawableCache.GetLightList(),
                                               m_pAssetManager,
                                               m_pAssetManager->GetResourceManager(),
                                               ViewDesc,
                                               m_FrameTargets);
        if (RADIENT_FAILED(Status))
            return Status;

        if (HasDrawables)
        {
            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE),
                                           m_DrawableCache,
                                           m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;

            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           m_DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK),
                                           m_DrawableCache,
                                           m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        if (HasSkybox)
        {
            const RadientEnvironmentDesc& Environment = ViewDesc.pScene->GetEnvironment();

            Status = m_SkyboxPass.Execute(m_GeometryRenderer,
                                          pContext,
                                          ViewDesc,
                                          Environment,
                                          m_pAssetManager,
                                          m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        if (HasDrawables)
        {
            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
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
