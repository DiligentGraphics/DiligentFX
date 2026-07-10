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
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientGLTFLoader.hpp"
#include "Errors.hpp"
#include "GLTFDocument.hpp"
#include "GLTFLoader.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"

#include <atomic>
#include <cctype>
#include <exception>
#include <memory>
#include <string>
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

struct ImportedSceneStorage
{
    explicit ImportedSceneStorage(RADIENT_STATUS InitLoadStatus = RADIENT_STATUS_OK) :
        LoadStatus{InitLoadStatus},
        GPUResourceStatus{InitLoadStatus}
    {
    }

    ImportedSceneStorage& operator=(ImportedSceneStorage&& Rhs)  = delete;
    ImportedSceneStorage(ImportedSceneStorage&& Rhs)             = delete;
    ImportedSceneStorage(const ImportedSceneStorage&)            = delete;
    ImportedSceneStorage& operator=(const ImportedSceneStorage&) = delete;

    void SetScene(RadientImport::ImportedDocument ImportedScene,
                  RADIENT_STATUS                  InitialGPUStatus)
    {
        Scene = std::move(ImportedScene);

        GPUResourceStatus.store(InitialGPUStatus, std::memory_order_release);
        SceneDataReady.store(true, std::memory_order_release);
        LoadStatus.store(RADIENT_STATUS_PENDING, std::memory_order_release);
    }

    void SetFailedStatus(RADIENT_STATUS Status) noexcept
    {
        if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING)
            Status = RADIENT_STATUS_INVALID_OPERATION;

        GPUResourceStatus.store(Status, std::memory_order_release);
        LoadStatus.store(Status, std::memory_order_release);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        RADIENT_STATUS Status = LoadStatus.load(std::memory_order_acquire);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        if (!SceneDataReady.load(std::memory_order_acquire))
            return RADIENT_STATUS_PENDING;

        Status = GetDependencyLoadStatus();
        if (Status != RADIENT_STATUS_PENDING)
            LoadStatus.store(Status, std::memory_order_release);

        return Status;
    }

    RADIENT_STATUS GetGPUResourceStatus() const noexcept
    {
        const RADIENT_STATUS Status = GetLoadStatus();
        if (Status != RADIENT_STATUS_OK)
            return Status;

        RADIENT_STATUS GPUStatus = GPUResourceStatus.load(std::memory_order_acquire);
        if (GPUStatus != RADIENT_STATUS_PENDING)
            return GPUStatus;

        GPUStatus = GetDependencyGPUResourceStatus();
        if (GPUStatus != RADIENT_STATUS_PENDING)
            GPUResourceStatus.store(GPUStatus, std::memory_order_release);

        return GPUStatus;
    }

    RADIENT_STATUS GetDependencyLoadStatus() const noexcept
    {
        RADIENT_STATUS Status = RADIENT_STATUS_OK;

        for (IRadientTextureAsset* pTexture : Scene.Textures)
        {
            if (pTexture != nullptr)
                Status = CombineDependencyStatus(Status, RadientTextureAssetManager::GetLoadStatus(pTexture));
        }

        for (IRadientMaterialAsset* pMaterial : Scene.Materials)
        {
            if (pMaterial != nullptr)
                Status = CombineDependencyStatus(Status, RadientMaterialAssetManager::GetLoadStatus(pMaterial));
        }

        for (IRadientMeshAsset* pMesh : Scene.Meshes)
        {
            if (pMesh != nullptr)
                Status = CombineDependencyStatus(Status, RadientMeshAssetManager::GetLoadStatus(pMesh));
        }

        return Status;
    }

    RADIENT_STATUS GetDependencyGPUResourceStatus() const noexcept
    {
        RADIENT_STATUS Status = RADIENT_STATUS_OK;

        for (IRadientTextureAsset* pTexture : Scene.Textures)
        {
            if (pTexture != nullptr)
                Status = CombineDependencyStatus(Status, RadientTextureAssetManager::GetGPUResourceStatus(pTexture));
        }

        for (IRadientMaterialAsset* pMaterial : Scene.Materials)
        {
            if (pMaterial != nullptr)
                Status = CombineDependencyStatus(Status, RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial));
        }

        for (IRadientMeshAsset* pMesh : Scene.Meshes)
        {
            if (pMesh != nullptr)
                Status = CombineDependencyStatus(Status, RadientMeshAssetManager::GetGPUResourceStatus(pMesh));
        }

        return Status;
    }

    RadientImport::ImportedDocument     Scene;
    std::atomic_bool                    SceneDataReady{false};
    mutable std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    mutable std::atomic<RADIENT_STATUS> GPUResourceStatus{RADIENT_STATUS_OK};
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

std::string MakeSceneCacheKey(RADIENT_SCENE_FORMAT Format, const char* URI)
{
    const char* Prefix = "scene:";
    switch (Format)
    {
        case RADIENT_SCENE_FORMAT_GLTF:
            Prefix = "gltf:";
            break;

        default:
            break;
    }

    return std::string{Prefix} + URI;
}

