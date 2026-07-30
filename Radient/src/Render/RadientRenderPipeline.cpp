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
#include "Render/Tessera/RadientTesseraRenderTechnique.hpp"

#include "Cast.hpp"
#include "Errors.hpp"

namespace Diligent
{

RadientRenderPipeline::RadientRenderPipeline(IRadientBackend*           pBackend,
                                             RadientAssetManagerImpl*   pAssetManager,
                                             const RadientRendererDesc& Desc) :
    m_pBackend{pBackend},
    m_pAssetManager{pAssetManager}
{
    if (m_pBackend == nullptr)
        LOG_ERROR_AND_THROW("Radient render pipeline backend must not be null");
    if (m_pAssetManager == nullptr)
        LOG_ERROR_AND_THROW("Radient render pipeline asset manager must not be null");

    m_pTechnique = std::make_unique<RadientTesseraRenderTechnique>(pAssetManager, Desc);
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

    const RadientRenderContext Context{Attribs, pDevice, pContext};

    // Remote execution and headless local tests use the same public renderer object.
    // The concrete command serialization/GPU execution will be plugged in behind this pipeline.
    if (pDevice == nullptr || pContext == nullptr)
        return m_pTechnique->PrepareFrame(Context);

    RADIENT_STATUS Status = m_pAssetManager->UpdateGPUResources(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    const RADIENT_STATUS SyncStatus = m_pTechnique->SyncScene(*ViewDesc.pScene);
    if (RADIENT_FAILED(SyncStatus))
        return SyncStatus;
    pSceneImpl->ClearPendingRenderChanges();

    return m_pTechnique->PrepareFrame(Context);
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

    const RadientRenderContext Context{Attribs, pDevice, pContext};

    RADIENT_STATUS Status = m_pTechnique->BeginFrame(Context);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_pTechnique->Render(Context);
    m_pTechnique->EndFrame(Context);
    return Status;
}

} // namespace Diligent
