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
#include "Assets/RadientAssetResolver.hpp"
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientCacheKeyBuilder.hpp"
#include "Import/RadientGLTFSceneAssetImporter.hpp"
#include "Errors.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/inlined_vector.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

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

        AnimationClips.clear();
        AnimationClips.reserve(Scene.Animations.size());
        for (const RadientImport::ImportedAnimation& Animation : Scene.Animations)
        {
            if (Animation.pClip != nullptr)
                AnimationClips.push_back(Animation.pClip);
        }
        AssetDesc.ppAnimationClips   = AnimationClips.empty() ? nullptr : AnimationClips.data();
        AssetDesc.AnimationClipCount = static_cast<Uint32>(AnimationClips.size());

        GPUResourceStatus.store(InitialGPUStatus, std::memory_order_relaxed);
        LoadStatus.store(RADIENT_STATUS_PENDING, std::memory_order_relaxed);
        // Publish the scene and initial status values last. Readers use this
        // acquire flag before scanning dependencies and caching terminal status.
        SceneDataReady.store(true, std::memory_order_release);
    }

    void SetFailedStatus(RADIENT_STATUS Status) noexcept
    {
        if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING)
            Status = RADIENT_STATUS_FAILED;

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

        // Materials own their render texture dependencies and substitute
        // defaults for failed sources. Raw document texture slots therefore do
        // not independently determine whether the imported scene succeeded.
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

        // Materials own their render texture dependencies and substitute
        // defaults for failed sources. Raw document texture slots therefore do
        // not independently determine whether the imported scene succeeded.
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

    RadientImport::ImportedDocument          Scene;
    std::vector<IRadientAnimationClipAsset*> AnimationClips;
    RadientSceneAssetDesc                    AssetDesc;
    std::atomic_bool                         SceneDataReady{false};
    mutable std::atomic<RADIENT_STATUS>      LoadStatus{RADIENT_STATUS_OK};
    mutable std::atomic<RADIENT_STATUS>      GPUResourceStatus{RADIENT_STATUS_OK};
};

GLTF::ResourceManager::CreateInfo CreateResourceManagerInfo(const RadientResourceManagerCreateInfo& ResourceCI)
{
    GLTF::ResourceManager::CreateInfo CreateInfo;

    CreateInfo.IndexAllocatorCI.Desc.Name      = "Radient index pool";
    CreateInfo.IndexAllocatorCI.Desc.Size      = ResourceCI.IndexBufferSize;
    CreateInfo.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    CreateInfo.IndexAllocatorCI.ExpansionSize  = ResourceCI.IndexBufferSize;
    CreateInfo.IndexAllocatorCI.MaxSize        = ResourceCI.MaxIndexBufferSize;

    CreateInfo.MorphTargetAllocatorCI.Desc.Name              = "Radient morph target buffer";
    CreateInfo.MorphTargetAllocatorCI.Desc.Size              = ResourceCI.MorphTargetBufferSize;
    CreateInfo.MorphTargetAllocatorCI.Desc.Usage             = USAGE_DEFAULT;
    CreateInfo.MorphTargetAllocatorCI.Desc.BindFlags         = BIND_SHADER_RESOURCE;
    CreateInfo.MorphTargetAllocatorCI.Desc.Mode              = BUFFER_MODE_STRUCTURED;
    CreateInfo.MorphTargetAllocatorCI.Desc.ElementByteStride = sizeof(Float32);
    CreateInfo.MorphTargetAllocatorCI.ExpansionSize          = ResourceCI.MorphTargetBufferSize;
    CreateInfo.MorphTargetAllocatorCI.MaxSize                = ResourceCI.MaxMorphTargetBufferSize;

    CreateInfo.DefaultPoolDesc.Name        = "Radient vertex pool";
    CreateInfo.DefaultPoolDesc.VertexCount = ResourceCI.VertexPoolSize;
    CreateInfo.DefaultPoolDesc.Usage       = USAGE_DEFAULT;
    CreateInfo.DefaultPoolDesc.Mode        = BUFFER_MODE_UNDEFINED;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient texture atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = ResourceCI.TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = ResourceCI.TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.MipLevels = 0;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = ResourceCI.TextureAtlasSlices;
    CreateInfo.DefaultAtlasDesc.Desc.Format    = TEX_FORMAT_RGBA8_TYPELESS;
    CreateInfo.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    CreateInfo.DefaultAtlasDesc.MaxSliceCount  = ResourceCI.TextureAtlasMaxSlices;
    CreateInfo.DefaultAtlasMipLevel0Size       = ResourceCI.TextureAtlasMipLevel0Size;

    return CreateInfo;
}

