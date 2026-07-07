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
#include "Assets/RadientDrawableMeshConverter.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshSource.hpp"
#include "Assets/RadientMeshViewSource.hpp"
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
#include <variant>
#include <vector>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MeshAssetImpl   = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};
static constexpr INTERFACE_ID IID_MeshGPUDataImpl = {0xdb8786a7, 0xe63e, 0x4128, {0x92, 0xce, 0x22, 0x86, 0xa4, 0x76, 0x9d, 0x14}};

} // namespace

class MeshGPUData final : public RadientAssetPayloadImpl<std::monostate, MeshGPUData, IRadientMeshGPUData>
{
public:
    using TBase = RadientAssetPayloadImpl<std::monostate, MeshGPUData, IRadientMeshGPUData>;

    MeshGPUData(IReferenceCounters*     pRefCounters,
                RADIENT_STATUS          InitLoadStatus,
                std::string             CacheKey,
                Uint32                  IndexCount,
                PBR_Renderer::PSO_FLAGS VertexAttribFlags) :
        TBase{pRefCounters},
        CacheKey{std::move(CacheKey)},
        IndexCount{IndexCount},
        VertexAttribFlags{VertexAttribFlags},
        LoadStatus{InitLoadStatus},
        GPUResourceStatus{InitLoadStatus}
    {
    }
    ~MeshGPUData() = default;

    static RefCntAutoPtr<MeshGPUData> Create(RADIENT_STATUS          InitLoadStatus,
                                             std::string             CacheKey,
                                             Uint32                  IndexCount,
                                             PBR_Renderer::PSO_FLAGS VertexAttribFlags)
    {
        return TBase::Create(InitLoadStatus, std::move(CacheKey), IndexCount, VertexAttribFlags);
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

    // clang-format off
    MeshGPUData           (MeshGPUData&& Rhs)  = delete;
    MeshGPUData& operator=(MeshGPUData&& Rhs)  = delete;
    MeshGPUData           (const MeshGPUData&) = delete;
    MeshGPUData& operator=(const MeshGPUData&) = delete;
    // clang-format on

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_MeshGPUDataImpl)
        {
            *reinterpret_cast<MeshGPUData**>(ppInterface) = this;
            AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }

    RefCntAutoPtr<IBufferSuballocation>  pIndexAllocation;
    RefCntAutoPtr<IVertexPoolAllocation> pVertexAllocation;

    const std::string             CacheKey;
    const Uint32                  IndexCount        = 0;
    const PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    std::atomic<RADIENT_STATUS> GPUResourceStatus{RADIENT_STATUS_OK};
    std::atomic_bool            GPUResourcesReady{false};
    std::atomic<Uint32>         PendingUploads{0};
};

namespace
{

struct MeshGeometryStorage
{
    RefCntAutoPtr<MeshGPUData> pGPUData;
};

struct MeshStorage
{
    MeshStorage(std::vector<RefCntAutoPtr<MeshGPUData>> pMeshGPUData,
                const RadientMeshViewSource&            View);

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
    RefCntAutoPtr<MeshGPUData>   pGPUData;
    RefCntAutoPtr<IRenderDevice> pDevice;

    RefCntAutoPtr<IBufferSuballocation> pIndexAllocation;
};

struct MeshVertexBufferCopyData
{
    RefCntAutoPtr<MeshGPUData>   pGPUData;
    RefCntAutoPtr<IRenderDevice> pDevice;

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

void UpdateMeshUploadProgress(MeshGPUData& GPUData,
                              bool         CopyScheduled)
{
    if (!CopyScheduled)
        GPUData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);

