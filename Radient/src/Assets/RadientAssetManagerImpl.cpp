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

#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientDrawableMeshConverter.hpp"
#include "Assets/RadientMeshSource.hpp"
#include "Errors.hpp"
#include "GLTFLoader.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"

#include <atomic>
#include <cstring>
#include <exception>
#include <memory>
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

struct MeshBufferWriteData
{
    const RadientMeshSource* pSource = nullptr;

    bool   WriteIndices      = false;
    Uint32 VertexBufferIndex = 0;
};

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

std::string MakeGLTFCacheKey(const char* URI)
{
    return std::string{"gltf:"} + URI;
}

PBR_Renderer::PSO_FLAGS GetVertexAttribFlags(const GLTF::Model& Model)
{
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_NONE;
    for (Uint32 AttribIndex = 0; AttribIndex < Model.GetNumVertexAttributes(); ++AttribIndex)
    {
        if (!Model.IsVertexAttributeEnabled(AttribIndex))
            continue;

        const GLTF::VertexAttributeDesc& Attrib = Model.GetVertexAttribute(AttribIndex);
        if (std::strcmp(Attrib.Name, GLTF::NormalAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord0AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord1AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
        else if (std::strcmp(Attrib.Name, GLTF::JointsAttributeName) == 0)
        {
            // Radient skinning is not wired yet; keep the pass on the rigid path.
        }
        else if (std::strcmp(Attrib.Name, GLTF::VertexColorAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
        else if (std::strcmp(Attrib.Name, GLTF::TangentAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    }

    return Flags;
}

} // namespace

RadientAssetManagerImpl::MeshStorage::MeshStorage(MeshStorage&& Rhs) noexcept :
    DrawableMesh{std::move(Rhs.DrawableMesh)},
    pIndexAllocation{std::move(Rhs.pIndexAllocation)},
    pVertexAllocation{std::move(Rhs.pVertexAllocation)},
    Materials{std::move(Rhs.Materials)},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    PendingUploads{Rhs.PendingUploads.load(std::memory_order_relaxed)}
{
}

RadientAssetManagerImpl::GLTFModelStorage::GLTFModelStorage(GLTFModelStorage&& Rhs) noexcept :
    SourceURI{std::move(Rhs.SourceURI)},
    pModel{std::move(Rhs.pModel)},
    VertexAttribFlags{Rhs.VertexAttribFlags},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    GPUUpdateQueued{Rhs.GPUUpdateQueued.load(std::memory_order_relaxed)}
{
}

RadientAssetManagerImpl::TextureStorage::TextureStorage(TextureStorage&& Rhs) noexcept :
    SourceURI{std::move(Rhs.SourceURI)},
    pTexture{std::move(Rhs.pTexture)},
    pAtlasSuballocation{std::move(Rhs.pAtlasSuballocation)},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    PendingUploads{Rhs.PendingUploads.load(std::memory_order_relaxed)}
{
}

RADIENT_STATUS RadientAssetManagerImpl::InitializeMeshStorage(const RadientMeshSource& Source,
                                                              MeshStorage&             Storage) const
{
    if (m_pResourceManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const RADIENT_STATUS SourceStatus = Source.GetStatus();
    if (RADIENT_FAILED(SourceStatus))
        return SourceStatus;

    if (Source.GetIndexDataSize() == 0 ||
        Source.GetVertexCount() == 0 ||
        Source.GetVertexBufferCount() == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    Storage.DrawableMesh.VertexAttribFlags = Source.GetVertexAttribFlags();
    Storage.DrawableMesh.Primitives.clear();
    Storage.Materials.clear();

    const Uint32 PrimitiveCount = Source.GetPrimitiveCount();
    Storage.DrawableMesh.Primitives.reserve(PrimitiveCount);
    Storage.Materials.reserve(PrimitiveCount);

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = Source.GetPrimitive(PrimitiveIndex);

        const GLTF::Material* pMaterial = nullptr;
        if (IRadientMaterialAsset* pMaterialAsset = PrimitiveCI.pMaterial)
        {
            pMaterial = GetMaterial(pMaterialAsset);
            if (pMaterial == nullptr)
                return RADIENT_STATUS_INVALID_ARGUMENT;

            Storage.Materials.emplace_back(pMaterialAsset);
        }

        Storage.DrawableMesh.Primitives.push_back(RadientDrawableMeshPrimitive{
            pMaterial,
            true,
            PrimitiveCI.FirstIndex,
            PrimitiveCI.IndexCount});
    }

    Storage.pIndexAllocation = m_pResourceManager->AllocateIndices(Source.GetIndexDataSize(),
                                                                   alignof(Uint32));
    if (Storage.pIndexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const Uint32                           VertexBufferCount = Source.GetVertexBufferCount();
    GLTF::ResourceManager::VertexLayoutKey LayoutKey;
    LayoutKey.Elements.reserve(VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        LayoutKey.Elements.emplace_back(Source.GetVertexStride(BufferIndex), BIND_VERTEX_BUFFER);

    Storage.pVertexAllocation = m_pResourceManager->AllocateVertices(LayoutKey, Source.GetVertexCount());
    if (Storage.pVertexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    Storage.DrawableMesh.pVertexPool        = Storage.pVertexAllocation->GetPool();
    Storage.DrawableMesh.FirstIndexLocation = Storage.pIndexAllocation->GetOffset() / sizeof(Uint32);
    Storage.DrawableMesh.BaseVertex         = Storage.pVertexAllocation->GetStartVertex();
    Storage.GPUResourcesReady.store(false, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

void RadientAssetManagerImpl::UpdateMeshUploadProgress(MeshStorage& Storage,
                                                       bool         CopyScheduled)
{
    if (!CopyScheduled)
        Storage.LoadStatus.store(RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);

    const Uint32 PrevPendingUploads = Storage.PendingUploads.fetch_sub(1, std::memory_order_acq_rel);
    VERIFY_EXPR(PrevPendingUploads > 0);
    if (PrevPendingUploads == 1)
    {
        const bool Ready = Storage.LoadStatus.load(std::memory_order_acquire) == RADIENT_STATUS_OK;
        Storage.GPUResourcesReady.store(Ready, std::memory_order_release);
    }
}

RADIENT_STATUS RadientAssetManagerImpl::ScheduleMeshGPUUpload(MeshAssetImpl&           MeshAsset,
                                                              const RadientMeshSource& Source,
                                                              MeshStorage&             Storage) const
{
    if (m_pDevice == nullptr ||
        m_pUploadManager == nullptr ||
        Storage.pIndexAllocation == nullptr ||
        Storage.pVertexAllocation == nullptr ||
        Source.GetIndexCount() == 0)
    {
        Storage.LoadStatus.store(RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const Uint32 VertexBufferCount = Source.GetVertexBufferCount();
    if (VertexBufferCount == 0)
    {
        Storage.LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    Uint32 UploadCount = 1;
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (Source.IsVertexBufferActive(BufferIndex))
        {
            if (Source.GetVertexBufferDataSize(BufferIndex) == 0)
            {
                Storage.LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            ++UploadCount;
        }
    }

    Storage.PendingUploads.store(UploadCount, std::memory_order_release);
    Storage.GPUResourcesReady.store(false, std::memory_order_release);
    Storage.LoadStatus.store(RADIENT_STATUS_OK, std::memory_order_release);

    struct MeshBufferCopyData
    {
        RefCntAutoPtr<MeshAssetImpl> pMeshAsset;
        MeshStorage*                 pMeshStorage = nullptr;
        RefCntAutoPtr<IRenderDevice> pDevice;

        RefCntAutoPtr<IBufferSuballocation>  pIndexAllocation;
        RefCntAutoPtr<IVertexPoolAllocation> pVertexAllocation;

        Uint32 VertexBufferIndex = 0;
        Uint32 VertexStride      = 0;
    };

    auto ScheduleCopy =
        [this, &MeshAsset, &Storage, &Source](Uint32 DataSize,
                                              auto&& InitWriteData,
                                              auto&& InitCopyData) //
    {
        std::unique_ptr<MeshBufferCopyData> pCopyData{new MeshBufferCopyData{}};
        pCopyData->pMeshAsset   = &MeshAsset;
        pCopyData->pMeshStorage = &Storage;
        pCopyData->pDevice      = m_pDevice;
        InitCopyData(*pCopyData);

        MeshBufferWriteData WriteData;
        WriteData.pSource = &Source;
        InitWriteData(WriteData);

        ScheduleBufferUpdateInfo UpdateInfo;
        UpdateInfo.pContext          = nullptr;
        UpdateInfo.NumBytes          = DataSize;
        UpdateInfo.pSrcData          = nullptr;
        UpdateInfo.WriteDataCallback = [](void* pDstData, Uint32 NumBytes, void* pUserData) {
            MeshBufferWriteData* Data = static_cast<MeshBufferWriteData*>(pUserData);
            VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
            if (Data == nullptr || Data->pSource == nullptr)
                return;

            RadientMeshSource::PackDestinations Destinations{Data->pSource->GetVertexBufferCount()};
            if (Data->WriteIndices)
                Destinations.Indices = RadientMeshSource::PackDestination{pDstData, NumBytes};
            else
                Destinations.VertexBuffers[Data->VertexBufferIndex] = RadientMeshSource::PackDestination{pDstData, NumBytes};

            const RADIENT_STATUS Status = Data->pSource->Pack(Destinations);
            VERIFY_EXPR(Status == RADIENT_STATUS_OK);
        };
        UpdateInfo.pWriteDataCallbackUserData = &WriteData;
        UpdateInfo.pCopyBufferData            = pCopyData.get();

        UpdateInfo.CopyBuffer = [](IDeviceContext* pContext,
                                   IBuffer*        pSrcBuffer,
                                   Uint32          SrcOffset,
                                   Uint32          NumBytes,
                                   void*           pUserData) {
            std::unique_ptr<MeshBufferCopyData> Data{static_cast<MeshBufferCopyData*>(pUserData)};

            bool CopyScheduled = false;
            if (pContext != nullptr && pSrcBuffer != nullptr)
            {
                if (Data->pIndexAllocation != nullptr)
                {
                    IBuffer* pDstBuffer = Data->pIndexAllocation->Update(Data->pDevice, pContext);
                    if (pDstBuffer != nullptr)
                    {
                        pContext->CopyBuffer(pSrcBuffer, SrcOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                             pDstBuffer, Data->pIndexAllocation->GetOffset(), NumBytes,
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        CopyScheduled = true;
                    }
                }
                else if (Data->pVertexAllocation != nullptr)
                {
                    IBuffer* pDstBuffer = Data->pVertexAllocation->Update(Data->VertexBufferIndex, Data->pDevice, pContext);
                    if (pDstBuffer != nullptr)
                    {
                        const Uint32 DstOffset = Data->pVertexAllocation->GetStartVertex() * Data->VertexStride;
                        pContext->CopyBuffer(pSrcBuffer, SrcOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                             pDstBuffer, DstOffset, NumBytes,
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        CopyScheduled = true;
                    }
                }
            }

            VERIFY_EXPR(Data->pMeshStorage != nullptr);
            if (Data->pMeshStorage != nullptr)
                UpdateMeshUploadProgress(*Data->pMeshStorage, CopyScheduled);
        };

        m_pUploadManager->ScheduleBufferUpdate(UpdateInfo);
        pCopyData.release();
    };

    ScheduleCopy(Source.GetIndexDataSize(), [](MeshBufferWriteData& WriteData) { WriteData.WriteIndices = true; }, [&Storage](MeshBufferCopyData& CopyData) { CopyData.pIndexAllocation = Storage.pIndexAllocation; });

    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (!Source.IsVertexBufferActive(BufferIndex))
            continue;

        const Uint32 DataSize = Source.GetVertexBufferDataSize(BufferIndex);
        ScheduleCopy(DataSize, [BufferIndex](MeshBufferWriteData& WriteData) { WriteData.VertexBufferIndex = BufferIndex; }, [&Storage, &Source, BufferIndex](MeshBufferCopyData& CopyData) {
                         CopyData.pVertexAllocation = Storage.pVertexAllocation;
                         CopyData.VertexBufferIndex = BufferIndex;
                         CopyData.VertexStride      = Source.GetVertexStride(BufferIndex); });
    }

    return RADIENT_STATUS_OK;
}

template <typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          const INTERFACE_ID& ImplID,
          RADIENT_ASSET_TYPE  AssetType,
          typename StorageType>
RadientAssetManagerImpl::AssetImpl<InterfaceType, InterfaceID, ImplID, AssetType, StorageType>::AssetImpl(IReferenceCounters* pRefCounters,
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

template <typename ImplType>
RefCntAutoPtr<ImplType> RadientAssetManagerImpl::CreateAsset(const char*                  Type,
                                                             const Char*                  Name,
                                                             typename ImplType::Storage&& Storage)
{
    return RefCntAutoPtr<ImplType>{MakeNewRCObj<ImplType>()(MakeURI(Type), Name, std::move(Storage))};
}

template <typename ImplType, typename InterfaceType>
RADIENT_STATUS RadientAssetManagerImpl::CreateAsset(const char*                  Type,
                                                    const Char*                  Name,
                                                    typename ImplType::Storage&& Storage,
                                                    InterfaceType**              ppAsset)
{
    RefCntAutoPtr<ImplType> pAsset = CreateAsset<ImplType>(Type, Name, std::move(Storage));
    *ppAsset                       = pAsset.Detach();
    return RADIENT_STATUS_OK;
}

template <typename ImplType, typename CreateAssetFuncType>
std::pair<RefCntAutoPtr<ImplType>, bool> RadientAssetManagerImpl::CacheAssetOrGetExisting(const std::string&    CacheKey,
                                                                                          CreateAssetFuncType&& CreateAssetFunc)
{
    std::unique_lock<std::shared_mutex> Lock{m_Mutex};

    const auto It = m_Assets.find(HashMapStringKey{CacheKey.c_str()});
    if (It != m_Assets.end())
    {
        RefCntAutoPtr<IRadientAsset> pExisting = It->second.Lock();
        if (pExisting != nullptr)
        {
            return {RefCntAutoPtr<ImplType>{pExisting.RawPtr<ImplType>()}, false};
        }
    }

    RefCntAutoPtr<ImplType> pAsset = CreateAssetFunc();
    if (!pAsset)
    {
        UNEXPECTED("Failed to create asset for cache key '", CacheKey, "'");
        return {};
    }

    if (It != m_Assets.end())
    {
        It->second = RefCntWeakPtr<IRadientAsset>{pAsset};
    }
    else
    {
        m_Assets.emplace(HashMapStringKey{CacheKey, true},
                         RefCntWeakPtr<IRadientAsset>{pAsset});
    }

    return {pAsset, true};
}

std::pair<RefCntAutoPtr<IRadientTextureAsset>, bool>
RadientAssetManagerImpl::CacheTextureAssetOrGetExisting(const std::string& CacheKey,
                                                        const std::string& SourceURI)
{
    auto [pTextureAsset, Created] = CacheAssetOrGetExisting<TextureAssetImpl>(
        CacheKey,
        [this, &SourceURI]() {
            TextureStorage TextureData;
            TextureData.SourceURI = SourceURI;
            TextureData.LoadStatus.store(RADIENT_STATUS_PENDING, std::memory_order_release);
            return CreateAsset<TextureAssetImpl>("texture",
                                                 SourceURI.empty() ? nullptr : SourceURI.c_str(),
                                                 std::move(TextureData));
        });

    RefCntAutoPtr<IRadientTextureAsset> pTextureInterface{pTextureAsset.RawPtr()};
    return {pTextureInterface, Created};
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterialAsset(const Char*             Name,
                                                            MaterialStorage&&       Storage,
                                                            IRadientMaterialAsset** ppMaterial)
{
    return CreateAsset<MaterialAssetImpl>("material",
                                          Name,
                                          std::move(Storage),
                                          ppMaterial);
}

RadientAssetManagerImpl::RadientAssetManagerImpl(IReferenceCounters* pRefCounters,
                                                 const CreateInfo&   CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Assets.Desc.Name != nullptr ? CreateInfo.Assets.Desc.Name : ""},
    m_Desc{CreateInfo.Assets.Desc},
    m_MaterialManager{*this},
    m_TextureManager{*this},
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
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (!ValidateMeshCreateInfo(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::unique_ptr<RadientMeshSource> pMeshSource  = std::make_unique<RadientMeshSource>(MeshCI);
    const RADIENT_STATUS               SourceStatus = pMeshSource->GetStatus();
    if (RADIENT_FAILED(SourceStatus))
        return SourceStatus;

    const bool CanUploadMesh =
        m_pDevice != nullptr &&
        m_pResourceManager != nullptr &&
        m_pUploadManager != nullptr;
    if (CanUploadMesh && m_pThreadPool == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    MeshStorage MeshData;
    MeshData.LoadStatus.store(CanUploadMesh ? RADIENT_STATUS_PENDING : RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);
    MeshData.GPUResourcesReady.store(false, std::memory_order_release);

    RefCntAutoPtr<MeshAssetImpl> pMeshAsset =
        CreateAsset<MeshAssetImpl>("mesh",
                                   MeshCI.Name,
                                   std::move(MeshData));
    VERIFY_EXPR(pMeshAsset != nullptr);
    pMeshAsset->QueryInterface(IID_RadientMeshAsset, ppMesh);
    if (!CanUploadMesh)
    {
        return RADIENT_STATUS_OK;
    }

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakSelf{this};
    RefCntAutoPtr<MeshAssetImpl>           pMeshForWorker{pMeshAsset};

    EnqueueAsyncWork(
        m_pThreadPool,
        [pWeakSelf, pMeshForWorker, pMeshSource = std::move(pMeshSource)](Uint32) mutable //
        {
            RefCntAutoPtr<RadientAssetManagerImpl> pSelf = pWeakSelf.Lock();
            if (pSelf == nullptr)
                return ASYNC_TASK_STATUS_CANCELLED;

            pSelf->LoadMeshFromSource(*pMeshForWorker, std::move(pMeshSource));
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return RADIENT_STATUS_OK;
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

    const std::string CacheKey = MakeGLTFCacheKey(LoadInfo.URI);
    auto [pModelAsset, ModelCreated] =
        CacheAssetOrGetExisting<SceneAssetImpl>(
            CacheKey,
            [this, &LoadInfo]() {
                GLTFModelStorage GLTFModelData;
                GLTFModelData.SourceURI = LoadInfo.URI;
                GLTFModelData.LoadStatus.store(RADIENT_STATUS_PENDING);
                return CreateAsset<SceneAssetImpl>("gltf", LoadInfo.URI, std::move(GLTFModelData));
            });
    VERIFY_EXPR(pModelAsset != nullptr);
    if (!ModelCreated)
    {
        const RADIENT_STATUS Status = pModelAsset->GetLoadStatus();
        pModelAsset->QueryInterface(IID_RadientSceneAsset, ppModel);
        return Status;
    }

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakSelf{this};
    RefCntAutoPtr<SceneAssetImpl>          pModelForWorker{pModelAsset};

    pModelAsset->QueryInterface(IID_RadientSceneAsset, ppModel);

    EnqueueAsyncWork(
        m_pThreadPool,
        [pWeakSelf, pModelForWorker](Uint32) mutable //
        {
            RefCntAutoPtr<RadientAssetManagerImpl> pSelf = pWeakSelf.Lock();
            if (pSelf == nullptr)
                return ASYNC_TASK_STATUS_CANCELLED;

            pSelf->LoadGLTFModel(*pModelForWorker);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pModelAsset->GetLoadStatus();
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
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    const SceneAssetImpl* pModelImpl = ClassPtrCast<const SceneAssetImpl>(pModel);
    if (pModelImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFModelStorage& GLTFModel  = pModelImpl->GetStorage();
    const RADIENT_STATUS    LoadStatus = GLTFModel.LoadStatus.load(std::memory_order_acquire);
    if (LoadStatus != RADIENT_STATUS_OK)
        return LoadStatus;

    if (GLTFModel.pModel == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    GLTFMeshStorage GLTFMeshData;
    GLTFMeshData.pModel = pModel;

    const RADIENT_STATUS Status = CreateGLTFDrawableMesh(*GLTFModel.pModel,
                                                         MeshIndex,
                                                         GLTFModel.VertexAttribFlags,
                                                         GLTFMeshData.DrawableMesh);
    if (RADIENT_FAILED(Status))
        return Status;

    return CreateAsset<MeshAssetImpl>("mesh",
                                      Name,
                                      MeshAssetStorage{std::move(GLTFMeshData)},
                                      ppMesh);
}

RadientDrawableMeshResolveResult RadientAssetManagerImpl::GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                                          bool               RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    const MeshAssetImpl* pMeshImpl = ClassPtrCast<const MeshAssetImpl>(pMesh);
    if (pMeshImpl == nullptr)
        return Result;

    const MeshAssetStorage& Storage = pMeshImpl->GetStorage();
    if (const MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
        return GetDrawableMesh(*pMeshStorage, RequireGPUResourcesReady);
    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return GetDrawableMesh(*pGLTFMeshStorage, RequireGPUResourcesReady);

    return Result;
}

RadientDrawableMeshResolveResult RadientAssetManagerImpl::GetDrawableMesh(const MeshStorage& Mesh,
                                                                          bool               RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    const RADIENT_STATUS LoadStatus = Mesh.LoadStatus.load(std::memory_order_acquire);
    if (LoadStatus != RADIENT_STATUS_OK)
    {
        Result.Status = LoadStatus;
        return Result;
    }

    if (RequireGPUResourcesReady &&
        !Mesh.GPUResourcesReady.load(std::memory_order_acquire))
    {
        Result.Status = RADIENT_STATUS_PENDING;
        return Result;
    }

    if (RequireGPUResourcesReady && Mesh.DrawableMesh.pVertexPool == nullptr)
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    Result.pMesh  = &Mesh.DrawableMesh;
    Result.Status = RADIENT_STATUS_OK;
    return Result;
}

RadientDrawableMeshResolveResult RadientAssetManagerImpl::GetDrawableMesh(const GLTFMeshStorage& Mesh,
                                                                          bool                   RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    if (Mesh.pModel == nullptr)
        return Result;

    const SceneAssetImpl* pModelImpl = Mesh.pModel.RawPtr<SceneAssetImpl>();
    if (pModelImpl == nullptr)
        return Result;

    const GLTFModelStorage& GLTFModel  = pModelImpl->GetStorage();
    const RADIENT_STATUS    LoadStatus = GLTFModel.LoadStatus.load(std::memory_order_acquire);
    if (LoadStatus != RADIENT_STATUS_OK)
    {
        Result.Status = LoadStatus;
        return Result;
    }

    if (RequireGPUResourcesReady &&
        !GLTFModel.GPUResourcesReady.load(std::memory_order_acquire))
    {
        Result.Status = RADIENT_STATUS_PENDING;
        return Result;
    }

    if (GLTFModel.pModel == nullptr)
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    if (RequireGPUResourcesReady && Mesh.DrawableMesh.pVertexPool == nullptr)
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    Result.pMesh  = &Mesh.DrawableMesh;
    Result.Status = RADIENT_STATUS_OK;
    return Result;
}

const GLTF::Material* RadientAssetManagerImpl::GetMaterial(IRadientMaterialAsset* pMaterial)
{
    return RadientMaterialAssetManager::GetMaterial(pMaterial);
}

const GLTF::Model* RadientAssetManagerImpl::GetGLTFModel(IRadientSceneAsset* pModel,
                                                         bool                RequireGPUResourcesReady)
{
    const SceneAssetImpl* pImpl = ClassPtrCast<const SceneAssetImpl>(pModel);
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

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFLoadStatus(IRadientSceneAsset* pModel)
{
    const SceneAssetImpl* pImpl = ClassPtrCast<const SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
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

    std::vector<GLTFModelToPrepare> ModelsToPrepare;

    ModelsToPrepare.reserve(m_PendingGPUResourceUpdates.Size());
    for (RefCntWeakPtr<IRadientSceneAsset> WeakAsset; m_PendingGPUResourceUpdates.Dequeue(WeakAsset);)
    {
        RefCntAutoPtr<IRadientSceneAsset> pSceneAsset = WeakAsset.Lock();
        if (pSceneAsset == nullptr)
            continue;

        SceneAssetImpl* pImpl = pSceneAsset.RawPtr<SceneAssetImpl>();
        VERIFY_EXPR(pImpl != nullptr);

        GLTFModelStorage& GLTFModel = pImpl->GetStorage();

        if (GLTFModel.GPUResourcesReady.load(std::memory_order_acquire) ||
            GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
            GLTFModel.pModel == nullptr)
        {
            GLTFModel.GPUUpdateQueued.store(false, std::memory_order_release);
            continue;
        }

        ModelsToPrepare.push_back({pSceneAsset, GLTFModel.pModel.get()});
    }

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    auto GetQueuedGLTFModel = [](const GLTFModelToPrepare& Model) -> GLTFModelStorage* //
    {
        SceneAssetImpl* pImpl = Model.pAsset.RawPtr<SceneAssetImpl>();
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
            if (GetQueuedGLTFModel(Model) != nullptr)
                m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<IRadientSceneAsset>{Model.pAsset});

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
        {
            RefCntAutoPtr<MeshAssetImpl> pMesh{pAsset, IID_MeshAssetImpl};
            if (!pMesh)
                return RADIENT_STATUS_INVALID_ARGUMENT;

            const MeshAssetStorage& Storage = pMesh->GetStorage();
            if (const MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
                return pMeshStorage->LoadStatus.load(std::memory_order_acquire);

            if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
                return pGLTFMeshStorage->pModel ? GetGLTFLoadStatus(pGLTFMeshStorage->pModel) : RADIENT_STATUS_INVALID_OPERATION;

            return RADIENT_STATUS_INVALID_OPERATION;
        }

        case RADIENT_ASSET_TYPE_SCENE:
            return SceneAssetImpl::GetLoadStatus(pAsset);

        case RADIENT_ASSET_TYPE_TEXTURE:
            return TextureAssetImpl::GetLoadStatus(pAsset);

        default:
            return RADIENT_STATUS_OK;
    }
}

std::string RadientAssetManagerImpl::MakeURI(const char* Type)
{
    const RadientHandle AssetID = m_NextAssetID.fetch_add(1, std::memory_order_relaxed);
    return std::string{"radient://session/"} + Type + "/" + std::to_string(AssetID);
}

void RadientAssetManagerImpl::LoadGLTFModel(SceneAssetImpl& Model)
{
    GLTFModelStorage& GLTFModel = Model.GetStorage();
    const std::string SourceURI = GLTFModel.SourceURI;

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

    GLTFModel.VertexAttribFlags = pModelData != nullptr ?
        GetVertexAttribFlags(*pModelData) :
        PBR_Renderer::PSO_FLAG_NONE;

    GLTFModel.pModel = std::move(pModelData);
    GLTFModel.GPUResourcesReady.store(false, std::memory_order_release);
    GLTFModel.GPUUpdateQueued.store(Status == RADIENT_STATUS_OK, std::memory_order_release);
    GLTFModel.LoadStatus.store(Status, std::memory_order_release);

    if (Status == RADIENT_STATUS_OK)
        m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<IRadientSceneAsset>{&Model});
}

void RadientAssetManagerImpl::LoadMeshFromSource(MeshAssetImpl&                     Mesh,
                                                 std::unique_ptr<RadientMeshSource> pSource)
{
    MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Mesh.GetStorage());
    VERIFY_EXPR(pMeshStorage != nullptr);
    if (pMeshStorage == nullptr)
        return;

    auto SetLoadStatus =
        [pMeshStorage](RADIENT_STATUS Status) //
    {
        pMeshStorage->GPUResourcesReady.store(false, std::memory_order_release);
        pMeshStorage->LoadStatus.store(Status, std::memory_order_release);
    };

    if (pSource == nullptr)
    {
        SetLoadStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return;
    }

    RADIENT_STATUS Status = pSource->SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size()));
    if (RADIENT_FAILED(Status))
    {
        SetLoadStatus(Status);
        return;
    }

    Status = InitializeMeshStorage(*pSource, *pMeshStorage);
    if (RADIENT_FAILED(Status))
    {
        SetLoadStatus(Status);
        return;
    }

    Status = ScheduleMeshGPUUpload(Mesh,
                                   *pSource,
                                   *pMeshStorage);
    if (RADIENT_FAILED(Status))
        SetLoadStatus(Status);
}

} // namespace Diligent
