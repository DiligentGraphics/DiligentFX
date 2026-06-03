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

#include "Errors.hpp"
#include "GLTFLoader.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"
#include "TextureUtilities.h"

#include <exception>
#include <thread>
#include <utility>

namespace Diligent
{

namespace
{

struct GLTFModelToPrepare
{
    RefCntAutoPtr<IRadientSceneAsset> pAsset;
    GLTF::Model*                      pModel = nullptr;
};

template <typename ValueType>
std::vector<ValueType> CopyArray(const ValueType* pData, Uint32 Count)
{
    return pData != nullptr && Count != 0 ?
        std::vector<ValueType>{pData, pData + Count} :
        std::vector<ValueType>{};
}

constexpr Uint64 RadientDefaultIndexBufferSize       = 16ull * 1024ull * 1024ull;
constexpr Uint64 RadientDefaultMaxIndexBufferSize    = 256ull * 1024ull * 1024ull;
constexpr Uint32 RadientDefaultVertexPoolSize        = 1024u * 1024u;
constexpr Uint32 RadientDefaultTextureAtlasSize      = 4096u;
constexpr Uint32 RadientDefaultTextureAtlasSlices    = 1u;
constexpr Uint32 RadientDefaultTextureAtlasMaxSlices = 2048u;

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

RADIENT_STATUS LoadGLTFModel(const std::string&            SourceURI,
                             IRenderDevice*                pDevice,
                             GLTF::ResourceManager*        pResourceManager,
                             IGPUUploadManager*            pUploadManager,
                             std::unique_ptr<GLTF::Model>& pModel)
{
    try
    {
        GLTF::ModelCreateInfo ModelCI{SourceURI.c_str()};
        ModelCI.pResourceManager = pResourceManager;
        ModelCI.pUploadMgr       = pUploadManager;
        pModel                   = std::make_unique<GLTF::Model>(pDevice, nullptr, ModelCI);
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "': ", Error.what());
        return RADIENT_STATUS_INVALID_OPERATION;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "'");
        return RADIENT_STATUS_INVALID_OPERATION;
    }
}

RADIENT_STATUS LoadTextureAsset(const std::string&       SourceURI,
                                Bool                     IsSRGB,
                                IRenderDevice*           pDevice,
                                RefCntAutoPtr<ITexture>& pTexture)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        TextureLoadInfo LoadInfo{SourceURI.c_str()};
        LoadInfo.IsSRGB = IsSRGB;
        CreateTextureFromFile(SourceURI.c_str(), LoadInfo, pDevice, &pTexture);
        return pTexture != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient texture asset '", SourceURI, "': ", Error.what());
        return RADIENT_STATUS_INVALID_OPERATION;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient texture asset '", SourceURI, "'");
        return RADIENT_STATUS_INVALID_OPERATION;
    }
}

} // namespace

RadientAssetManagerImpl::GLTFModelStorage::GLTFModelStorage(GLTFModelStorage&& Rhs) noexcept :
    SourceURI{std::move(Rhs.SourceURI)},
    pModel{std::move(Rhs.pModel)},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    GPUUpdateQueued{Rhs.GPUUpdateQueued}
{
}

template <typename InterfaceType, const INTERFACE_ID& InterfaceID, RADIENT_ASSET_TYPE AssetType, typename StorageType>
RadientAssetManagerImpl::AssetImpl<InterfaceType, InterfaceID, AssetType, StorageType>::AssetImpl(IReferenceCounters* pRefCounters,
                                                                                                  std::string&&       URI,
                                                                                                  const Char*         Name,
                                                                                                  StorageType&&       Storage) :
    TBase{pRefCounters},
    m_URI{std::move(URI)},
    m_Name{Name != nullptr ? Name : ""},
    m_Storage{std::move(Storage)}
{
    m_Ref.URI     = m_URI.c_str();
    m_Ref.Version = 1;
}

