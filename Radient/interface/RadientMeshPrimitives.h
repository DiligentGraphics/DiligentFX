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

/// \file
/// Defines helpers that create primitive Radient mesh assets.

#include "RadientAssets.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)


/// Cube mesh creation attributes.
struct RadientCubeMeshCreateInfo
{
    /// Mesh name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Cube size. The cube is centered at the origin.
    Float32 Size DEFAULT_INITIALIZER(1.f);

    /// Number of subdivisions per face. Zero uses one subdivision.
    Uint32 Subdivisions DEFAULT_INITIALIZER(1);

    /// Default material assigned to the cube primitive.
    IRadientMaterialAsset* pMaterial DEFAULT_INITIALIZER(nullptr);

    /// Optional six face colors in +X, -X, +Y, -Y, +Z, -Z order.
    /// When null, the cube mesh does not include vertex colors.
    const RadientColorRGBA8* pFaceColors DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientCubeMeshCreateInfo RadientCubeMeshCreateInfo;


/// Sphere mesh creation attributes.
struct RadientSphereMeshCreateInfo
{
    /// Mesh name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Sphere radius. The sphere is centered at the origin.
    Float32 Radius DEFAULT_INITIALIZER(1.f);

    /// Number of subdivisions per projected cube face. Zero uses one subdivision.
    Uint32 Subdivisions DEFAULT_INITIALIZER(16);

    /// Default material assigned to the sphere primitive.
    IRadientMaterialAsset* pMaterial DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientSphereMeshCreateInfo RadientSphereMeshCreateInfo;


/// Creates a cube mesh asset with positions, normals, texture coordinates, and 32-bit indices.
#include "../../../DiligentCore/Primitives/interface/DefineRefMacro.h"
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientCubeMesh)(IRadientAssetManager*               pAssetManager,
                                                               const RadientCubeMeshCreateInfo REF MeshCI,
                                                               IRadientMeshAsset**                 ppMesh);

/// Creates a sphere mesh asset with positions, normals, texture coordinates, and 32-bit indices.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientSphereMesh)(IRadientAssetManager*                 pAssetManager,
                                                                 const RadientSphereMeshCreateInfo REF MeshCI,
                                                                 IRadientMeshAsset**                   ppMesh);
#include "../../../DiligentCore/Primitives/interface/UndefRefMacro.h"


DILIGENT_END_NAMESPACE // namespace Diligent
