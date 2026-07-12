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
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientCacheKeyBuilder.hpp"
#include "Assets/RadientDrawableMeshConverter.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "Assets/RadientMeshViewSource.hpp"
#include "RadientMeshViewCreateInfoSnapshot.hpp"
#include "Cast.hpp"
#include "DebugUtilities.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "ThreadPool.hpp"
#include "BufferSuballocator.h"
#include "VertexPool.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MeshAssetImpl         = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};
static constexpr INTERFACE_ID IID_RadientMeshIndexData  = {0xeb134756, 0x0bac, 0x4bb9, {0x85, 0x84, 0xd5, 0x5f, 0x83, 0x6e, 0x7e, 0x7f}};
static constexpr INTERFACE_ID IID_MeshIndexDataImpl     = {0xdb8786a7, 0xe63e, 0x4128, {0x92, 0xce, 0x22, 0x86, 0xa4, 0x76, 0x9d, 0x14}};
static constexpr INTERFACE_ID IID_RadientMeshVertexData = {0x33b53b79, 0x66b9, 0x44ae, {0x82, 0xd6, 0xc4, 0x7f, 0xe3, 0x6, 0x34, 0xa3}};
static constexpr INTERFACE_ID IID_MeshVertexDataImpl    = {0x59bbe7e, 0x96ed, 0x4213, {0xb6, 0x90, 0xa4, 0x28, 0x60, 0xb0, 0x34, 0xa7}};

} // namespace

class MeshDataStatusStorage
{
public:
    MeshDataStatusStorage(RADIENT_STATUS InitLoadStatus,
                          std::string    CacheKey) :
        CacheKey{std::move(CacheKey)},
        LoadStatus{InitLoadStatus},
        GPUResourceStatus{InitLoadStatus}
    {
    }

    void SetStatus(RADIENT_STATUS Status)
    {
        SetGPUResourceStatus(Status);
        SetLoadStatus(Status);
    }

    void SetLoadStatus(RADIENT_STATUS Status)
    {
        LoadStatus.store(Status, std::memory_order_release);
    }

    void SetGPUResourceStatus(RADIENT_STATUS Status)
    {
        GPUResourcesReady.store(Status == RADIENT_STATUS_OK, std::memory_order_release);
        GPUResourceStatus.store(Status, std::memory_order_release);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        return LoadStatus.load(std::memory_order_acquire);
    }

    RADIENT_STATUS GetGPUResourceStatus() const noexcept
    {
        const RADIENT_STATUS Status = GetLoadStatus();
        return Status == RADIENT_STATUS_OK ? GPUResourceStatus.load(std::memory_order_acquire) : Status;
    }

    // clang-format off
    MeshDataStatusStorage           (MeshDataStatusStorage&& Rhs)  = delete;
    MeshDataStatusStorage& operator=(MeshDataStatusStorage&& Rhs)  = delete;
    MeshDataStatusStorage           (const MeshDataStatusStorage&) = delete;
    MeshDataStatusStorage& operator=(const MeshDataStatusStorage&) = delete;
    // clang-format on

    const std::string CacheKey;

    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    std::atomic<RADIENT_STATUS> GPUResourceStatus{RADIENT_STATUS_OK};
    std::atomic_bool            GPUResourcesReady{false};
    std::atomic<Uint32>         PendingUploads{0};
};

class MeshIndexDataStorage : public MeshDataStatusStorage
{
public:
    MeshIndexDataStorage(RADIENT_STATUS InitLoadStatus,
                         std::string    CacheKey,
                         Uint32         IndexCount) :
        MeshDataStatusStorage{InitLoadStatus, std::move(CacheKey)},
        IndexCount{IndexCount}
    {
    }

    RefCntAutoPtr<IBufferSuballocation> pIndexAllocation;

    const Uint32 IndexCount = 0;
};

class MeshVertexDataStorage : public MeshDataStatusStorage
{
public:
    MeshVertexDataStorage(RADIENT_STATUS          InitLoadStatus,
                          std::string             CacheKey,
                          Uint32                  VertexCount,
                          PBR_Renderer::PSO_FLAGS VertexAttribFlags) :
        MeshDataStatusStorage{InitLoadStatus, std::move(CacheKey)},
        VertexCount{VertexCount},
        VertexAttribFlags{VertexAttribFlags}
    {
    }

    RefCntAutoPtr<IVertexPoolAllocation> pVertexAllocation;

    const Uint32                  VertexCount       = 0;
    const PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
};

class MeshIndexDataPayloadImpl final : public RadientAssetPayloadImpl<MeshIndexDataStorage, MeshIndexDataPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MeshIndexDataStorage, MeshIndexDataPayloadImpl>;
    using TBase::TBase;
};

class MeshVertexDataPayloadImpl final : public RadientAssetPayloadImpl<MeshVertexDataStorage, MeshVertexDataPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MeshVertexDataStorage, MeshVertexDataPayloadImpl>;
    using TBase::TBase;
};

namespace
{

using MeshIndexDataAssetBase =
    RadientAssetImpl<IRadientMeshIndexData, IID_RadientMeshIndexData, IID_MeshIndexDataImpl, RADIENT_ASSET_TYPE_MESH, MeshIndexDataPayloadImpl>;

class MeshIndexDataAssetImpl final : public MeshIndexDataAssetBase
{
public:
    using TBase = MeshIndexDataAssetBase;
    using TBase::TBase;

    static RefCntAutoPtr<MeshIndexDataAssetImpl> Create(std::string AssetURI)
    {
        return RefCntAutoPtr<MeshIndexDataAssetImpl>{
            MakeNewRCObj<MeshIndexDataAssetImpl>()(std::move(AssetURI))};
    }

    void SetLoadTask(IAsyncTask* pTask)
    {
        m_pLoadTask = pTask;
    }

    RefCntAutoPtr<IAsyncTask> LockLoadTask() const
    {
        return m_pLoadTask.Lock();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_MeshIndexDataImpl, TBase)

private:
    RefCntWeakPtr<IAsyncTask> m_pLoadTask;
};

using MeshVertexDataAssetBase =
    RadientAssetImpl<IRadientMeshVertexData, IID_RadientMeshVertexData, IID_MeshVertexDataImpl, RADIENT_ASSET_TYPE_MESH, MeshVertexDataPayloadImpl>;

class MeshVertexDataAssetImpl final : public MeshVertexDataAssetBase
{
public:
    using TBase = MeshVertexDataAssetBase;
    using TBase::TBase;

