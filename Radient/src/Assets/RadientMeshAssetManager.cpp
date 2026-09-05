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
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMorphTargetData.hpp"
#include "Assets/RadientMorphTargetSource.hpp"
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

static constexpr INTERFACE_ID IID_MeshAssetImpl              = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};
static constexpr INTERFACE_ID IID_RadientMeshIndexData       = {0xeb134756, 0x0bac, 0x4bb9, {0x85, 0x84, 0xd5, 0x5f, 0x83, 0x6e, 0x7e, 0x7f}};
static constexpr INTERFACE_ID IID_MeshIndexDataImpl          = {0xdb8786a7, 0xe63e, 0x4128, {0x92, 0xce, 0x22, 0x86, 0xa4, 0x76, 0x9d, 0x14}};
static constexpr INTERFACE_ID IID_RadientMeshVertexData      = {0x33b53b79, 0x66b9, 0x44ae, {0x82, 0xd6, 0xc4, 0x7f, 0xe3, 0x06, 0x34, 0xa3}};
static constexpr INTERFACE_ID IID_MeshVertexDataImpl         = {0x059bbe7e, 0x96ed, 0x4213, {0xb6, 0x90, 0xa4, 0x28, 0x60, 0xb0, 0x34, 0xa7}};
static constexpr INTERFACE_ID IID_RadientMeshMorphTargetData = {0xdc6645d7, 0x7bff, 0x4964, {0x83, 0xaf, 0x18, 0x1c, 0x25, 0x2f, 0x3b, 0xed}};
static constexpr INTERFACE_ID IID_MeshMorphTargetDataImpl    = {0x2d315efd, 0x3fd0, 0x4904, {0x8a, 0x15, 0xb6, 0x5f, 0xfd, 0x55, 0x53, 0x1b}};

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

    void SetStatus(RADIENT_STATUS Status) noexcept
    {
        SetGPUResourceStatus(Status);
        SetLoadStatus(Status);
    }

    void SetLoadStatus(RADIENT_STATUS Status) noexcept
    {
        LoadStatus.store(Status, std::memory_order_release);
    }

    void SetGPUResourceStatus(RADIENT_STATUS Status) noexcept
    {
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

class MeshMorphTargetDataStorage : public MeshDataStatusStorage
{
public:
    MeshMorphTargetDataStorage(RADIENT_STATUS                  InitLoadStatus,
                               std::string                     CacheKey,
                               const RadientMorphTargetSource& Source) :
        MeshDataStatusStorage{InitLoadStatus, std::move(CacheKey)},
        Data{Source}
    {
    }

    RadientMorphTargetData              Data;
    RefCntAutoPtr<IBufferSuballocation> pAllocation;
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

class MeshMorphTargetDataPayloadImpl final : public RadientAssetPayloadImpl<MeshMorphTargetDataStorage, MeshMorphTargetDataPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MeshMorphTargetDataStorage, MeshMorphTargetDataPayloadImpl>;
    using TBase::TBase;
};

namespace
{

template <typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          const INTERFACE_ID& ImplID,
          typename PayloadType>
class MeshDataAssetImpl final :
    public RadientAssetImpl<InterfaceType,
                            InterfaceID,
                            ImplID,
                            RADIENT_ASSET_TYPE_MESH,
                            PayloadType,
                            MeshDataAssetImpl<InterfaceType, InterfaceID, ImplID, PayloadType>>
{
public:
    using TBase = RadientAssetImpl<InterfaceType,
                                   InterfaceID,
                                   ImplID,
                                   RADIENT_ASSET_TYPE_MESH,
                                   PayloadType,
                                   MeshDataAssetImpl<InterfaceType, InterfaceID, ImplID, PayloadType>>;
    using TBase::TBase;
    using TBase::Create;

    void SetLoadTask(IAsyncTask* pTask)
    {
        m_pLoadTask = pTask;
    }

    RefCntAutoPtr<IAsyncTask> LockLoadTask() const
    {
        return m_pLoadTask.Lock();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(ImplID, TBase)

private:
    RefCntWeakPtr<IAsyncTask> m_pLoadTask;
};

using MeshIndexDataAssetImpl = MeshDataAssetImpl<IRadientMeshIndexData,
                                                 IID_RadientMeshIndexData,
                                                 IID_MeshIndexDataImpl,
                                                 MeshIndexDataPayloadImpl>;

using MeshVertexDataAssetImpl = MeshDataAssetImpl<IRadientMeshVertexData,
                                                  IID_RadientMeshVertexData,
                                                  IID_MeshVertexDataImpl,
                                                  MeshVertexDataPayloadImpl>;

using MeshMorphTargetDataAssetImpl = MeshDataAssetImpl<IRadientMeshMorphTargetData,
                                                       IID_RadientMeshMorphTargetData,
                                                       IID_MeshMorphTargetDataImpl,
                                                       MeshMorphTargetDataPayloadImpl>;

struct MeshGeometryStorage
{
    RefCntAutoPtr<MeshIndexDataAssetImpl>         pIndexDataAsset;
    RefCntAutoPtr<MeshIndexDataPayloadImpl>       pIndexDataPayload;
    RefCntAutoPtr<MeshVertexDataAssetImpl>        pVertexDataAsset;
    RefCntAutoPtr<MeshVertexDataPayloadImpl>      pVertexDataPayload;
    RefCntAutoPtr<MeshMorphTargetDataAssetImpl>   pMorphTargetDataAsset;
    RefCntAutoPtr<MeshMorphTargetDataPayloadImpl> pMorphTargetDataPayload;
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

    // Aggregate dependency statuses are computed lazily while dependencies are
    // pending, then cached once they become terminal. This keeps steady-state
    // render-thread queries to cheap atomic loads.
    std::atomic<RADIENT_STATUS> GeometryLoadStatus{RADIENT_STATUS_PENDING};
    std::atomic<RADIENT_STATUS> GeometryGPUResourceStatus{RADIENT_STATUS_PENDING};
    std::atomic<RADIENT_STATUS> MaterialStatus{RADIENT_STATUS_OK};
    std::atomic<RADIENT_STATUS> MaterialGPUResourceStatus{RADIENT_STATUS_PENDING};

    std::atomic_bool MaterialsResolved{false};
    std::atomic_bool DrawableResolved{false};
    std::atomic_bool DrawableGPUResourcesReady{false};
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

class MeshAssetImpl;

using MeshAssetBase =
    RadientAssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, IID_MeshAssetImpl, RADIENT_ASSET_TYPE_MESH, MeshPayloadImpl, MeshAssetImpl>;

class MeshAssetImpl final : public MeshAssetBase
{
public:
    using TBase = MeshAssetBase;
    using TBase::TBase;
    using TBase::Create;
    using TBase::ResolveAsset;

    virtual const RadientMeshAssetDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        static const RadientMeshAssetDesc EmptyDesc{};
        RefCntAutoPtr<MeshPayloadImpl>    pPayload = GetPayload();
        if (pPayload == nullptr)
            return EmptyDesc;

        const MeshStorage& Storage = pPayload->GetStorage();
        for (const MeshGeometryStorage& Geometry : Storage.Geometries)
        {
            if (Geometry.pMorphTargetDataPayload != nullptr)
                return Geometry.pMorphTargetDataPayload->GetStorage().Data.GetDesc();
        }
        return EmptyDesc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMorphTargetWeights(IRadientMorphTargetWeights** ppWeights) override final
    {
        return CreateRadientMorphTargetWeights(this, GetDesc(), ppWeights);
    }
};

struct MeshIndexBufferWriteData
{
    const RadientMeshIndexSource* pSource = nullptr;
};

struct MeshVertexBufferWriteData
{
    const RadientMeshVertexSource* pSource           = nullptr;
    Uint32                         VertexBufferIndex = 0;
};

struct MorphTargetBufferWriteData
{
    const RadientMorphTargetSource* pSource = nullptr;
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

struct MorphTargetBufferCopyData
{
    RefCntAutoPtr<MeshMorphTargetDataPayloadImpl> pMorphTargetDataPayload;
    RefCntAutoPtr<IRenderDevice>                  pDevice;
};

void UpdateMeshUploadProgress(MeshDataStatusStorage& Data,
                              bool                   CopyScheduled) noexcept
{
    if (!CopyScheduled)
        Data.SetGPUResourceStatus(RADIENT_STATUS_FAILED);

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

        const RadientMorphTargetData* pMorphTargetData =
            Geometry.pMorphTargetDataPayload != nullptr ?
            &Geometry.pMorphTargetDataPayload->GetStorage().Data :
            nullptr;

        DrawableMesh.Geometries.push_back(RadientDrawableMeshGeometry{
            nullptr,
            pMorphTargetData,
            VertexData.VertexAttribFlags,
            0,
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

        if (pMaterialAsset != nullptr)
        {
            const RADIENT_STATUS MaterialLoadStatus = RadientMaterialAssetManager::GetLoadStatus(pMaterialAsset);
            if (RADIENT_FAILED(MaterialLoadStatus))
            {
                LoadStatus.store(MaterialLoadStatus, std::memory_order_release);
                return;
            }

            if (MaterialLoadStatus == RADIENT_STATUS_PENDING)
                MaterialStatusValue = RADIENT_STATUS_PENDING;
        }

        Materials.emplace_back(pMaterialAsset);
        DrawableMesh.Primitives.push_back(RadientDrawableMeshPrimitive{
            pMaterialAsset,
            GeometryIndex,
            true,
            PrimitiveCI.FirstIndex,
            PrimitiveCI.IndexCount});
    }

    MaterialStatus.store(MaterialStatusValue, std::memory_order_release);
}

void WriteMeshIndexData(void* pDstData, Uint32 NumBytes, void* pUserData) noexcept
{
    MeshIndexBufferWriteData* Data = static_cast<MeshIndexBufferWriteData*>(pUserData);
    VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
    if (Data == nullptr || Data->pSource == nullptr)
        return;

    const RADIENT_STATUS Status =
        Data->pSource->PackIndexData(RadientMeshIndexSource::PackDestination{pDstData, NumBytes});
    VERIFY_EXPR(Status == RADIENT_STATUS_OK);
}

void WriteMeshVertexData(void* pDstData, Uint32 NumBytes, void* pUserData) noexcept
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

void WriteMorphTargetData(void* pDstData, Uint32 NumBytes, void* pUserData) noexcept
{
    MorphTargetBufferWriteData* Data = static_cast<MorphTargetBufferWriteData*>(pUserData);
    VERIFY_EXPR(Data != nullptr && Data->pSource != nullptr);
    if (Data == nullptr || Data->pSource == nullptr)
        return;

    const RADIENT_STATUS Status =
        Data->pSource->PackData(RadientMorphTargetSource::PackDestination{pDstData, NumBytes});
    VERIFY_EXPR(Status == RADIENT_STATUS_OK);
}

void CopyMeshIndexBuffer(IDeviceContext* pContext,
                         IBuffer*        pSrcBuffer,
                         Uint32          SrcOffset,
                         Uint32          NumBytes,
                         void*           pUserData) noexcept
{
    std::unique_ptr<MeshIndexBufferCopyData> Data{static_cast<MeshIndexBufferCopyData*>(pUserData)};

    IBufferSuballocation* pIndexAllocation = nullptr;
    if (Data != nullptr && Data->pIndexDataPayload != nullptr)
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

    VERIFY_EXPR(Data != nullptr && Data->pIndexDataPayload != nullptr);
    if (Data != nullptr && Data->pIndexDataPayload != nullptr)
        UpdateMeshUploadProgress(Data->pIndexDataPayload->GetStorage(), CopyScheduled);
}

void CopyMeshVertexBuffer(IDeviceContext* pContext,
                          IBuffer*        pSrcBuffer,
                          Uint32          SrcOffset,
                          Uint32          NumBytes,
                          void*           pUserData) noexcept
{
    std::unique_ptr<MeshVertexBufferCopyData> Data{static_cast<MeshVertexBufferCopyData*>(pUserData)};

    IVertexPoolAllocation* pVertexAllocation = nullptr;
    if (Data != nullptr && Data->pVertexDataPayload != nullptr)
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

    VERIFY_EXPR(Data != nullptr && Data->pVertexDataPayload != nullptr);
    if (Data != nullptr && Data->pVertexDataPayload != nullptr)
        UpdateMeshUploadProgress(Data->pVertexDataPayload->GetStorage(), CopyScheduled);
}

void CopyMorphTargetBuffer(IDeviceContext* pContext,
                           IBuffer*        pSrcBuffer,
                           Uint32          SrcOffset,
                           Uint32          NumBytes,
                           void*           pUserData) noexcept
{
    std::unique_ptr<MorphTargetBufferCopyData> Data{static_cast<MorphTargetBufferCopyData*>(pUserData)};

    MeshMorphTargetDataStorage* pMorphTargetData = nullptr;
    if (Data != nullptr && Data->pMorphTargetDataPayload != nullptr)
        pMorphTargetData = &Data->pMorphTargetDataPayload->GetStorage();

    bool CopyScheduled = false;
    if (pContext != nullptr &&
        pSrcBuffer != nullptr &&
        pMorphTargetData != nullptr)
    {
        IBufferSuballocation* const pAllocation = pMorphTargetData->pAllocation;
        IBuffer* const              pDstBuffer =
            pAllocation != nullptr ? pAllocation->Update(Data->pDevice, pContext) : nullptr;
        if (pDstBuffer != nullptr)
        {
            pContext->CopyBuffer(pSrcBuffer, SrcOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 pDstBuffer, pAllocation->GetOffset(), NumBytes,
                                 RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            CopyScheduled = true;
        }
    }

    VERIFY_EXPR(pMorphTargetData != nullptr);
    if (pMorphTargetData != nullptr)
        UpdateMeshUploadProgress(*pMorphTargetData, CopyScheduled);
}

void ScheduleMeshIndexUpload(IGPUUploadManager&            UploadManager,
                             IRenderDevice&                Device,
                             const RadientMeshIndexSource& Source,
                             MeshIndexDataPayloadImpl*     pIndexDataPayload)
{
    std::unique_ptr<MeshIndexBufferCopyData> pCopyData{new MeshIndexBufferCopyData{}};
    pCopyData->pIndexDataPayload = pIndexDataPayload;
    pCopyData->pDevice           = &Device;

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

    UploadManager.ScheduleBufferUpdate(UpdateInfo);
    pCopyData.release();
}

void ScheduleMeshVertexUpload(IGPUUploadManager&             UploadManager,
                              IRenderDevice&                 Device,
                              const RadientMeshVertexSource& Source,
                              MeshVertexDataPayloadImpl*     pVertexDataPayload,
                              Uint32                         VertexBufferIndex)
{
    std::unique_ptr<MeshVertexBufferCopyData> pCopyData{new MeshVertexBufferCopyData{}};
    pCopyData->pVertexDataPayload = pVertexDataPayload;
    pCopyData->pDevice            = &Device;
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

    UploadManager.ScheduleBufferUpdate(UpdateInfo);
    pCopyData.release();
}

void ScheduleMorphTargetUpload(IGPUUploadManager&              UploadManager,
                               IRenderDevice&                  Device,
                               const RadientMorphTargetSource& Source,
                               MeshMorphTargetDataPayloadImpl* pMorphTargetDataPayload)
{
    std::unique_ptr<MorphTargetBufferCopyData> pCopyData{new MorphTargetBufferCopyData{}};
    pCopyData->pMorphTargetDataPayload = pMorphTargetDataPayload;
    pCopyData->pDevice                 = &Device;

    MorphTargetBufferWriteData WriteData;
    WriteData.pSource = &Source;

    ScheduleBufferUpdateInfo UpdateInfo;
    UpdateInfo.pContext                   = nullptr;
    UpdateInfo.NumBytes                   = Source.GetDataSize();
    UpdateInfo.pSrcData                   = nullptr;
    UpdateInfo.WriteDataCallback          = WriteMorphTargetData;
    UpdateInfo.pWriteDataCallbackUserData = &WriteData;
    UpdateInfo.pCopyBufferData            = pCopyData.get();
    UpdateInfo.CopyBuffer                 = CopyMorphTargetBuffer;

    UploadManager.ScheduleBufferUpdate(UpdateInfo);
    pCopyData.release();
}

void CreateMeshIndexDataFromSource(const RadientMeshIndexSource& IndexSource,
                                   MeshIndexDataPayloadImpl&     IndexDataPayload,
                                   IRenderDevice&                Device,
                                   GLTF::ResourceManager&        ResourceManager,
                                   IGPUUploadManager&            UploadManager)
{
    MeshIndexDataStorage& IndexData = IndexDataPayload.GetStorage();

    IndexData.pIndexAllocation = ResourceManager.AllocateIndices(IndexSource.GetIndexDataSize(), alignof(Uint32));
    if (IndexData.pIndexAllocation == nullptr)
    {
        IndexData.SetGPUResourceStatus(RADIENT_STATUS_FAILED);
    }
    else
    {
        IndexData.PendingUploads.store(1, std::memory_order_release);
        IndexData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);
        ScheduleMeshIndexUpload(UploadManager, Device, IndexSource, &IndexDataPayload);
    }

    // LoadStatus publishes GPU resource status and pIndexAllocation to readers.
    // Keep this as the final release store after allocation/scheduling state is settled.
    IndexData.SetLoadStatus(RADIENT_STATUS_OK);
}

void CreateMeshVertexDataFromSource(const RadientMeshVertexSource& VertexSource,
                                    MeshVertexDataPayloadImpl&     VertexDataPayload,
                                    IRenderDevice&                 Device,
                                    GLTF::ResourceManager&         ResourceManager,
                                    IGPUUploadManager&             UploadManager)
{
    MeshVertexDataStorage& VertexData = VertexDataPayload.GetStorage();

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

    GLTF::ResourceManager::VertexLayoutKey LayoutKey;
    LayoutKey.Elements.reserve(VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        LayoutKey.Elements.emplace_back(VertexSource.GetVertexStride(BufferIndex), BIND_VERTEX_BUFFER);

    VertexData.pVertexAllocation = ResourceManager.AllocateVertices(LayoutKey, VertexSource.GetVertexCount());
    if (VertexData.pVertexAllocation == nullptr)
    {
        VertexData.SetGPUResourceStatus(RADIENT_STATUS_FAILED);
    }
    else
    {
        VertexData.PendingUploads.store(UploadCount, std::memory_order_release);
        VertexData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);

        for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        {
            if (!VertexSource.IsVertexBufferActive(BufferIndex))
                continue;

            ScheduleMeshVertexUpload(UploadManager, Device, VertexSource, &VertexDataPayload, BufferIndex);
        }
    }

    // LoadStatus publishes GPU resource status and pVertexAllocation to readers.
    // Keep this as the final release store after allocation/scheduling state is settled.
    VertexData.SetLoadStatus(RADIENT_STATUS_OK);
}

void CreateMorphTargetDataFromSource(const RadientMorphTargetSource& Source,
                                     MeshMorphTargetDataPayloadImpl& MorphTargetDataPayload,
                                     IRenderDevice&                  Device,
                                     GLTF::ResourceManager&          ResourceManager,
                                     IGPUUploadManager&              UploadManager)
{
    MeshMorphTargetDataStorage& MorphTargetData = MorphTargetDataPayload.GetStorage();

    const Uint32 DataSize = Source.GetDataSize();
    if (DataSize != MorphTargetData.Data.GetDataSize())
    {
        MorphTargetData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return;
    }

    RefCntAutoPtr<IBufferSuballocation> pAllocation = ResourceManager.AllocateMorphTargetData(DataSize, alignof(Float32));
    if (pAllocation == nullptr)
    {
        MorphTargetData.SetGPUResourceStatus(RADIENT_STATUS_FAILED);
    }
    else
    {
        MorphTargetData.pAllocation = std::move(pAllocation);
        MorphTargetData.PendingUploads.store(1, std::memory_order_release);
        MorphTargetData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);
        ScheduleMorphTargetUpload(UploadManager, Device, Source, &MorphTargetDataPayload);
    }

    MorphTargetData.SetLoadStatus(RADIENT_STATUS_OK);
}


bool AreMorphTargetDescsCompatible(const RadientMeshAssetDesc& Lhs,
                                   const RadientMeshAssetDesc& Rhs)
{
    if (Lhs.MorphTargetCount != Rhs.MorphTargetCount)
        return false;

    const auto StringsEqual = [](const Char* LhsString, const Char* RhsString) {
        if (LhsString == nullptr || RhsString == nullptr)
            return LhsString == RhsString;
        return std::strcmp(LhsString, RhsString) == 0;
    };

    for (Uint32 TargetIndex = 0; TargetIndex < Lhs.MorphTargetCount; ++TargetIndex)
    {
        const RadientMorphTargetDesc& LhsTarget = Lhs.pMorphTargets[TargetIndex];
        const RadientMorphTargetDesc& RhsTarget = Rhs.pMorphTargets[TargetIndex];
        if (!StringsEqual(LhsTarget.Name, RhsTarget.Name) ||
            LhsTarget.DefaultWeight != RhsTarget.DefaultWeight ||
            LhsTarget.AttributeCount != RhsTarget.AttributeCount)
        {
            return false;
        }

        for (Uint32 AttributeIndex = 0; AttributeIndex < LhsTarget.AttributeCount; ++AttributeIndex)
        {
            const RadientMorphTargetAttributeDesc& LhsAttribute = LhsTarget.pAttributes[AttributeIndex];
            const RadientMorphTargetAttributeDesc& RhsAttribute = RhsTarget.pAttributes[AttributeIndex];
            if (!StringsEqual(LhsAttribute.Semantic, RhsAttribute.Semantic) ||
                LhsAttribute.ComponentCount != RhsAttribute.ComponentCount)
            {
                return false;
            }
        }
    }

    return true;
}

std::string MakeMeshGeometryCacheKey(const MeshVertexDataStorage&      VertexData,
                                     const MeshIndexDataStorage&       IndexData,
                                     const MeshMorphTargetDataStorage* pMorphTargetData)
{
    if (VertexData.CacheKey.empty() ||
        IndexData.CacheKey.empty() ||
        (pMorphTargetData != nullptr && pMorphTargetData->CacheKey.empty()))
        return {};

    RadientCacheKeyBuilder Builder{"mesh-geometry", 1};
    Builder.AddString("vertex", VertexData.CacheKey)
        .AddString("index", IndexData.CacheKey);

    if (pMorphTargetData != nullptr)
        Builder.AddString("morph-target", pMorphTargetData->CacheKey);

    return Builder.GetKey();
}

struct MeshDataCreationContext
{
    // Keep the cache owner alive until its asynchronous creation task completes.
    RadientMeshAssetManagerSharedPtr     pManager;
    RefCntAutoPtr<IRenderDevice>         pDevice;
    RefCntWeakPtr<GLTF::ResourceManager> pResourceManager;
    RefCntWeakPtr<IGPUUploadManager>     pUploadManager;
};

template <typename SourceType>
bool RequiresGPUUpload(const SourceType&) noexcept
{
    return true;
}

bool RequiresGPUUpload(const RadientMorphTargetSource& Source) noexcept
{
    return Source.GetDataSize() != 0;
}

template <typename AssetImplType,
          typename SourceType,
          typename DataInterfaceType,
          typename PayloadType,
          typename PrepareSourceType,
          typename CreatePayloadType,
          typename InitializePayloadType>
RADIENT_STATUS CreateMeshDataAsset(IThreadPool&                    ThreadPool,
                                   MeshDataCreationContext         Context,
                                   RadientAssetCache<PayloadType>& Cache,
                                   std::unique_ptr<SourceType>     pSource,
                                   DataInterfaceType**             ppData,
                                   const Char*                     AssetURIType,
                                   const Char*                     DataName,
                                   PrepareSourceType&&             PrepareSource,
                                   CreatePayloadType&&             CreatePayload,
                                   InitializePayloadType&&         InitializePayload)
{
    if (ppData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppData == nullptr, "Output ", DataName, " pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppData = nullptr;

    if (pSource == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<AssetImplType> pDataAsset = AssetImplType::Create(MakeRadientAssetURI(AssetURIType));
    if (pDataAsset == nullptr)
        return RADIENT_STATUS_FAILED;

    auto                      CacheAccessor = Cache.GetAccessor();
    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [Context       = std::move(Context),
             CacheAccessor = std::move(CacheAccessor),
             pSource       = std::move(pSource),
             pDataAsset,
             PrepareSource     = std::forward<PrepareSourceType>(PrepareSource),
             CreatePayload     = std::forward<CreatePayloadType>(CreatePayload),
             InitializePayload = std::forward<InitializePayloadType>(InitializePayload)](Uint32) mutable //
            {
                const auto Fail = [&pDataAsset](RADIENT_STATUS Status = RADIENT_STATUS_FAILED) {
                    pDataAsset->Fail(Status);
                    return ASYNC_TASK_STATUS_COMPLETE;
                };

                const RADIENT_STATUS SourceStatus = pSource->GetStatus();
                if (RADIENT_FAILED(SourceStatus))
                    return Fail(SourceStatus);

                const RADIENT_STATUS PrepareStatus = PrepareSource(*pSource);
                if (RADIENT_FAILED(PrepareStatus))
                    return Fail(PrepareStatus);

                std::string CacheKey = pSource->MakeCacheKey();
                if (CacheKey.empty())
                    return Fail();

                auto [pPayload, PayloadCreated] =
                    CacheAccessor.GetOrCreate(
                        CacheKey.c_str(),
                        [&pSource, &CreatePayload, CacheKey]() mutable {
                            return CreatePayload(*pSource, std::move(CacheKey));
                        });

                if (pPayload == nullptr)
                    return Fail();

                if (!pDataAsset->SetPayload(RefCntAutoPtr<PayloadType>{pPayload}))
                    return ASYNC_TASK_STATUS_COMPLETE;

                if (PayloadCreated)
                {
                    MeshDataStatusStorage& Data = pPayload->GetStorage();
                    if (!RequiresGPUUpload(*pSource))
                    {
                        Data.SetStatus(RADIENT_STATUS_OK);
                    }
                    else if (Context.pDevice == nullptr)
                    {
                        Data.SetGPUResourceStatus(RADIENT_STATUS_NO_GPU_DATA);
                        Data.SetLoadStatus(RADIENT_STATUS_OK);
                    }
                    else
                    {
                        RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = Context.pResourceManager.Lock();
                        RefCntAutoPtr<IGPUUploadManager>     pUploadManager   = Context.pUploadManager.Lock();
                        if (pResourceManager == nullptr || pUploadManager == nullptr)
                        {
                            Data.SetGPUResourceStatus(RADIENT_STATUS_CANCELLED);
                            Data.SetLoadStatus(RADIENT_STATUS_OK);
                        }
                        else
                        {
                            InitializePayload(*pSource,
                                              *pPayload,
                                              *Context.pDevice,
                                              *pResourceManager,
                                              *pUploadManager);
                        }
                    }
                }

                return ASYNC_TASK_STATUS_COMPLETE;
            });

    pDataAsset->SetLoadTask(pLoadTask);
    const bool TaskEnqueued = ThreadPool.EnqueueTask(pLoadTask);
    if (!TaskEnqueued)
        pDataAsset->Fail(RADIENT_STATUS_INVALID_OPERATION);

    *ppData = pDataAsset.Detach();
    return TaskEnqueued ? RADIENT_STATUS_PENDING : RADIENT_STATUS_INVALID_OPERATION;
}

RadientDrawableMeshResolveResult ResolveDrawableMesh(MeshStorage& Mesh,
                                                     bool         RequireGPUResourcesReady);

RADIENT_STATUS GetMeshGeometryLoadStatus(MeshStorage& Mesh);
RADIENT_STATUS GetMeshGeometryGPUResourceStatus(MeshStorage& Mesh);
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

    RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphTargetData;
    if (MeshCI.MorphTargetCount != 0)
    {
        try
        {
            RADIENT_STATUS Status = CreateMeshMorphTargetData(
                ThreadPool,
                std::make_unique<RadientMorphTargetSource>(MeshCI.pMorphTargets,
                                                           MeshCI.MorphTargetCount,
                                                           MeshCI.VertexCount),
                pMorphTargetData.GetAddressOfEmpty());
            if (RADIENT_FAILED(Status) || pMorphTargetData == nullptr)
            {
                LOG_ERROR_MESSAGE("Failed to create Radient mesh morph-target data; creating the mesh without morph targets");
                pMorphTargetData.Release();
            }
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to copy Radient mesh morph targets: ", Error.what(),
                              "; creating the mesh without morph targets");
        }
    }

    const RadientMeshViewCreateInfo ViewCI{
        MeshCI.pPrimitives,
        MeshCI.PrimitiveCount};

    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    RADIENT_STATUS                        Status = CreateMeshVertexData(ThreadPool,
                                                                        std::make_unique<RadientMeshVertexSource>(MeshCI),
                                                                        pVertexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pVertexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;
    Status = CreateMeshIndexData(ThreadPool,
                                 std::make_unique<RadientMeshIndexSource>(MeshCI),
                                 pIndexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pIndexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    const RadientMeshGeometryData GeometryData{pVertexData, pIndexData, pMorphTargetData};
    return CreateMeshView(ThreadPool, &GeometryData, 1, ViewCI, ppMesh);
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshIndexData(IThreadPool&                            ThreadPool,
                                                            std::unique_ptr<RadientMeshIndexSource> pIndexSource,
                                                            IRadientMeshIndexData**                 ppIndexData)
{
    return CreateMeshDataAsset<MeshIndexDataAssetImpl>(
        ThreadPool,
        MeshDataCreationContext{shared_from_this(), m_pDevice, m_WeakResourceManager, m_WeakUploadManager},
        m_MeshIndexDataCache,
        std::move(pIndexSource),
        ppIndexData,
        "mesh-index-data",
        "mesh index data",
        [](const RadientMeshIndexSource& Source) {
            return Source.GetIndexCount() != 0 && Source.GetIndexDataSize() != 0 ?
                RADIENT_STATUS_OK :
                RADIENT_STATUS_INVALID_ARGUMENT;
        },
        [](const RadientMeshIndexSource& Source, std::string CacheKey) {
            return MeshIndexDataPayloadImpl::Create(RADIENT_STATUS_PENDING,
                                                    std::move(CacheKey),
                                                    Source.GetIndexCount());
        },
        CreateMeshIndexDataFromSource);
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshVertexData(IThreadPool&                             ThreadPool,
                                                             std::unique_ptr<RadientMeshVertexSource> pVertexSource,
                                                             IRadientMeshVertexData**                 ppVertexData)
{
    return CreateMeshDataAsset<MeshVertexDataAssetImpl>(
        ThreadPool,
        MeshDataCreationContext{shared_from_this(), m_pDevice, m_WeakResourceManager, m_WeakUploadManager},
        m_MeshVertexDataCache,
        std::move(pVertexSource),
        ppVertexData,
        "mesh-vertex-data",
        "mesh vertex data",
        [](RadientMeshVertexSource& Source) {
            if (!Source.HasVertexAttributes())
            {
                const RADIENT_STATUS Status =
                    Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                               static_cast<Uint32>(GLTF::DefaultVertexAttributes.size()));
                if (RADIENT_FAILED(Status))
                    return Status;
            }

            return Source.GetVertexCount() != 0 && Source.GetVertexBufferCount() != 0 ?
                RADIENT_STATUS_OK :
                RADIENT_STATUS_INVALID_ARGUMENT;
        },
        [](const RadientMeshVertexSource& Source, std::string CacheKey) {
            return MeshVertexDataPayloadImpl::Create(RADIENT_STATUS_PENDING,
                                                     std::move(CacheKey),
                                                     Source.GetVertexCount(),
                                                     Source.GetVertexAttribFlags());
        },
        CreateMeshVertexDataFromSource);
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshMorphTargetData(IThreadPool&                              ThreadPool,
                                                                  std::unique_ptr<RadientMorphTargetSource> pMorphTargetSource,
                                                                  IRadientMeshMorphTargetData**             ppMorphTargetData)
{
    return CreateMeshDataAsset<MeshMorphTargetDataAssetImpl>(
        ThreadPool,
        MeshDataCreationContext{shared_from_this(), m_pDevice, m_WeakResourceManager, m_WeakUploadManager},
        m_MeshMorphTargetDataCache,
        std::move(pMorphTargetSource),
        ppMorphTargetData,
        "mesh-morph-target-data",
        "mesh morph-target data",
        [](const RadientMorphTargetSource&) {
            return RADIENT_STATUS_OK;
        },
        [](const RadientMorphTargetSource& Source, std::string CacheKey) {
            return MeshMorphTargetDataPayloadImpl::Create(RADIENT_STATUS_PENDING,
                                                          std::move(CacheKey),
                                                          Source);
        },
        CreateMorphTargetDataFromSource);
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

    // Keep the stored payload representation consistent with the mesh-view
    // cache key: only geometries referenced by primitives are retained, and
    // primitive geometry indices are remapped to compact first-use order.
    RadientMeshViewGeometryRemap GeometryRemap =
        BuildMeshViewGeometryRemap(ViewCI.pGeometryIndices,
                                   ViewCI.PrimitiveCount,
                                   GeometryCount);
    if (RADIENT_FAILED(GeometryRemap.Status))
        return GeometryRemap.Status;

    RadientMeshViewCreateInfo CanonicalViewCI = ViewCI;
    CanonicalViewCI.pGeometryIndices          = GeometryRemap.PrimitiveGeometryIndices.data();
    MeshViewCreateInfoSnapshot CanonicalStableViewCI{CanonicalViewCI};
    if (RADIENT_FAILED(CanonicalStableViewCI.GetStatus()))
        return CanonicalStableViewCI.GetStatus();

    std::vector<MeshGeometryStorage> ConcreteGeometries;
    ConcreteGeometries.reserve(GeometryRemap.UsedGeometryIndices.size());

    for (Uint32 GeometryIndex : GeometryRemap.UsedGeometryIndices)
    {
        RefCntAutoPtr<MeshVertexDataAssetImpl> pVertexData{pGeometryData[GeometryIndex].pVertexData, IID_MeshVertexDataImpl};
        RefCntAutoPtr<MeshIndexDataAssetImpl>  pIndexData{pGeometryData[GeometryIndex].pIndexData, IID_MeshIndexDataImpl};
        if (pVertexData == nullptr ||
            pIndexData == nullptr)
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        RefCntAutoPtr<MeshMorphTargetDataAssetImpl> pMorphTargetData{
            pGeometryData[GeometryIndex].pMorphTargetData,
            IID_MeshMorphTargetDataImpl};
        if (pGeometryData[GeometryIndex].pMorphTargetData != nullptr &&
            pMorphTargetData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        MeshGeometryStorage& Geometry  = ConcreteGeometries.emplace_back();
        Geometry.pIndexDataAsset       = std::move(pIndexData);
        Geometry.pVertexDataAsset      = std::move(pVertexData);
        Geometry.pMorphTargetDataAsset = std::move(pMorphTargetData);
    }

    RefCntAutoPtr<MeshAssetImpl> pMeshAsset =
        MeshAssetImpl::Create(MakeRadientAssetURI("mesh"));
    VERIFY_EXPR(pMeshAsset != nullptr);
    if (!pMeshAsset)
        return RADIENT_STATUS_FAILED;

    pMeshAsset->QueryInterface(IID_RadientMeshAsset, ppMesh);

    std::vector<RefCntAutoPtr<IAsyncTask>> StrongPrerequisites;
    StrongPrerequisites.reserve(ConcreteGeometries.size() * 3);
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

        if (Geometry.pMorphTargetDataAsset)
        {
            if (RefCntAutoPtr<IAsyncTask> pTask = Geometry.pMorphTargetDataAsset->LockLoadTask())
                StrongPrerequisites.emplace_back(std::move(pTask));
        }
    }

    std::vector<IAsyncTask*> Prerequisites{StrongPrerequisites.begin(), StrongPrerequisites.end()};

    RefCntAutoPtr<IAsyncTask> pLoadTask =
        CreateAsyncWorkTask(
            [pSelf = shared_from_this(),
             pMeshAsset,
             ConcreteGeometries    = std::move(ConcreteGeometries),
             CanonicalStableViewCI = std::move(CanonicalStableViewCI)](Uint32) mutable //
            {
                const auto FailMesh = [&pMeshAsset](RADIENT_STATUS Status = RADIENT_STATUS_FAILED) {
                    pMeshAsset->Fail(Status);
                    return ASYNC_TASK_STATUS_COMPLETE;
                };

                std::vector<Uint32> GeometryIndexCounts;
                GeometryIndexCounts.reserve(ConcreteGeometries.size());
                std::vector<std::string> GeometryCacheKeys;
                GeometryCacheKeys.reserve(ConcreteGeometries.size());
                const RadientMeshAssetDesc* pMeshMorphTargetDesc = nullptr;
                for (MeshGeometryStorage& Geometry : ConcreteGeometries)
                {
                    if (Geometry.pIndexDataAsset == nullptr ||
                        Geometry.pVertexDataAsset == nullptr)
                        return FailMesh();

                    const RADIENT_STATUS IndexPayloadStatus = Geometry.pIndexDataAsset->GetPayloadStatus();
                    if (IndexPayloadStatus != RADIENT_STATUS_OK)
                        return FailMesh(IndexPayloadStatus);

                    const RADIENT_STATUS VertexPayloadStatus = Geometry.pVertexDataAsset->GetPayloadStatus();
                    if (VertexPayloadStatus != RADIENT_STATUS_OK)
                        return FailMesh(VertexPayloadStatus);

                    Geometry.pIndexDataPayload  = Geometry.pIndexDataAsset->GetPayload();
                    Geometry.pVertexDataPayload = Geometry.pVertexDataAsset->GetPayload();
                    if (Geometry.pIndexDataPayload == nullptr ||
                        Geometry.pVertexDataPayload == nullptr)
                        return FailMesh();

                    MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
                    MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();
                    if (IndexData.IndexCount == 0 ||
                        VertexData.VertexCount == 0)
                        return FailMesh();

                    const RADIENT_STATUS IndexDataStatus = IndexData.GetLoadStatus();
                    if (RADIENT_FAILED(IndexDataStatus))
                        return FailMesh(IndexDataStatus);

                    const RADIENT_STATUS VertexDataStatus = VertexData.GetLoadStatus();
                    if (RADIENT_FAILED(VertexDataStatus))
                        return FailMesh(VertexDataStatus);

                    MeshMorphTargetDataStorage* pMorphTargetData = nullptr;
                    if (Geometry.pMorphTargetDataAsset != nullptr)
                    {
                        const RADIENT_STATUS MorphPayloadStatus = Geometry.pMorphTargetDataAsset->GetPayloadStatus();
                        if (MorphPayloadStatus != RADIENT_STATUS_OK)
                            return FailMesh(MorphPayloadStatus);

                        Geometry.pMorphTargetDataPayload = Geometry.pMorphTargetDataAsset->GetPayload();
                        if (Geometry.pMorphTargetDataPayload == nullptr)
                            return FailMesh();

                        pMorphTargetData                     = &Geometry.pMorphTargetDataPayload->GetStorage();
                        const RADIENT_STATUS MorphDataStatus = pMorphTargetData->GetLoadStatus();
                        if (MorphDataStatus != RADIENT_STATUS_OK)
                            return FailMesh(MorphDataStatus);

                        if (pMorphTargetData->Data.GetVertexCount() != VertexData.VertexCount)
                        {
                            LOG_ERROR_MESSAGE("Morph-target vertex count ", pMorphTargetData->Data.GetVertexCount(),
                                              " does not match geometry vertex count ", VertexData.VertexCount);
                            return FailMesh(RADIENT_STATUS_INVALID_DATA);
                        }

                        const RadientMeshAssetDesc& MorphTargetDesc = pMorphTargetData->Data.GetDesc();
                        if (pMeshMorphTargetDesc != nullptr &&
                            !AreMorphTargetDescsCompatible(*pMeshMorphTargetDesc, MorphTargetDesc))
                        {
                            LOG_ERROR_MESSAGE("Morph-target schemas are inconsistent between mesh geometries");
                            return FailMesh(RADIENT_STATUS_INVALID_DATA);
                        }
                        pMeshMorphTargetDesc = &MorphTargetDesc;
                    }

                    const std::string GeometryCacheKey =
                        MakeMeshGeometryCacheKey(VertexData, IndexData, pMorphTargetData);
                    if (GeometryCacheKey.empty())
                        return FailMesh();

                    GeometryIndexCounts.push_back(IndexData.IndexCount);
                    GeometryCacheKeys.push_back(GeometryCacheKey);
                }

                RadientMeshViewSource MeshView{
                    CanonicalStableViewCI.GetCreateInfo(),
                    GeometryIndexCounts.data(),
                    static_cast<Uint32>(GeometryIndexCounts.size()),
                };
                if (RADIENT_FAILED(MeshView.GetStatus()))
                    return FailMesh(MeshView.GetStatus());

                const std::string MeshCacheKey = MeshView.MakeCacheKey(GeometryCacheKeys);
                if (MeshCacheKey.empty())
                    return FailMesh();

                auto [pMeshPayload, PayloadCreated] =
                    pSelf->m_MeshCache.GetOrCreate(
                        MeshCacheKey.c_str(),
                        [&ConcreteGeometries, &MeshView]() {
                            return MeshPayloadImpl::Create(std::move(ConcreteGeometries),
                                                           MeshView);
                        });

                (void)PayloadCreated;

                if (!pMeshPayload)
                    return FailMesh();

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
    RefCntAutoPtr<MeshAssetImpl> pMeshImpl{pMesh, IID_MeshAssetImpl};
    if (!pMeshImpl)
        return RadientDrawableMeshResolveResult{};

    const RADIENT_STATUS PayloadStatus = pMeshImpl->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return {nullptr, PayloadStatus};

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

namespace
{

template <typename AssetImplType, typename DataInterfaceType>
RADIENT_STATUS GetMeshDataLoadStatus(DataInterfaceType* pData)
{
    const AssetImplType* const pDataAsset = ClassPtrCast<AssetImplType>(pData);
    return pDataAsset != nullptr ?
        pDataAsset->GetLoadStatus() :
        RADIENT_STATUS_INVALID_ARGUMENT;
}

template <typename PayloadType>
const PayloadType* GetMeshGeometryDataPayload(
    IRadientMeshAsset*         pMeshAsset,
    Uint32                     GeometryIndex,
    RefCntAutoPtr<PayloadType> MeshGeometryStorage::* pPayloadMember)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    const MeshStorage& Storage = pMesh->GetStorage();
    if (GeometryIndex >= Storage.Geometries.size())
        return nullptr;

    return (Storage.Geometries[GeometryIndex].*pPayloadMember).RawPtr();
}

} // namespace

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientMeshIndexData* pMeshIndexData)
{
    return GetMeshDataLoadStatus<MeshIndexDataAssetImpl>(pMeshIndexData);
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientMeshVertexData* pMeshVertexData)
{
    return GetMeshDataLoadStatus<MeshVertexDataAssetImpl>(pMeshVertexData);
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientMeshMorphTargetData* pMorphTargetData)
{
    return GetMeshDataLoadStatus<MeshMorphTargetDataAssetImpl>(pMorphTargetData);
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
    return GetMeshGeometryDataPayload(pMeshAsset, GeometryIndex, &MeshGeometryStorage::pIndexDataPayload);
}

const MeshVertexDataPayloadImpl* RadientMeshAssetManager::GetMeshVertexDataPayload(IRadientMeshAsset* pMeshAsset, Uint32 GeometryIndex)
{
    return GetMeshGeometryDataPayload(pMeshAsset, GeometryIndex, &MeshGeometryStorage::pVertexDataPayload);
}

const MeshMorphTargetDataPayloadImpl* RadientMeshAssetManager::GetMeshMorphTargetDataPayload(IRadientMeshAsset* pMeshAsset,
                                                                                             Uint32             GeometryIndex)
{
    return GetMeshGeometryDataPayload(pMeshAsset, GeometryIndex, &MeshGeometryStorage::pMorphTargetDataPayload);
}

const IRadientMeshIndexData* RadientMeshAssetManager::GetMeshIndexData(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (Storage.Geometries.empty())
        return nullptr;

    return Storage.Geometries.front().pIndexDataAsset;
}

const IRadientMeshVertexData* RadientMeshAssetManager::GetMeshVertexData(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshStorage& Storage = pMesh->GetStorage();
    if (Storage.Geometries.empty())
        return nullptr;

    return Storage.Geometries.front().pVertexDataAsset;
}

namespace
{

RADIENT_STATUS CacheTerminalStatus(std::atomic<RADIENT_STATUS>& CachedStatus,
                                   RADIENT_STATUS               Status)
{
    if (Status != RADIENT_STATUS_PENDING)
        CachedStatus.store(Status, std::memory_order_release);
    return Status;
}

RADIENT_STATUS GetMeshGeometryLoadStatus(MeshStorage& Mesh)
{
    RADIENT_STATUS CachedStatus = Mesh.GeometryLoadStatus.load(std::memory_order_acquire);
    if (CachedStatus != RADIENT_STATUS_PENDING)
        return CachedStatus;

    if (Mesh.Geometries.empty())
        return CacheTerminalStatus(Mesh.GeometryLoadStatus, RADIENT_STATUS_INVALID_OPERATION);

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const MeshGeometryStorage& Geometry : Mesh.Geometries)
    {
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
            return CacheTerminalStatus(Mesh.GeometryLoadStatus, RADIENT_STATUS_INVALID_OPERATION);

        const MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
        const MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();
        Status                                  = CombineDependencyStatus(Status, IndexData.LoadStatus.load(std::memory_order_acquire));
        Status                                  = CombineDependencyStatus(Status, VertexData.LoadStatus.load(std::memory_order_acquire));

        if (Geometry.pMorphTargetDataAsset != nullptr ||
            Geometry.pMorphTargetDataPayload != nullptr)
        {
            if (Geometry.pMorphTargetDataAsset == nullptr ||
                Geometry.pMorphTargetDataPayload == nullptr)
            {
                return CacheTerminalStatus(Mesh.GeometryLoadStatus, RADIENT_STATUS_INVALID_OPERATION);
            }

            const MeshMorphTargetDataStorage& MorphTargetData = Geometry.pMorphTargetDataPayload->GetStorage();
            Status                                            = CombineDependencyStatus(Status, MorphTargetData.LoadStatus.load(std::memory_order_acquire));
        }
    }

    return CacheTerminalStatus(Mesh.GeometryLoadStatus, Status);
}

RADIENT_STATUS GetMeshGeometryGPUResourceStatus(MeshStorage& Mesh)
{
    RADIENT_STATUS CachedStatus = Mesh.GeometryGPUResourceStatus.load(std::memory_order_acquire);
    if (CachedStatus != RADIENT_STATUS_PENDING)
        return CachedStatus;

    const RADIENT_STATUS LoadStatus = GetMeshGeometryLoadStatus(Mesh);
    if (LoadStatus != RADIENT_STATUS_OK)
        return CacheTerminalStatus(Mesh.GeometryGPUResourceStatus, LoadStatus);

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const MeshGeometryStorage& Geometry : Mesh.Geometries)
    {
        VERIFY_EXPR(Geometry.pIndexDataPayload != nullptr && Geometry.pVertexDataPayload != nullptr);
        if (Geometry.pIndexDataPayload == nullptr ||
            Geometry.pVertexDataPayload == nullptr)
            return CacheTerminalStatus(Mesh.GeometryGPUResourceStatus, RADIENT_STATUS_INVALID_OPERATION);

        const MeshIndexDataStorage&  IndexData  = Geometry.pIndexDataPayload->GetStorage();
        const MeshVertexDataStorage& VertexData = Geometry.pVertexDataPayload->GetStorage();

        Status = CombineDependencyStatus(Status, IndexData.GPUResourceStatus.load(std::memory_order_acquire));
        Status = CombineDependencyStatus(Status, VertexData.GPUResourceStatus.load(std::memory_order_acquire));

        if (Geometry.pMorphTargetDataPayload != nullptr)
        {
            const MeshMorphTargetDataStorage& MorphTargetData = Geometry.pMorphTargetDataPayload->GetStorage();
            Status                                            = CombineDependencyStatus(Status, MorphTargetData.GPUResourceStatus.load(std::memory_order_acquire));
        }
    }

    return CacheTerminalStatus(Mesh.GeometryGPUResourceStatus, Status);
}

RADIENT_STATUS GetMeshMaterialStatus(MeshStorage& Mesh)
{
    RADIENT_STATUS Status = Mesh.MaterialStatus.load(std::memory_order_acquire);
    if (Status != RADIENT_STATUS_PENDING)
        return Status;

    if (Mesh.Materials.empty())
        return CacheTerminalStatus(Mesh.MaterialStatus, RADIENT_STATUS_OK);

    if (Mesh.Materials.size() != Mesh.DrawableMesh.Primitives.size())
        return CacheTerminalStatus(Mesh.MaterialStatus, RADIENT_STATUS_INVALID_OPERATION);

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

    return CacheTerminalStatus(Mesh.MaterialStatus, Status);
}

RADIENT_STATUS GetMeshMaterialGPUResourceStatus(MeshStorage& Mesh)
{
    RADIENT_STATUS CachedStatus = Mesh.MaterialGPUResourceStatus.load(std::memory_order_acquire);
    if (CachedStatus != RADIENT_STATUS_PENDING)
        return CachedStatus;

    const RADIENT_STATUS MaterialStatus = GetMeshMaterialStatus(Mesh);
    if (MaterialStatus != RADIENT_STATUS_OK)
        return CacheTerminalStatus(Mesh.MaterialGPUResourceStatus, MaterialStatus);

    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    for (IRadientMaterialAsset* pMaterialAsset : Mesh.Materials)
    {
        if (pMaterialAsset == nullptr)
            continue;

        const RADIENT_STATUS MaterialGPUStatus = RadientMaterialAssetManager::GetGPUResourceStatus(pMaterialAsset);
        Status                                 = CombineDependencyStatus(Status, MaterialGPUStatus);
    }

    return CacheTerminalStatus(Mesh.MaterialGPUResourceStatus, Status);
}

RADIENT_STATUS ResolveMeshMaterialDependencies(MeshStorage& Mesh)
{
    if (Mesh.MaterialsResolved.load(std::memory_order_acquire))
        return RADIENT_STATUS_OK;

    RADIENT_STATUS Status = GetMeshMaterialStatus(Mesh);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    Mesh.MaterialsResolved.store(true, std::memory_order_release);
    return RADIENT_STATUS_OK;
}

RadientDrawableMeshResolveResult ResolveDrawableMesh(MeshStorage& Mesh,
                                                     bool         RequireGPUResourcesReady)
{
    RadientDrawableMeshResolveResult Result;

    if (!Mesh.DrawableResolved.load(std::memory_order_acquire))
    {
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

        const RADIENT_STATUS GPUStatus            = GetMeshGeometryGPUResourceStatus(Mesh);
        const bool           CanUsePendingGPUData = GPUStatus == RADIENT_STATUS_PENDING && !RequireGPUResourcesReady;
        const bool           CanUseMissingGPUData = GPUStatus == RADIENT_STATUS_NO_GPU_DATA && !RequireGPUResourcesReady;
        if (GPUStatus != RADIENT_STATUS_OK &&
            !CanUsePendingGPUData &&
            !CanUseMissingGPUData)
        {
            Result.Status = GPUStatus;
            return Result;
        }

        // LoadStatus publishes the non-atomic allocation pointers. Only read them
        // after the acquire loads above have observed OK.
        if (GPUStatus != RADIENT_STATUS_NO_GPU_DATA)
        {
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

                if (Geometry.pMorphTargetDataPayload != nullptr)
                {
                    MeshMorphTargetDataStorage& MorphTargetData = Geometry.pMorphTargetDataPayload->GetStorage();
                    if (MorphTargetData.Data.GetDataSize() != 0)
                    {
                        IBufferSuballocation* const pMorphTargetAllocation = MorphTargetData.pAllocation;
                        if (pMorphTargetAllocation == nullptr)
                        {
                            Result.Status = RADIENT_STATUS_INVALID_OPERATION;
                            return Result;
                        }
                        DrawableGeometry.MorphTargetDataOffset = pMorphTargetAllocation->GetOffset();
                    }
                }
            }
        }

        const RADIENT_STATUS MaterialStatus = ResolveMeshMaterialDependencies(Mesh);
        if (MaterialStatus != RADIENT_STATUS_OK)
        {
            Result.Status = MaterialStatus;
            return Result;
        }

        Mesh.DrawableResolved.store(true, std::memory_order_release);
    }

    if (RequireGPUResourcesReady &&
        !Mesh.DrawableGPUResourcesReady.load(std::memory_order_acquire))
    {
        const RADIENT_STATUS GPUStatus = GetMeshGeometryGPUResourceStatus(Mesh);
        if (GPUStatus != RADIENT_STATUS_OK)
        {
            Result.Status = GPUStatus;
            return Result;
        }

        const RADIENT_STATUS MaterialGPUStatus = GetMeshMaterialGPUResourceStatus(Mesh);
        if (MaterialGPUStatus != RADIENT_STATUS_OK)
        {
            Result.Status = MaterialGPUStatus;
            return Result;
        }

        for (const RadientDrawableMeshGeometry& Geometry : Mesh.DrawableMesh.Geometries)
        {
            if (Geometry.pVertexPool == nullptr)
            {
                Result.Status = RADIENT_STATUS_INVALID_OPERATION;
                return Result;
            }
        }

        Mesh.DrawableGPUResourcesReady.store(true, std::memory_order_release);
    }

    Result.pMesh  = &Mesh.DrawableMesh;
    Result.Status = RADIENT_STATUS_OK;
    return Result;
}

} // namespace

} // namespace Diligent