template <typename InterfaceType, typename ImplType>
InterfaceType* RadientAssetManagerImpl::StoreAsset(const char*                  Type,
                                                   const Char*                  Name,
                                                   typename ImplType::Storage&& Storage)
{
    RefCntAutoPtr<ImplType> pAsset{MakeNewRCObj<ImplType>()(MakeURI(Type), Name, std::move(Storage))};

    RefCntAutoPtr<IRadientAsset> pBase{pAsset, IID_RadientAsset};
    VERIFY(pBase != nullptr, "Radient asset object does not expose IRadientAsset");

    {
        std::unique_lock<std::shared_mutex> Lock{m_Mutex};

        const auto It = m_Assets.find(HashMapStringKey{pBase->GetReference().URI});
        if (It != m_Assets.end())
        {
            VERIFY(It->second.Lock() == nullptr, "Asset URI already exists");
            It->second = RefCntWeakPtr<IRadientAsset>{pBase};
        }
        else
        {
            m_Assets.emplace(HashMapStringKey{pBase->GetReference().URI, true},
                             RefCntWeakPtr<IRadientAsset>{pBase});
        }
    }

    return pAsset.Detach();
}

template <typename InterfaceType, typename ImplType>
ImplType* RadientAssetManagerImpl::GetAssetImpl(InterfaceType* pAsset)
{
    return pAsset != nullptr ? ClassPtrCast<ImplType>(pAsset) : nullptr;
}

