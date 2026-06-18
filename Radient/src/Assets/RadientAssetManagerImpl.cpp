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

#include "Assets/RadientAssetManagerImpl.hpp"

#include "Assets/RadientAssetImpl.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Errors.hpp"
#include "GLTFLoader.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace Diligent
{

namespace
{

constexpr Uint64 RadientDefaultIndexBufferSize       = 16ull * 1024ull * 1024ull;
constexpr Uint64 RadientDefaultMaxIndexBufferSize    = 256ull * 1024ull * 1024ull;
constexpr Uint32 RadientDefaultVertexPoolSize        = 1024u * 1024u;
constexpr Uint32 RadientDefaultTextureAtlasSize      = 4096u;
constexpr Uint32 RadientDefaultTextureAtlasSlices    = 1u;
constexpr Uint32 RadientDefaultTextureAtlasMaxSlices = 2048u;

static constexpr INTERFACE_ID IID_SceneAssetImpl = {0xb59806f1, 0xa08a, 0x4dff, {0xb0, 0x37, 0x84, 0x75, 0xd6, 0xfd, 0x7f, 0x1b}};

struct GLTFModelStorage
{
    explicit GLTFModelStorage(RADIENT_STATUS InitLoadStatus = RADIENT_STATUS_OK) :
        LoadStatus{InitLoadStatus}
    {
    }

    GLTFModelStorage(GLTFModelStorage&& Rhs) noexcept :
        pModel{std::move(Rhs.pModel)},
        LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
        GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
        GPUUpdateQueued{Rhs.GPUUpdateQueued.load(std::memory_order_relaxed)}
    {
    }

    GLTFModelStorage& operator=(GLTFModelStorage&& Rhs)  = delete;
    GLTFModelStorage(const GLTFModelStorage&)            = delete;
    GLTFModelStorage& operator=(const GLTFModelStorage&) = delete;

    std::unique_ptr<GLTF::Model> pModel;
    std::atomic<RADIENT_STATUS>  LoadStatus{RADIENT_STATUS_OK};
    std::atomic_bool             GPUResourcesReady{false};
    std::atomic_bool             GPUUpdateQueued{false};
};

GLTF::ResourceManager::CreateInfo CreateResourceManagerInfo()
{
    GLTF::ResourceManager::CreateInfo CreateInfo;

    CreateInfo.IndexAllocatorCI.Desc.Name      = "Radient index pool";
    CreateInfo.IndexAllocatorCI.Desc.Size      = RadientDefaultIndexBufferSize;
    CreateInfo.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    CreateInfo.IndexAllocatorCI.ExpansionSize  = static_cast<Uint32>(RadientDefaultIndexBufferSize);
    CreateInfo.IndexAllocatorCI.MaxSize        = RadientDefaultMaxIndexBufferSize;

    CreateInfo.DefaultPoolDesc.Name        = "Radient vertex pool";
    CreateInfo.DefaultPoolDesc.VertexCount = RadientDefaultVertexPoolSize;
    CreateInfo.DefaultPoolDesc.Usage       = USAGE_DEFAULT;
    CreateInfo.DefaultPoolDesc.Mode        = BUFFER_MODE_UNDEFINED;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient texture atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = RadientDefaultTextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = RadientDefaultTextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = RadientDefaultTextureAtlasSlices;
    CreateInfo.DefaultAtlasDesc.Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    CreateInfo.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    CreateInfo.DefaultAtlasDesc.MaxSliceCount  = RadientDefaultTextureAtlasMaxSlices;

    return CreateInfo;
}

RefCntAutoPtr<GLTF::ResourceManager> CreateRadientResourceManager(IRenderDevice* pDevice)
{
    if (pDevice == nullptr)
        return {};

    return GLTF::ResourceManager::Create(pDevice, CreateResourceManagerInfo());
}

RefCntAutoPtr<IGPUUploadManager> CreateRadientGPUUploadManager(IRenderDevice* pDevice)
{
    if (pDevice == nullptr)
        return {};

    RefCntAutoPtr<IGPUUploadManager> pUploadManager;
    GPUUploadManagerCreateInfo       UploadCI;
    UploadCI.pDevice = pDevice;
    CreateGPUUploadManager(UploadCI, &pUploadManager);
    return pUploadManager;
}

std::string MakeGLTFCacheKey(const char* URI)
{
    return std::string{"gltf:"} + URI;
}

} // namespace

class ScenePayloadImpl final : public RadientAssetPayloadImpl<GLTFModelStorage, ScenePayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<GLTFModelStorage, ScenePayloadImpl>;
    using TBase::TBase;
};