    static RefCntAutoPtr<MeshVertexDataAssetImpl> Create(std::string AssetURI)
    {
        return RefCntAutoPtr<MeshVertexDataAssetImpl>{
            MakeNewRCObj<MeshVertexDataAssetImpl>()(std::move(AssetURI))};
    }

    void SetLoadTask(IAsyncTask* pTask)
    {
        m_pLoadTask = pTask;
    }

    RefCntAutoPtr<IAsyncTask> LockLoadTask() const
    {
        return m_pLoadTask.Lock();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_MeshVertexDataImpl, TBase)

private:
    RefCntWeakPtr<IAsyncTask> m_pLoadTask;
};

struct MeshGeometryStorage
{
    RefCntAutoPtr<MeshIndexDataAssetImpl>    pIndexDataAsset;
    RefCntAutoPtr<MeshIndexDataPayloadImpl>  pIndexDataPayload;
    RefCntAutoPtr<MeshVertexDataAssetImpl>   pVertexDataAsset;
    RefCntAutoPtr<MeshVertexDataPayloadImpl> pVertexDataPayload;
};

struct MeshStorage
{
    MeshStorage(std::vector<MeshGeometryStorage> Geometries,
                const RadientMeshViewSource&     View);

    // clang-format off
    MeshStorage           (MeshStorage&& Rhs)  = delete;
    MeshStorage& operator=(MeshStorage&& Rhs)  = delete;
    MeshStorage           (const MeshStorage&) = delete;
    MeshStorage& operator=(const MeshStorage&) = delete;
    // clang-format on

    RadientDrawableMesh DrawableMesh;

    std::vector<MeshGeometryStorage>                  Geometries;
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> Materials;

    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    std::atomic<RADIENT_STATUS> MaterialStatus{RADIENT_STATUS_OK};
};

} // namespace

class MeshPayloadImpl final : public RadientAssetPayloadImpl<MeshStorage, MeshPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MeshStorage, MeshPayloadImpl>;
    using TBase::TBase;
};

namespace
{

using MeshAssetImpl =
    RadientAssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, IID_MeshAssetImpl, RADIENT_ASSET_TYPE_MESH, MeshPayloadImpl>;

struct MeshIndexBufferWriteData
{
    const RadientMeshIndexSource* pSource = nullptr;
};

struct MeshVertexBufferWriteData
{
    const RadientMeshVertexSource* pSource           = nullptr;
    Uint32                         VertexBufferIndex = 0;
};

struct MeshIndexBufferCopyData
{
    RefCntAutoPtr<MeshIndexDataPayloadImpl> pIndexDataPayload;
    RefCntAutoPtr<IRenderDevice>            pDevice;
};

struct MeshVertexBufferCopyData
{
    RefCntAutoPtr<MeshVertexDataPayloadImpl> pVertexDataPayload;
    RefCntAutoPtr<IRenderDevice>             pDevice;

    Uint32 VertexBufferIndex = 0;
    Uint32 VertexStride      = 0;
};

void UpdateMeshUploadProgress(MeshDataStatusStorage& Data,
                              bool                   CopyScheduled)
{
    if (!CopyScheduled)
        Data.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);

    const Uint32 PrevPendingUploads = Data.PendingUploads.fetch_sub(1, std::memory_order_acq_rel);
    VERIFY_EXPR(PrevPendingUploads > 0);
    if (PrevPendingUploads == 1)
    {
        const RADIENT_STATUS GPUStatus = Data.GPUResourceStatus.load(std::memory_order_acquire);
        Data.SetGPUResourceStatus(GPUStatus == RADIENT_STATUS_PENDING ? RADIENT_STATUS_OK : GPUStatus);
    }
}

MeshStorage::MeshStorage(std::vector<MeshGeometryStorage> GeometryData,
                         const RadientMeshViewSource&     View)
{
    Geometries = std::move(GeometryData);

    if (Geometries.empty())
    {
        LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
        return;
    }

    DrawableMesh.Geometries.reserve(Geometries.size());
    for (const MeshGeometryStorage& Geometry : Geometries)
    {
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
        {
            LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
            return;
        }

        const MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
        const MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();
        if (IndexData.IndexCount == 0 ||
            VertexData.VertexCount == 0)
        {
            LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
            return;
        }

        DrawableMesh.Geometries.push_back(RadientDrawableMeshGeometry{
            nullptr,
            VertexData.VertexAttribFlags,
            0,
            0});
    }

    const Uint32 PrimitiveCount = View.GetPrimitiveCount();
    DrawableMesh.Primitives.reserve(PrimitiveCount);
    Materials.reserve(PrimitiveCount);

    RADIENT_STATUS MaterialStatusValue = RADIENT_STATUS_OK;

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI   = View.GetPrimitive(PrimitiveIndex);
        const Uint32                          GeometryIndex = View.GetGeometryIndex(PrimitiveIndex);
        if (GeometryIndex >= Geometries.size())
        {
            LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
            return;
        }

        IRadientMaterialAsset* pMaterialAsset = View.GetMaterial(PrimitiveIndex);
        const GLTF::Material*  pMaterial      = nullptr;

        if (pMaterialAsset != nullptr)
        {
            const RADIENT_STATUS MaterialLoadStatus = RadientMaterialAssetManager::GetLoadStatus(pMaterialAsset);
            if (RADIENT_FAILED(MaterialLoadStatus))
            {
                LoadStatus.store(MaterialLoadStatus, std::memory_order_release);
                return;
            }

            // Pending materials are resolved lazily by GetDrawableMesh().
            if (MaterialLoadStatus == RADIENT_STATUS_OK)
                pMaterial = RadientMaterialAssetManager::GetMaterial(pMaterialAsset);
            else if (MaterialLoadStatus == RADIENT_STATUS_PENDING)
                MaterialStatusValue = RADIENT_STATUS_PENDING;
        }

        Materials.emplace_back(pMaterialAsset);
        DrawableMesh.Primitives.push_back(RadientDrawableMeshPrimitive{
            pMaterial,
            GeometryIndex,
            true,
            PrimitiveCI.FirstIndex,
            PrimitiveCI.IndexCount});
    }

    MaterialStatus.store(MaterialStatusValue, std::memory_order_release);
}

