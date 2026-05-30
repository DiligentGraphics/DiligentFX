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

#include "RadientAssetManagerImpl.hpp"
#include "RadientTypes.h"
#include "PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"

#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

namespace GLTF
{
struct Material;
struct Model;
} // namespace GLTF

struct RadientRenderMeshPrimitive
{
    Uint32 FirstIndex  = 0;
    Uint32 IndexCount  = 0;
    Uint32 FirstVertex = 0;
    Uint32 VertexCount = 0;
    Uint32 MaterialId  = 0;

    bool HasIndices() const
    {
        return IndexCount > 0;
    }
};

struct RadientRenderMesh
{
    std::vector<RadientRenderMeshPrimitive> Primitives;
    std::vector<const GLTF::Material*>      Materials;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE;
    Uint32                  FirstIndexLocation = 0;
    Uint32                  BaseVertex         = 0;
};

/// Renderer-owned cache for uploaded Radient asset data.
///
/// CPU asset payloads are expected to be transient: they are copied into
/// upload work items and released once the upload has been scheduled.
class RadientRenderResourceCache
{
public:
    RadientRenderResourceCache(RadientAssetManagerImpl* pAssetManager,
                               IRenderDevice*           pDevice);
    ~RadientRenderResourceCache();

    RADIENT_STATUS Prepare(IRenderDevice*  pDevice,
                           IDeviceContext* pContext);

    /// Returns the renderer-ready mesh, or null if the asset is still loading
    /// or failed to load.
    const RadientRenderMesh* ResolveMesh(const RadientAssetReference& Mesh,
                                         IRenderDevice*               pDevice,
                                         IDeviceContext*              pContext);

    IGPUUploadManager*     GetUploadManager() const;
    GLTF::ResourceManager* GetResourceManager() const;

private:
    struct GLTFResource
    {
        std::string                  SourceURI;
        std::unique_ptr<GLTF::Model> pModel;
    };

    struct MeshResource
    {
        enum class STATE
        {
            NotRequested,
            Loading,
            Ready,
            Failed
        };

        STATE                 State = STATE::NotRequested;
        RadientAssetReference SourceModel;
        std::string           SourceModelURI;
        Uint32                SourceMeshIndex = ~0u;
        RadientRenderMesh     Mesh;
    };

    RADIENT_STATUS EnsureGLTFLoaded(const RadientAssetReference& Model,
                                    IRenderDevice*               pDevice,
                                    IDeviceContext*              pContext);
    RADIENT_STATUS PrepareGLTFResource(GLTFResource&   Resource,
                                       IRenderDevice*  pDevice,
                                       IDeviceContext* pContext);

    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;
    RefCntAutoPtr<IRenderDevice>           m_pDevice;
    RefCntAutoPtr<IGPUUploadManager>       m_pUploadManager;
    RefCntAutoPtr<GLTF::ResourceManager>   m_pResourceManager;

    std::unordered_map<std::string, GLTFResource> m_GLTFResources;
    std::unordered_map<std::string, MeshResource> m_MeshResources;
};

} // namespace Diligent