namespace
{

using SceneAssetImpl =
    RadientAssetImpl<IRadientSceneAsset, IID_RadientSceneAsset, IID_SceneAssetImpl, RADIENT_ASSET_TYPE_SCENE, ScenePayloadImpl>;

} // namespace

RadientAssetManagerImpl::RadientAssetManagerImpl(IReferenceCounters* pRefCounters,
                                                 const CreateInfo&   CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Assets.Desc.Name != nullptr ? CreateInfo.Assets.Desc.Name : ""},
    m_Desc{CreateInfo.Assets.Desc},
    m_pThreadPool{CreateInfo.pThreadPool},
    m_pDevice{CreateInfo.pDevice},
    m_pResourceManager{CreateRadientResourceManager(CreateInfo.pDevice)},
    m_pUploadManager{CreateRadientGPUUploadManager(CreateInfo.pDevice)},
    m_MeshManager{
        RadientMeshAssetManager::CreateInfo{
            m_pThreadPool,
            m_pDevice,
            m_pResourceManager,
            m_pUploadManager,
        },
    },
    m_MaterialManager{},
    m_TextureManager{
        RadientTextureAssetManager::CreateInfo{
            m_pThreadPool,
            m_pDevice,
            m_pResourceManager,
            m_pUploadManager,
        },
    }
{
    m_Desc.Name = m_Name.c_str();
}

RadientAssetManagerImpl::~RadientAssetManagerImpl()
{
}

RefCntAutoPtr<RadientAssetManagerImpl> RadientAssetManagerImpl::Create(const CreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientAssetManagerImpl>{MakeNewRCObj<RadientAssetManagerImpl>()(CreateInfo)};
}

const RadientAssetManagerDesc& RadientAssetManagerImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                   IRadientMeshAsset**          ppMesh)
{
    return m_MeshManager.CreateMesh(MeshCI, ppMesh);
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                       IRadientMaterialAsset**          ppMaterial)
{
    return m_MaterialManager.CreateMaterial(MaterialCI, ppMaterial);
}

RADIENT_STATUS RadientAssetManagerImpl::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                    IRadientTextureAsset**        ppTexture)
{
    return m_TextureManager.LoadTexture(LoadInfo, ppTexture);
}

