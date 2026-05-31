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

/// Renderer-owned cache for renderer-facing mesh data.
///
/// GLTF model loading and GPU resource preparation are owned by the asset
/// manager. This cache only expands loaded mesh assets into render mesh
/// metadata used by draw lists and geometry passes.
class RadientRenderResourceCache
{
public:
    explicit RadientRenderResourceCache(RadientAssetManagerImpl* pAssetManager);
    ~RadientRenderResourceCache();

    /// Returns the renderer-ready mesh, or null if the asset is still loading
    /// or failed to load.
    const RadientRenderMesh* ResolveMesh(const RadientAssetReference& Mesh);

private:
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

    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;

    std::unordered_map<std::string, MeshResource> m_MeshResources;
};

} // namespace Diligent