bool EndsWithCaseInsensitive(const std::string& Text, const char* Suffix)
{
    const size_t SuffixLength = std::char_traits<char>::length(Suffix);
    if (Text.size() < SuffixLength)
        return false;

    const size_t Offset = Text.size() - SuffixLength;
    for (size_t Index = 0; Index < SuffixLength; ++Index)
    {
        const unsigned char Lhs = static_cast<unsigned char>(Text[Offset + Index]);
        const unsigned char Rhs = static_cast<unsigned char>(Suffix[Index]);
        if (std::tolower(Lhs) != std::tolower(Rhs))
            return false;
    }

    return true;
}

RADIENT_SCENE_FORMAT DetectSceneFormatFromURI(const char* URI)
{
    if (URI == nullptr)
        return RADIENT_SCENE_FORMAT_AUTO;

    std::string  Path{URI};
    const size_t QueryPos = Path.find_first_of("?#");
    if (QueryPos != std::string::npos)
        Path.resize(QueryPos);

    if (EndsWithCaseInsensitive(Path, ".gltf") || EndsWithCaseInsensitive(Path, ".glb"))
        return RADIENT_SCENE_FORMAT_GLTF;

    return RADIENT_SCENE_FORMAT_AUTO;
}

} // namespace

class ScenePayloadImpl final : public RadientAssetPayloadImpl<ImportedSceneStorage, ScenePayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<ImportedSceneStorage, ScenePayloadImpl>;
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
    m_pMeshManager{
        RadientMeshAssetManager::Create(
            RadientMeshAssetManager::CreateInfo{
                m_pDevice,
                m_pResourceManager,
                m_pUploadManager,
            })},
    m_pMaterialManager{RadientMaterialAssetManager::Create()},
    m_pTextureManager{
        RadientTextureAssetManager::Create(
            RadientTextureAssetManager::CreateInfo{
                m_pDevice,
                m_pResourceManager,
                m_pUploadManager,
            })}
{
    m_Desc.Name = m_Name.c_str();
}

RadientAssetManagerImpl::~RadientAssetManagerImpl()
{
    DEV_CHECK_ERR(m_pUploadManager == nullptr || m_Stopped,
                  "RadientAssetManagerImpl::Stop() must be called before destroying a GPU-backed asset manager");
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
    return m_pThreadPool ?
        m_pMeshManager->CreateMesh(*m_pThreadPool, MeshCI, ppMesh) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                       IRadientMaterialAsset**          ppMaterial)
{
    return m_pMaterialManager->CreateMaterial(MaterialCI, ppMaterial);
}