RADIENT_STATUS InitializeMeshIndexData(GLTF::ResourceManager*        pResourceManager,
                                       const RadientMeshIndexSource& IndexSource,
                                       MeshIndexDataStorage&         IndexData)
{
    if (pResourceManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (IndexSource.GetIndexDataSize() == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    IndexData.pIndexAllocation = pResourceManager->AllocateIndices(IndexSource.GetIndexDataSize(),
                                                                   alignof(Uint32));
    if (IndexData.pIndexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    IndexData.GPUResourcesReady.store(false, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS InitializeMeshVertexData(GLTF::ResourceManager*         pResourceManager,
                                        const RadientMeshVertexSource& VertexSource,
                                        MeshVertexDataStorage&         VertexData)
{
    if (pResourceManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (VertexSource.GetVertexCount() == 0 ||
        VertexSource.GetVertexBufferCount() == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32                           VertexBufferCount = VertexSource.GetVertexBufferCount();
    GLTF::ResourceManager::VertexLayoutKey LayoutKey;
    LayoutKey.Elements.reserve(VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        LayoutKey.Elements.emplace_back(VertexSource.GetVertexStride(BufferIndex), BIND_VERTEX_BUFFER);

    VertexData.pVertexAllocation = pResourceManager->AllocateVertices(LayoutKey, VertexSource.GetVertexCount());
    if (VertexData.pVertexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    VertexData.GPUResourcesReady.store(false, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

void WriteMeshIndexData(void* pDstData, Uint32 NumBytes, void* pUserData)
{
    MeshIndexBufferWriteData* Data = static_cast<MeshIndexBufferWriteData*>(pUserData);
    VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
    if (Data == nullptr || Data->pSource == nullptr)
        return;

    const RADIENT_STATUS Status =
        Data->pSource->PackIndexData(RadientMeshIndexSource::PackDestination{pDstData, NumBytes});
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
                                      RadientMeshVertexSource::PackDestination{pDstData, NumBytes});
    VERIFY_EXPR(Status == RADIENT_STATUS_OK);
}

void CopyMeshIndexBuffer(IDeviceContext* pContext,
                         IBuffer*        pSrcBuffer,
                         Uint32          SrcOffset,
                         Uint32          NumBytes,
                         void*           pUserData)
{
    std::unique_ptr<MeshIndexBufferCopyData> Data{static_cast<MeshIndexBufferCopyData*>(pUserData)};

    IBufferSuballocation* pIndexAllocation = nullptr;
    if (Data->pIndexDataPayload != nullptr)
        pIndexAllocation = Data->pIndexDataPayload->GetStorage().pIndexAllocation;

    bool CopyScheduled = false;
    if (pContext != nullptr &&
        pSrcBuffer != nullptr &&
        pIndexAllocation != nullptr)
    {
        IBuffer* pDstBuffer = pIndexAllocation->Update(Data->pDevice, pContext);
        if (pDstBuffer != nullptr)
        {
            pContext->CopyBuffer(pSrcBuffer, SrcOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 pDstBuffer, pIndexAllocation->GetOffset(), NumBytes,
                                 RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            CopyScheduled = true;
        }
    }

    VERIFY_EXPR(Data->pIndexDataPayload != nullptr);
    if (Data->pIndexDataPayload != nullptr)
        UpdateMeshUploadProgress(Data->pIndexDataPayload->GetStorage(), CopyScheduled);
}

void CopyMeshVertexBuffer(IDeviceContext* pContext,
                          IBuffer*        pSrcBuffer,
                          Uint32          SrcOffset,
                          Uint32          NumBytes,
                          void*           pUserData)
{
    std::unique_ptr<MeshVertexBufferCopyData> Data{static_cast<MeshVertexBufferCopyData*>(pUserData)};

    IVertexPoolAllocation* pVertexAllocation = nullptr;
    if (Data->pVertexDataPayload != nullptr)
        pVertexAllocation = Data->pVertexDataPayload->GetStorage().pVertexAllocation;

    bool CopyScheduled = false;
    if (pContext != nullptr &&
        pSrcBuffer != nullptr &&
        pVertexAllocation != nullptr)
    {
        IBuffer* pDstBuffer = pVertexAllocation->Update(Data->VertexBufferIndex, Data->pDevice, pContext);
        if (pDstBuffer != nullptr)
        {
            const Uint32 DstOffset = pVertexAllocation->GetStartVertex() * Data->VertexStride;
            pContext->CopyBuffer(pSrcBuffer, SrcOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 pDstBuffer, DstOffset, NumBytes,
                                 RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            CopyScheduled = true;
        }
    }

    VERIFY_EXPR(Data->pVertexDataPayload != nullptr);
    if (Data->pVertexDataPayload != nullptr)
        UpdateMeshUploadProgress(Data->pVertexDataPayload->GetStorage(), CopyScheduled);
}

void ScheduleMeshIndexUpload(IGPUUploadManager*            pUploadManager,
                             IRenderDevice*                pDevice,
                             const RadientMeshIndexSource& Source,
                             MeshIndexDataPayloadImpl*     pIndexDataPayload)
{
    std::unique_ptr<MeshIndexBufferCopyData> pCopyData{new MeshIndexBufferCopyData{}};
    pCopyData->pIndexDataPayload = pIndexDataPayload;
    pCopyData->pDevice           = pDevice;

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

void ScheduleMeshVertexUpload(IGPUUploadManager*             pUploadManager,
                              IRenderDevice*                 pDevice,
                              const RadientMeshVertexSource& Source,
                              MeshVertexDataPayloadImpl*     pVertexDataPayload,
                              Uint32                         VertexBufferIndex)
{
    std::unique_ptr<MeshVertexBufferCopyData> pCopyData{new MeshVertexBufferCopyData{}};
    pCopyData->pVertexDataPayload = pVertexDataPayload;
    pCopyData->pDevice            = pDevice;
    pCopyData->VertexBufferIndex  = VertexBufferIndex;
    pCopyData->VertexStride       = Source.GetVertexStride(VertexBufferIndex);

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

void CreateMeshIndexDataFromSource(const RadientMeshIndexSource& IndexSource,
                                   MeshIndexDataPayloadImpl&     IndexDataPayload,
                                   IRenderDevice*                pDevice,
                                   GLTF::ResourceManager*        pResourceManager,
                                   IGPUUploadManager*            pUploadManager)
{
    MeshIndexDataStorage& IndexData = IndexDataPayload.GetStorage();

    if (IndexSource.GetIndexDataSize() == 0)
    {
        IndexData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return;
    }

    if (pDevice == nullptr)
    {
        IndexData.SetGPUResourceStatus(RADIENT_STATUS_NO_GPU_DATA);
    }
    else if (pResourceManager == nullptr || pUploadManager == nullptr)
    {
        IndexData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
    }
    else
    {
        RADIENT_STATUS Status = InitializeMeshIndexData(pResourceManager, IndexSource, IndexData);
        if (RADIENT_FAILED(Status))
        {
            IndexData.SetGPUResourceStatus(Status);
        }
        else if (IndexData.pIndexAllocation == nullptr ||
                 IndexSource.GetIndexCount() == 0)
        {
            IndexData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
        }
        else
        {
            IndexData.PendingUploads.store(1, std::memory_order_release);
            IndexData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);
            ScheduleMeshIndexUpload(pUploadManager, pDevice, IndexSource, &IndexDataPayload);
        }
    }

    // LoadStatus publishes GPU resource status and pIndexAllocation to readers.
    // Keep this as the final release store after allocation/scheduling state is settled.
    IndexData.SetLoadStatus(RADIENT_STATUS_OK);
}

void CreateMeshVertexDataFromSource(const RadientMeshVertexSource& VertexSource,
                                    MeshVertexDataPayloadImpl&     VertexDataPayload,
                                    IRenderDevice*                 pDevice,
                                    GLTF::ResourceManager*         pResourceManager,
                                    IGPUUploadManager*             pUploadManager)
{
    MeshVertexDataStorage& VertexData = VertexDataPayload.GetStorage();

    if (VertexSource.GetVertexCount() == 0 ||
        VertexSource.GetVertexBufferCount() == 0)
    {
        VertexData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return;
    }

    if (pDevice == nullptr)
    {
        VertexData.SetGPUResourceStatus(RADIENT_STATUS_NO_GPU_DATA);
    }
    else if (pResourceManager == nullptr || pUploadManager == nullptr)
    {
        VertexData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
    }
    else
    {
        const Uint32 VertexBufferCount = VertexSource.GetVertexBufferCount();
        Uint32       UploadCount       = 0;
        for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        {
            if (!VertexSource.IsVertexBufferActive(BufferIndex))
                continue;

            if (VertexSource.GetVertexBufferDataSize(BufferIndex) == 0)
            {
                VertexData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
                return;
            }

            ++UploadCount;
        }

        if (UploadCount == 0)
        {
            VertexData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
            return;
        }

        RADIENT_STATUS Status = InitializeMeshVertexData(pResourceManager, VertexSource, VertexData);
        if (RADIENT_FAILED(Status))
        {
            VertexData.SetGPUResourceStatus(Status);
        }
        else if (VertexData.pVertexAllocation == nullptr)
        {
            VertexData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
        }
        else
        {
            VertexData.PendingUploads.store(UploadCount, std::memory_order_release);
            VertexData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);

            for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
            {
                if (!VertexSource.IsVertexBufferActive(BufferIndex))
                    continue;

                ScheduleMeshVertexUpload(pUploadManager, pDevice, VertexSource, &VertexDataPayload, BufferIndex);
            }
        }
    }

    // LoadStatus publishes GPU resource status and pVertexAllocation to readers.
    // Keep this as the final release store after allocation/scheduling state is settled.
    VertexData.SetLoadStatus(RADIENT_STATUS_OK);
}

std::string MakeMeshGeometryCacheKey(const MeshVertexDataStorage& VertexData,
                                     const MeshIndexDataStorage&  IndexData)
{
    if (VertexData.CacheKey.empty() || IndexData.CacheKey.empty())
        return {};

    RadientCacheKeyBuilder Builder{"mesh-geometry", 1};
    Builder.AddString("vertex", VertexData.CacheKey)
        .AddString("index", IndexData.CacheKey);
    return Builder.GetKey();
}

RadientDrawableMeshResolveResult ResolveDrawableMesh(MeshStorage& Mesh,
                                                     bool         RequireGPUResourcesReady);

RADIENT_STATUS GetMeshGeometryLoadStatus(const MeshStorage& Mesh);
RADIENT_STATUS GetMeshGeometryGPUResourceStatus(const MeshStorage& Mesh);
RADIENT_STATUS GetMeshMaterialStatus(MeshStorage& Mesh);
RADIENT_STATUS GetMeshMaterialGPUResourceStatus(MeshStorage& Mesh);
RADIENT_STATUS ResolveMeshMaterialDependencies(MeshStorage& Mesh);

} // namespace

RadientMeshAssetManager::RadientMeshAssetManager(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_WeakResourceManager{CI.pResourceManager},
    m_WeakUploadManager{CI.pUploadManager}
{
}

RadientMeshAssetManager::~RadientMeshAssetManager() = default;

RadientMeshAssetManagerSharedPtr RadientMeshAssetManager::Create(const CreateInfo& CI)
{
    return RadientMeshAssetManagerSharedPtr{new RadientMeshAssetManager{CI}};
}

RADIENT_STATUS RadientMeshAssetManager::CreateMesh(IThreadPool&                 ThreadPool,
                                                   const RadientMeshCreateInfo& MeshCI,
                                                   IRadientMeshAsset**          ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (!ValidateMeshCreateInfo(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientMeshViewCreateInfo ViewCI{
        MeshCI.pPrimitives,
        MeshCI.PrimitiveCount};

    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    RADIENT_STATUS                        Status = CreateMeshVertexData(ThreadPool,
                                                                        std::make_unique<RadientMeshVertexSource>(MeshCI),
                                                                        pVertexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pVertexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;
    Status = CreateMeshIndexData(ThreadPool,
                                 std::make_unique<RadientMeshIndexSource>(MeshCI),
                                 pIndexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pIndexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    const RadientMeshGeometryData GeometryData{
        pVertexData.RawPtr(),
        pIndexData.RawPtr()};
    return CreateMeshView(ThreadPool, &GeometryData, 1, ViewCI, ppMesh);
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshIndexData(IThreadPool&                            ThreadPool,
                                                            std::unique_ptr<RadientMeshIndexSource> pIndexSource,
                                                            IRadientMeshIndexData**                 ppIndexData)
{
    if (ppIndexData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppIndexData == nullptr, "Output mesh index data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppIndexData = nullptr;

    if (pIndexSource == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<MeshIndexDataAssetImpl> pIndexDataAsset =
        MeshIndexDataAssetImpl::Create(MakeRadientAssetURI("mesh-index-data", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)));
    if (pIndexDataAsset == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [pSelf        = shared_from_this(),
             pIndexSource = std::move(pIndexSource),
             pIndexDataAsset](Uint32) mutable //
            {
                const RADIENT_STATUS IndexStatus = pIndexSource->GetStatus();
                if (RADIENT_FAILED(IndexStatus))
                {
                    pIndexDataAsset->Fail(IndexStatus);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                if (pIndexSource->GetIndexDataSize() == 0)
                {
                    pIndexDataAsset->Fail(RADIENT_STATUS_INVALID_ARGUMENT);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                std::string IndexCacheKey = pIndexSource->MakeCacheKey();
                if (IndexCacheKey.empty())
                {
                    pIndexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                const Uint32 IndexCount = pIndexSource->GetIndexCount();

                auto [pIndexDataPayload, IndexDataCreated] =
                    pSelf->m_MeshIndexDataCache.GetOrCreate(
                        IndexCacheKey.c_str(),
                        [IndexCacheKey,
                         IndexCount]() mutable {
                            return MeshIndexDataPayloadImpl::Create(RADIENT_STATUS_PENDING,
                                                                    std::move(IndexCacheKey),
                                                                    IndexCount);
                        });

                if (pIndexDataPayload == nullptr)
                {
                    pIndexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                if (!pIndexDataAsset->SetPayload(RefCntAutoPtr<MeshIndexDataPayloadImpl>{pIndexDataPayload}))
                    return ASYNC_TASK_STATUS_COMPLETE;

                if (IndexDataCreated)
                {
                    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = pSelf->m_WeakResourceManager.Lock();
                    RefCntAutoPtr<IGPUUploadManager>     pUploadManager   = pSelf->m_WeakUploadManager.Lock();

                    CreateMeshIndexDataFromSource(*pIndexSource,
                                                  *pIndexDataPayload,
                                                  pSelf->m_pDevice,
                                                  pResourceManager,
                                                  pUploadManager);
                }

                return ASYNC_TASK_STATUS_COMPLETE;
            });

    pIndexDataAsset->SetLoadTask(pLoadTask);
    const bool TaskEnqueued = ThreadPool.EnqueueTask(pLoadTask);
    if (!TaskEnqueued)
        pIndexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);

    *ppIndexData = pIndexDataAsset.Detach();
    return TaskEnqueued ? RADIENT_STATUS_PENDING : RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshVertexData(IThreadPool&                             ThreadPool,
                                                             std::unique_ptr<RadientMeshVertexSource> pVertexSource,
                                                             IRadientMeshVertexData**                 ppVertexData)
{
    if (ppVertexData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppVertexData == nullptr, "Output mesh vertex data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppVertexData = nullptr;

    if (pVertexSource == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<MeshVertexDataAssetImpl> pVertexDataAsset =
        MeshVertexDataAssetImpl::Create(MakeRadientAssetURI("mesh-vertex-data", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)));
    if (pVertexDataAsset == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [pSelf         = shared_from_this(),
             pVertexSource = std::move(pVertexSource),
             pVertexDataAsset](Uint32) mutable //
            {
                const RADIENT_STATUS VertexStatus = pVertexSource->GetStatus();
                if (RADIENT_FAILED(VertexStatus))
                {
                    pVertexDataAsset->Fail(VertexStatus);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                if (!pVertexSource->HasVertexAttributes())
                {
                    const RADIENT_STATUS Status =
                        pVertexSource->SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                                           static_cast<Uint32>(GLTF::DefaultVertexAttributes.size()));
                    if (RADIENT_FAILED(Status))
                    {
                        pVertexDataAsset->Fail(Status);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }
                }

                if (pVertexSource->GetVertexCount() == 0 ||
                    pVertexSource->GetVertexBufferCount() == 0)
                {
                    pVertexDataAsset->Fail(RADIENT_STATUS_INVALID_ARGUMENT);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                std::string VertexCacheKey = pVertexSource->MakeCacheKey();
                if (VertexCacheKey.empty())
                {
                    pVertexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                const Uint32                  VertexCount       = pVertexSource->GetVertexCount();
                const PBR_Renderer::PSO_FLAGS VertexAttribFlags = pVertexSource->GetVertexAttribFlags();

                auto [pVertexDataPayload, VertexDataCreated] =
                    pSelf->m_MeshVertexDataCache.GetOrCreate(
                        VertexCacheKey.c_str(),
                        [VertexCacheKey,
                         VertexCount,
                         VertexAttribFlags]() mutable {
                            return MeshVertexDataPayloadImpl::Create(RADIENT_STATUS_PENDING,
                                                                     std::move(VertexCacheKey),
                                                                     VertexCount,
                                                                     VertexAttribFlags);
                        });

                if (pVertexDataPayload == nullptr)
                {
                    pVertexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                if (!pVertexDataAsset->SetPayload(RefCntAutoPtr<MeshVertexDataPayloadImpl>{pVertexDataPayload}))
                    return ASYNC_TASK_STATUS_COMPLETE;

                if (VertexDataCreated)
                {
                    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = pSelf->m_WeakResourceManager.Lock();
                    RefCntAutoPtr<IGPUUploadManager>     pUploadManager   = pSelf->m_WeakUploadManager.Lock();

                    CreateMeshVertexDataFromSource(*pVertexSource,
                                                   *pVertexDataPayload,
                                                   pSelf->m_pDevice,
                                                   pResourceManager,
                                                   pUploadManager);
                }

                return ASYNC_TASK_STATUS_COMPLETE;
            });

    pVertexDataAsset->SetLoadTask(pLoadTask);
    const bool TaskEnqueued = ThreadPool.EnqueueTask(pLoadTask);
    if (!TaskEnqueued)
        pVertexDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);

    *ppVertexData = pVertexDataAsset.Detach();
    return TaskEnqueued ? RADIENT_STATUS_PENDING : RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshView(IThreadPool&                     ThreadPool,
                                                       const RadientMeshGeometryData*   pGeometryData,
                                                       Uint32                           GeometryCount,
                                                       const RadientMeshViewCreateInfo& ViewCI,
                                                       IRadientMeshAsset**              ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (pGeometryData == nullptr || GeometryCount == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    MeshViewCreateInfoSnapshot StableViewCI{ViewCI};
    if (RADIENT_FAILED(StableViewCI.GetStatus()))
        return StableViewCI.GetStatus();

    std::vector<MeshGeometryStorage> ConcreteGeometries;
    ConcreteGeometries.reserve(GeometryCount);

    for (Uint32 GeometryIndex = 0; GeometryIndex < GeometryCount; ++GeometryIndex)
    {
        RefCntAutoPtr<MeshVertexDataAssetImpl> pVertexData{pGeometryData[GeometryIndex].pVertexData, IID_MeshVertexDataImpl};
        RefCntAutoPtr<MeshIndexDataAssetImpl>  pIndexData{pGeometryData[GeometryIndex].pIndexData, IID_MeshIndexDataImpl};
        if (pVertexData == nullptr ||
            pIndexData == nullptr)
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        ConcreteGeometries.push_back(MeshGeometryStorage{
            std::move(pIndexData),
            {},
            std::move(pVertexData),
            {}});
    }

    RefCntAutoPtr<MeshAssetImpl> pMeshAsset =
        MeshAssetImpl::Create(MakeRadientAssetURI("mesh", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)));
    VERIFY_EXPR(pMeshAsset != nullptr);
    if (!pMeshAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    pMeshAsset->QueryInterface(IID_RadientMeshAsset, ppMesh);

    std::vector<RefCntAutoPtr<IAsyncTask>> StrongPrerequisites;
    StrongPrerequisites.reserve(ConcreteGeometries.size() * 2);
    for (const MeshGeometryStorage& Geometry : ConcreteGeometries)
    {
        if (Geometry.pIndexDataAsset)
        {
            if (RefCntAutoPtr<IAsyncTask> pTask = Geometry.pIndexDataAsset->LockLoadTask())
                StrongPrerequisites.emplace_back(std::move(pTask));
        }

        if (Geometry.pVertexDataAsset)
        {
            if (RefCntAutoPtr<IAsyncTask> pTask = Geometry.pVertexDataAsset->LockLoadTask())
                StrongPrerequisites.emplace_back(std::move(pTask));
        }
    }

    std::vector<IAsyncTask*> Prerequisites;
    Prerequisites.reserve(StrongPrerequisites.size());
    for (const RefCntAutoPtr<IAsyncTask>& pTask : StrongPrerequisites)
        Prerequisites.push_back(pTask.RawPtr());

    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [pSelf = shared_from_this(),
             pMeshAsset,
             ConcreteGeometries = std::move(ConcreteGeometries),
             StableViewCI       = std::move(StableViewCI)](Uint32) mutable //
            {
                std::vector<Uint32> GeometryIndexCounts;
                GeometryIndexCounts.reserve(ConcreteGeometries.size());
                std::vector<std::string> GeometryCacheKeys;
                GeometryCacheKeys.reserve(ConcreteGeometries.size());
                for (MeshGeometryStorage& Geometry : ConcreteGeometries)
                {
                    if (Geometry.pIndexDataAsset == nullptr ||
                        Geometry.pVertexDataAsset == nullptr)
                    {
                        pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    const RADIENT_STATUS IndexPayloadStatus = Geometry.pIndexDataAsset->GetPayloadStatus();
                    if (IndexPayloadStatus != RADIENT_STATUS_OK)
                    {
                        pMeshAsset->Fail(IndexPayloadStatus);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    const RADIENT_STATUS VertexPayloadStatus = Geometry.pVertexDataAsset->GetPayloadStatus();
                    if (VertexPayloadStatus != RADIENT_STATUS_OK)
                    {
                        pMeshAsset->Fail(VertexPayloadStatus);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    Geometry.pIndexDataPayload  = Geometry.pIndexDataAsset->GetPayload();
                    Geometry.pVertexDataPayload = Geometry.pVertexDataAsset->GetPayload();
                    if (Geometry.pIndexDataPayload == nullptr ||
                        Geometry.pVertexDataPayload == nullptr)
                    {
                        pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    MeshIndexDataStorage&  IndexData        = Geometry.pIndexDataPayload->GetStorage();
                    MeshVertexDataStorage& VertexData       = Geometry.pVertexDataPayload->GetStorage();
                    const std::string      GeometryCacheKey = MakeMeshGeometryCacheKey(VertexData, IndexData);
                    if (IndexData.IndexCount == 0 ||
                        VertexData.VertexCount == 0 ||
                        GeometryCacheKey.empty())
                    {
                        pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    const RADIENT_STATUS IndexDataStatus = IndexData.GetLoadStatus();
                    if (RADIENT_FAILED(IndexDataStatus))
                    {
                        pMeshAsset->Fail(IndexDataStatus);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    const RADIENT_STATUS VertexDataStatus = VertexData.GetLoadStatus();
                    if (RADIENT_FAILED(VertexDataStatus))
                    {
                        pMeshAsset->Fail(VertexDataStatus);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    }

                    GeometryIndexCounts.push_back(IndexData.IndexCount);
                    GeometryCacheKeys.push_back(GeometryCacheKey);
                }

                RadientMeshViewSource MeshView{
                    StableViewCI.GetCreateInfo(),
                    GeometryIndexCounts.data(),
                    static_cast<Uint32>(GeometryIndexCounts.size())};
                if (RADIENT_FAILED(MeshView.GetStatus()))
                {
                    pMeshAsset->Fail(MeshView.GetStatus());
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                const std::string MeshCacheKey = MeshView.MakeCacheKey(GeometryCacheKeys);
                if (MeshCacheKey.empty())
                {
                    pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                auto [pMeshPayload, PayloadCreated] =
                    pSelf->m_MeshCache.GetOrCreate(
                        MeshCacheKey.c_str(),
                        [&ConcreteGeometries, &MeshView]() {
                            return MeshPayloadImpl::Create(std::move(ConcreteGeometries),
                                                           MeshView);
                        });

                (void)PayloadCreated;

                if (!pMeshPayload)
                {
                    pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);
                    return ASYNC_TASK_STATUS_COMPLETE;
                }

                pMeshAsset->SetPayload(std::move(pMeshPayload));
                return ASYNC_TASK_STATUS_COMPLETE;
            });

    if (!ThreadPool.EnqueueTask(pLoadTask, Prerequisites.data(), static_cast<Uint32>(Prerequisites.size())))
        pMeshAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);

    return pMeshAsset->GetPayloadStatus();
}

RadientDrawableMeshResolveResult RadientMeshAssetManager::GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                                          bool               RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    RefCntAutoPtr<MeshAssetImpl> pMeshImpl = MeshAssetImpl::ResolveAsset(pMesh);
    if (!pMeshImpl)
        return Result;

    return ResolveDrawableMesh(pMeshImpl->GetStorage(), RequireGPUResourcesReady);
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientAsset* pMeshAsset)
{
    MeshAssetImpl* pMesh = ClassPtrCast<MeshAssetImpl>(pMeshAsset);
    if (!pMesh)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pMesh->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    MeshStorage&         Storage    = pMesh->GetStorage();
    const RADIENT_STATUS ViewStatus = Storage.LoadStatus.load(std::memory_order_acquire);
    if (ViewStatus != RADIENT_STATUS_OK)
        return ViewStatus;

    const RADIENT_STATUS MeshStatus = GetMeshGeometryLoadStatus(Storage);
    if (MeshStatus != RADIENT_STATUS_OK)
        return MeshStatus;

    return GetMeshMaterialStatus(Storage);
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientMeshIndexData* pMeshIndexData)
{
    MeshIndexDataAssetImpl* const pIndexDataAsset = ClassPtrCast<MeshIndexDataAssetImpl>(pMeshIndexData);
    return pIndexDataAsset ? pIndexDataAsset->GetLoadStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientMeshVertexData* pMeshVertexData)
{
    MeshVertexDataAssetImpl* const pVertexDataAsset = ClassPtrCast<MeshVertexDataAssetImpl>(pMeshVertexData);
    return pVertexDataAsset ? pVertexDataAsset->GetLoadStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientMeshAssetManager::GetGPUResourceStatus(IRadientAsset* pMeshAsset)
{
    MeshAssetImpl* pMesh = ClassPtrCast<MeshAssetImpl>(pMeshAsset);
    if (!pMesh)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pMesh->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    MeshStorage&         Storage    = pMesh->GetStorage();
    const RADIENT_STATUS ViewStatus = Storage.LoadStatus.load(std::memory_order_acquire);
    if (ViewStatus != RADIENT_STATUS_OK)
        return ViewStatus;

    const RADIENT_STATUS MeshStatus = GetMeshGeometryLoadStatus(Storage);
    if (MeshStatus != RADIENT_STATUS_OK)
        return MeshStatus;

    RADIENT_STATUS Status = GetMeshGeometryGPUResourceStatus(Storage);
    Status                = CombineDependencyStatus(Status, GetMeshMaterialGPUResourceStatus(Storage));
    return Status;
}

const MeshPayloadImpl* RadientMeshAssetManager::GetMeshPayload(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    return pMesh ? pMesh->GetPayload().RawPtr() : nullptr;
}

Uint32 RadientMeshAssetManager::GetMeshGeometryCount(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return 0;

    MeshStorage& Storage = pMesh->GetStorage();
    return static_cast<Uint32>(Storage.Geometries.size());
}

const MeshIndexDataPayloadImpl* RadientMeshAssetManager::GetMeshIndexDataPayload(IRadientMeshAsset* pMeshAsset, Uint32 GeometryIndex)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (GeometryIndex >= Storage.Geometries.size())
        return nullptr;

    return Storage.Geometries[GeometryIndex].pIndexDataPayload.RawPtr();
}

const MeshVertexDataPayloadImpl* RadientMeshAssetManager::GetMeshVertexDataPayload(IRadientMeshAsset* pMeshAsset, Uint32 GeometryIndex)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (GeometryIndex >= Storage.Geometries.size())
        return nullptr;

    return Storage.Geometries[GeometryIndex].pVertexDataPayload.RawPtr();
}

const IRadientMeshIndexData* RadientMeshAssetManager::GetMeshIndexData(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (Storage.Geometries.empty())
        return nullptr;

    return Storage.Geometries.front().pIndexDataAsset.RawPtr();
}

const IRadientMeshVertexData* RadientMeshAssetManager::GetMeshVertexData(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (Storage.Geometries.empty())
        return nullptr;

    return Storage.Geometries.front().pVertexDataAsset.RawPtr();
}

namespace
{

RADIENT_STATUS GetMeshGeometryLoadStatus(const MeshStorage& Mesh)
{
    if (Mesh.Geometries.empty())
        return RADIENT_STATUS_INVALID_OPERATION;

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const MeshGeometryStorage& Geometry : Mesh.Geometries)
    {
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
        const MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();
        Status                                  = CombineDependencyStatus(Status, IndexData.LoadStatus.load(std::memory_order_acquire));
        Status                                  = CombineDependencyStatus(Status, VertexData.LoadStatus.load(std::memory_order_acquire));
    }

    return Status;
}

RADIENT_STATUS GetMeshGeometryGPUResourceStatus(const MeshStorage& Mesh)
{
    const RADIENT_STATUS LoadStatus = GetMeshGeometryLoadStatus(Mesh);
    if (LoadStatus != RADIENT_STATUS_OK)
        return LoadStatus;

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const MeshGeometryStorage& Geometry : Mesh.Geometries)
    {
        VERIFY_EXPR(Geometry.pIndexDataPayload != nullptr && Geometry.pVertexDataPayload != nullptr);
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
        const MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();
        Status                                  = CombineDependencyStatus(Status, IndexData.GPUResourceStatus.load(std::memory_order_acquire));
        Status                                  = CombineDependencyStatus(Status, VertexData.GPUResourceStatus.load(std::memory_order_acquire));
    }

    return Status;
}

RADIENT_STATUS GetMeshMaterialStatus(MeshStorage& Mesh)
{
    RADIENT_STATUS Status = Mesh.MaterialStatus.load(std::memory_order_acquire);
    if (Status != RADIENT_STATUS_PENDING)
        return Status;

    if (Mesh.Materials.empty())
        return RADIENT_STATUS_OK;

    if (Mesh.Materials.size() != Mesh.DrawableMesh.Primitives.size())
    {
        Mesh.MaterialStatus.store(RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    Status = RADIENT_STATUS_OK;

    for (IRadientMaterialAsset* pMaterialAsset : Mesh.Materials)
    {
        if (pMaterialAsset == nullptr)
            continue;

        const RADIENT_STATUS MaterialLoadStatus = RadientMaterialAssetManager::GetLoadStatus(pMaterialAsset);
        if (MaterialLoadStatus == RADIENT_STATUS_OK)
            continue;

        if (MaterialLoadStatus != RADIENT_STATUS_PENDING || Status == RADIENT_STATUS_OK)
            Status = MaterialLoadStatus;
    }

    if (Status != RADIENT_STATUS_PENDING)
        Mesh.MaterialStatus.store(Status, std::memory_order_release);

    return Status;
}

RADIENT_STATUS GetMeshMaterialGPUResourceStatus(MeshStorage& Mesh)
{
    const RADIENT_STATUS MaterialStatus = GetMeshMaterialStatus(Mesh);
    if (MaterialStatus != RADIENT_STATUS_OK)
        return MaterialStatus;

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    for (IRadientMaterialAsset* pMaterialAsset : Mesh.Materials)
    {
        if (pMaterialAsset == nullptr)
            continue;

        const RADIENT_STATUS MaterialGPUStatus = RadientMaterialAssetManager::GetGPUResourceStatus(pMaterialAsset);
        Status                                 = CombineDependencyStatus(Status, MaterialGPUStatus);
    }

    return Status;
}

RADIENT_STATUS ResolveMeshMaterialDependencies(MeshStorage& Mesh)
{
    RADIENT_STATUS Status = GetMeshMaterialStatus(Mesh);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    // Once all material dependencies report OK, attach the final GLTF material
    // pointers to the drawable primitives.
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh.Materials.size(); ++PrimitiveIndex)
    {
        if (Mesh.DrawableMesh.Primitives[PrimitiveIndex].pMaterial != nullptr)
            continue;

        IRadientMaterialAsset* pMaterialAsset = Mesh.Materials[PrimitiveIndex];
        if (pMaterialAsset == nullptr)
            continue;

        const GLTF::Material* pMaterial = RadientMaterialAssetManager::GetMaterial(pMaterialAsset);
        if (pMaterial == nullptr)
            return RADIENT_STATUS_PENDING;

        Mesh.DrawableMesh.Primitives[PrimitiveIndex].pMaterial = pMaterial;
    }

    return RADIENT_STATUS_OK;
}

RadientDrawableMeshResolveResult ResolveDrawableMesh(MeshStorage& Mesh,
                                                     bool         RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    if (Mesh.Geometries.empty() ||
        Mesh.DrawableMesh.Geometries.size() != Mesh.Geometries.size())
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    const RADIENT_STATUS ViewStatus = Mesh.LoadStatus.load(std::memory_order_acquire);
    if (ViewStatus != RADIENT_STATUS_OK)
    {
        Result.Status = ViewStatus;
        return Result;
    }

    const RADIENT_STATUS LoadStatus = GetMeshGeometryLoadStatus(Mesh);
    if (LoadStatus != RADIENT_STATUS_OK)
    {
        Result.Status = LoadStatus;
        return Result;
    }

    const RADIENT_STATUS GPUStatus           = GetMeshGeometryGPUResourceStatus(Mesh);
    const bool           AllowPendingGPUData = GPUStatus == RADIENT_STATUS_PENDING && !RequireGPUResourcesReady;
    const bool           AllowMissingGPUData = GPUStatus == RADIENT_STATUS_NO_GPU_DATA && !RequireGPUResourcesReady;
    if (GPUStatus != RADIENT_STATUS_OK &&
        !AllowPendingGPUData &&
        !AllowMissingGPUData)
    {
        Result.Status = GPUStatus;
        return Result;
    }

    if (GPUStatus == RADIENT_STATUS_NO_GPU_DATA && !RequireGPUResourcesReady)
    {
        const RADIENT_STATUS MaterialStatus = ResolveMeshMaterialDependencies(Mesh);
        if (MaterialStatus != RADIENT_STATUS_OK)
        {
            Result.Status = MaterialStatus;
            return Result;
        }

        Result.pMesh  = &Mesh.DrawableMesh;
        Result.Status = RADIENT_STATUS_OK;
        return Result;
    }

    // LoadStatus publishes the non-atomic allocation pointers. Only read them
    // after the acquire loads above have observed OK.
    for (size_t GeometryIndex = 0; GeometryIndex < Mesh.Geometries.size(); ++GeometryIndex)
    {
        MeshGeometryStorage& Geometry = Mesh.Geometries[GeometryIndex];
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
        {
            Result.Status = RADIENT_STATUS_INVALID_OPERATION;
            return Result;
        }

        MeshIndexDataStorage&        IndexData         = Geometry.pIndexDataPayload->GetStorage();
        MeshVertexDataStorage&       VertexData        = Geometry.pVertexDataPayload->GetStorage();
        IBufferSuballocation* const  pIndexAllocation  = IndexData.pIndexAllocation;
        IVertexPoolAllocation* const pVertexAllocation = VertexData.pVertexAllocation;
        IVertexPool* const           pVertexPool       = pVertexAllocation != nullptr ? pVertexAllocation->GetPool() : nullptr;
        if (pIndexAllocation == nullptr || pVertexAllocation == nullptr || pVertexPool == nullptr)
        {
            Result.Status = RADIENT_STATUS_INVALID_OPERATION;
            return Result;
        }

        RadientDrawableMeshGeometry& DrawableGeometry = Mesh.DrawableMesh.Geometries[GeometryIndex];
        DrawableGeometry.pVertexPool                  = pVertexPool;
        DrawableGeometry.FirstIndexLocation           = pIndexAllocation->GetOffset() / sizeof(Uint32);
        DrawableGeometry.BaseVertex                   = pVertexAllocation->GetStartVertex();
    }

    const RADIENT_STATUS MaterialStatus = ResolveMeshMaterialDependencies(Mesh);
    if (MaterialStatus != RADIENT_STATUS_OK)
    {
        Result.Status = MaterialStatus;
        return Result;
    }

    if (RequireGPUResourcesReady)
    {
        const RADIENT_STATUS MaterialGPUStatus = GetMeshMaterialGPUResourceStatus(Mesh);
        if (MaterialGPUStatus != RADIENT_STATUS_OK)
        {
            Result.Status = MaterialGPUStatus;
            return Result;
        }
    }

    if (RequireGPUResourcesReady &&
        GPUStatus == RADIENT_STATUS_PENDING)
    {
        Result.Status = RADIENT_STATUS_PENDING;
        return Result;
    }

    if (RequireGPUResourcesReady)
    {
        for (const RadientDrawableMeshGeometry& Geometry : Mesh.DrawableMesh.Geometries)
        {
            if (Geometry.pVertexPool == nullptr)
            {
                Result.Status = RADIENT_STATUS_INVALID_OPERATION;
                return Result;
            }
        }
    }

    Result.pMesh  = &Mesh.DrawableMesh;
    Result.Status = RADIENT_STATUS_OK;
    return Result;
}

} // namespace

} // namespace Diligent