RefCntAutoPtr<GLTF::ResourceManager> CreateRadientResourceManager(IRenderDevice* pDevice, const RadientResourceManagerCreateInfo& ResourceCI)
{
    if (pDevice == nullptr)
        return {};

    return GLTF::ResourceManager::Create(pDevice, CreateResourceManagerInfo(ResourceCI));
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

RadientMaterialDefaultTextures CreateDefaultMaterialTextures(IThreadPool&                ThreadPool,
                                                             RadientTextureAssetManager& TextureManager)
{
    static constexpr Uint32 DefaultTextureSize = 16;

    RadientMaterialDefaultTextures DefaultTextures;

    auto LoadDefaultTexture = [&](const char* URI, Uint32 Pixel, IRadientTextureAsset** ppTexture) {
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size = DefaultTextureSize * DefaultTextureSize * sizeof(Pixel);
        RefCntAutoPtr<IRadientMutableDataBlob> pPixels;
        RADIENT_STATUS                         Status = CreateRadientMutableDataBlob(BlobCI, &pPixels);
        if (Status != RADIENT_STATUS_OK)
        {
            LOG_ERROR_MESSAGE("Failed to allocate Radient default material texture '", URI, "'");
            return;
        }

        void* pData = nullptr;
        Status      = pPixels->BeginWrite(&pData);
        if (Status != RADIENT_STATUS_OK)
        {
            LOG_ERROR_MESSAGE("Failed to access Radient default material texture '", URI, "'");
            return;
        }
        for (Uint32 Index = 0; Index < DefaultTextureSize * DefaultTextureSize; ++Index)
            std::memcpy(static_cast<Uint8*>(pData) + Index * sizeof(Pixel), &Pixel, sizeof(Pixel));
        Status = pPixels->EndWrite();
        if (Status != RADIENT_STATUS_OK)
        {
            LOG_ERROR_MESSAGE("Failed to finish Radient default material texture '", URI, "'");
            return;
        }

        const RadientTextureMipData Mip{pPixels, 0, DefaultTextureSize * sizeof(Pixel)};
        RadientTextureData          TextureData;
        TextureData.Width         = DefaultTextureSize;
        TextureData.Height        = DefaultTextureSize;
        TextureData.Format        = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
        TextureData.pMipLevels    = &Mip;
        TextureData.MipLevelCount = 1;
        TextureData.GenerateMips  = True;

        RadientTextureLoadInfo LoadInfo;
        LoadInfo.URI          = URI;
        LoadInfo.pTextureData = &TextureData;

        Status = TextureManager.LoadTexture(ThreadPool, LoadInfo, ppTexture);
        if (RADIENT_FAILED(Status))
            LOG_ERROR_MESSAGE("Failed to create Radient default material texture '", URI, "'");
    };

    LoadDefaultTexture("radient://default-texture/white", 0xFFFFFFFFu, DefaultTextures.pWhite.GetAddressOfEmpty());
    LoadDefaultTexture("radient://default-texture/black", 0x00000000u, DefaultTextures.pBlack.GetAddressOfEmpty());
    LoadDefaultTexture("radient://default-texture/normal", 0x00FF7F7Fu, DefaultTextures.pNormal.GetAddressOfEmpty());
    LoadDefaultTexture("radient://default-texture/physical-description", 0x0000FF00u, DefaultTextures.pPhysicalDesc.GetAddressOfEmpty());

    return DefaultTextures;
}

std::string MakeSceneCacheKey(const IRadientSceneAssetImporter* pImporter, const char* Location)
{
    if (Location == nullptr || Location[0] == '\0')
        return {};

    RadientCacheKeyBuilder Builder{"scene", 2};
    // The manager retains every registered importer for the cache's lifetime,
    // so its address identifies the importer without a separate ID.
    Builder.AddInteger("importer", reinterpret_cast<std::uintptr_t>(pImporter))
        .AddString("location", Location);
    return Builder.GetKey();
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

using SceneAssetImplBase =
    RadientAssetImpl<IRadientSceneAsset, IID_RadientSceneAsset, IID_SceneAssetImpl, RADIENT_ASSET_TYPE_SCENE, ScenePayloadImpl>;

class SceneAssetImpl final : public SceneAssetImplBase
{
public:
    using TBase = SceneAssetImplBase;

    SceneAssetImpl(IReferenceCounters*               pRefCounters,
                   std::string&&                     AssetURI,
                   RefCntAutoPtr<ScenePayloadImpl>&& pPayload = {}) :
        TBase{pRefCounters, std::move(AssetURI), std::move(pPayload)}
    {
    }

    static RefCntAutoPtr<SceneAssetImpl> Create(std::string                       AssetURI,
                                                RefCntAutoPtr<ScenePayloadImpl>&& pPayload = {})
    {
        return RefCntAutoPtr<SceneAssetImpl>{
            MakeNewRCObj<SceneAssetImpl>()(std::move(AssetURI), std::move(pPayload))};
    }

    static RefCntAutoPtr<SceneAssetImpl> ResolveAsset(IRadientSceneAsset* pAsset)
    {
        RefCntAutoPtr<SceneAssetImpl> pImpl{pAsset, IID_SceneAssetImpl};
        if (pImpl == nullptr || pImpl->GetPayloadStatus() != RADIENT_STATUS_OK)
            return {};

        return pImpl;
    }

    virtual const RadientSceneAssetDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        static const RadientSceneAssetDesc EmptyDesc{};

        if (GetLoadStatus() != RADIENT_STATUS_OK)
            return EmptyDesc;

        const RefCntAutoPtr<ScenePayloadImpl> pPayload = GetPayload();
        if (pPayload == nullptr)
            return EmptyDesc;

        return pPayload->GetStorage().AssetDesc;
    }
};

} // namespace

