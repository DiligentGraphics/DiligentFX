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

#include "Assets/RadientMeshAssetManager.hpp"

#include "Assets/RadientAssetImpl.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientDrawableMeshConverter.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshSource.hpp"
#include "Cast.hpp"
#include "DebugUtilities.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"
#include "BufferSuballocator.h"
#include "VertexPool.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MeshAssetImpl = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};

struct MeshStorage
{
    explicit MeshStorage(RADIENT_STATUS InitLoadStatus = RADIENT_STATUS_OK) :
        LoadStatus{InitLoadStatus}
    {
    }

    MeshStorage(MeshStorage&& Rhs) noexcept;

    MeshStorage& operator=(MeshStorage&& Rhs)  = delete;
    MeshStorage(const MeshStorage&)            = delete;
    MeshStorage& operator=(const MeshStorage&) = delete;

    RadientDrawableMesh DrawableMesh;

    RefCntAutoPtr<IBufferSuballocation>               pIndexAllocation;
    RefCntAutoPtr<IVertexPoolAllocation>              pVertexAllocation;
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> Materials;

    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    std::atomic_bool            GPUResourcesReady{false};
    std::atomic<Uint32>         PendingUploads{0};
};

struct GLTFMeshStorage
{
    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientDrawableMesh               DrawableMesh;
};

using MeshAssetStorage = std::variant<MeshStorage, GLTFMeshStorage>;

} // namespace

class MeshPayloadImpl final : public RadientAssetPayloadImpl<MeshAssetStorage, MeshPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MeshAssetStorage, MeshPayloadImpl>;
    using TBase::TBase;
};

namespace
{

using MeshAssetImpl =
    RadientAssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, IID_MeshAssetImpl, RADIENT_ASSET_TYPE_MESH, MeshPayloadImpl>;

struct MeshIndexBufferWriteData
{
    const RadientMeshSource* pSource = nullptr;
};

struct MeshVertexBufferWriteData
{
    const RadientMeshSource* pSource           = nullptr;
    Uint32                   VertexBufferIndex = 0;
};

struct MeshIndexBufferCopyData
{
    RefCntAutoPtr<MeshPayloadImpl> pMeshPayload;
    MeshStorage*                   pMeshStorage = nullptr;
    RefCntAutoPtr<IRenderDevice>   pDevice;

    RefCntAutoPtr<IBufferSuballocation> pIndexAllocation;
};

struct MeshVertexBufferCopyData
{
    RefCntAutoPtr<MeshPayloadImpl> pMeshPayload;
    MeshStorage*                   pMeshStorage = nullptr;
    RefCntAutoPtr<IRenderDevice>   pDevice;

    RefCntAutoPtr<IVertexPoolAllocation> pVertexAllocation;

