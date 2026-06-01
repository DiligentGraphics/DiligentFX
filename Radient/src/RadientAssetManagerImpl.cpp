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

#include "RadientAssetManagerImpl.hpp"

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
    std::string  URI;
    Uint64       Version = 0;
    GLTF::Model* pModel  = nullptr;
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
                                                   RadientAssetReference&       Mesh)
{
    Mesh = {};

    if (!ValidateMesh(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_MESH;
    Record.Name = MeshCI.Name != nullptr ? MeshCI.Name : "";
    Record.URI  = MakeURI("mesh");

    MeshStorage& MeshData = Record.Storage.emplace<MeshStorage>();

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
        Primitive.MaterialVersion   = PrimitiveCI.Material.Version;
        Primitive.MaterialURI       = PrimitiveCI.Material.URI != nullptr ? PrimitiveCI.Material.URI : "";

        MeshData.MeshPrimitives.emplace_back(std::move(Primitive));
    }

    Mesh = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                       RadientAssetReference&           Material)
{
    Material = {};

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_MATERIAL;
    Record.Name = MaterialCI.Name != nullptr ? MaterialCI.Name : "";
    Record.URI  = MakeURI("material");

    MaterialStorage& MaterialData         = Record.Storage.emplace<MaterialStorage>();
    MaterialData.BaseColorFactor          = MaterialCI.BaseColorFactor;
    MaterialData.MetallicFactor           = MaterialCI.MetallicFactor;
    MaterialData.RoughnessFactor          = MaterialCI.RoughnessFactor;
    MaterialData.EmissiveFactor           = MaterialCI.EmissiveFactor;
    MaterialData.AlphaCutoff              = MaterialCI.AlphaCutoff;
    MaterialData.DoubleSided              = MaterialCI.DoubleSided;
    MaterialData.BaseColorTexture         = CopyAssetReference(MaterialCI.BaseColorTexture, MaterialData.BaseColorTextureURI);
    MaterialData.MetallicRoughnessTexture = CopyAssetReference(MaterialCI.MetallicRoughnessTexture, MaterialData.MetallicRoughnessTextureURI);
    MaterialData.NormalTexture            = CopyAssetReference(MaterialCI.NormalTexture, MaterialData.NormalTextureURI);
    MaterialData.OcclusionTexture         = CopyAssetReference(MaterialCI.OcclusionTexture, MaterialData.OcclusionTextureURI);
    MaterialData.EmissiveTexture          = CopyAssetReference(MaterialCI.EmissiveTexture, MaterialData.EmissiveTextureURI);

    Material = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                    RadientAssetReference&        Texture)
{
    Texture = {};

    if (!ValidateTexture(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_TEXTURE;
    Record.URI  = MakeURI("texture");
    Record.Name = LoadInfo.URI;

    TextureStorage& TextureData = Record.Storage.emplace<TextureStorage>();
    TextureData.SourceURI       = LoadInfo.URI;
    TextureData.LoadStatus      = LoadTextureAsset(TextureData.SourceURI,
                                                   LoadInfo.IsSRGB,
                                                   m_pDevice,
                                                   TextureData.pTexture);
    if (RADIENT_FAILED(TextureData.LoadStatus))
        return TextureData.LoadStatus;

    Texture = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::LoadGLTF(const RadientGLTFLoadInfo& LoadInfo,
                                                 RadientAssetReference&     Model)
{
    Model = {};

    if (!ValidateGLTF(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_GLTF_MODEL;
    Record.URI  = MakeURI("gltf");
    Record.Name = LoadInfo.URI;

    GLTFModelStorage& GLTFModelData = Record.Storage.emplace<GLTFModelStorage>();
    GLTFModelData.SourceURI         = LoadInfo.URI;

    if (m_pThreadPool == nullptr)
    {
        const RADIENT_STATUS Status = LoadGLTFModel(GLTFModelData.SourceURI,
                                                    m_pDevice,
                                                    m_pResourceManager,
                                                    m_pUploadManager,
                                                    GLTFModelData.pModel);
        if (Status != RADIENT_STATUS_OK)
            return Status;

        GLTFModelData.LoadStatus = RADIENT_STATUS_OK;
        Model                    = StoreAsset(std::move(Record));
        return RADIENT_STATUS_OK;
    }

    GLTFModelData.LoadStatus = RADIENT_STATUS_PENDING;
    Model                    = StoreAsset(std::move(Record));

    RefCntAutoPtr<RadientAssetManagerImpl> pSelf{this};
    const RadientAssetReference            ModelRef = Model;
    const std::string                      SourceURI{LoadInfo.URI};

    EnqueueAsyncWork(
        m_pThreadPool,
        [pSelf, ModelRef, SourceURI](Uint32) //
        {
            std::unique_ptr<GLTF::Model> pModel;
            const RADIENT_STATUS         Status = LoadGLTFModel(SourceURI,
                                                                pSelf->m_pDevice,
                                                                pSelf->m_pResourceManager,
                                                                pSelf->m_pUploadManager,
                                                                pModel);

            pSelf->CompleteGLTFLoad(ModelRef, std::move(pModel), Status);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return GetGLTFLoadStatus(Model);
}

RADIENT_STATUS RadientAssetManagerImpl::WaitForAssetLoad(const RadientAssetReference& Asset)
{
    for (;;)
    {
        RADIENT_STATUS Status = RADIENT_STATUS_INVALID_OPERATION;
        {
            std::shared_lock<std::shared_mutex> Lock{m_Mutex};
            Status = GetAssetLoadStatusLocked(Asset);
        }

        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        if (m_pThreadPool == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        if (!m_pThreadPool->ProcessTask(0, false))
            std::this_thread::yield();
    }
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMeshFromGLTFMesh(const RadientAssetReference& Model,
                                                               Uint32                       MeshIndex,
                                                               const Char*                  Name,
                                                               RadientAssetReference&       Mesh)
{
    Mesh = {};

    {
        std::shared_lock<std::shared_mutex> Lock{m_Mutex};

        const AssetRecord* pModelRecord = FindAssetLocked(Model);
        if (pModelRecord == nullptr)
            return RADIENT_STATUS_NOT_FOUND;

        if (pModelRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pModelRecord->Storage);
        VERIFY(pGLTFModel != nullptr, "GLTF model asset has unexpected storage");
        if (pGLTFModel == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        if (pGLTFModel->LoadStatus != RADIENT_STATUS_OK)
            return pGLTFModel->LoadStatus;
    }

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_MESH;
    Record.Name = Name != nullptr ? Name : "";
    Record.URI  = MakeURI("mesh");

    GLTFMeshStorage& GLTFMeshData = Record.Storage.emplace<GLTFMeshStorage>();
    GLTFMeshData.Model            = CopyAssetReference(Model, GLTFMeshData.ModelURI);
    GLTFMeshData.MeshIndex        = MeshIndex;

    Mesh = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::GetMeshGLTFSource(const RadientAssetReference& Mesh,
                                                          RadientAssetReference&       Model,
                                                          Uint32&                      MeshIndex) const
{
    Model     = {};
    MeshIndex = ~0u;

    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const AssetRecord* pRecord = FindAssetLocked(Mesh);
    if (pRecord == nullptr)
        return RADIENT_STATUS_NOT_FOUND;

    if (pRecord->Type != RADIENT_ASSET_TYPE_MESH)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFMeshStorage* pGLTFMesh = std::get_if<GLTFMeshStorage>(&pRecord->Storage);
    if (pGLTFMesh == nullptr || pGLTFMesh->Model.URI == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    Model     = pGLTFMesh->Model;
    MeshIndex = pGLTFMesh->MeshIndex;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFSourceURI(const RadientAssetReference& Model,
                                                         const Char*&                 SourceURI) const
{
    SourceURI = nullptr;

    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const AssetRecord* pRecord = FindAssetLocked(Model);
    if (pRecord == nullptr)
        return RADIENT_STATUS_NOT_FOUND;

    if (pRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pRecord->Storage);
    VERIFY(pGLTFModel != nullptr, "GLTF model asset has unexpected storage");
    if (pGLTFModel == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    SourceURI = pGLTFModel->SourceURI.c_str();
    return RADIENT_STATUS_OK;
}

const GLTF::Model* RadientAssetManagerImpl::GetGLTFModel(const RadientAssetReference& Model,
                                                         bool                         RequireGPUResourcesReady) const
{
    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const AssetRecord* pRecord = FindAssetLocked(Model);
    if (pRecord == nullptr || pRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL)
        return nullptr;

    const GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pRecord->Storage);
    if (pGLTFModel == nullptr ||
        pGLTFModel->LoadStatus != RADIENT_STATUS_OK ||
        (RequireGPUResourcesReady && !pGLTFModel->GPUResourcesReady))
    {
        return nullptr;
    }

    return pGLTFModel->pModel.get();
}

RADIENT_STATUS RadientAssetManagerImpl::GetGLTFLoadStatus(const RadientAssetReference& Model) const
{
    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const AssetRecord* pRecord = FindAssetLocked(Model);
    if (pRecord == nullptr)
        return RADIENT_STATUS_NOT_FOUND;

    if (pRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pRecord->Storage);
    VERIFY(pGLTFModel != nullptr, "GLTF model asset has unexpected storage");
    return pGLTFModel != nullptr ? pGLTFModel->LoadStatus : RADIENT_STATUS_INVALID_OPERATION;
}

ITextureView* RadientAssetManagerImpl::GetTextureSRV(const RadientAssetReference& Texture) const
{
    std::shared_lock<std::shared_mutex> Lock{m_Mutex};

    const AssetRecord*    pRecord  = FindAssetLocked(Texture);
    const TextureStorage* pTexture = pRecord != nullptr ?
        std::get_if<TextureStorage>(&pRecord->Storage) :
        nullptr;
    if (pRecord == nullptr ||
        pRecord->Type != RADIENT_ASSET_TYPE_TEXTURE ||
        pTexture == nullptr ||
        pTexture->LoadStatus != RADIENT_STATUS_OK ||
        pTexture->pTexture == nullptr)
    {
        return nullptr;
    }

    return pTexture->pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
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
        std::shared_lock<std::shared_mutex> Lock{m_Mutex};

        for (const auto& AssetIt : m_Assets)
        {
            const AssetRecord*      pRecord    = AssetIt.second.get();
            const GLTFModelStorage* pGLTFModel = pRecord != nullptr ?
                std::get_if<GLTFModelStorage>(&pRecord->Storage) :
                nullptr;
            if (pRecord == nullptr ||
                pRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL ||
                pGLTFModel == nullptr ||
                pGLTFModel->LoadStatus != RADIENT_STATUS_OK ||
                pGLTFModel->GPUResourcesReady ||
                pGLTFModel->pModel == nullptr)
            {
                continue;
            }

            ModelsToPrepare.push_back({pRecord->URI, pRecord->Version, pGLTFModel->pModel.get()});
        }
    }

    if (m_pUploadManager != nullptr)
        m_pUploadManager->RenderThreadUpdate(pContext);

    if (m_pResourceManager != nullptr)
        m_pResourceManager->UpdateAllResources(pDevice, pContext);

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const GLTFModelToPrepare& Model : ModelsToPrepare)
    {
        if (!Model.pModel->PrepareGPUResources(pDevice, pContext))
        {
            Status = RADIENT_STATUS_OUT_OF_DATE;
            continue;
        }

        RadientAssetReference ModelRef;
        ModelRef.URI     = Model.URI.c_str();
        ModelRef.Version = Model.Version;

        std::unique_lock<std::shared_mutex> Lock{m_Mutex};

        AssetRecord*      pRecord    = FindAssetLocked(ModelRef);
        GLTFModelStorage* pGLTFModel = pRecord != nullptr ?
            std::get_if<GLTFModelStorage>(&pRecord->Storage) :
            nullptr;
        if (pRecord != nullptr &&
            pRecord->Type == RADIENT_ASSET_TYPE_GLTF_MODEL &&
            pGLTFModel != nullptr &&
            pGLTFModel->pModel.get() == Model.pModel)
        {
            pGLTFModel->GPUResourcesReady = true;
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

RadientAssetManagerImpl::AssetRecord* RadientAssetManagerImpl::FindAssetLocked(const RadientAssetReference& Ref)
{
    return const_cast<AssetRecord*>(static_cast<const RadientAssetManagerImpl*>(this)->FindAssetLocked(Ref));
}

const RadientAssetManagerImpl::AssetRecord* RadientAssetManagerImpl::FindAssetLocked(const RadientAssetReference& Ref) const
{
    if (Ref.URI == nullptr || *Ref.URI == 0 || Ref.Version == 0)
        return nullptr;

    const auto It = m_Assets.find(HashMapStringKey{Ref.URI});
    if (It == m_Assets.end())
        return nullptr;

    const AssetRecord* pRecord = It->second.get();
    return pRecord != nullptr && pRecord->Version == Ref.Version ? pRecord : nullptr;
}

RADIENT_STATUS RadientAssetManagerImpl::GetAssetLoadStatusLocked(const RadientAssetReference& Asset) const
{
    const AssetRecord* pRecord = FindAssetLocked(Asset);
    if (pRecord == nullptr)
        return RADIENT_STATUS_NOT_FOUND;

    switch (pRecord->Type)
    {
        case RADIENT_ASSET_TYPE_GLTF_MODEL:
        {
            const GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pRecord->Storage);
            VERIFY(pGLTFModel != nullptr, "GLTF model asset has unexpected storage");
            return pGLTFModel != nullptr ? pGLTFModel->LoadStatus : RADIENT_STATUS_INVALID_OPERATION;
        }

        case RADIENT_ASSET_TYPE_TEXTURE:
        {
            const TextureStorage* pTexture = std::get_if<TextureStorage>(&pRecord->Storage);
            VERIFY(pTexture != nullptr, "Texture asset has unexpected storage");
            return pTexture != nullptr ? pTexture->LoadStatus : RADIENT_STATUS_INVALID_OPERATION;
        }

        default:
            return RADIENT_STATUS_OK;
    }
}

std::string RadientAssetManagerImpl::MakeURI(const char* Type)
{
    std::unique_lock<std::shared_mutex> Lock{m_Mutex};

    const RadientHandle AssetID = m_NextAssetID++;
    return std::string{"radient://session/"} + Type + "/" + std::to_string(AssetID);
}

RadientAssetReference RadientAssetManagerImpl::StoreAsset(AssetRecord&& Record)
{
    std::unique_lock<std::shared_mutex> Lock{m_Mutex};

    auto pRecord = std::make_unique<AssetRecord>(std::move(Record));
    FixupAssetRecord(*pRecord);

    RadientAssetReference Ref;
    Ref.URI     = pRecord->URI.c_str();
    Ref.Version = pRecord->Version;

    const auto InsertResult = m_Assets.emplace(HashMapStringKey{pRecord->URI.c_str(), true}, std::move(pRecord));
    VERIFY(InsertResult.second, "Asset URI already exists");

    return Ref;
}

void RadientAssetManagerImpl::FixupAssetRecord(AssetRecord& Record)
{
    if (MaterialStorage* pMaterial = std::get_if<MaterialStorage>(&Record.Storage))
    {
        FixupAssetReference(pMaterial->BaseColorTexture, pMaterial->BaseColorTextureURI);
        FixupAssetReference(pMaterial->MetallicRoughnessTexture, pMaterial->MetallicRoughnessTextureURI);
        FixupAssetReference(pMaterial->NormalTexture, pMaterial->NormalTextureURI);
        FixupAssetReference(pMaterial->OcclusionTexture, pMaterial->OcclusionTextureURI);
        FixupAssetReference(pMaterial->EmissiveTexture, pMaterial->EmissiveTextureURI);
    }
    else if (GLTFMeshStorage* pGLTFMesh = std::get_if<GLTFMeshStorage>(&Record.Storage))
    {
        FixupAssetReference(pGLTFMesh->Model, pGLTFMesh->ModelURI);
    }
}

void RadientAssetManagerImpl::FixupAssetReference(RadientAssetReference& Ref, const std::string& URIStorage)
{
    Ref.URI = Ref.URI != nullptr ? URIStorage.c_str() : nullptr;
}

RadientAssetReference RadientAssetManagerImpl::CopyAssetReference(const RadientAssetReference& Ref, std::string& URIStorage)
{
    URIStorage = Ref.URI != nullptr ? Ref.URI : "";

    RadientAssetReference Result = Ref;
    Result.URI                   = Ref.URI != nullptr ? URIStorage.c_str() : nullptr;
    return Result;
}

void RadientAssetManagerImpl::CompleteGLTFLoad(const RadientAssetReference& Model,
                                               std::unique_ptr<GLTF::Model> pModel,
                                               RADIENT_STATUS               Status)
{
    std::unique_lock<std::shared_mutex> Lock{m_Mutex};

    AssetRecord* pRecord = FindAssetLocked(Model);
    if (pRecord == nullptr || pRecord->Type != RADIENT_ASSET_TYPE_GLTF_MODEL)
        return;

    GLTFModelStorage* pGLTFModel = std::get_if<GLTFModelStorage>(&pRecord->Storage);
    VERIFY(pGLTFModel != nullptr, "GLTF model asset has unexpected storage");
    if (pGLTFModel == nullptr)
        return;

    pGLTFModel->pModel     = std::move(pModel);
    pGLTFModel->LoadStatus = Status;
}

} // namespace Diligent
