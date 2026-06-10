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

#include "Core/RadientEngineImpl.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Core/RadientBackendImpl.hpp"
#include "Render/RadientRendererImpl.hpp"
#include "Scene/RadientSceneImpl.hpp"
#include "Import/RadientSceneImporterImpl.hpp"
#include "Scene/RadientSceneWriterImpl.hpp"

#include "Errors.hpp"
#include "ThreadPool.hpp"

#include <algorithm>
#include <exception>
#include <thread>

namespace Diligent
{

namespace
{

size_t GetDefaultRadientWorkerThreadCount()
{
    const unsigned int HardwareThreads = std::thread::hardware_concurrency();
    return std::max(1u, HardwareThreads != 0 ? HardwareThreads - 1u : 1u);
}

} // namespace

RadientEngineImpl::RadientEngineImpl(IReferenceCounters* pRefCounters, const RadientEngineCreateInfo& CreateInfo) :
    TBase{pRefCounters},
    m_pBackend{RadientBackendImpl::Create(CreateInfo.Backend)}
{
    if (CreateInfo.pThreadPool != nullptr)
    {
        m_pThreadPool = CreateInfo.pThreadPool;
    }
    else
    {
        ThreadPoolCreateInfo ThreadPoolCI;
        ThreadPoolCI.NumThreads = CreateInfo.WorkerThreadCount != 0 ?
            CreateInfo.WorkerThreadCount :
            GetDefaultRadientWorkerThreadCount();

        m_pThreadPool    = CreateThreadPool(ThreadPoolCI);
        m_OwnsThreadPool = true;
    }

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.Assets      = CreateInfo.Assets;
    AssetManagerCI.pThreadPool = m_pThreadPool;
    AssetManagerCI.pDevice     = m_pBackend != nullptr ? m_pBackend->GetNativeDevice() : nullptr;
    m_pAssetManager            = RadientAssetManagerImpl::Create(AssetManagerCI);
}

RadientEngineImpl::~RadientEngineImpl()
{
    if (m_OwnsThreadPool && m_pThreadPool)
    {
        m_pThreadPool->WaitForAllTasks();
        m_pThreadPool->StopThreads();
    }
}

RefCntAutoPtr<IRadientEngine> RadientEngineImpl::Create(const RadientEngineCreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientEngineImpl>{MakeNewRCObj<RadientEngineImpl>()(CreateInfo)};
}

RADIENT_STATUS RadientEngineImpl::GetBackend(IRadientBackend** ppBackend)
{
    if (ppBackend == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    DEV_CHECK_ERR(*ppBackend == nullptr, "Output backend pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
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

    DEV_CHECK_ERR(*ppAssetManager == nullptr, "Output asset manager pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
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

    DEV_CHECK_ERR(*ppScene == nullptr, "Output scene pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppScene = nullptr;

    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create(Desc);
    *ppScene                               = pScene.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateSceneWriter(IRadientScene* pScene, IRadientSceneWriter** ppWriter)
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    DEV_CHECK_ERR(*ppWriter == nullptr, "Output scene writer pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppWriter = nullptr;
    if (pScene == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(static_cast<RadientSceneImpl*>(pScene));
    *ppWriter                                  = pWriter.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateSceneImporter(IRadientSceneWriter* pWriter, IRadientSceneImporter** ppImporter)
{
    if (ppImporter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    DEV_CHECK_ERR(*ppImporter == nullptr, "Output scene importer pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppImporter = nullptr;
    if (pWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<IRadientSceneImporter> pImporter = RadientSceneImporterImpl::Create(m_pAssetManager, pWriter);
    *ppImporter                                    = pImporter.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientEngineImpl::CreateRenderer(const RadientRendererDesc& Desc, IRadientRenderer** ppRenderer)
{
    if (ppRenderer == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    DEV_CHECK_ERR(*ppRenderer == nullptr, "Output renderer pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppRenderer = nullptr;
    if (m_pBackend == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientRendererImpl::CreateInfo RendererCI{};
    RendererCI.Desc          = Desc;
    RendererCI.pBackend      = m_pBackend;
    RendererCI.pAssetManager = m_pAssetManager;

    try
    {
        RefCntAutoPtr<IRadientRenderer> pRenderer = RadientRendererImpl::Create(RendererCI);
        *ppRenderer                               = pRenderer.Detach();
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient renderer: ", Error.what());
        return RADIENT_STATUS_INVALID_OPERATION;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient renderer");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientEngine)(const RadientEngineCreateInfo& EngineCI,
                                                             IRadientEngine**               ppEngine)
{
    if (ppEngine == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    DEV_CHECK_ERR(*ppEngine == nullptr, "Output engine pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppEngine = nullptr;

    RefCntAutoPtr<IRadientEngine> pEngine = RadientEngineImpl::Create(EngineCI);
    *ppEngine                             = pEngine.Detach();
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