    Uint32 VertexBufferIndex = 0;
    Uint32 VertexStride      = 0;
};

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

void UpdateMeshUploadProgress(MeshStorage& Storage,
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

RADIENT_STATUS InitializeMeshStorage(GLTF::ResourceManager*   pResourceManager,
                                     const RadientMeshSource& Source,
                                     MeshStorage&             Storage)
{
    if (pResourceManager == nullptr)
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
            pMaterial = RadientMaterialAssetManager::GetMaterial(pMaterialAsset);
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

    Storage.pIndexAllocation = pResourceManager->AllocateIndices(Source.GetIndexDataSize(),
                                                                 alignof(Uint32));
    if (Storage.pIndexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const Uint32                           VertexBufferCount = Source.GetVertexBufferCount();
    GLTF::ResourceManager::VertexLayoutKey LayoutKey;
    LayoutKey.Elements.reserve(VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        LayoutKey.Elements.emplace_back(Source.GetVertexStride(BufferIndex), BIND_VERTEX_BUFFER);

    Storage.pVertexAllocation = pResourceManager->AllocateVertices(LayoutKey, Source.GetVertexCount());
    if (Storage.pVertexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    Storage.DrawableMesh.pVertexPool        = Storage.pVertexAllocation->GetPool();
    Storage.DrawableMesh.FirstIndexLocation = Storage.pIndexAllocation->GetOffset() / sizeof(Uint32);
    Storage.DrawableMesh.BaseVertex         = Storage.pVertexAllocation->GetStartVertex();
    Storage.GPUResourcesReady.store(false, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

void WriteMeshIndexData(void* pDstData, Uint32 NumBytes, void* pUserData)
{
    MeshIndexBufferWriteData* Data = static_cast<MeshIndexBufferWriteData*>(pUserData);
    VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
    if (Data == nullptr || Data->pSource == nullptr)
        return;

    const RADIENT_STATUS Status =
        Data->pSource->PackIndexData(RadientMeshSource::PackDestination{pDstData, NumBytes});
    VERIFY_EXPR(Status == RADIENT_STATUS_OK);
}

void WriteMeshVertexData(void* pDstData, Uint32 NumBytes, void* pUserData)
{
    MeshVertexBufferWriteData* Data = static_cast<MeshVertexBufferWriteData*>(pUserData);
    VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
    if (Data == nullptr || Data->pSource == nullptr)
        return;

    const RADIENT_STATUS Status =
        Data->pSource->PackVertexData(Data->VertexBufferIndex,
                                      RadientMeshSource::PackDestination{pDstData, NumBytes});
    VERIFY_EXPR(Status == RADIENT_STATUS_OK);
}

void CopyMeshIndexBuffer(IDeviceContext* pContext,
                         IBuffer*        pSrcBuffer,
                         Uint32          SrcOffset,
                         Uint32          NumBytes,
                         void*           pUserData)
{
    std::unique_ptr<MeshIndexBufferCopyData> Data{static_cast<MeshIndexBufferCopyData*>(pUserData)};

    bool CopyScheduled = false;
    if (pContext != nullptr &&
        pSrcBuffer != nullptr &&
        Data->pIndexAllocation != nullptr)
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

    VERIFY_EXPR(Data->pMeshStorage != nullptr);
    if (Data->pMeshStorage != nullptr)
        UpdateMeshUploadProgress(*Data->pMeshStorage, CopyScheduled);
}

void CopyMeshVertexBuffer(IDeviceContext* pContext,
                          IBuffer*        pSrcBuffer,
                          Uint32          SrcOffset,
                          Uint32          NumBytes,
                          void*           pUserData)
{
    std::unique_ptr<MeshVertexBufferCopyData> Data{static_cast<MeshVertexBufferCopyData*>(pUserData)};

    bool CopyScheduled = false;
    if (pContext != nullptr &&
        pSrcBuffer != nullptr &&
        Data->pVertexAllocation != nullptr)
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

    VERIFY_EXPR(Data->pMeshStorage != nullptr);
    if (Data->pMeshStorage != nullptr)
        UpdateMeshUploadProgress(*Data->pMeshStorage, CopyScheduled);
}

void ScheduleMeshIndexUpload(IGPUUploadManager*       pUploadManager,
                             IRenderDevice*           pDevice,
                             MeshPayloadImpl&         MeshPayload,
                             const RadientMeshSource& Source,
                             MeshStorage&             Storage)
{
    std::unique_ptr<MeshIndexBufferCopyData> pCopyData{new MeshIndexBufferCopyData{}};
    pCopyData->pMeshPayload     = &MeshPayload;
    pCopyData->pMeshStorage     = &Storage;
    pCopyData->pDevice          = pDevice;
    pCopyData->pIndexAllocation = Storage.pIndexAllocation;

    MeshIndexBufferWriteData WriteData;
    WriteData.pSource = &Source;

    ScheduleBufferUpdateInfo UpdateInfo;
    UpdateInfo.pContext                   = nullptr;
    UpdateInfo.NumBytes                   = Source.GetIndexDataSize();
    UpdateInfo.pSrcData                   = nullptr;
    UpdateInfo.WriteDataCallback          = WriteMeshIndexData;
    UpdateInfo.pWriteDataCallbackUserData = &WriteData;
    UpdateInfo.pCopyBufferData            = pCopyData.get();
    UpdateInfo.CopyBuffer                 = CopyMeshIndexBuffer;

    pUploadManager->ScheduleBufferUpdate(UpdateInfo);
    pCopyData.release();
}

void ScheduleMeshVertexUpload(IGPUUploadManager*       pUploadManager,
                              IRenderDevice*           pDevice,
                              MeshPayloadImpl&         MeshPayload,
                              const RadientMeshSource& Source,
                              MeshStorage&             Storage,
                              Uint32                   VertexBufferIndex)
{
    std::unique_ptr<MeshVertexBufferCopyData> pCopyData{new MeshVertexBufferCopyData{}};
    pCopyData->pMeshPayload      = &MeshPayload;
    pCopyData->pMeshStorage      = &Storage;
    pCopyData->pDevice           = pDevice;
    pCopyData->pVertexAllocation = Storage.pVertexAllocation;
    pCopyData->VertexBufferIndex = VertexBufferIndex;
    pCopyData->VertexStride      = Source.GetVertexStride(VertexBufferIndex);

    MeshVertexBufferWriteData WriteData;
    WriteData.pSource           = &Source;
    WriteData.VertexBufferIndex = VertexBufferIndex;

    ScheduleBufferUpdateInfo UpdateInfo;
    UpdateInfo.pContext                   = nullptr;
    UpdateInfo.NumBytes                   = Source.GetVertexBufferDataSize(VertexBufferIndex);
    UpdateInfo.pSrcData                   = nullptr;
    UpdateInfo.WriteDataCallback          = WriteMeshVertexData;
    UpdateInfo.pWriteDataCallbackUserData = &WriteData;
    UpdateInfo.pCopyBufferData            = pCopyData.get();
    UpdateInfo.CopyBuffer                 = CopyMeshVertexBuffer;

    pUploadManager->ScheduleBufferUpdate(UpdateInfo);
    pCopyData.release();
}

RADIENT_STATUS ScheduleMeshGPUUpload(IRenderDevice*           pDevice,
                                     IGPUUploadManager*       pUploadManager,
                                     MeshPayloadImpl&         MeshPayload,
                                     const RadientMeshSource& Source,
                                     MeshStorage&             Storage)
{
    if (pDevice == nullptr ||
        pUploadManager == nullptr ||
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

    ScheduleMeshIndexUpload(pUploadManager, pDevice, MeshPayload, Source, Storage);

    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (!Source.IsVertexBufferActive(BufferIndex))
            continue;

        ScheduleMeshVertexUpload(pUploadManager, pDevice, MeshPayload, Source, Storage, BufferIndex);
    }

    return RADIENT_STATUS_OK;
}

void LoadMeshFromSource(MeshPayloadImpl&                   Mesh,
                        std::unique_ptr<RadientMeshSource> pSource,
                        IRenderDevice*                     pDevice,
                        GLTF::ResourceManager*             pResourceManager,
                        IGPUUploadManager*                 pUploadManager)
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

    Status = InitializeMeshStorage(pResourceManager, *pSource, *pMeshStorage);
    if (RADIENT_FAILED(Status))
    {
        SetLoadStatus(Status);
        return;
    }

    Status = ScheduleMeshGPUUpload(pDevice,
                                   pUploadManager,
                                   Mesh,
                                   *pSource,
                                   *pMeshStorage);
    if (RADIENT_FAILED(Status))
        SetLoadStatus(Status);
}

RefCntAutoPtr<MeshAssetImpl> CreateCachedMeshAsset(const char*                      CacheKey,
                                                   RefCntAutoPtr<MeshPayloadImpl>&& pPayload)
{
    return MeshAssetImpl::Create(MakeRadientAssetCacheURI("mesh", CacheKey), std::move(pPayload));
}

std::string MakeGLTFMeshCacheKey(const IRadientSceneAsset& Model,
                                 Uint32                    MeshIndex)
{
    const RadientAssetReference& ModelRef = Model.GetReference();
    if (ModelRef.URI == nullptr || ModelRef.URI[0] == '\0')
        return {};

    return std::string{"gltf-mesh:"} + ModelRef.URI + ":mesh:" + std::to_string(MeshIndex);
}

RadientDrawableMeshResolveResult ResolveDrawableMesh(const MeshStorage& Mesh,
                                                     bool               RequireGPUResourcesReady);
RadientDrawableMeshResolveResult ResolveDrawableMesh(const GLTFMeshStorage& Mesh,
                                                     bool                   RequireGPUResourcesReady);

} // namespace

MeshStorage::MeshStorage(MeshStorage&& Rhs) noexcept :
    DrawableMesh{std::move(Rhs.DrawableMesh)},
    pIndexAllocation{std::move(Rhs.pIndexAllocation)},
    pVertexAllocation{std::move(Rhs.pVertexAllocation)},
    Materials{std::move(Rhs.Materials)},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    PendingUploads{Rhs.PendingUploads.load(std::memory_order_relaxed)}
{
}

RadientMeshAssetManager::RadientMeshAssetManager(const CreateInfo& CI) noexcept :
    m_pThreadPool{CI.pThreadPool},
    m_pDevice{CI.pDevice},
    m_pResourceManager{CI.pResourceManager},
    m_pUploadManager{CI.pUploadManager}
{
}

RadientMeshAssetManager::~RadientMeshAssetManager() = default;

RADIENT_STATUS RadientMeshAssetManager::CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                   IRadientMeshAsset**          ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (!ValidateMeshCreateInfo(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pThreadPool == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const bool CanUploadMesh =
        m_pDevice != nullptr &&
        m_pResourceManager != nullptr &&
        m_pUploadManager != nullptr;

    RefCntAutoPtr<MeshAssetImpl> pMeshAsset =
        MeshAssetImpl::Create(MakeRadientAssetURI("mesh", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)));
    VERIFY_EXPR(pMeshAsset != nullptr);
    if (!pMeshAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    std::unique_ptr<RadientMeshSource> pMeshSource = std::make_unique<RadientMeshSource>(MeshCI);

    pMeshAsset->QueryInterface(IID_RadientMeshAsset, ppMesh);

    EnqueueAsyncWork(
        m_pThreadPool,
        [pMeshAsset,
         MeshCache = m_MeshCache.GetAccessor(),
         CanUploadMesh,
         pDevice          = m_pDevice,
         pResourceManager = m_pResourceManager,
         pUploadManager   = m_pUploadManager,
         pMeshSource      = std::move(pMeshSource)](Uint32) mutable //
        {
            const RADIENT_STATUS SourceStatus = pMeshSource->GetStatus();
            if (RADIENT_FAILED(SourceStatus))
            {
                pMeshAsset->Fail(SourceStatus);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            const std::string CacheKey = pMeshSource->MakeCacheKey();
            if (CacheKey.empty())
            {
                pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            auto [pMeshPayload, PayloadCreated] =
                MeshCache.GetOrCreate(
                    CacheKey.c_str(),
                    [CanUploadMesh]() {
                        return MeshPayloadImpl::Create(MeshAssetStorage{
                            MeshStorage{CanUploadMesh ? RADIENT_STATUS_PENDING : RADIENT_STATUS_INVALID_OPERATION},
                        });
                    });

            if (!pMeshPayload)
            {
                pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            MeshPayloadImpl* const pMeshPayloadRaw = pMeshPayload.RawPtr();
            pMeshAsset->Resolve(std::move(pMeshPayload));

            if (!PayloadCreated)
                return ASYNC_TASK_STATUS_COMPLETE;

            if (CanUploadMesh)
                LoadMeshFromSource(*pMeshPayloadRaw, std::move(pMeshSource), pDevice, pResourceManager, pUploadManager);

            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pMeshAsset->GetResolveStatus();
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshFromGLTFMesh(IRadientSceneAsset* pModel,
                                                               Uint32              MeshIndex,
                                                               const Char*         Name,
                                                               IRadientMeshAsset** ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    const RADIENT_STATUS LoadStatus = RadientAssetManagerImpl::GetGLTFLoadStatus(pModel);
    if (LoadStatus != RADIENT_STATUS_OK)
        return LoadStatus;

    const GLTF::Model* pModelData = RadientAssetManagerImpl::GetGLTFModel(pModel);
    if (pModelData == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const std::string CacheKey = MakeGLTFMeshCacheKey(*pModel, MeshIndex);
    if (CacheKey.empty())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RADIENT_STATUS CreateStatus = RADIENT_STATUS_INVALID_OPERATION;
    auto [pMeshPayload, PayloadCreated] =
        m_GLTFMeshCache.GetOrCreate(
            CacheKey.c_str(),
            [pModel, MeshIndex, pModelData, &CreateStatus]() {
                GLTFMeshStorage GLTFMeshData;
                GLTFMeshData.pModel = pModel;

                CreateStatus = CreateGLTFDrawableMesh(*pModelData,
                                                      MeshIndex,
                                                      GetVertexAttribFlags(*pModelData),
                                                      GLTFMeshData.DrawableMesh);
                if (RADIENT_FAILED(CreateStatus))
                    return RefCntAutoPtr<MeshPayloadImpl>{};

                return MeshPayloadImpl::Create(MeshAssetStorage{std::move(GLTFMeshData)});
            });

    (void)PayloadCreated;

    if (!pMeshPayload)
        return CreateStatus;

    RefCntAutoPtr<MeshAssetImpl> pMesh = CreateCachedMeshAsset(CacheKey.c_str(), std::move(pMeshPayload));
    if (!pMesh)
        return RADIENT_STATUS_INVALID_OPERATION;

    pMesh->QueryInterface(IID_RadientMeshAsset, ppMesh);
    return RADIENT_STATUS_OK;
}

RadientDrawableMeshResolveResult RadientMeshAssetManager::GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                                          bool               RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    RefCntAutoPtr<MeshAssetImpl> pMeshImpl = MeshAssetImpl::ResolveAsset(pMesh);
    if (!pMeshImpl)
        return Result;

    const MeshAssetStorage& Storage = pMeshImpl->GetStorage();
    if (const MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
        return ResolveDrawableMesh(*pMeshStorage, RequireGPUResourcesReady);
    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return ResolveDrawableMesh(*pGLTFMeshStorage, RequireGPUResourcesReady);

    return Result;
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientAsset* pMeshAsset)
{
    const MeshAssetImpl* pMesh = ClassPtrCast<const MeshAssetImpl>(pMeshAsset);
    if (!pMesh)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS ResolveStatus = pMesh->GetResolveStatus();
    if (ResolveStatus != RADIENT_STATUS_OK)
        return ResolveStatus;

    const MeshAssetStorage& Storage = pMesh->GetStorage();
    if (const MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
        return pMeshStorage->LoadStatus.load(std::memory_order_acquire);

    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return pGLTFMeshStorage->pModel ? RadientAssetManagerImpl::GetGLTFLoadStatus(pGLTFMeshStorage->pModel) : RADIENT_STATUS_INVALID_OPERATION;

    return RADIENT_STATUS_INVALID_OPERATION;
}

const MeshPayloadImpl* RadientMeshAssetManager::GetMeshPayload(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    return pMesh ? pMesh->GetPayload().RawPtr() : nullptr;
}

namespace
{

RadientDrawableMeshResolveResult ResolveDrawableMesh(const MeshStorage& Mesh,
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

RadientDrawableMeshResolveResult ResolveDrawableMesh(const GLTFMeshStorage& Mesh,
                                                     bool                   RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    if (Mesh.pModel == nullptr)
        return Result;

    const RADIENT_STATUS LoadStatus = RadientAssetManagerImpl::GetGLTFLoadStatus(Mesh.pModel);
    if (LoadStatus != RADIENT_STATUS_OK)
    {
        Result.Status = LoadStatus;
        return Result;
    }

    if (RadientAssetManagerImpl::GetGLTFModel(Mesh.pModel) == nullptr)
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    if (RequireGPUResourcesReady &&
        RadientAssetManagerImpl::GetGLTFModel(Mesh.pModel, true) == nullptr)
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

} // namespace

} // namespace Diligent
