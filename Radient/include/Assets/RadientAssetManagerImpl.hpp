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

#pragma once

#include "Render/RadientDrawableMesh.hpp"
#include "RadientAssets.h"
#include "ThreadPool.h"
#include "Cast.hpp"
#include "HashUtils.hpp"
#include "MPSCQueue.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "../../../PBR/interface/PBR_Renderer.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Diligent
{

struct IDeviceContext;
struct IRenderDevice;
struct IGPUUploadManager;
struct ITexture;
struct ITextureAtlasSuballocation;
struct ITextureLoader;
struct ITextureView;

namespace GLTF
{
struct Model;
class ResourceManager;
} // namespace GLTF

class RadientAssetManagerImpl final : public ObjectBase<IRadientAssetManager>
{
public:
    using TBase = ObjectBase<IRadientAssetManager>;

    struct CreateInfo
    {
        RadientAssetManagerCreateInfo Assets;
        IThreadPool*                  pThreadPool = nullptr;
        IRenderDevice*                pDevice     = nullptr;
    };

    RadientAssetManagerImpl(IReferenceCounters* pRefCounters,
                            const CreateInfo&   CreateInfo);
    ~RadientAssetManagerImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetManager, TBase)

    static RefCntAutoPtr<RadientAssetManagerImpl> Create(const CreateInfo& CreateInfo);

    virtual const RadientAssetManagerDesc& DILIGENT_CALL_TYPE GetDesc() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                         IRadientMeshAsset**          ppMesh) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                             IRadientMaterialAsset**          ppMaterial) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                          IRadientTextureAsset**        ppTexture) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadGLTF(const RadientGLTFLoadInfo& LoadInfo,
                                                       IRadientSceneAsset**       ppModel) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE WaitForAssetLoad(IRadientAsset* pAsset) override final;

    RADIENT_STATUS CreateMeshFromGLTFMesh(IRadientSceneAsset* pModel,
                                          Uint32              MeshIndex,
                                          const Char*         Name,
                                          IRadientMeshAsset** ppMesh);

    static RadientDrawableMeshResolveResult GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                            bool               RequireGPUResourcesReady);

    static const GLTF::Model* GetGLTFModel(IRadientSceneAsset* pModel,
                                           bool                RequireGPUResourcesReady = false);
    static RADIENT_STATUS     GetGLTFLoadStatus(IRadientSceneAsset* pModel);
    static ITextureView*      GetTextureSRV(IRadientTextureAsset* pTexture);

    RADIENT_STATUS UpdateGPUResources(IRenderDevice*  pDevice,
                                      IDeviceContext* pContext);

    GLTF::ResourceManager* GetResourceManager() const;