    const Uint32 PrevPendingUploads = GPUData.PendingUploads.fetch_sub(1, std::memory_order_acq_rel);
    VERIFY_EXPR(PrevPendingUploads > 0);
    if (PrevPendingUploads == 1)
    {
        const RADIENT_STATUS GPUStatus = GPUData.GPUResourceStatus.load(std::memory_order_acquire);
        GPUData.SetGPUResourceStatus(GPUStatus == RADIENT_STATUS_PENDING ? RADIENT_STATUS_OK : GPUStatus);
    }
}

MeshStorage::MeshStorage(std::vector<RefCntAutoPtr<MeshGPUData>> pMeshGPUData,
                         const RadientMeshViewSource&            View)
{
    Geometries.reserve(pMeshGPUData.size());
    for (RefCntAutoPtr<MeshGPUData>& pGPUData : pMeshGPUData)
        Geometries.push_back(MeshGeometryStorage{std::move(pGPUData)});

    if (Geometries.empty())
    {
        LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
        return;
    }

    DrawableMesh.Geometries.reserve(Geometries.size());
    for (const MeshGeometryStorage& Geometry : Geometries)
    {
        const MeshGPUData* const pGPUData = Geometry.pGPUData;
        if (pGPUData == nullptr)
        {
            LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
            return;
        }

        if (pGPUData->IndexCount == 0)
        {
            LoadStatus.store(RADIENT_STATUS_INVALID_ARGUMENT, std::memory_order_release);
            return;
        }

        DrawableMesh.Geometries.push_back(RadientDrawableMeshGeometry{
            nullptr,
            pGPUData->VertexAttribFlags,
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

RADIENT_STATUS InitializeMeshGPUData(GLTF::ResourceManager*   pResourceManager,
                                     const RadientMeshSource& Source,
                                     MeshGPUData&             GPUData)
{
    if (pResourceManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (Source.GetIndexDataSize() == 0 ||
        Source.GetVertexCount() == 0 ||
        Source.GetVertexBufferCount() == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    GPUData.pIndexAllocation = pResourceManager->AllocateIndices(Source.GetIndexDataSize(),
                                                                 alignof(Uint32));
    if (GPUData.pIndexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const Uint32                           VertexBufferCount = Source.GetVertexBufferCount();
    GLTF::ResourceManager::VertexLayoutKey LayoutKey;
    LayoutKey.Elements.reserve(VertexBufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
        LayoutKey.Elements.emplace_back(Source.GetVertexStride(BufferIndex), BIND_VERTEX_BUFFER);

    GPUData.pVertexAllocation = pResourceManager->AllocateVertices(LayoutKey, Source.GetVertexCount());
    if (GPUData.pVertexAllocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    GPUData.GPUResourcesReady.store(false, std::memory_order_release);

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

    VERIFY_EXPR(Data->pGPUData != nullptr);
    if (Data->pGPUData != nullptr)
        UpdateMeshUploadProgress(*Data->pGPUData, CopyScheduled);
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

    VERIFY_EXPR(Data->pGPUData != nullptr);
    if (Data->pGPUData != nullptr)
        UpdateMeshUploadProgress(*Data->pGPUData, CopyScheduled);
}

void ScheduleMeshIndexUpload(IGPUUploadManager*       pUploadManager,
                             IRenderDevice*           pDevice,
                             const RadientMeshSource& Source,
                             MeshGPUData*             pGPUData)
{
    std::unique_ptr<MeshIndexBufferCopyData> pCopyData{new MeshIndexBufferCopyData{}};
    pCopyData->pGPUData         = pGPUData;
    pCopyData->pDevice          = pDevice;
    pCopyData->pIndexAllocation = pGPUData->pIndexAllocation;

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
                              const RadientMeshSource& Source,
                              MeshGPUData*             pGPUData,
                              Uint32                   VertexBufferIndex)
{
    std::unique_ptr<MeshVertexBufferCopyData> pCopyData{new MeshVertexBufferCopyData{}};
    pCopyData->pGPUData          = pGPUData;
    pCopyData->pDevice           = pDevice;
    pCopyData->pVertexAllocation = pGPUData->pVertexAllocation;
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
                                     const RadientMeshSource& Source,
                                     MeshGPUData&             GPUData)
{
    if (pDevice == nullptr ||
        pUploadManager == nullptr ||
        GPUData.pIndexAllocation == nullptr ||
        GPUData.pVertexAllocation == nullptr ||
        Source.GetIndexCount() == 0)
    {
        GPUData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const Uint32 VertexBufferCount = Source.GetVertexBufferCount();
    if (VertexBufferCount == 0)
    {
        GPUData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    Uint32 UploadCount = 1;
    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (Source.IsVertexBufferActive(BufferIndex))
        {
            if (Source.GetVertexBufferDataSize(BufferIndex) == 0)
            {
                GPUData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            ++UploadCount;
        }
    }

    GPUData.PendingUploads.store(UploadCount, std::memory_order_release);
    GPUData.SetLoadStatus(RADIENT_STATUS_OK);
    GPUData.SetGPUResourceStatus(RADIENT_STATUS_PENDING);

    ScheduleMeshIndexUpload(pUploadManager, pDevice, Source, &GPUData);

    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (!Source.IsVertexBufferActive(BufferIndex))
            continue;

        ScheduleMeshVertexUpload(pUploadManager, pDevice, Source, &GPUData, BufferIndex);
    }

    return RADIENT_STATUS_OK;
}

void CreateMeshGPUDataFromSource(const RadientMeshSource& Source,
                                 MeshGPUData&             GPUData,
                                 IRenderDevice*           pDevice,
                                 GLTF::ResourceManager*   pResourceManager,
                                 IGPUUploadManager*       pUploadManager)
{
    if (Source.GetIndexDataSize() == 0 ||
        Source.GetVertexCount() == 0 ||
        Source.GetVertexBufferCount() == 0)
    {
        GPUData.SetStatus(RADIENT_STATUS_INVALID_ARGUMENT);
        return;
    }

    GPUData.SetLoadStatus(RADIENT_STATUS_OK);

    if (pDevice == nullptr)
    {
        GPUData.SetGPUResourceStatus(RADIENT_STATUS_NO_GPU_DATA);
        return;
    }

    if (pResourceManager == nullptr || pUploadManager == nullptr)
    {
        GPUData.SetGPUResourceStatus(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    RADIENT_STATUS Status = InitializeMeshGPUData(pResourceManager, Source, GPUData);
    if (RADIENT_FAILED(Status))
    {
        GPUData.SetGPUResourceStatus(Status);
        return;
    }

    Status = ScheduleMeshGPUUpload(pDevice,
                                   pUploadManager,
                                   Source,
                                   GPUData);
    if (RADIENT_FAILED(Status))
        GPUData.SetGPUResourceStatus(Status);
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

RadientDrawableMeshResolveResult ResolveDrawableMesh(MeshStorage& Mesh,
                                                     bool         RequireGPUResourcesReady);
RadientDrawableMeshResolveResult ResolveDrawableMesh(const GLTFMeshStorage& Mesh,
                                                     bool                   RequireGPUResourcesReady);

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

    RefCntAutoPtr<IRadientMeshGPUData> pGPUData;
    const RADIENT_STATUS               Status = CreateMeshGPUData(ThreadPool, std::make_unique<RadientMeshSource>(MeshCI), pGPUData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pGPUData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    IRadientMeshGPUData* const pGPUDataArray[] = {pGPUData.RawPtr()};
    return CreateMeshView(pGPUDataArray, 1, ViewCI, ppMesh);
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshGPUData(IThreadPool&                       ThreadPool,
                                                          std::unique_ptr<RadientMeshSource> pMeshSource,
                                                          IRadientMeshGPUData**              ppMeshGPUData)
{
    if (ppMeshGPUData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMeshGPUData == nullptr, "Output mesh GPU data pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMeshGPUData = nullptr;

    if (pMeshSource == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS SourceStatus = pMeshSource->GetStatus();
    if (RADIENT_FAILED(SourceStatus))
        return SourceStatus;

    if (!pMeshSource->HasVertexAttributes())
    {
        const RADIENT_STATUS Status =
            pMeshSource->SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                             static_cast<Uint32>(GLTF::DefaultVertexAttributes.size()));
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (pMeshSource->GetIndexDataSize() == 0 ||
        pMeshSource->GetVertexCount() == 0 ||
        pMeshSource->GetVertexBufferCount() == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::string MeshSourceCacheKey = pMeshSource->MakeCacheKey();
    if (MeshSourceCacheKey.empty())
        return RADIENT_STATUS_INVALID_OPERATION;

    const Uint32                  IndexCount        = pMeshSource->GetIndexCount();
    const PBR_Renderer::PSO_FLAGS VertexAttribFlags = pMeshSource->GetVertexAttribFlags();

    auto MeshGPUDataResult =
        m_MeshGPUDataCache.GetOrCreate(
            MeshSourceCacheKey.c_str(),
            [MeshSourceCacheKey, IndexCount, VertexAttribFlags]() {
                return MeshGPUData::Create(RADIENT_STATUS_PENDING,
                                           MeshSourceCacheKey,
                                           IndexCount,
                                           VertexAttribFlags);
            });

    RefCntAutoPtr<MeshGPUData> pGPUData       = std::move(MeshGPUDataResult.first);
    const bool                 GPUDataCreated = MeshGPUDataResult.second;

    if (pGPUData == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const RADIENT_STATUS CurrentStatus = pGPUData->LoadStatus.load(std::memory_order_acquire);

    if (GPUDataCreated)
    {
        EnqueueAsyncWork(
            &ThreadPool,
            [pSelf       = shared_from_this(),
             pMeshSource = std::move(pMeshSource),
             pGPUData](Uint32) mutable //
            {
                RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = pSelf->m_WeakResourceManager.Lock();
                RefCntAutoPtr<IGPUUploadManager>     pUploadManager   = pSelf->m_WeakUploadManager.Lock();

                CreateMeshGPUDataFromSource(*pMeshSource,
                                            *pGPUData,
                                            pSelf->m_pDevice,
                                            pResourceManager,
                                            pUploadManager);

                return ASYNC_TASK_STATUS_COMPLETE;
            });
    }

    *ppMeshGPUData = pGPUData.Detach();
    return CurrentStatus;
}

RADIENT_STATUS RadientMeshAssetManager::CreateMeshView(IRadientMeshGPUData* const*      ppMeshGPUData,
                                                       Uint32                           MeshGPUDataCount,
                                                       const RadientMeshViewCreateInfo& ViewCI,
                                                       IRadientMeshAsset**              ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMesh == nullptr, "Output mesh pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMesh = nullptr;

    if (ppMeshGPUData == nullptr || MeshGPUDataCount == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::vector<RefCntAutoPtr<MeshGPUData>> ConcreteGPUData;
    ConcreteGPUData.reserve(MeshGPUDataCount);
    std::vector<Uint32> GeometryIndexCounts;
    GeometryIndexCounts.reserve(MeshGPUDataCount);
    std::vector<std::string> MeshSourceCacheKeys;
    MeshSourceCacheKeys.reserve(MeshGPUDataCount);

    for (Uint32 GeometryIndex = 0; GeometryIndex < MeshGPUDataCount; ++GeometryIndex)
    {
        RefCntAutoPtr<MeshGPUData> pGPUData{ppMeshGPUData[GeometryIndex], IID_MeshGPUDataImpl};
        if (pGPUData == nullptr ||
            pGPUData->IndexCount == 0 ||
            pGPUData->CacheKey.empty())
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        GeometryIndexCounts.push_back(pGPUData->IndexCount);
        MeshSourceCacheKeys.push_back(pGPUData->CacheKey);
        ConcreteGPUData.emplace_back(std::move(pGPUData));
    }

    RadientMeshViewSource MeshView{
        ViewCI,
        GeometryIndexCounts.data(),
        static_cast<Uint32>(GeometryIndexCounts.size())};
    if (RADIENT_FAILED(MeshView.GetStatus()))
        return MeshView.GetStatus();

    const std::string MeshCacheKey = MeshView.MakeCacheKey(MeshSourceCacheKeys);
    if (MeshCacheKey.empty())
        return RADIENT_STATUS_INVALID_OPERATION;

    auto [pMeshPayload, PayloadCreated] =
        m_MeshCache.GetOrCreate(
            MeshCacheKey.c_str(),
            [&ConcreteGPUData, &MeshView]() {
                return MeshPayloadImpl::Create(std::in_place_type<MeshStorage>,
                                               ConcreteGPUData,
                                               MeshView);
            });

    (void)PayloadCreated;

    if (!pMeshPayload)
        return RADIENT_STATUS_INVALID_OPERATION;

    RefCntAutoPtr<MeshAssetImpl> pMeshAsset =
        MeshAssetImpl::Create(MakeRadientAssetURI("mesh", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)),
                              std::move(pMeshPayload));
    VERIFY_EXPR(pMeshAsset != nullptr);
    if (!pMeshAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    pMeshAsset->QueryInterface(IID_RadientMeshAsset, ppMesh);
    return pMeshAsset->GetPayloadStatus();
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

                return MeshPayloadImpl::Create(std::in_place_type<GLTFMeshStorage>, std::move(GLTFMeshData));
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

    MeshAssetStorage& Storage = pMeshImpl->GetStorage();
    if (MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
        return ResolveDrawableMesh(*pMeshStorage, RequireGPUResourcesReady);
    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return ResolveDrawableMesh(*pGLTFMeshStorage, RequireGPUResourcesReady);

    return Result;
}

RADIENT_STATUS RadientMeshAssetManager::GetLoadStatus(IRadientAsset* pMeshAsset)
{
    MeshAssetImpl* pMesh = ClassPtrCast<MeshAssetImpl>(pMeshAsset);
    if (!pMesh)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pMesh->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    MeshAssetStorage& Storage = pMesh->GetStorage();
    if (MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
    {
        const RADIENT_STATUS ViewStatus = pMeshStorage->LoadStatus.load(std::memory_order_acquire);
        if (ViewStatus != RADIENT_STATUS_OK)
            return ViewStatus;

        const RADIENT_STATUS MeshStatus = GetMeshGeometryLoadStatus(*pMeshStorage);
        if (MeshStatus != RADIENT_STATUS_OK)
            return MeshStatus;

        return GetMeshMaterialStatus(*pMeshStorage);
    }

    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return pGLTFMeshStorage->pModel ? RadientAssetManagerImpl::GetGLTFLoadStatus(pGLTFMeshStorage->pModel) : RADIENT_STATUS_INVALID_OPERATION;

    return RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientMeshAssetManager::GetGPUResourceStatus(IRadientAsset* pMeshAsset)
{
    MeshAssetImpl* pMesh = ClassPtrCast<MeshAssetImpl>(pMeshAsset);
    if (!pMesh)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pMesh->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    MeshAssetStorage& Storage = pMesh->GetStorage();
    if (MeshStorage* pMeshStorage = std::get_if<MeshStorage>(&Storage))
    {
        const RADIENT_STATUS ViewStatus = pMeshStorage->LoadStatus.load(std::memory_order_acquire);
        if (ViewStatus != RADIENT_STATUS_OK)
            return ViewStatus;

        const RADIENT_STATUS MeshStatus = GetMeshGeometryLoadStatus(*pMeshStorage);
        if (MeshStatus != RADIENT_STATUS_OK)
            return MeshStatus;

        RADIENT_STATUS Status = GetMeshGeometryGPUResourceStatus(*pMeshStorage);
        Status                = CombineDependencyStatus(Status, GetMeshMaterialGPUResourceStatus(*pMeshStorage));
        return Status;
    }

    if (const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&Storage))
        return pGLTFMeshStorage->pModel ? RadientAssetManagerImpl::GetGLTFGPUResourceStatus(pGLTFMeshStorage->pModel) : RADIENT_STATUS_INVALID_OPERATION;

    return RADIENT_STATUS_INVALID_OPERATION;
}

const MeshPayloadImpl* RadientMeshAssetManager::GetMeshPayload(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    return pMesh ? pMesh->GetPayload().RawPtr() : nullptr;
}

const IRadientMeshGPUData* RadientMeshAssetManager::GetMeshGPUData(IRadientMeshAsset* pMeshAsset)
{
    RefCntAutoPtr<MeshAssetImpl> pMesh = MeshAssetImpl::ResolveAsset(pMeshAsset);
    if (!pMesh)
        return nullptr;

    MeshAssetStorage& Storage      = pMesh->GetStorage();
    MeshStorage*      pMeshStorage = std::get_if<MeshStorage>(&Storage);
    if (pMeshStorage == nullptr || pMeshStorage->Geometries.empty())
        return nullptr;

    return pMeshStorage->Geometries.front().pGPUData.RawPtr();
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
        if (Geometry.pGPUData == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        Status = CombineDependencyStatus(Status, Geometry.pGPUData->LoadStatus.load(std::memory_order_acquire));
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
        VERIFY_EXPR(Geometry.pGPUData != nullptr);
        if (Geometry.pGPUData == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        Status = CombineDependencyStatus(Status, Geometry.pGPUData->GPUResourceStatus.load(std::memory_order_acquire));
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

    const RADIENT_STATUS GPUStatus = GetMeshGeometryGPUResourceStatus(Mesh);
    if (GPUStatus != RADIENT_STATUS_OK &&
        (GPUStatus != RADIENT_STATUS_PENDING || RequireGPUResourcesReady))
    {
        Result.Status = GPUStatus;
        return Result;
    }

    // LoadStatus publishes the non-atomic allocation pointers. Only read them
    // after the acquire loads above have observed OK.
    for (size_t GeometryIndex = 0; GeometryIndex < Mesh.Geometries.size(); ++GeometryIndex)
    {
        MeshGPUData* const pGPUData = Mesh.Geometries[GeometryIndex].pGPUData;
        if (pGPUData == nullptr)
        {
            Result.Status = RADIENT_STATUS_INVALID_OPERATION;
            return Result;
        }

        IBufferSuballocation* const  pIndexAllocation  = pGPUData->pIndexAllocation;
        IVertexPoolAllocation* const pVertexAllocation = pGPUData->pVertexAllocation;
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

    if (RequireGPUResourcesReady)
    {
        const RADIENT_STATUS GPUStatus = RadientAssetManagerImpl::GetGLTFGPUResourceStatus(Mesh.pModel);
        if (GPUStatus != RADIENT_STATUS_OK)
        {
            Result.Status = GPUStatus;
            return Result;
        }
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
