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
#include "HashUtils.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
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
                                                         RadientAssetReference&       Mesh) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                             RadientAssetReference&           Material) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                          RadientAssetReference&        Texture) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadGLTF(const RadientGLTFLoadInfo& LoadInfo,
                                                       RadientAssetReference&     Model) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE WaitForAssetLoad(const RadientAssetReference& Asset) override final;

    RADIENT_STATUS CreateMeshFromGLTFMesh(const RadientAssetReference& Model,
                                          Uint32                       MeshIndex,
                                          const Char*                  Name,
                                          RadientAssetReference&       Mesh);

    RADIENT_STATUS GetMeshGLTFSource(const RadientAssetReference& Mesh,
                                     RadientAssetReference&       Model,
                                     Uint32&                      MeshIndex) const;

    RADIENT_STATUS GetGLTFSourceURI(const RadientAssetReference& Model,
                                    const Char*&                 SourceURI) const;

    const GLTF::Model* GetGLTFModel(const RadientAssetReference& Model,
                                    bool                         RequireGPUResourcesReady = false) const;
    RADIENT_STATUS     GetGLTFLoadStatus(const RadientAssetReference& Model) const;
    ITextureView*      GetTextureSRV(const RadientAssetReference& Texture) const;

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

        std::string MaterialURI;
        Uint64      MaterialVersion = 0;
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

        RadientAssetReference BaseColorTexture;
        RadientAssetReference MetallicRoughnessTexture;
        RadientAssetReference NormalTexture;
        RadientAssetReference OcclusionTexture;
        RadientAssetReference EmissiveTexture;

        std::string BaseColorTextureURI;
        std::string MetallicRoughnessTextureURI;
        std::string NormalTextureURI;
        std::string OcclusionTextureURI;
        std::string EmissiveTextureURI;
    };

    struct GLTFModelStorage
    {
        std::string                  SourceURI;
        std::unique_ptr<GLTF::Model> pModel;
        RADIENT_STATUS               LoadStatus        = RADIENT_STATUS_OK;
        bool                         GPUResourcesReady = false;
    };

    struct GLTFMeshStorage
    {
        RadientAssetReference Model;
        std::string           ModelURI;
        Uint32                MeshIndex = ~0u;
    };

    struct TextureStorage
    {
        std::string             SourceURI;
        RADIENT_STATUS          LoadStatus = RADIENT_STATUS_OK;
        RefCntAutoPtr<ITexture> pTexture;
    };

    struct AssetRecord
    {
        RADIENT_ASSET_TYPE Type    = RADIENT_ASSET_TYPE_MESH;
        Uint64             Version = 1;
        std::string        URI;
        std::string        Name;

        std::variant<MeshStorage,
                     MaterialStorage,
                     GLTFModelStorage,
                     GLTFMeshStorage,
                     TextureStorage>
            Storage;
    };

    bool               ValidateMesh(const RadientMeshCreateInfo& MeshCI) const;
    bool               ValidateGLTF(const RadientGLTFLoadInfo& LoadInfo) const;
    bool               ValidateTexture(const RadientTextureLoadInfo& LoadInfo) const;
    AssetRecord*       FindAssetLocked(const RadientAssetReference& Ref);
    const AssetRecord* FindAssetLocked(const RadientAssetReference& Ref) const;
    RADIENT_STATUS     GetAssetLoadStatusLocked(const RadientAssetReference& Asset) const;

    std::string MakeURI(const char* Type);

    RadientAssetReference StoreAsset(AssetRecord&& Record);
    void                  FixupAssetRecord(AssetRecord& Record);
    void                  FixupAssetReference(RadientAssetReference& Ref, const std::string& URIStorage);
    RadientAssetReference CopyAssetReference(const RadientAssetReference& Ref, std::string& URIStorage);
    void                  CompleteGLTFLoad(const RadientAssetReference& Model,
                                           std::unique_ptr<GLTF::Model> pModel,
                                           RADIENT_STATUS               Status);

    std::string             m_Name;
    RadientAssetManagerDesc m_Desc;

    RefCntAutoPtr<IThreadPool>           m_pThreadPool;
    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntAutoPtr<GLTF::ResourceManager> m_pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     m_pUploadManager;

    mutable std::shared_mutex m_Mutex;
    RadientHandle             m_NextAssetID = 1;

    using AssetMapType = std::unordered_map<HashMapStringKey, std::unique_ptr<AssetRecord>>;
    AssetMapType m_Assets;
};

} // namespace Diligent
