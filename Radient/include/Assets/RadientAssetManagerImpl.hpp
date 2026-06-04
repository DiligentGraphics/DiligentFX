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

#include "RadientAssets.h"
#include "ThreadPool.h"
#include "Cast.hpp"
#include "HashUtils.hpp"
#include "MPSCQueue.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

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

    static RADIENT_STATUS GetGLTFSourceURI(IRadientSceneAsset* pModel,
                                           const Char*&        SourceURI);

    struct GLTFMeshResolveResult
    {
        RADIENT_STATUS     Status    = RADIENT_STATUS_INVALID_ARGUMENT;
        const GLTF::Model* pModel    = nullptr;
        Uint32             MeshIndex = ~0u;
    };

    static GLTFMeshResolveResult GetGLTFMesh(IRadientMeshAsset* pMesh,
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

        Uint32 VertexBufferIndex = 0;
        Uint32 FirstIndex        = 0;
        Uint32 IndexCount        = 0;

        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    };

    struct MeshVertexBufferStorage
    {
        std::string Name;

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
        std::vector<MeshVertexBufferStorage> VertexBuffers;
        MeshIndexBufferStorage               IndexBuffer;
        std::vector<MeshPrimitiveStorage>    MeshPrimitives;
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
        std::atomic<RADIENT_STATUS>  LoadStatus{RADIENT_STATUS_OK};
        std::atomic_bool             GPUResourcesReady{false};
        std::atomic_bool             GPUUpdateQueued{false};
    };

    struct GLTFMeshStorage
    {
        RefCntAutoPtr<IRadientSceneAsset> pModel;
        Uint32                            MeshIndex = ~0u;
    };

    struct TextureStorage
    {
        std::string             SourceURI;
        RADIENT_STATUS          LoadStatus = RADIENT_STATUS_OK;
        RefCntAutoPtr<ITexture> pTexture;
    };

    using MeshAssetStorage = std::variant<MeshStorage, GLTFMeshStorage>;

    template <typename InterfaceType, const INTERFACE_ID& InterfaceID, RADIENT_ASSET_TYPE AssetType, typename StorageType>
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

        IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(InterfaceID, IID_RadientAsset, TBase)

    private:
        std::string           m_URI;
        std::string           m_Name;
        RadientAssetReference m_Ref;
        StorageType           m_Storage;
    };

    using MeshAssetImpl =
        AssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, RADIENT_ASSET_TYPE_MESH, MeshAssetStorage>;
    using MaterialAssetImpl =
        AssetImpl<IRadientMaterialAsset, IID_RadientMaterialAsset, RADIENT_ASSET_TYPE_MATERIAL, MaterialStorage>;
    using TextureAssetImpl =
        AssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, RADIENT_ASSET_TYPE_TEXTURE, TextureStorage>;
    using SceneAssetImpl =
        AssetImpl<IRadientSceneAsset, IID_RadientSceneAsset, RADIENT_ASSET_TYPE_SCENE, GLTFModelStorage>;

    static bool           ValidateMesh(const RadientMeshCreateInfo& MeshCI);
    static bool           ValidateGLTF(const RadientGLTFLoadInfo& LoadInfo);
    static bool           ValidateTexture(const RadientTextureLoadInfo& LoadInfo);
    static RADIENT_STATUS GetAssetLoadStatus(IRadientAsset* pAsset);

    std::string MakeURI(const char* Type);

    template <typename InterfaceType, typename ImplType>
    InterfaceType* StoreAsset(const char*                  Type,
                              const Char*                  Name,
                              typename ImplType::Storage&& Storage);

    void TryEnqueueGPUResourceUpdate(IRadientSceneAsset* pModel,
                                     GLTFModelStorage&   GLTFModel);
    void CompleteGLTFLoad(IRadientSceneAsset*          pModel,
                          std::unique_ptr<GLTF::Model> pModelData);

    std::string             m_Name;
    RadientAssetManagerDesc m_Desc;

    RefCntAutoPtr<IThreadPool>           m_pThreadPool;
    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntAutoPtr<GLTF::ResourceManager> m_pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     m_pUploadManager;

    mutable std::shared_mutex  m_Mutex;
    std::atomic<RadientHandle> m_NextAssetID{1};

    using AssetMapType = std::unordered_map<HashMapStringKey, RefCntWeakPtr<IRadientAsset>>;
    mutable AssetMapType m_Assets;

    MPSCQueue<RefCntWeakPtr<IRadientSceneAsset>> m_PendingGPUResourceUpdates;
};

} // namespace Diligent
