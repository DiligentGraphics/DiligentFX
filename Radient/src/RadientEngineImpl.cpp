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

#include "RadientEngineImpl.hpp"

#include "RadientAssetManagerImpl.hpp"
#include "RadientBackendImpl.hpp"
#include "RadientRendererImpl.hpp"
#include "RadientSceneImpl.hpp"
#include "RadientSceneWriterImpl.hpp"

namespace Diligent
{

RadientEngineImpl::RadientEngineImpl(IReferenceCounters* pRefCounters, const RadientEngineCreateInfo& CreateInfo) :
    TBase{pRefCounters},
    m_pBackend{RadientBackendImpl::Create(CreateInfo.Backend)},
    m_pAssetManager{RadientAssetManagerImpl::Create(CreateInfo.Assets)}
{
}

RadientEngineImpl::~RadientEngineImpl()
{
}

RefCntAutoPtr<IRadientEngine> RadientEngineImpl::Create(const RadientEngineCreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientEngineImpl>{MakeNewRCObj<RadientEngineImpl>()(CreateInfo)};
}

RADIENT_STATUS RadientEngineImpl::GetBackend(IRadientBackend** ppBackend)
{
    if (ppBackend == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppBackend = nullptr;
    if (m_pBackend == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    m_pBackend->AddRef();
    *ppBackend = m_pBackend.RawPtr();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::GetAssetManager(IRadientAssetManager** ppAssetManager)
{
    if (ppAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppAssetManager = nullptr;
    if (m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    m_pAssetManager->AddRef();
    *ppAssetManager = m_pAssetManager;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateScene(const RadientSceneDesc& Desc, IRadientScene** ppScene)
{
    if (ppScene == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppScene = nullptr;

    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create(Desc);
    *ppScene                               = pScene.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateSceneWriter(IRadientScene* pScene, IRadientSceneWriter** ppWriter)
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppWriter = nullptr;
    if (pScene == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(static_cast<RadientSceneImpl*>(pScene));
    *ppWriter                                  = pWriter.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateRenderer(const RadientRendererDesc& Desc, IRadientRenderer** ppRenderer)
{
    if (ppRenderer == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppRenderer = nullptr;
    if (m_pBackend == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<IRadientRenderer> pRenderer = RadientRendererImpl::Create(Desc, m_pBackend);
    *ppRenderer                               = pRenderer.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientEngine)(const RadientEngineCreateInfo& EngineCI,
                                                             IRadientEngine**               ppEngine)
{
    if (ppEngine == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppEngine = nullptr;

    RefCntAutoPtr<IRadientEngine> pEngine = RadientEngineImpl::Create(EngineCI);
    *ppEngine                             = pEngine.Detach();
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
