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

#include "RadientTypes.h"

#include "GLTFLoader.hpp"
#include "PBR_Renderer.hpp"

#include <vector>

namespace Diligent
{

struct RadientDrawableMeshPrimitive
{
    const GLTF::Material* pMaterial = nullptr;

    Uint32 GeometryIndex = 0;

    bool IsIndexed = false;

    Uint32 FirstElement = 0;
    Uint32 ElementCount = 0;
};

struct RadientDrawableMeshGeometry
{
    IVertexPool* pVertexPool = nullptr;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 FirstIndexLocation = 0;
    Uint32 BaseVertex         = 0;
};

/// Resolved mesh data needed to expand one scene renderable into drawable primitive slots.
struct RadientDrawableMesh
{
    std::vector<RadientDrawableMeshGeometry>  Geometries;
    std::vector<RadientDrawableMeshPrimitive> Primitives;
};

struct RadientDrawableMeshResolveResult
{
    // PENDING is a generic "not render-ready yet" status. It may come from
    // mesh source/view processing, geometry GPU resources, or material/texture
    // GPU resources.
    const RadientDrawableMesh* pMesh  = nullptr;
    RADIENT_STATUS             Status = RADIENT_STATUS_INVALID_OPERATION;
};

} // namespace Diligent