RADIENT_STATUS RadientAssetManagerImpl::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                    IRadientTextureAsset**        ppTexture)
{
    return m_pThreadPool ?
        m_pTextureManager->LoadTexture(*m_pThreadPool, LoadInfo, ppTexture) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::LoadScene(const RadientSceneLoadInfo& LoadInfo,
                                                  IRadientSceneAsset**        ppScene)
{
    if (ppScene == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppScene == nullptr, "Output scene pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppScene = nullptr;

    if (!ValidateSceneLoadInfo(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pThreadPool == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const std::string SourceURI = LoadInfo.URI;

    const RADIENT_SCENE_FORMAT SceneFormat =
        LoadInfo.Format == RADIENT_SCENE_FORMAT_AUTO ?
        DetectSceneFormatFromURI(SourceURI.c_str()) :
        LoadInfo.Format;

    if (SceneFormat == RADIENT_SCENE_FORMAT_AUTO)
    {
        LOG_ERROR_MESSAGE("Failed to infer scene format from URI '", SourceURI, "'");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (SceneFormat != RADIENT_SCENE_FORMAT_GLTF)
    {
        LOG_ERROR_MESSAGE("Scene format ", static_cast<Int32>(SceneFormat), " is not supported yet.");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakSelf{this};
    RefCntAutoPtr<SceneAssetImpl>          pModelAsset =
        SceneAssetImpl::Create(SourceURI);
    VERIFY_EXPR(pModelAsset != nullptr);
    if (!pModelAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    pModelAsset->QueryInterface(IID_RadientSceneAsset, ppScene);

    EnqueueAsyncWork(
        m_pThreadPool,
        [pWeakSelf, pModelAsset, SourceURI, SceneFormat](Uint32) mutable //
        {
            RefCntAutoPtr<RadientAssetManagerImpl> pSelf = pWeakSelf.Lock();
            if (pSelf == nullptr)
            {
                pModelAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_CANCELLED;
            }

            const std::string CacheKey = MakeSceneCacheKey(SceneFormat, SourceURI.c_str());

            auto [pModelPayload, PayloadCreated] =
                pSelf->m_SceneAssetCache.GetOrCreate(
                    CacheKey.c_str(),
                    []() {
                        return ScenePayloadImpl::Create(RADIENT_STATUS_PENDING);
                    });

            if (!pModelAsset->SetPayload(std::move(pModelPayload)))
                return ASYNC_TASK_STATUS_COMPLETE;

            if (!PayloadCreated)
                return ASYNC_TASK_STATUS_COMPLETE;

            pSelf->LoadSceneAsset(*pModelAsset->GetPayload(), SceneFormat, SourceURI);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pModelAsset->GetPayloadStatus();
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

RADIENT_STATUS RadientAssetManagerImpl::Stop(IDeviceContext* pContext)
{
    if (m_Stopped)
        return RADIENT_STATUS_OK;

    if (m_pUploadManager == nullptr)
    {
        m_Stopped = true;
        return RADIENT_STATUS_OK;
    }

    if (pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    m_pUploadManager->Stop(pContext);
    m_Stopped = true;

    return RADIENT_STATUS_OK;
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

const RadientImport::ImportedDocument* RadientAssetManagerImpl::GetImportedScene(IRadientSceneAsset* pScene)
{
    RefCntAutoPtr<SceneAssetImpl> pImpl = SceneAssetImpl::ResolveAsset(pScene);
    if (!pImpl)
        return nullptr;

    const ImportedSceneStorage& Scene = pImpl->GetStorage();
    if (Scene.GetLoadStatus() != RADIENT_STATUS_OK)
        return nullptr;

    return &Scene.Scene;
}

RADIENT_STATUS RadientAssetManagerImpl::GetSceneLoadStatus(IRadientSceneAsset* pScene)
{
    const SceneAssetImpl* pImpl = ClassPtrCast<const SceneAssetImpl>(pScene);
    if (!pImpl)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    return pImpl->GetLoadStatus();
}

RADIENT_STATUS RadientAssetManagerImpl::GetSceneGPUResourceStatus(IRadientSceneAsset* pScene)
{
    const SceneAssetImpl* pImpl = ClassPtrCast<const SceneAssetImpl>(pScene);
    if (!pImpl)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pImpl->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    return pImpl->GetStorage().GetGPUResourceStatus();
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

    return RADIENT_STATUS_OK;
}

GLTF::ResourceManager* RadientAssetManagerImpl::GetResourceManager() const
{
    return m_pResourceManager;
}

RadientTextureAssetManagerStats RadientAssetManagerImpl::GetTextureManagerStats() const
{
    return m_pTextureManager ? m_pTextureManager->GetStats() : RadientTextureAssetManagerStats{};
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

        case RADIENT_ASSET_TYPE_MATERIAL:
            return RadientMaterialAssetManager::GetLoadStatus(pAsset);

        default:
            return RADIENT_STATUS_OK;
    }
}

RADIENT_STATUS RadientAssetManagerImpl::LoadGLTFSceneAsset(RadientImport::ImportedDocument& ImportedScene,
                                                           const std::string&               SourceURI)
{
    GLTF::DocumentLoadInfo DocLoadInfo;
    DocLoadInfo.FileName     = SourceURI.c_str();
    DocLoadInfo.DecodeImages = false;

    std::shared_ptr<GLTF::Document> pDocument = std::make_shared<GLTF::Document>(DocLoadInfo);

    ImportedScene.Textures =
        RadientGLTFLoader::LoadTextures(*m_pThreadPool,
                                        *m_pTextureManager,
                                        SourceURI,
                                        pDocument);

    ImportedScene.Materials =
        RadientGLTFLoader::LoadMaterials(*m_pMaterialManager,
                                         pDocument,
                                         ImportedScene.Textures);

    return RadientGLTFLoader::LoadScene(*m_pThreadPool,
                                        *m_pMeshManager,
                                        SourceURI,
                                        pDocument,
                                        ImportedScene.Materials,
                                        ImportedScene);
}

void RadientAssetManagerImpl::LoadSceneAsset(ScenePayloadImpl&    Scene,
                                             RADIENT_SCENE_FORMAT Format,
                                             const std::string&   SourceURI)
{
    ImportedSceneStorage& SceneStorage = Scene.GetStorage();

    RadientImport::ImportedDocument ImportedScene;
    RADIENT_STATUS                  Status = RADIENT_STATUS_INVALID_OPERATION;
    try
    {
        switch (Format)
        {
            case RADIENT_SCENE_FORMAT_GLTF:
                Status = LoadGLTFSceneAsset(ImportedScene, SourceURI);
                break;

            default:
                LOG_ERROR_MESSAGE("Scene format ", static_cast<Int32>(Format), " is not supported yet.");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                break;
        }
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient scene asset '", SourceURI, "': ", Error.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient scene asset '", SourceURI, "'");
    }

    if (RADIENT_FAILED(Status))
    {
        SceneStorage.SetFailedStatus(Status);
        return;
    }

    SceneStorage.SetScene(std::move(ImportedScene),
                          m_pDevice != nullptr ? RADIENT_STATUS_PENDING : RADIENT_STATUS_NO_GPU_DATA);
}

} // namespace Diligent
