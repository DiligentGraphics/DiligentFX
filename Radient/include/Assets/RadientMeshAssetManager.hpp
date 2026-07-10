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

class RadientMeshIndexSource;
class RadientMeshVertexSource;
class MeshIndexDataPayloadImpl;
class MeshVertexDataPayloadImpl;
class MeshPayloadImpl;
class RadientMeshAssetManager;
struct RadientMeshViewCreateInfo;

using RadientMeshAssetManagerSharedPtr = std::shared_ptr<RadientMeshAssetManager>;

// Opaque handle to shared mesh index data. The implementation details are
// private to RadientMeshAssetManager.
class IRadientMeshIndexData : public IRadientAsset
{
};

// Opaque handle to shared mesh vertex data. The implementation details are
// private to RadientMeshAssetManager.
class IRadientMeshVertexData : public IRadientAsset
{
};

struct RadientMeshGeometryData
{
    /// Vertex and index data that together form one drawable geometry.
    IRadientMeshVertexData* pVertexData = nullptr;
    IRadientMeshIndexData*  pIndexData  = nullptr;
};

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

    RADIENT_STATUS CreateMeshIndexData(IThreadPool&                            ThreadPool,
                                       std::unique_ptr<RadientMeshIndexSource> pIndexSource,
                                       IRadientMeshIndexData**                 ppIndexData);

    RADIENT_STATUS CreateMeshVertexData(IThreadPool&                             ThreadPool,
                                        std::unique_ptr<RadientMeshVertexSource> pVertexSource,
                                        IRadientMeshVertexData**                 ppVertexData);

    // Creates a mesh handle and schedules mesh-view payload creation. When
    // vertex or index data is still loading, the view task depends on the
    // corresponding data tasks if they are still available.
    RADIENT_STATUS CreateMeshView(IThreadPool&                     ThreadPool,
                                  const RadientMeshGeometryData*   pGeometryData,
                                  Uint32                           GeometryCount,
                                  const RadientMeshViewCreateInfo& ViewCI,
                                  IRadientMeshAsset**              ppMesh);

    // Returns drawable mesh data when the mesh asset is ready. A pending status
    // means that any mesh dependency may still be unresolved: source/view
    // processing, geometry GPU resources, or material/texture GPU resources.
    // This method accesses render data and must be called from the render
    // thread.
    static RadientDrawableMeshResolveResult GetDrawableMesh(IRadientMeshAsset* pMesh,
                                                            bool               RequireGPUResourcesReady);

    // Reports CPU-side mesh readiness: mesh source/view processing and material
    // load dependencies. OK does not imply GPU buffers exist or that GPU copy
    // commands have been enqueued.
    static RADIENT_STATUS GetLoadStatus(IRadientAsset* pMeshAsset);

    // Reports render-resource readiness. This follows GetLoadStatus(), then
    // checks geometry GPU resources and material/texture GPU resources.
    static RADIENT_STATUS                   GetGPUResourceStatus(IRadientAsset* pMeshAsset);
    static const MeshPayloadImpl*           GetMeshPayload(IRadientMeshAsset* pMeshAsset);
    static Uint32                           GetMeshGeometryCount(IRadientMeshAsset* pMeshAsset);
    static const MeshIndexDataPayloadImpl*  GetMeshIndexDataPayload(IRadientMeshAsset* pMeshAsset, Uint32 GeometryIndex);
    static const MeshVertexDataPayloadImpl* GetMeshVertexDataPayload(IRadientMeshAsset* pMeshAsset, Uint32 GeometryIndex);
    static const IRadientMeshIndexData*     GetMeshIndexData(IRadientMeshAsset* pMeshAsset);
    static const IRadientMeshVertexData*    GetMeshVertexData(IRadientMeshAsset* pMeshAsset);

private:
    explicit RadientMeshAssetManager(const CreateInfo& CI);

    RefCntAutoPtr<IRenderDevice>         m_pDevice;
    RefCntWeakPtr<GLTF::ResourceManager> m_WeakResourceManager;
    RefCntWeakPtr<IGPUUploadManager>     m_WeakUploadManager;

    RadientAssetCache<MeshPayloadImpl>           m_MeshCache;
    RadientAssetCache<MeshIndexDataPayloadImpl>  m_MeshIndexDataCache;
    RadientAssetCache<MeshVertexDataPayloadImpl> m_MeshVertexDataCache;
    std::atomic<RadientHandle>                   m_NextAssetID{1};
};

} // namespace Diligent