RADIENT_STATUS RadientAssetManagerImpl::LoadGLTF(const RadientGLTFLoadInfo& LoadInfo,
                                                 IRadientSceneAsset**       ppModel)
{
    if (ppModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppModel == nullptr, "Output GLTF model pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppModel = nullptr;

    if (!ValidateGLTFLoadInfo(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pThreadPool == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const std::string SourceURI = LoadInfo.URI;

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakSelf{this};
    RefCntAutoPtr<SceneAssetImpl>          pModelAsset =
        SceneAssetImpl::Create(SourceURI);
    VERIFY_EXPR(pModelAsset != nullptr);
    if (!pModelAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    pModelAsset->QueryInterface(IID_RadientSceneAsset, ppModel);

    EnqueueAsyncWork(
        m_pThreadPool,
        [pWeakSelf, pModelAsset, SourceURI](Uint32) mutable //
        {
            RefCntAutoPtr<RadientAssetManagerImpl> pSelf = pWeakSelf.Lock();
            if (pSelf == nullptr)
            {
                pModelAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_CANCELLED;
            }

            const std::string CacheKey = MakeGLTFCacheKey(SourceURI.c_str());

            auto [pModelPayload, PayloadCreated] =
                pSelf->m_GLTFAssetCache.GetOrCreate(
                    CacheKey.c_str(),
                    []() {
                        return ScenePayloadImpl::Create(GLTFModelStorage{RADIENT_STATUS_PENDING});
                    });

            if (!pModelPayload)
            {
                pModelAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            pModelAsset->Resolve(std::move(pModelPayload));

            if (!PayloadCreated)
                return ASYNC_TASK_STATUS_COMPLETE;

            pSelf->LoadGLTFModel(*pModelAsset->GetPayload(), SourceURI);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pModelAsset->GetResolveStatus();
}

RADIENT_STATUS RadientAssetManagerImpl::WaitForAssetLoad(IRadientAsset* pAsset)
{
    if (pAsset == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    for (;;)
    {
        const RADIENT_STATUS Status = GetAssetLoadStatus(pAsset);

        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        if (m_pThreadPool == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        if (!m_pThreadPool->ProcessTask(0, false))
            std::this_thread::yield();
    }
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMeshFromGLTFMesh(IRadientSceneAsset* pModel,
                                                               Uint32              MeshIndex,
                                                               const Char*         Name,
                                                               IRadientMeshAsset** ppMesh)
{
    return m_MeshManager.CreateMeshFromGLTFMesh(pModel, MeshIndex, Name, ppMesh);
}

RadientDrawableMeshResolveResult RadientAssetManagerImpl::GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                                          bool               RequireGPUResourcesReady)
{
    return RadientMeshAssetManager::GetDrawableMesh(pMesh, RequireGPUResourcesReady);
}

const GLTF::Material* RadientAssetManagerImpl::GetMaterial(IRadientMaterialAsset* pMaterial)
{
    return RadientMaterialAssetManager::GetMaterial(pMaterial);
}

const GLTF::Model* RadientAssetManagerImpl::GetGLTFModel(IRadientSceneAsset* pModel,
                                                         bool                RequireGPUResourcesReady)
{
    RefCntAutoPtr<SceneAssetImpl> pImpl = SceneAssetImpl::ResolveAsset(pModel);
    if (!pImpl)
        return nullptr;

    const GLTFModelStorage& GLTFModel = pImpl->GetStorage();
    if (GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
        (RequireGPUResourcesReady && !GLTFModel.GPUResourcesReady.load(std::memory_order_acquire)))
    {
        return nullptr;
    }

    return GLTFModel.pModel.get();
}

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFLoadStatus(IRadientSceneAsset* pModel)
{
    const SceneAssetImpl* pImpl = ClassPtrCast<const SceneAssetImpl>(pModel);
    if (!pImpl)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    return pImpl->GetLoadStatus();
}

ITextureView* RadientAssetManagerImpl::GetTextureSRV(IRadientTextureAsset* pTextureAsset)
{
    return RadientTextureAssetManager::GetTextureSRV(pTextureAsset);
}

RADIENT_STATUS RadientAssetManagerImpl::UpdateGPUResources(IRenderDevice*  pDevice,
                                                           IDeviceContext* pContext)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_OK;

    if (pContext == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    if (m_pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (m_pDevice != pDevice)
    {
        UNEXPECTED("Radient asset manager device changed. This should never happen.");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    if (m_pUploadManager != nullptr)
        m_pUploadManager->RenderThreadUpdate(pContext);

    if (m_pResourceManager != nullptr)
        m_pResourceManager->UpdateAllResources(pDevice, pContext);

    struct GLTFModelToPrepare
    {
        RefCntAutoPtr<ScenePayloadImpl> pPayload;
        GLTF::Model*                    pModel = nullptr;
    };

    std::vector<GLTFModelToPrepare> ModelsToPrepare;

    ModelsToPrepare.reserve(m_PendingGPUResourceUpdates.Size());
    for (RefCntWeakPtr<ScenePayloadImpl> WeakPayload; m_PendingGPUResourceUpdates.Dequeue(WeakPayload);)
    {
        RefCntAutoPtr<ScenePayloadImpl> pPayload = WeakPayload.Lock();
        if (pPayload == nullptr)
            continue;

        GLTFModelStorage& GLTFModel = pPayload->GetStorage();

        if (GLTFModel.GPUResourcesReady.load(std::memory_order_acquire) ||
            GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
            GLTFModel.pModel == nullptr)
        {
            GLTFModel.GPUUpdateQueued.store(false, std::memory_order_release);
            continue;
        }

        ModelsToPrepare.push_back({pPayload, GLTFModel.pModel.get()});
    }

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    auto GetQueuedGLTFModel = [](const GLTFModelToPrepare& Model) -> GLTFModelStorage* //
    {
        if (Model.pPayload == nullptr)
            return nullptr;

        GLTFModelStorage& GLTFModel = Model.pPayload->GetStorage();
        VERIFY(GLTFModel.pModel.get() == Model.pModel,
               "Queued GPU resource update references a stale GLTF model");
        if (GLTFModel.pModel.get() != Model.pModel)
            return nullptr;

        return &GLTFModel;
    };

    for (const GLTFModelToPrepare& Model : ModelsToPrepare)
    {
        if (!Model.pModel->PrepareGPUResources(pDevice, pContext))
        {
            if (GetQueuedGLTFModel(Model) != nullptr)
                m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<ScenePayloadImpl>{Model.pPayload});

            if (Status == RADIENT_STATUS_OK)
                Status = RADIENT_STATUS_OUT_OF_DATE;
            continue;
        }

        if (GLTFModelStorage* pGLTFModel = GetQueuedGLTFModel(Model))
        {
            pGLTFModel->GPUResourcesReady.store(true, std::memory_order_release);
            pGLTFModel->GPUUpdateQueued.store(false, std::memory_order_release);
        }
    }

    return Status;
}

GLTF::ResourceManager* RadientAssetManagerImpl::GetResourceManager() const
{
    return m_pResourceManager;
}

RADIENT_STATUS RadientAssetManagerImpl::GetAssetLoadStatus(IRadientAsset* pAsset)
{
    if (pAsset == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    switch (pAsset->GetType())
    {
        case RADIENT_ASSET_TYPE_MESH:
            return RadientMeshAssetManager::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_SCENE:
            return SceneAssetImpl::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_TEXTURE:
            return RadientTextureAssetManager::GetLoadStatus(pAsset);

        default:
            return RADIENT_STATUS_OK;
    }
}

void RadientAssetManagerImpl::LoadGLTFModel(ScenePayloadImpl&  Model,
                                            const std::string& SourceURI)
{
    GLTFModelStorage& GLTFModel = Model.GetStorage();

    std::unique_ptr<GLTF::Model> pModelData;
    try
    {
        GLTF::ModelCreateInfo ModelCI{SourceURI.c_str()};
        ModelCI.pResourceManager = m_pResourceManager;
        ModelCI.pUploadMgr       = m_pUploadManager;
        pModelData               = std::make_unique<GLTF::Model>(m_pDevice, nullptr, ModelCI);
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "': ", Error.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "'");
    }

    const RADIENT_STATUS Status = pModelData != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;

    GLTFModel.pModel = std::move(pModelData);
    GLTFModel.GPUResourcesReady.store(false, std::memory_order_release);
    GLTFModel.GPUUpdateQueued.store(Status == RADIENT_STATUS_OK, std::memory_order_release);
    GLTFModel.LoadStatus.store(Status, std::memory_order_release);

    if (Status == RADIENT_STATUS_OK)
        m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<ScenePayloadImpl>{&Model});
}

} // namespace Diligent
