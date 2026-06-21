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

#include "RadientAssetCache.hpp"
#include "RadientAssets.h"
#include "RadientMaterialAssetManager.hpp"
#include "RadientMeshAssetManager.hpp"
#include "RadientTextureAssetManager.hpp"
#include "ThreadPool.h"
#include "Cast.hpp"
#include "MPSCQueue.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <string>

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
struct Material;
struct Model;
class ResourceManager;
} // namespace GLTF

class ScenePayloadImpl;

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

    // Reports GLTF model load and upload-scheduling status. OK does not imply
    // that render-thread GPU upload callbacks have completed.
    static RADIENT_STATUS     GetGLTFLoadStatus(IRadientSceneAsset* pModel);
    static ITextureView*      GetTextureSRV(IRadientTextureAsset* pTexture);

    RADIENT_STATUS UpdateGPUResources(IRenderDevice*  pDevice,
                                      IDeviceContext* pContext);

    GLTF::ResourceManager* GetResourceManager() const;

private:
    // Dispatches to the asset-type-specific load status. OK means source data
    // has been processed and GPU upload work has been scheduled when needed.
    static RADIENT_STATUS GetAssetLoadStatus(IRadientAsset* pAsset);

    void LoadGLTFModel(ScenePayloadImpl&  Model,
                       const std::string& SourceURI);

    std::string             m_Name;
    RadientAssetManagerDesc m_Desc;

    RefCntAutoPtr<IThreadPool>           m_pThreadPool;
    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntAutoPtr<GLTF::ResourceManager> m_pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     m_pUploadManager;

    RadientMeshAssetManager              m_MeshManager;
    RadientMaterialAssetManager          m_MaterialManager;
    RadientTextureAssetManagerSharedPtr  m_pTextureManager;

    RadientAssetCache<ScenePayloadImpl> m_GLTFAssetCache;

    MPSCQueue<RefCntWeakPtr<ScenePayloadImpl>> m_PendingGPUResourceUpdates;
};

} // namespace Diligent
