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

#include <cstring>
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

std::unique_ptr<GLTF::Model> LoadGLTFModel(const std::string&     SourceURI,
                                           IRenderDevice*         pDevice,
                                           GLTF::ResourceManager* pResourceManager,
                                           IGPUUploadManager*     pUploadManager)
{
    try
    {
        GLTF::ModelCreateInfo ModelCI{SourceURI.c_str()};
        ModelCI.pResourceManager = pResourceManager;
        ModelCI.pUploadMgr       = pUploadManager;
        return std::make_unique<GLTF::Model>(pDevice, nullptr, ModelCI);
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "': ", Error.what());
        return {};
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", SourceURI, "'");
        return {};
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

RadientAssetManagerImpl::GLTFModelStorage::GLTFModelStorage(GLTFModelStorage&& Rhs) noexcept :
    SourceURI{std::move(Rhs.SourceURI)},
    pModel{std::move(Rhs.pModel)},
    VertexAttribFlags{Rhs.VertexAttribFlags},
    LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
    GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
    GPUUpdateQueued{Rhs.GPUUpdateQueued.load(std::memory_order_relaxed)}
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

    MeshData.VertexData.Positions    = CopyArray(MeshCI.pPositions, MeshCI.VertexCount);
    MeshData.VertexData.Normals      = CopyArray(MeshCI.pNormals, MeshCI.VertexCount);
    MeshData.VertexData.Tangents     = CopyArray(MeshCI.pTangents, MeshCI.VertexCount);
    MeshData.VertexData.TexCoords0   = CopyArray(MeshCI.pTexCoords0, MeshCI.VertexCount);
    MeshData.VertexData.Colors0      = CopyArray(MeshCI.pColors0, MeshCI.VertexCount);
    MeshData.VertexData.BoneIndices0 = CopyArray(MeshCI.pBoneIndices0, MeshCI.VertexCount);
    MeshData.VertexData.BoneWeights0 = CopyArray(MeshCI.pBoneWeights0, MeshCI.VertexCount);

    MeshData.IndexBuffer.IndexType  = MeshCI.IndexType;
    MeshData.IndexBuffer.IndexCount = MeshCI.IndexCount;
    if (MeshCI.IndexCount != 0)
    {
        const size_t IndexSize =
            MeshCI.IndexType == RADIENT_INDEX_TYPE_UINT16 ? sizeof(Uint16) : sizeof(Uint32);
        const Uint8* pIndexData = static_cast<const Uint8*>(MeshCI.pIndices);
        MeshData.IndexBuffer.Indices.assign(pIndexData, pIndexData + MeshCI.IndexCount * IndexSize);
    }

    MeshData.MeshPrimitives.reserve(MeshCI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];

        MeshPrimitiveStorage Primitive;
        Primitive.Name       = PrimitiveCI.Name != nullptr ? PrimitiveCI.Name : "";
        Primitive.FirstIndex = PrimitiveCI.FirstIndex;
        Primitive.IndexCount = PrimitiveCI.IndexCount;
        Primitive.pMaterial  = PrimitiveCI.pMaterial;

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
        GLTFModelData.pModel = LoadGLTFModel(GLTFModelData.SourceURI,
                                             m_pDevice,
                                             m_pResourceManager,
                                             m_pUploadManager);
        if (GLTFModelData.pModel == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        GLTFModelData.VertexAttribFlags = GetVertexAttribFlags(*GLTFModelData.pModel);
        GLTFModelData.LoadStatus.store(RADIENT_STATUS_OK);
        GLTFModelData.GPUUpdateQueued.store(true);
        *ppModel = StoreAsset<IRadientSceneAsset, SceneAssetImpl>("gltf", LoadInfo.URI, std::move(GLTFModelData));

        m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<IRadientSceneAsset>{*ppModel});

        return RADIENT_STATUS_OK;
    }

    GLTFModelData.LoadStatus.store(RADIENT_STATUS_PENDING);
    *ppModel = StoreAsset<IRadientSceneAsset, SceneAssetImpl>("gltf", LoadInfo.URI, std::move(GLTFModelData));

    RefCntAutoPtr<RadientAssetManagerImpl> pSelf{this};
    RefCntAutoPtr<IRadientSceneAsset>      pModelAsset{*ppModel};
    const std::string                      SourceURI{LoadInfo.URI};

    EnqueueAsyncWork(
        m_pThreadPool,
        [pSelf, pModelAsset, SourceURI](Uint32) //
        {
            std::unique_ptr<GLTF::Model> pModel = LoadGLTFModel(SourceURI,
                                                                pSelf->m_pDevice,
                                                                pSelf->m_pResourceManager,
                                                                pSelf->m_pUploadManager);

            pSelf->CompleteGLTFLoad(pModelAsset, std::move(pModel));
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

    const SceneAssetImpl* pModelImpl = ClassPtrCast<const SceneAssetImpl>(pModel);
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

RadientAssetManagerImpl::GLTFMeshResolveResult RadientAssetManagerImpl::GetGLTFMesh(IRadientMeshAsset* pMesh,
                                                                                    bool               RequireGPUResourcesReady)
{
    GLTFMeshResolveResult Result;

    const MeshAssetImpl* pMeshImpl = ClassPtrCast<const MeshAssetImpl>(pMesh);
    if (pMeshImpl == nullptr)
        return Result;

    const GLTFMeshStorage* pGLTFMeshStorage = std::get_if<GLTFMeshStorage>(&pMeshImpl->GetStorage());
    if (pGLTFMeshStorage == nullptr || pGLTFMeshStorage->pModel == nullptr)
        return Result;

    const SceneAssetImpl* pModelImpl = pGLTFMeshStorage->pModel.RawPtr<SceneAssetImpl>();
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

    if (GLTFModel.pModel == nullptr ||
        pGLTFMeshStorage->MeshIndex >= GLTFModel.pModel->Meshes.size())
    {
        Result.Status = RADIENT_STATUS_INVALID_OPERATION;
        return Result;
    }

    Result.Status            = RADIENT_STATUS_OK;
    Result.pModel            = GLTFModel.pModel.get();
    Result.MeshIndex         = pGLTFMeshStorage->MeshIndex;
    Result.VertexAttribFlags = GLTFModel.VertexAttribFlags;
    return Result;
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

    return pImpl->GetStorage().LoadStatus.load(std::memory_order_acquire);
}

ITextureView* RadientAssetManagerImpl::GetTextureSRV(IRadientTextureAsset* pTextureAsset)
{
    const TextureAssetImpl* pImpl = ClassPtrCast<const TextureAssetImpl>(pTextureAsset);
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

    if (m_pUploadManager != nullptr)
        m_pUploadManager->RenderThreadUpdate(pContext);

    if (m_pResourceManager != nullptr)
        m_pResourceManager->UpdateAllResources(pDevice, pContext);

    std::vector<GLTFModelToPrepare> ModelsToPrepare;

    if (m_pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (m_pDevice != pDevice)
    {
        UNEXPECTED("Radient asset manager device changed. This should never happen.");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

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

bool RadientAssetManagerImpl::ValidateMesh(const RadientMeshCreateInfo& MeshCI)
{
    if (MeshCI.VertexCount == 0 || MeshCI.pPositions == nullptr ||
        MeshCI.PrimitiveCount == 0 || MeshCI.pPrimitives == nullptr)
    {
        return false;
    }

    const bool HasBoneIndices = MeshCI.pBoneIndices0 != nullptr;
    const bool HasBoneWeights = MeshCI.pBoneWeights0 != nullptr;
    if (HasBoneIndices != HasBoneWeights)
        return false;

    if (MeshCI.IndexCount == 0 ||
        MeshCI.pIndices == nullptr ||
        (MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
         MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT32))
    {
        return false;
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= MeshCI.IndexCount)
        {
            return false;
        }

        if (PrimitiveCI.IndexCount > MeshCI.IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
    }

    return true;
}

bool RadientAssetManagerImpl::ValidateGLTF(const RadientGLTFLoadInfo& LoadInfo)
{
    return LoadInfo.URI != nullptr && *LoadInfo.URI != 0;
}

bool RadientAssetManagerImpl::ValidateTexture(const RadientTextureLoadInfo& LoadInfo)
{
    return LoadInfo.URI != nullptr && *LoadInfo.URI != 0;
}

RADIENT_STATUS RadientAssetManagerImpl::GetAssetLoadStatus(IRadientAsset* pAsset)
{
    if (pAsset == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    switch (pAsset->GetType())
    {
        case RADIENT_ASSET_TYPE_SCENE:
        {
            RefCntAutoPtr<IRadientSceneAsset> pScene{pAsset, IID_RadientSceneAsset};
            const SceneAssetImpl*             pImpl = pScene.RawPtr<SceneAssetImpl>();
            return pImpl != nullptr ?
                pImpl->GetStorage().LoadStatus.load(std::memory_order_acquire) :
                RADIENT_STATUS_INVALID_ARGUMENT;
        }

        case RADIENT_ASSET_TYPE_TEXTURE:
        {
            RefCntAutoPtr<IRadientTextureAsset> pTexture{pAsset, IID_RadientTextureAsset};
            const TextureAssetImpl*             pImpl = pTexture.RawPtr<TextureAssetImpl>();
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

void RadientAssetManagerImpl::TryEnqueueGPUResourceUpdate(IRadientSceneAsset* pModel,
                                                          GLTFModelStorage&   GLTFModel)
{
    if (pModel == nullptr ||
        GLTFModel.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
        GLTFModel.GPUResourcesReady.load(std::memory_order_acquire) ||
        GLTFModel.pModel == nullptr)
    {
        return;
    }

    if (GLTFModel.GPUUpdateQueued.exchange(true, std::memory_order_acq_rel))
        return;

    m_PendingGPUResourceUpdates.Enqueue(RefCntWeakPtr<IRadientSceneAsset>{pModel});
}

void RadientAssetManagerImpl::CompleteGLTFLoad(IRadientSceneAsset*          pModel,
                                               std::unique_ptr<GLTF::Model> pModelData)
{
    SceneAssetImpl* pImpl = ClassPtrCast<SceneAssetImpl>(pModel);
    if (pImpl == nullptr)
        return;

    const RADIENT_STATUS Status = pModelData != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;

    GLTFModelStorage& GLTFModel = pImpl->GetStorage();
    GLTFModel.VertexAttribFlags = pModelData != nullptr ?
        GetVertexAttribFlags(*pModelData) :
        PBR_Renderer::PSO_FLAG_NONE;
    GLTFModel.pModel            = std::move(pModelData);
    GLTFModel.GPUResourcesReady.store(false, std::memory_order_release);
    GLTFModel.GPUUpdateQueued.store(false, std::memory_order_release);
    GLTFModel.LoadStatus.store(Status, std::memory_order_release);

    TryEnqueueGPUResourceUpdate(pModel, GLTFModel);
}

} // namespace Diligent