RadientAssetManagerImpl::RadientAssetManagerImpl(IReferenceCounters* pRefCounters,
                                                 const CreateInfo&   CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Assets.Desc.Name != nullptr ? CreateInfo.Assets.Desc.Name : ""},
    m_Desc{CreateInfo.Assets.Desc},
    m_pThreadPool{CreateInfo.pThreadPool}
{
    m_Desc.Name = m_Name.c_str();

    if (CreateInfo.pDevice != nullptr)
    {
        GPUUploadManagerCreateInfo UploadCI;
        UploadCI.pDevice = CreateInfo.pDevice;
        CreateGPUUploadManager(UploadCI, &m_pUploadManager);

        const GLTF::ResourceManager::CreateInfo ResourceCI = CreateResourceManagerInfo();

        m_pDevice          = CreateInfo.pDevice;
        m_pResourceManager = GLTF::ResourceManager::Create(CreateInfo.pDevice, ResourceCI);
    }
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
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMesh = nullptr;

    if (!ValidateMesh(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    MeshStorage MeshData;

    MeshData.VertexBuffers.reserve(MeshCI.VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < MeshCI.VertexBufferCount; ++BufferIndex)
    {
        const RadientVertexBufferCreateInfo& VertexBufferCI = MeshCI.pVertexBuffers[BufferIndex];

        MeshVertexBufferStorage VertexBuffer;
        VertexBuffer.Name         = VertexBufferCI.Name != nullptr ? VertexBufferCI.Name : "";
        VertexBuffer.Positions    = CopyArray(VertexBufferCI.pPositions, VertexBufferCI.VertexCount);
        VertexBuffer.Normals      = CopyArray(VertexBufferCI.pNormals, VertexBufferCI.VertexCount);
        VertexBuffer.Tangents     = CopyArray(VertexBufferCI.pTangents, VertexBufferCI.VertexCount);
        VertexBuffer.TexCoords0   = CopyArray(VertexBufferCI.pTexCoords0, VertexBufferCI.VertexCount);
        VertexBuffer.Colors0      = CopyArray(VertexBufferCI.pColors0, VertexBufferCI.VertexCount);
        VertexBuffer.BoneIndices0 = CopyArray(VertexBufferCI.pBoneIndices0, VertexBufferCI.VertexCount);
        VertexBuffer.BoneWeights0 = CopyArray(VertexBufferCI.pBoneWeights0, VertexBufferCI.VertexCount);
        MeshData.VertexBuffers.emplace_back(std::move(VertexBuffer));
    }

    MeshData.IndexBuffer.IndexType  = MeshCI.IndexBuffer.IndexType;
    MeshData.IndexBuffer.IndexCount = MeshCI.IndexBuffer.IndexCount;
    if (MeshCI.IndexBuffer.IndexCount != 0)
    {
        const size_t IndexSize =
            MeshCI.IndexBuffer.IndexType == RADIENT_INDEX_TYPE_UINT16 ? sizeof(Uint16) : sizeof(Uint32);
        const Uint8* pIndexData = static_cast<const Uint8*>(MeshCI.IndexBuffer.pIndices);
        MeshData.IndexBuffer.Indices.assign(pIndexData, pIndexData + MeshCI.IndexBuffer.IndexCount * IndexSize);
    }

    MeshData.MeshPrimitives.reserve(MeshCI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];

        MeshPrimitiveStorage Primitive;
        Primitive.Name              = PrimitiveCI.Name != nullptr ? PrimitiveCI.Name : "";
        Primitive.VertexBufferIndex = PrimitiveCI.VertexBufferIndex;
        Primitive.FirstIndex        = PrimitiveCI.FirstIndex;
        Primitive.IndexCount        = PrimitiveCI.IndexCount;
        Primitive.pMaterial         = PrimitiveCI.pMaterial;

        MeshData.MeshPrimitives.emplace_back(std::move(Primitive));
    }

    *ppMesh = StoreAsset<IRadientMeshAsset, MeshAssetImpl>("mesh", MeshCI.Name, MeshAssetStorage{std::move(MeshData)});
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                       IRadientMaterialAsset**          ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMaterial = nullptr;

    MaterialStorage MaterialData;
    MaterialData.BaseColorFactor           = MaterialCI.BaseColorFactor;
    MaterialData.MetallicFactor            = MaterialCI.MetallicFactor;
    MaterialData.RoughnessFactor           = MaterialCI.RoughnessFactor;
    MaterialData.EmissiveFactor            = MaterialCI.EmissiveFactor;
    MaterialData.AlphaCutoff               = MaterialCI.AlphaCutoff;
    MaterialData.DoubleSided               = MaterialCI.DoubleSided;
    MaterialData.pBaseColorTexture         = MaterialCI.pBaseColorTexture;
    MaterialData.pMetallicRoughnessTexture = MaterialCI.pMetallicRoughnessTexture;
    MaterialData.pNormalTexture            = MaterialCI.pNormalTexture;
    MaterialData.pOcclusionTexture         = MaterialCI.pOcclusionTexture;
    MaterialData.pEmissiveTexture          = MaterialCI.pEmissiveTexture;

    *ppMaterial = StoreAsset<IRadientMaterialAsset, MaterialAssetImpl>("material", MaterialCI.Name, std::move(MaterialData));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                    IRadientTextureAsset**        ppTexture)
{
    if (ppTexture == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppTexture = nullptr;

    if (!ValidateTexture(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    TextureStorage TextureData;
    TextureData.SourceURI  = LoadInfo.URI;
    TextureData.LoadStatus = LoadTextureAsset(TextureData.SourceURI,
                                              LoadInfo.IsSRGB,
                                              m_pDevice,
                                              TextureData.pTexture);
    if (RADIENT_FAILED(TextureData.LoadStatus))
        return TextureData.LoadStatus;

    *ppTexture = StoreAsset<IRadientTextureAsset, TextureAssetImpl>("texture", LoadInfo.URI, std::move(TextureData));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::LoadGLTF(const RadientGLTFLoadInfo& LoadInfo,
                                                 IRadientSceneAsset**       ppModel)
{
    if (ppModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppModel = nullptr;

    if (!ValidateGLTF(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    GLTFModelStorage GLTFModelData;
    GLTFModelData.SourceURI = LoadInfo.URI;

    if (m_pThreadPool == nullptr)
    {
        const RADIENT_STATUS Status = LoadGLTFModel(GLTFModelData.SourceURI,
                                                    m_pDevice,
                                                    m_pResourceManager,
                                                    m_pUploadManager,
                                                    GLTFModelData.pModel);
        if (Status != RADIENT_STATUS_OK)
            return Status;

        GLTFModelData.LoadStatus.store(RADIENT_STATUS_OK, std::memory_order_relaxed);
        *ppModel = StoreAsset<IRadientSceneAsset, SceneAssetImpl>("gltf", LoadInfo.URI, std::move(GLTFModelData));
        EnqueueGPUResourceUpdate(*ppModel);
        return RADIENT_STATUS_OK;
    }

    GLTFModelData.LoadStatus.store(RADIENT_STATUS_PENDING, std::memory_order_relaxed);
    *ppModel = StoreAsset<IRadientSceneAsset, SceneAssetImpl>("gltf", LoadInfo.URI, std::move(GLTFModelData));

    RefCntAutoPtr<RadientAssetManagerImpl> pSelf{this};
    RefCntAutoPtr<IRadientSceneAsset>      pModelAsset{*ppModel};
    const std::string                      SourceURI{LoadInfo.URI};

    EnqueueAsyncWork(
        m_pThreadPool,
        [pSelf, pModelAsset, SourceURI](Uint32) //
        {
            std::unique_ptr<GLTF::Model> pModel;
            const RADIENT_STATUS         Status = LoadGLTFModel(SourceURI,
                                                                pSelf->m_pDevice,
                                                                pSelf->m_pResourceManager,
                                                                pSelf->m_pUploadManager,
                                                                pModel);

            pSelf->CompleteGLTFLoad(pModelAsset, std::move(pModel), Status);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return GetGLTFLoadStatus(pModelAsset);
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
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMesh = nullptr;

    if (pModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const SceneAssetImpl* pModelImpl = GetAssetImpl<const IRadientSceneAsset, const SceneAssetImpl>(pModel);
    if (pModelImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFModelStorage& GLTFModel  = pModelImpl->GetStorage();
    const RADIENT_STATUS    LoadStatus = GLTFModel.LoadStatus.load(std::memory_order_acquire);
    if (LoadStatus != RADIENT_STATUS_OK)
        return LoadStatus;

    GLTFMeshStorage GLTFMeshData;
    GLTFMeshData.pModel    = pModel;
    GLTFMeshData.MeshIndex = MeshIndex;

    *ppMesh = StoreAsset<IRadientMeshAsset, MeshAssetImpl>("mesh", Name, MeshAssetStorage{std::move(GLTFMeshData)});
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::GetMeshGLTFSource(IRadientMeshAsset*   pMesh,
                                                          IRadientSceneAsset** ppModel,
                                                          Uint32&              MeshIndex) const
{
    if (ppModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppModel  = nullptr;
    MeshIndex = ~0u;

    const MeshAssetImpl* pImpl = GetAssetImpl<const IRadientMeshAsset, const MeshAssetImpl>(pMesh);
    if (pImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFMeshStorage* pGLTFMesh = std::get_if<GLTFMeshStorage>(&pImpl->GetStorage());
    if (pGLTFMesh == nullptr || pGLTFMesh->pModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppModel = pGLTFMesh->pModel;
    (*ppModel)->AddRef();
    MeshIndex = pGLTFMesh->MeshIndex;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFSourceURI(IRadientSceneAsset* pModel,
                                                         const Char*&        SourceURI) const
{
    SourceURI = nullptr;

    const SceneAssetImpl* pImpl = GetAssetImpl<const IRadientSceneAsset, const SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    SourceURI = pImpl->GetStorage().SourceURI.c_str();
    return RADIENT_STATUS_OK;
}

const GLTF::Model* RadientAssetManagerImpl::GetGLTFModel(IRadientSceneAsset* pModel,
                                                         bool                RequireGPUResourcesReady) const
{
    const SceneAssetImpl* pImpl = GetAssetImpl<const IRadientSceneAsset, const SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return nullptr;

    const GLTFModelStorage& GLTFModel = pImpl->GetStorage();
    if (GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
        (RequireGPUResourcesReady && !GLTFModel.GPUResourcesReady.load(std::memory_order_acquire)))
    {
        return nullptr;
    }

    return GLTFModel.pModel.get();
}

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFLoadStatus(IRadientSceneAsset* pModel) const
{
    const SceneAssetImpl* pImpl = GetAssetImpl<const IRadientSceneAsset, const SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    return pImpl->GetStorage().LoadStatus.load(std::memory_order_acquire);
}

ITextureView* RadientAssetManagerImpl::GetTextureSRV(IRadientTextureAsset* pTextureAsset) const
{
    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const TextureAssetImpl* pImpl = GetAssetImpl<const IRadientTextureAsset, const TextureAssetImpl>(pTextureAsset);
    if (pImpl == nullptr)
        return nullptr;

    const TextureStorage& Texture = pImpl->GetStorage();
    if (Texture.LoadStatus != RADIENT_STATUS_OK ||
        Texture.pTexture == nullptr)
    {
        return nullptr;
    }

    return Texture.pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

RADIENT_STATUS RadientAssetManagerImpl::UpdateGPUResources(IRenderDevice*  pDevice,
                                                           IDeviceContext* pContext)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_OK;

    if (pContext == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    std::vector<GLTFModelToPrepare> ModelsToPrepare;

    if (m_pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (m_pDevice != pDevice)
    {
        UNEXPECTED("Radient asset manager device changed. This should never happen.");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    {
        std::unique_lock<std::shared_mutex> Lock{m_Mutex};
        m_GPUResourceUpdateScratch.swap(m_PendingGPUResourceUpdates);
    }

    ModelsToPrepare.reserve(m_GPUResourceUpdateScratch.size());
    for (RefCntWeakPtr<IRadientSceneAsset>& WeakAsset : m_GPUResourceUpdateScratch)
    {
        RefCntAutoPtr<IRadientSceneAsset> pSceneAsset = WeakAsset.Lock();
        if (pSceneAsset == nullptr)
            continue;

        SceneAssetImpl* pImpl = GetAssetImpl<IRadientSceneAsset, SceneAssetImpl>(pSceneAsset);
        VERIFY(pImpl != nullptr,
               "Pending GPU resource update references an invalid asset");
        if (pImpl == nullptr)
            continue;

        GLTFModelStorage& GLTFModel = pImpl->GetStorage();

        if (GLTFModel.GPUResourcesReady.load(std::memory_order_acquire) ||
            GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
            GLTFModel.pModel == nullptr)
        {
            GLTFModel.GPUUpdateQueued = false;
            continue;
        }

        ModelsToPrepare.push_back({pSceneAsset, GLTFModel.pModel.get()});
    }
    m_GPUResourceUpdateScratch.clear();

    if (m_pUploadManager != nullptr)
        m_pUploadManager->RenderThreadUpdate(pContext);

    if (m_pResourceManager != nullptr)
        m_pResourceManager->UpdateAllResources(pDevice, pContext);

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    auto GetQueuedGLTFModelLocked = [](const GLTFModelToPrepare& Model) -> GLTFModelStorage* //
    {
        SceneAssetImpl* pImpl = GetAssetImpl<IRadientSceneAsset, SceneAssetImpl>(Model.pAsset);
        VERIFY(pImpl != nullptr, "Queued GPU resource update references an invalid asset");
        if (pImpl == nullptr)
            return nullptr;

        GLTFModelStorage& GLTFModel = pImpl->GetStorage();
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
            std::unique_lock<std::shared_mutex> Lock{m_Mutex};

            if (GLTFModelStorage* pGLTFModel = GetQueuedGLTFModelLocked(Model))
            {
                pGLTFModel->GPUUpdateQueued = false;
                EnqueueGPUResourceUpdateLocked(Model.pAsset, *pGLTFModel);
            }

            Status = RADIENT_STATUS_OUT_OF_DATE;
            continue;
        }

        std::unique_lock<std::shared_mutex> Lock{m_Mutex};

        if (GLTFModelStorage* pGLTFModel = GetQueuedGLTFModelLocked(Model))
        {
            pGLTFModel->GPUResourcesReady.store(true, std::memory_order_release);
            pGLTFModel->GPUUpdateQueued = false;
        }
    }

    return Status;
}

GLTF::ResourceManager* RadientAssetManagerImpl::GetResourceManager() const
{
    return m_pResourceManager;
}

bool RadientAssetManagerImpl::ValidateMesh(const RadientMeshCreateInfo& MeshCI) const
{
    if (MeshCI.VertexBufferCount == 0 || MeshCI.pVertexBuffers == nullptr ||
        MeshCI.PrimitiveCount == 0 || MeshCI.pPrimitives == nullptr)
        return false;

    for (Uint32 BufferIndex = 0; BufferIndex < MeshCI.VertexBufferCount; ++BufferIndex)
    {
        const RadientVertexBufferCreateInfo& VertexBufferCI = MeshCI.pVertexBuffers[BufferIndex];
        if (VertexBufferCI.VertexCount == 0 || VertexBufferCI.pPositions == nullptr)
            return false;

        const bool HasBoneIndices = VertexBufferCI.pBoneIndices0 != nullptr;
        const bool HasBoneWeights = VertexBufferCI.pBoneWeights0 != nullptr;
        if (HasBoneIndices != HasBoneWeights)
            return false;
    }

    if (MeshCI.IndexBuffer.IndexCount == 0 ||
        MeshCI.IndexBuffer.pIndices == nullptr ||
        (MeshCI.IndexBuffer.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
         MeshCI.IndexBuffer.IndexType != RADIENT_INDEX_TYPE_UINT32))
    {
        return false;
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.VertexBufferIndex >= MeshCI.VertexBufferCount ||
            PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= MeshCI.IndexBuffer.IndexCount)
        {
            return false;
        }

        if (PrimitiveCI.IndexCount > MeshCI.IndexBuffer.IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
    }

    return true;
}

bool RadientAssetManagerImpl::ValidateGLTF(const RadientGLTFLoadInfo& LoadInfo) const
{
    return LoadInfo.URI != nullptr && *LoadInfo.URI != 0;
}

bool RadientAssetManagerImpl::ValidateTexture(const RadientTextureLoadInfo& LoadInfo) const
{
    return LoadInfo.URI != nullptr && *LoadInfo.URI != 0;
}

RefCntAutoPtr<IRadientAsset> RadientAssetManagerImpl::FindAssetLocked(const RadientAssetReference& Ref) const
{
    if (Ref.URI == nullptr || *Ref.URI == 0 || Ref.Version == 0)
        return {};

    const auto It = m_Assets.find(HashMapStringKey{Ref.URI});
    if (It == m_Assets.end())
        return {};

    RefCntAutoPtr<IRadientAsset> pAsset = It->second.Lock();
    return pAsset != nullptr && pAsset->GetReference().Version == Ref.Version ? pAsset : RefCntAutoPtr<IRadientAsset>{};
}

RADIENT_STATUS RadientAssetManagerImpl::GetAssetLoadStatus(IRadientAsset* pAsset) const
{
    if (pAsset == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    switch (pAsset->GetType())
    {
        case RADIENT_ASSET_TYPE_SCENE:
        {
            RefCntAutoPtr<IRadientSceneAsset> pScene{pAsset, IID_RadientSceneAsset};
            const SceneAssetImpl*             pImpl = GetAssetImpl<const IRadientSceneAsset, const SceneAssetImpl>(pScene);
            return pImpl != nullptr ?
                pImpl->GetStorage().LoadStatus.load(std::memory_order_acquire) :
                RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_TEXTURE:
        {
            RefCntAutoPtr<IRadientTextureAsset> pTexture{pAsset, IID_RadientTextureAsset};
            const TextureAssetImpl*             pImpl = GetAssetImpl<const IRadientTextureAsset, const TextureAssetImpl>(pTexture);
            return pImpl != nullptr ? pImpl->GetStorage().LoadStatus : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        default:
            return RADIENT_STATUS_OK;
    }
}

std::string RadientAssetManagerImpl::MakeURI(const char* Type)
{
    const RadientHandle AssetID = m_NextAssetID.fetch_add(1, std::memory_order_relaxed);
    return std::string{"radient://session/"} + Type + "/" + std::to_string(AssetID);
}

void RadientAssetManagerImpl::EnqueueGPUResourceUpdate(IRadientSceneAsset* pModel)
{
    SceneAssetImpl* pImpl = GetAssetImpl<IRadientSceneAsset, SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return;

    std::unique_lock<std::shared_mutex> Lock{m_Mutex};
    EnqueueGPUResourceUpdateLocked(pModel, pImpl->GetStorage());
}

void RadientAssetManagerImpl::EnqueueGPUResourceUpdateLocked(IRadientSceneAsset* pModel,
                                                             GLTFModelStorage&   GLTFModel)
{
    if (pModel == nullptr ||
        GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
        GLTFModel.GPUResourcesReady.load(std::memory_order_acquire) ||
        GLTFModel.GPUUpdateQueued ||
        GLTFModel.pModel == nullptr)
    {
        return;
    }

    GLTFModel.GPUUpdateQueued = true;
    m_PendingGPUResourceUpdates.emplace_back(pModel);
}

void RadientAssetManagerImpl::CompleteGLTFLoad(IRadientSceneAsset*          pModel,
                                               std::unique_ptr<GLTF::Model> pModelData,
                                               RADIENT_STATUS               Status)
{
    std::unique_lock<std::shared_mutex> Lock{m_Mutex};

    SceneAssetImpl* pImpl = GetAssetImpl<IRadientSceneAsset, SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return;

    GLTFModelStorage& GLTFModel = pImpl->GetStorage();
    GLTFModel.pModel            = std::move(pModelData);
    GLTFModel.GPUResourcesReady.store(false, std::memory_order_release);
    GLTFModel.GPUUpdateQueued = false;
    GLTFModel.LoadStatus.store(Status, std::memory_order_release);

    EnqueueGPUResourceUpdateLocked(pModel, GLTFModel);
}

} // namespace Diligent
