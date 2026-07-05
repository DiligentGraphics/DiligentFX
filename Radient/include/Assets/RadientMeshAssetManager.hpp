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
#include "RadientAssets.h"
#include "RefCntAutoPtr.hpp"

#include <atomic>
#include <memory>

namespace Diligent
{

struct IGPUUploadManager;
struct IRenderDevice;
struct IThreadPool;

namespace GLTF
{
class ResourceManager;
} // namespace GLTF

class RadientMeshSource;
class MeshGPUData;
class MeshPayloadImpl;
class RadientMeshAssetManager;
struct RadientMeshViewCreateInfo;

using RadientMeshAssetManagerSharedPtr = std::shared_ptr<RadientMeshAssetManager>;

class RadientMeshAssetManager final : public std::enable_shared_from_this<RadientMeshAssetManager>
{
public:
    struct CreateInfo
    {
        IRenderDevice*         pDevice          = nullptr;
        GLTF::ResourceManager* pResourceManager = nullptr;
        IGPUUploadManager*     pUploadManager   = nullptr;
    };

    ~RadientMeshAssetManager();

    static RadientMeshAssetManagerSharedPtr Create(const CreateInfo& CI);

    RADIENT_STATUS CreateMesh(IThreadPool&                 ThreadPool,
                              const RadientMeshCreateInfo& MeshCI,
                              IRadientMeshAsset**          ppMesh);

    RADIENT_STATUS CreateMesh(IThreadPool&                       ThreadPool,
                              std::unique_ptr<RadientMeshSource> pMeshSource,
                              const RadientMeshViewCreateInfo&   ViewCI,
                              IRadientMeshAsset**                ppMesh);

    RADIENT_STATUS CreateMeshFromGLTFMesh(IRadientSceneAsset* pModel,
                                          Uint32              MeshIndex,
                                          const Char*         Name,
                                          IRadientMeshAsset** ppMesh);

    // Returns drawable mesh data when the mesh asset is ready. This method
    // accesses render data and must be called from the render thread.
    static RadientDrawableMeshResolveResult GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                            bool               RequireGPUResourcesReady);

    // Reports mesh source processing and upload-scheduling status. OK does not
    // imply GPU buffers are ready unless queried through GetDrawableMesh().
    static RADIENT_STATUS         GetLoadStatus(IRadientAsset* pMeshAsset);
    static const MeshPayloadImpl* GetMeshPayload(IRadientMeshAsset* pMeshAsset);
    static const MeshGPUData*     GetMeshGPUData(IRadientMeshAsset* pMeshAsset);

private:
    explicit RadientMeshAssetManager(const CreateInfo& CI);

    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntWeakPtr<GLTF::ResourceManager> m_WeakResourceManager;
    RefCntWeakPtr<IGPUUploadManager>     m_WeakUploadManager;

    RadientAssetCache<MeshPayloadImpl> m_MeshCache;
    RadientAssetCache<MeshPayloadImpl> m_GLTFMeshCache;
    RadientAssetCache<MeshGPUData>     m_MeshGPUDataCache;
    std::atomic<RadientHandle>         m_NextAssetID{1};
};

} // namespace Diligent
