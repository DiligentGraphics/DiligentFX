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
#include "RadientAssetCache.hpp"
#include "RadientAssetImpl.hpp"
#include "RadientAssets.h"
#include "RadientMaterialAssetManager.hpp"
#include "RadientMeshSource.hpp"
#include "RadientTextureAssetManager.hpp"
#include "ThreadPool.h"
#include "Cast.hpp"
#include "MPSCQueue.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "BufferSuballocator.h"
#include "VertexPool.h"
#include "../../../PBR/interface/PBR_Renderer.hpp"

#include <atomic>
#include <memory>
#include <string>
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

    static const GLTF::Material* GetMaterial(IRadientMaterialAsset* pMaterial);

    static const GLTF::Model* GetGLTFModel(IRadientSceneAsset* pModel,
                                           bool                RequireGPUResourcesReady = false);
    static RADIENT_STATUS     GetGLTFLoadStatus(IRadientSceneAsset* pModel);
    static ITextureView*      GetTextureSRV(IRadientTextureAsset* pTexture);

    RADIENT_STATUS UpdateGPUResources(IRenderDevice*  pDevice,
                                      IDeviceContext* pContext);

    GLTF::ResourceManager* GetResourceManager() const;

private:
    friend class RadientMaterialAssetManager;
    friend class RadientTextureAssetManager;

    struct MeshStorage
    {
        MeshStorage() = default;
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

    using MeshAssetStorage = std::variant<MeshStorage, GLTFMeshStorage>;

    static RadientDrawableMeshResolveResult GetDrawableMesh(const MeshStorage& Mesh,
                                                            bool               RequireGPUResourcesReady);
    static RadientDrawableMeshResolveResult GetDrawableMesh(const GLTFMeshStorage& Mesh,
                                                            bool                   RequireGPUResourcesReady);

    static constexpr INTERFACE_ID IID_MeshAssetImpl  = {0xee010529, 0xc9ad, 0x4044, {0xbb, 0x1a, 0x7c, 0x3e, 0x5f, 0x63, 0xc1, 0x5a}};
    static constexpr INTERFACE_ID IID_SceneAssetImpl = {0xb59806f1, 0xa08a, 0x4dff, {0xb0, 0x37, 0x84, 0x75, 0xd6, 0xfd, 0x7f, 0x1b}};

    using MeshAssetImpl =
        RadientAssetImpl<IRadientMeshAsset, IID_RadientMeshAsset, IID_MeshAssetImpl, RADIENT_ASSET_TYPE_MESH, MeshAssetStorage>;
    using SceneAssetImpl =
        RadientAssetImpl<IRadientSceneAsset, IID_RadientSceneAsset, IID_SceneAssetImpl, RADIENT_ASSET_TYPE_SCENE, GLTFModelStorage>;

    static RADIENT_STATUS GetAssetLoadStatus(IRadientAsset* pAsset);

    RADIENT_STATUS InitializeMeshStorage(const RadientMeshSource& Source,
                                         MeshStorage&             Storage) const;
    RADIENT_STATUS ScheduleMeshGPUUpload(MeshAssetImpl&           MeshAsset,
                                         const RadientMeshSource& Source,
                                         MeshStorage&             Storage) const;
    static void    UpdateMeshUploadProgress(MeshStorage& Storage,
                                            bool         CopyScheduled);

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

    void LoadMeshFromSource(MeshAssetImpl&                     Mesh,
                            std::unique_ptr<RadientMeshSource> pSource);
    void LoadGLTFModel(SceneAssetImpl& Model);

    std::string                 m_Name;
    RadientAssetManagerDesc     m_Desc;
    RadientMaterialAssetManager m_MaterialManager;
    RadientTextureAssetManager  m_TextureManager;

    RefCntAutoPtr<IThreadPool>           m_pThreadPool;
    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntAutoPtr<GLTF::ResourceManager> m_pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     m_pUploadManager;

    std::atomic<RadientHandle> m_NextAssetID{1};

    RadientAssetCache<IRadientSceneAsset> m_GLTFAssetCache;

    MPSCQueue<RefCntWeakPtr<IRadientSceneAsset>> m_PendingGPUResourceUpdates;
};

} // namespace Diligent