private:
    struct MeshPrimitiveStorage
    {
        std::string Name;

        Uint32 FirstIndex = 0;
        Uint32 IndexCount = 0;

        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    };

    struct MeshVertexDataStorage
    {
        std::vector<RadientFloat3>       Positions;
        std::vector<RadientFloat3>       Normals;
        std::vector<RadientFloat4>       Tangents;
        std::vector<RadientFloat2>       TexCoords0;
        std::vector<RadientColorRGBA8>   Colors0;
        std::vector<RadientBoneIndices4> BoneIndices0;
        std::vector<RadientFloat4>       BoneWeights0;
    };

    struct MeshIndexBufferStorage
    {
        RADIENT_INDEX_TYPE IndexType  = RADIENT_INDEX_TYPE_NONE;
        Uint32             IndexCount = 0;
        std::vector<Uint8> Indices;
    };

    struct MeshStorage
    {
        MeshVertexDataStorage             VertexData;
        MeshIndexBufferStorage            IndexBuffer;
        std::vector<MeshPrimitiveStorage> MeshPrimitives;
    };

    struct MaterialStorage
    {
        RadientFloat4 BaseColorFactor = {1.f, 1.f, 1.f, 1.f};
        Float32       MetallicFactor  = 1.f;
        Float32       RoughnessFactor = 1.f;
        RadientFloat3 EmissiveFactor  = {0.f, 0.f, 0.f};
        Float32       AlphaCutoff     = 0.5f;
        Bool          DoubleSided     = False;

        RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture;
        RefCntAutoPtr<IRadientTextureAsset> pMetallicRoughnessTexture;
        RefCntAutoPtr<IRadientTextureAsset> pNormalTexture;
        RefCntAutoPtr<IRadientTextureAsset> pOcclusionTexture;
        RefCntAutoPtr<IRadientTextureAsset> pEmissiveTexture;
    };

    struct GLTFModelStorage
    {
        GLTFModelStorage() = default;
        GLTFModelStorage(GLTFModelStorage&& Rhs) noexcept;

        GLTFModelStorage& operator=(GLTFModelStorage&& Rhs)  = delete;
        GLTFModelStorage(const GLTFModelStorage&)            = delete;
        GLTFModelStorage& operator=(const GLTFModelStorage&) = delete;

        std::string                  SourceURI;
        std::unique_ptr<GLTF::Model> pModel;
        PBR_Renderer::PSO_FLAGS      VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
        std::atomic<RADIENT_STATUS>  LoadStatus{RADIENT_STATUS_OK};
        std::atomic_bool             GPUResourcesReady{false};
        std::atomic_bool             GPUUpdateQueued{false};
    };

    struct GLTFMeshStorage
    {
        RefCntAutoPtr<IRadientSceneAsset> pModel;
        RadientDrawableMesh               DrawableMesh;
    };

    struct TextureStorage
    {
        TextureStorage() = default;
        TextureStorage(TextureStorage&& Rhs) noexcept;

        TextureStorage& operator=(TextureStorage&& Rhs)  = delete;
        TextureStorage(const TextureStorage&)            = delete;
        TextureStorage& operator=(const TextureStorage&) = delete;

        std::string                               SourceURI;
        RefCntAutoPtr<ITexture>                   pTexture;
        RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;
        std::atomic<RADIENT_STATUS>               LoadStatus{RADIENT_STATUS_OK};
        std::atomic_bool                          GPUResourcesReady{false};
        std::atomic<Uint32>                       PendingUploads{0};
    };

    using MeshAssetStorage = std::variant<MeshStorage, GLTFMeshStorage>;

    template <typename InterfaceType,
              const INTERFACE_ID& InterfaceID,
              const INTERFACE_ID& ImplID,
              RADIENT_ASSET_TYPE  AssetType,
              typename StorageType>
    class AssetImpl final : public ObjectBase<InterfaceType>
    {
    public:
        using TBase   = ObjectBase<InterfaceType>;
        using Storage = StorageType;

        AssetImpl(IReferenceCounters* pRefCounters,
                  std::string&&       URI,
                  const Char*         Name,
                  StorageType&&       Storage);

        virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
        {
            return m_Ref;
        }

        virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
        {
            return AssetType;
        }

        StorageType& GetStorage()
        {
            return m_Storage;
        }

        const StorageType& GetStorage() const
        {
            return m_Storage;
        }

        template <typename T = StorageType>
        auto GetLoadStatus() const -> decltype(std::declval<const T&>().LoadStatus.load())
        {
            return m_Storage.LoadStatus.load(std::memory_order_acquire);
        }

        template <typename T = StorageType>
        static auto GetLoadStatus(IRadientAsset* pAsset) -> decltype(std::declval<const T&>().LoadStatus.load())
        {
            RefCntAutoPtr<AssetImpl> pImpl{pAsset, ImplID};
            return pImpl ? pImpl->GetLoadStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
        }

        virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override
        {
            if (ppInterface == nullptr)
                return;
            if (IID == InterfaceID || IID == IID_RadientAsset || IID == ImplID)
            {
                *ppInterface = this;
                (*ppInterface)->AddRef();
            }
            else
            {
                TBase::QueryInterface(IID, ppInterface);
            }
        }
        using IObject::QueryInterface;

    private:
        std::string           m_URI;
        std::string           m_Name;
        RadientAssetReference m_Ref;
        StorageType           m_Storage;
    };

    static constexpr INTERFACE_ID IID_MeshAssetImpl     = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};
    static constexpr INTERFACE_ID IID_MaterialAssetImpl = {0x1a11a468, 0xbf30, 0x4c4d, {0xb8, 0xcd, 0x48, 0x89, 0xa4, 0x65, 0xa, 0x50}};
    static constexpr INTERFACE_ID IID_TextureAssetImpl  = {0x8bd4869c, 0x6ec8, 0x4944, {0xbc, 0x3d, 0xe7, 0xcc, 0x5d, 0xb, 0x26, 0xc5}};
    static constexpr INTERFACE_ID IID_SceneAssetImpl    = {0xb59806f1, 0xa08a, 0x4dff, {0xb0, 0x37, 0x84, 0x75, 0xd6, 0xfd, 0x7f, 0x1b}};

    using MeshAssetImpl =
        AssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, IID_MeshAssetImpl, RADIENT_ASSET_TYPE_MESH, MeshAssetStorage>;
    using MaterialAssetImpl =
        AssetImpl<IRadientMaterialAsset, IID_RadientMaterialAsset, IID_MaterialAssetImpl, RADIENT_ASSET_TYPE_MATERIAL, MaterialStorage>;
    using TextureAssetImpl =
        AssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TextureStorage>;
    using SceneAssetImpl =
        AssetImpl<IRadientSceneAsset, IID_RadientSceneAsset, IID_SceneAssetImpl, RADIENT_ASSET_TYPE_SCENE, GLTFModelStorage>;

    static bool           ValidateMesh(const RadientMeshCreateInfo& MeshCI);
    static bool           ValidateGLTF(const RadientGLTFLoadInfo& LoadInfo);
    static bool           ValidateTexture(const RadientTextureLoadInfo& LoadInfo);
    static RADIENT_STATUS GetAssetLoadStatus(IRadientAsset* pAsset);

    std::string MakeURI(const char* Type);

    template <typename ImplType>
    RefCntAutoPtr<ImplType> CreateAsset(const char*                  Type,
                                        const Char*                  Name,
                                        typename ImplType::Storage&& Storage);

    template <typename ImplType, typename InterfaceType>
    RADIENT_STATUS CreateAsset(const char*                  Type,
                               const Char*                  Name,
                               typename ImplType::Storage&& Storage,
                               InterfaceType**              ppAsset);

    template <typename ImplType, typename CreateAssetFuncType>
    std::pair<RefCntAutoPtr<ImplType>, bool> CacheAssetOrGetExisting(const std::string&    CacheKey,
                                                                     CreateAssetFuncType&& CreateAssetFunc);

    void TryEnqueueGPUResourceUpdate(IRadientSceneAsset* pModel,
                                     GLTFModelStorage&   GLTFModel);
    void CompleteGLTFLoad(IRadientSceneAsset*          pModel,
                          std::unique_ptr<GLTF::Model> pModelData);
    void CompleteTextureLoad(IRadientTextureAsset*         pTexture,
                             RefCntAutoPtr<ITextureLoader> pLoader);

    static RADIENT_STATUS ScheduleTextureGPUUpload(IRenderDevice*         pDevice,
                                                   GLTF::ResourceManager* pResourceManager,
                                                   IGPUUploadManager*     pUploadManager,
                                                   IRadientTextureAsset*  pTexture,
                                                   ITextureLoader*        pLoader,
                                                   TextureStorage&        Texture);

    std::string             m_Name;
    RadientAssetManagerDesc m_Desc;

    RefCntAutoPtr<IThreadPool>           m_pThreadPool;
    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntAutoPtr<GLTF::ResourceManager> m_pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     m_pUploadManager;

    mutable std::shared_mutex  m_Mutex;
    std::atomic<RadientHandle> m_NextAssetID{1};

    // Weak cache keyed by canonical source identity. The manager does not own
    // assets: entries expire when scenes/views/drop their strong references.
    using AssetMapType = std::unordered_map<HashMapStringKey, RefCntWeakPtr<IRadientAsset>>;
    mutable AssetMapType m_Assets;

    MPSCQueue<RefCntWeakPtr<IRadientSceneAsset>> m_PendingGPUResourceUpdates;
};

} // namespace Diligent