// Import services are separate from IRadientAssetManager. Retaining a service
// keeps its manager alive, while Stop() still prevents new import work.
class RadientAssetManagerImpl::MeshImportServicesImpl final : public ObjectBase<IRadientMeshImportServices>
{
public:
    using TBase = ObjectBase<IRadientMeshImportServices>;

    MeshImportServicesImpl(IReferenceCounters*      pRefCounters,
                           RadientAssetManagerImpl* pAssetManager) :
        TBase{pRefCounters},
        m_pAssetManager{pAssetManager}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMeshImportServices, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMeshVertexData(const RadientMeshVertexData& VertexCI,
                                                                   IRadientMeshVertexData**     ppVertexData) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMeshIndexData(const RadientMeshIndexData& IndexCI,
                                                                  IRadientMeshIndexData**     ppIndexData) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMeshMorphTargetData(const RadientMeshMorphTargetData& MorphCI,
                                                                        Uint32                            VertexCount,
                                                                        IRadientMeshMorphTargetData**     ppMorphData) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMeshView(const RadientMeshViewCreateInfo& ViewCI,
                                                             IRadientMeshAsset**              ppMesh) override final;

private:
    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;
};

RADIENT_STATUS RadientAssetManagerImpl::MeshImportServicesImpl::CreateMeshVertexData(const RadientMeshVertexData& VertexCI,
                                                                                     IRadientMeshVertexData**     ppVertexData)
{
    if (ppVertexData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppVertexData == nullptr, "Output vertex data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppVertexData = nullptr;

    if (m_pAssetManager->m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pAssetManager->m_pThreadPool ?
        m_pAssetManager->m_pMeshManager->CreateMeshVertexData(*m_pAssetManager->m_pThreadPool, VertexCI, ppVertexData) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::MeshImportServicesImpl::CreateMeshIndexData(const RadientMeshIndexData& IndexCI,
                                                                                    IRadientMeshIndexData**     ppIndexData)
{
    if (ppIndexData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppIndexData == nullptr, "Output index data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppIndexData = nullptr;

    if (m_pAssetManager->m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pAssetManager->m_pThreadPool ?
        m_pAssetManager->m_pMeshManager->CreateMeshIndexData(*m_pAssetManager->m_pThreadPool, IndexCI, ppIndexData) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::MeshImportServicesImpl::CreateMeshMorphTargetData(const RadientMeshMorphTargetData& MorphCI,
                                                                                          Uint32                            VertexCount,
                                                                                          IRadientMeshMorphTargetData**     ppMorphData)
{
    if (ppMorphData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMorphData == nullptr, "Output morph target data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMorphData = nullptr;

    if (m_pAssetManager->m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pAssetManager->m_pThreadPool ?
        m_pAssetManager->m_pMeshManager->CreateMeshMorphTargetData(*m_pAssetManager->m_pThreadPool, MorphCI, VertexCount, ppMorphData) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::MeshImportServicesImpl::CreateMeshView(const RadientMeshViewCreateInfo& ViewCI,
                                                                               IRadientMeshAsset**              ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (m_pAssetManager->m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pAssetManager->m_pThreadPool ?
        m_pAssetManager->m_pMeshManager->CreateMeshView(*m_pAssetManager->m_pThreadPool, ViewCI.pGeometryData, ViewCI.GeometryCount, ViewCI, ppMesh) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RadientAssetManagerImpl::RadientAssetManagerImpl(IReferenceCounters* pRefCounters,
                                                 const CreateInfo&   CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Assets.Desc.Name != nullptr ? CreateInfo.Assets.Desc.Name : ""},
    m_Desc{CreateInfo.Assets.Desc},
    m_pThreadPool{CreateInfo.pThreadPool},
    m_pDevice{CreateInfo.pDevice},
    m_pAssetResolver{GetRadientAssetResolverOrDefault(CreateInfo.Assets.pAssetResolver)},
    m_pResourceManager{CreateRadientResourceManager(CreateInfo.pDevice, CreateInfo.Resources)},
    m_pUploadManager{CreateRadientGPUUploadManager(CreateInfo.pDevice)},
    m_pTextureManager{
        RadientTextureAssetManager::Create(
            RadientTextureAssetManager::CreateInfo{
                m_pDevice,
                m_pResourceManager,
                m_pUploadManager,
                m_pAssetResolver,
            })},
    m_pMaterialManager{},
    m_pMeshManager{
        RadientMeshAssetManager::Create(
            RadientMeshAssetManager::CreateInfo{
                m_pDevice,
                m_pResourceManager,
                m_pUploadManager,
            })}
{
    m_Desc.Name = m_Name.c_str();

    RadientMaterialAssetManager::CreateInfo MaterialManagerCI;
    if (m_pDevice != nullptr && m_pThreadPool != nullptr && m_pTextureManager != nullptr)
        m_DefaultMaterialTextures = CreateDefaultMaterialTextures(*m_pThreadPool, *m_pTextureManager);

    MaterialManagerCI.DefaultTextures = m_DefaultMaterialTextures;

    m_pMaterialManager = RadientMaterialAssetManager::Create(MaterialManagerCI);

    RADIENT_STATUS DefaultMaterialStatus = RADIENT_STATUS_INVALID_OPERATION;
    if (m_pMaterialManager != nullptr)
    {
        RadientStandardMaterialDefinitionCreateInfo    DefinitionCI{};
        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
        DefaultMaterialStatus = m_pMaterialManager->CreateStandardMaterialDefinition(
            DefinitionCI, pDefinition.GetAddressOfEmpty());

        if (DefaultMaterialStatus == RADIENT_STATUS_OK)
            DefaultMaterialStatus = m_pMaterialManager->CreateMaterial(pDefinition, m_pDefaultMaterial.GetAddressOfEmpty());
    }
    if (RADIENT_FAILED(DefaultMaterialStatus) || m_pDefaultMaterial == nullptr)
        LOG_ERROR_AND_THROW("Failed to create the default Radient material");

    RefCntAutoPtr<IRadientSceneAssetImporter> pGLTFImporter = CreateRadientGLTFSceneAssetImporter();
    if (RegisterSceneAssetImporter(pGLTFImporter) != RADIENT_STATUS_OK)
        LOG_ERROR_AND_THROW("Failed to register the built-in Radient GLTF importer");
}

RadientAssetManagerImpl::~RadientAssetManagerImpl()
{
    DEV_CHECK_ERR(m_pUploadManager == nullptr || m_Stopped.load(std::memory_order_acquire),
                  "RadientAssetManagerImpl::Stop() must be called before destroying a GPU-backed asset manager");
}

RADIENT_STATUS RadientAssetManagerImpl::RegisterSceneAssetImporter(IRadientSceneAssetImporter* pImporter)
{
    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;
    if (pImporter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    try
    {
        const Char* Identifier = pImporter->GetIdentifier();
        if (Identifier == nullptr || *Identifier == '\0')
            return RADIENT_STATUS_INVALID_ARGUMENT;

        std::unique_lock<std::shared_mutex> Lock{m_SceneImportersMutex};
        if (m_Stopped.load(std::memory_order_acquire))
            return RADIENT_STATUS_INVALID_OPERATION;

        const auto InsertResult = m_SceneImporterIndices.emplace(Identifier, m_SceneImporters.size());
        if (!InsertResult.second)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        m_SceneImporters.emplace_back(pImporter);
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to register Radient scene importer: ", Error.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to register Radient scene importer");
    }
    return RADIENT_STATUS_FAILED;
}

RADIENT_STATUS RadientAssetManagerImpl::SelectSceneImporter(const RadientSceneLoadInfo&                LoadInfo,
                                                            RefCntAutoPtr<IRadientSceneAssetImporter>& pImporter) const
{
    try
    {
        const Char* RequestedId = LoadInfo.ImporterId;
        if ((RequestedId == nullptr || *RequestedId == '\0') && LoadInfo.Format == RADIENT_SCENE_FORMAT_GLTF)
            RequestedId = "gltf";

        if (RequestedId != nullptr && *RequestedId != '\0')
        {
            {
                std::shared_lock<std::shared_mutex> Lock{m_SceneImportersMutex};
                const auto                          It = m_SceneImporterIndices.find(RequestedId);
                if (It != m_SceneImporterIndices.end())
                {
                    pImporter = m_SceneImporters[It->second];
                    return RADIENT_STATUS_OK;
                }
            }
            LOG_ERROR_MESSAGE("Radient scene importer '", RequestedId, "' is not registered.");
            return RADIENT_STATUS_UNSUPPORTED;
        }

        absl::InlinedVector<IRadientSceneAssetImporter*, 8> Importers;
        {
            std::shared_lock<std::shared_mutex> Lock{m_SceneImportersMutex};
            Importers.assign(m_SceneImporters.begin(), m_SceneImporters.end());
        }

        // Keep callbacks outside the lock. The snapshot preserves registration
        // order; the manager retains all registered importers for its lifetime.
        Uint32 MatchCount = 0;
        for (IRadientSceneAssetImporter* pCandidate : Importers)
        {
            if (pCandidate->CanImport(LoadInfo.URI))
            {
                if (MatchCount == 0)
                    pImporter = pCandidate;
                ++MatchCount;
            }
        }
        if (MatchCount == 0)
        {
            LOG_ERROR_MESSAGE("No Radient scene importer supports URI '", LoadInfo.URI, "'.");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (MatchCount > 1)
            LOG_INFO_MESSAGE("Multiple Radient scene importers support URI '", LoadInfo.URI, "'; using the first match '", pImporter->GetIdentifier(), "'.");

        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to select Radient scene importer for '", LoadInfo.URI, "': ", Error.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to select Radient scene importer for '", LoadInfo.URI, "'");
    }
    return RADIENT_STATUS_FAILED;
}

RefCntAutoPtr<RadientAssetManagerImpl> RadientAssetManagerImpl::Create(const CreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientAssetManagerImpl>{MakeNewRCObj<RadientAssetManagerImpl>()(CreateInfo)};
}

RefCntAutoPtr<IRadientMeshImportServices> RadientAssetManagerImpl::CreateMeshImportServices()
{
    return RefCntAutoPtr<IRadientMeshImportServices>{MakeNewRCObj<MeshImportServicesImpl>()(this)};
}

const RadientAssetManagerDesc& RadientAssetManagerImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                   IRadientMeshAsset**          ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pThreadPool ?
        m_pMeshManager->CreateMesh(*m_pThreadPool, MeshCI, ppMesh) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateStandardMaterialDefinition(const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                                                         IRadientMaterialDefinitionAsset**                  ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppDefinition == nullptr, "Output material definition pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppDefinition = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pMaterialManager->CreateStandardMaterialDefinition(DefinitionCI, ppDefinition);
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(IRadientMaterialDefinitionAsset* pDefinition,
                                                       IRadientMaterialAsset**          ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    return m_pMaterialManager->CreateMaterial(pDefinition, ppMaterial);
}

RADIENT_STATUS RadientAssetManagerImpl::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                    IRadientTextureAsset**        ppTexture)
{
    if (ppTexture == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppTexture == nullptr, "Output texture pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppTexture = nullptr;

    if (m_pThreadPool == nullptr)
        return RadientTextureAssetManager::RejectTextureLoad(LoadInfo);

    if (m_Stopped.load(std::memory_order_acquire))
        return RadientTextureAssetManager::RejectTextureLoad(LoadInfo);

    return m_pTextureManager->LoadTexture(*m_pThreadPool, LoadInfo, ppTexture);
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

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    const std::string SourceURI = LoadInfo.URI;

    RefCntAutoPtr<IRadientSceneAssetImporter> pImporter;
    const RADIENT_STATUS                      SelectionStatus = SelectSceneImporter(LoadInfo, pImporter);
    if (SelectionStatus != RADIENT_STATUS_OK)
        return SelectionStatus;

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakSelf{this};
    RefCntAutoPtr<SceneAssetImpl>          pModelAsset =
        SceneAssetImpl::Create(SourceURI);
    VERIFY_EXPR(pModelAsset != nullptr);
    if (!pModelAsset)
        return RADIENT_STATUS_FAILED;

    pModelAsset->QueryInterface(IID_RadientSceneAsset, ppScene);

    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [pWeakSelf, pModelAsset, SourceURI, pImporter = std::move(pImporter)](Uint32) mutable //
            {
                RefCntAutoPtr<RadientAssetManagerImpl> pSelf = pWeakSelf.Lock();
                if (pSelf == nullptr)
                {
                    pModelAsset->Fail(RADIENT_STATUS_CANCELLED);
                    return ASYNC_TASK_STATUS_CANCELLED;
                }

                if (pSelf->m_Stopped.load(std::memory_order_acquire))
                {
                    pModelAsset->Fail(RADIENT_STATUS_CANCELLED);
                    return ASYNC_TASK_STATUS_CANCELLED;
                }

                RefCntAutoPtr<IRadientAssetLocation> pSceneLocation;
                const RADIENT_STATUS                 ResolveStatus =
                    pSelf->m_pAssetResolver->ResolveAssetLocation(
                        {SourceURI.c_str(), nullptr},
                        pSceneLocation.GetAddressOfEmpty());
                if (ResolveStatus != RADIENT_STATUS_OK)
                {
                    pModelAsset->Fail(ResolveStatus);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }
                if (pSceneLocation == nullptr)
                {
                    pModelAsset->Fail(RADIENT_STATUS_FAILED);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                const char* ResolvedSourceURI = pSceneLocation->GetLocation();
                if (ResolvedSourceURI == nullptr || ResolvedSourceURI[0] == '\0')
                {
                    pModelAsset->Fail(RADIENT_STATUS_FAILED);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                const std::string CacheKey = MakeSceneCacheKey(pImporter, ResolvedSourceURI);

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

                RefCntAutoPtr<IRadientAssetData> pSceneData;
                const RADIENT_STATUS             OpenStatus =
                    pSelf->m_pAssetResolver->OpenAsset(pSceneLocation, pSceneData.GetAddressOfEmpty());
                if (OpenStatus != RADIENT_STATUS_OK || pSceneData == nullptr)
                {
                    pModelAsset->GetStorage().SetFailedStatus(
                        OpenStatus != RADIENT_STATUS_OK ? OpenStatus : RADIENT_STATUS_FAILED);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                pSelf->LoadSceneAsset(*pModelAsset->GetPayload(), *pImporter, SourceURI, pSceneData);
                return ASYNC_TASK_STATUS_COMPLETE;
            });

    if (!m_pThreadPool->EnqueueTask(pLoadTask))
        pModelAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);

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

        if (m_Stopped.load(std::memory_order_acquire))
            return RADIENT_STATUS_INVALID_OPERATION;

        if (m_pThreadPool == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        if (!m_pThreadPool->ProcessTask(0, false))
            std::this_thread::yield();
    }
}

RADIENT_STATUS RadientAssetManagerImpl::Stop(IDeviceContext* pContext)
{
    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_OK;

    if (m_pUploadManager != nullptr && pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    // Publish the stopped state before stopping uploads so queued scene tasks
    // do not fan out into new texture/material/mesh work during shutdown.
    {
        std::unique_lock<std::shared_mutex> Lock{m_SceneImportersMutex};
        if (m_Stopped.exchange(true, std::memory_order_acq_rel))
            return RADIENT_STATUS_OK;
    }

    if (m_pUploadManager != nullptr)
        m_pUploadManager->Stop(pContext);

    return RADIENT_STATUS_OK;
}

RadientDrawableMeshResolveResult RadientAssetManagerImpl::GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                                          bool               RequireGPUResourcesReady)
{
    return RadientMeshAssetManager::GetDrawableMesh(pMesh, RequireGPUResourcesReady);
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

ITextureView* RadientAssetManagerImpl::GetTextureSRV(IRadientTextureAsset*  pTextureAsset,
                                                     RadientTextureViewType ViewType)
{
    return RadientTextureAssetManager::GetTextureSRV(pTextureAsset, ViewType);
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

        case RADIENT_ASSET_TYPE_MESH_VERTEX_DATA:
        {
            RefCntAutoPtr<IRadientMeshVertexData> pVertexData{pAsset, IID_RadientMeshVertexData};
            return pVertexData != nullptr ? RadientMeshAssetManager::GetLoadStatus(pVertexData.RawPtr()) : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_MESH_INDEX_DATA:
        {
            RefCntAutoPtr<IRadientMeshIndexData> pIndexData{pAsset, IID_RadientMeshIndexData};
            return pIndexData != nullptr ? RadientMeshAssetManager::GetLoadStatus(pIndexData.RawPtr()) : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_MESH_MORPH_TARGET_DATA:
        {
            RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphData{pAsset, IID_RadientMeshMorphTargetData};
            return pMorphData != nullptr ? RadientMeshAssetManager::GetLoadStatus(pMorphData.RawPtr()) : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_SCENE:
            return SceneAssetImpl::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_TEXTURE:
            return RadientTextureAssetManager::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_MATERIAL:
            return RadientMaterialAssetManager::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_MATERIAL_DEFINITION:
        {
            RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition{pAsset, IID_RadientMaterialDefinitionAsset};
            return pDefinition != nullptr ? pDefinition->GetStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_SKELETON:
        case RADIENT_ASSET_TYPE_SKIN:
        case RADIENT_ASSET_TYPE_ANIMATION_CLIP:
            return RADIENT_STATUS_OK;

        default:
            return RADIENT_STATUS_OK;
    }
}

void RadientAssetManagerImpl::LoadSceneAsset(ScenePayloadImpl&           Scene,
                                             IRadientSceneAssetImporter& Importer,
                                             const std::string&          SourceURI,
                                             IRadientAssetData*          pSceneData)
{
    ImportedSceneStorage& SceneStorage = Scene.GetStorage();

    RadientImport::ImportedDocument ImportedScene;
    RADIENT_STATUS                  Status = RADIENT_STATUS_FAILED;
    try
    {
        const RefCntAutoPtr<IRadientMeshImportServices> pMeshImportServices = CreateMeshImportServices();
        const RadientSceneAssetImportContext            Context{
            pSceneData,
            m_pAssetResolver,
            this,
            pMeshImportServices,
            m_pDefaultMaterial,
        };
        Status = Importer.Import(Context, ImportedScene);
        if (Status != RADIENT_STATUS_OK && RADIENT_SUCCEEDED(Status))
        {
            LOG_ERROR_MESSAGE("Radient scene importer must finish document conversion synchronously and return RADIENT_STATUS_OK; returned ",
                              static_cast<Int32>(Status), " for '", SourceURI, "'.");
            Status = RADIENT_STATUS_FAILED;
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

    if (Status != RADIENT_STATUS_OK)
    {
        SceneStorage.SetFailedStatus(Status);
        return;
    }

    SceneStorage.SetScene(std::move(ImportedScene),
                          m_pDevice != nullptr ? RADIENT_STATUS_PENDING : RADIENT_STATUS_NO_GPU_DATA);
}

} // namespace Diligent
