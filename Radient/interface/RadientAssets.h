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
/// Defines Radient asset creation interfaces.

#include "RadientTypes.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Asset type.
DILIGENT_TYPED_ENUM(RADIENT_ASSET_TYPE, Uint8)
{
    /// Mesh asset.
    RADIENT_ASSET_TYPE_MESH = 0,

    /// Material asset.
    RADIENT_ASSET_TYPE_MATERIAL
};


/// Mesh index buffer element type.
DILIGENT_TYPED_ENUM(RADIENT_INDEX_TYPE, Uint8)
{
    /// The primitive is not indexed.
    RADIENT_INDEX_TYPE_NONE = 0,

    /// 16-bit unsigned indices.
    RADIENT_INDEX_TYPE_UINT16,

    /// 32-bit unsigned indices.
    RADIENT_INDEX_TYPE_UINT32
};

// clang-format on


/// Asset manager description.
struct RadientAssetManagerDesc
{
    /// Asset manager name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAssetManagerDesc RadientAssetManagerDesc;


/// Asset manager creation attributes.
struct RadientAssetManagerCreateInfo
{
    /// Asset manager description.
    RadientAssetManagerDesc Desc DEFAULT_INITIALIZER({});
};
typedef struct RadientAssetManagerCreateInfo RadientAssetManagerCreateInfo;


/// CPU-side mesh primitive creation attributes.
struct RadientMeshPrimitiveCreateInfo
{
    /// Optional primitive name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Vertex positions. Required when VertexCount is not zero.
    const RadientFloat3* pPositions DEFAULT_INITIALIZER(nullptr);

    /// Vertex normals.
    const RadientFloat3* pNormals DEFAULT_INITIALIZER(nullptr);

    /// Vertex tangents.
    const RadientFloat4* pTangents DEFAULT_INITIALIZER(nullptr);

    /// Primary texture coordinates.
    const RadientFloat2* pTexCoords0 DEFAULT_INITIALIZER(nullptr);

    /// Primary vertex colors.
    const RadientFloat4* pColors0 DEFAULT_INITIALIZER(nullptr);

    /// Number of vertices.
    Uint32 VertexCount DEFAULT_INITIALIZER(0);

    /// Index data. Type is controlled by IndexType.
    const void* pIndices DEFAULT_INITIALIZER(nullptr);

    /// Number of indices.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);

    /// Index type.
    RADIENT_INDEX_TYPE IndexType DEFAULT_INITIALIZER(RADIENT_INDEX_TYPE_NONE);

    /// Default material for this primitive.
    RadientAssetReference Material DEFAULT_INITIALIZER({});
};
typedef struct RadientMeshPrimitiveCreateInfo RadientMeshPrimitiveCreateInfo;


/// CPU-side mesh creation attributes.
struct RadientMeshCreateInfo
{
    /// Mesh name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Mesh primitives.
    const RadientMeshPrimitiveCreateInfo* pPrimitives DEFAULT_INITIALIZER(nullptr);

    /// Number of primitives.
    Uint32 PrimitiveCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMeshCreateInfo RadientMeshCreateInfo;


/// PBR material creation attributes.
struct RadientMaterialCreateInfo
{
    /// Material name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Base color multiplier.
    RadientFloat4 BaseColorFactor DEFAULT_INITIALIZER({1.f, 1.f, 1.f, 1.f});

    /// Metallic multiplier.
    Float32 MetallicFactor DEFAULT_INITIALIZER(1.f);

    /// Roughness multiplier.
    Float32 RoughnessFactor DEFAULT_INITIALIZER(1.f);

    /// Emissive multiplier.
    RadientFloat3 EmissiveFactor DEFAULT_INITIALIZER({0.f, 0.f, 0.f});

    /// Alpha cutoff used by alpha-tested materials.
    Float32 AlphaCutoff DEFAULT_INITIALIZER(0.5f);

    /// Whether the material should render both sides.
    Bool DoubleSided DEFAULT_INITIALIZER(False);

    /// Optional base color texture.
    RadientAssetReference BaseColorTexture DEFAULT_INITIALIZER({});

    /// Optional metallic-roughness texture.
    RadientAssetReference MetallicRoughnessTexture DEFAULT_INITIALIZER({});

    /// Optional normal texture.
    RadientAssetReference NormalTexture DEFAULT_INITIALIZER({});

    /// Optional occlusion texture.
    RadientAssetReference OcclusionTexture DEFAULT_INITIALIZER({});

    /// Optional emissive texture.
    RadientAssetReference EmissiveTexture DEFAULT_INITIALIZER({});
};
typedef struct RadientMaterialCreateInfo RadientMaterialCreateInfo;


// {0B806532-011B-490C-A3AE-3AA6C8DB266C}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetManager =
    { 0xb806532, 0x11b, 0x490c, { 0xa3, 0xae, 0x3a, 0xa6, 0xc8, 0xdb, 0x26, 0x6c } };


#define DILIGENT_INTERFACE_NAME IRadientAssetManager
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAssetManagerInclusiveMethods \
    IObjectInclusiveMethods;                 \
    IRadientAssetManagerMethods RadientAssetManager

// clang-format off

/// Creates asset references from authored or procedural data.
DILIGENT_BEGIN_INTERFACE(IRadientAssetManager, IObject)
{
    /// Returns the asset manager description.
    VIRTUAL const RadientAssetManagerDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Creates a mesh asset from CPU-side primitive data.
    VIRTUAL RADIENT_STATUS METHOD(CreateMesh)(THIS_
                                              const RadientMeshCreateInfo REF MeshCI,
                                              RadientAssetReference REF       Mesh) PURE;

    /// Creates a material asset.
    VIRTUAL RADIENT_STATUS METHOD(CreateMaterial)(THIS_
                                                  const RadientMaterialCreateInfo REF MaterialCI,
                                                  RadientAssetReference REF           Material) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetManager_GetDesc(This)                 CALL_IFACE_METHOD(RadientAssetManager, GetDesc,        This)
#    define IRadientAssetManager_CreateMesh(This, ...)         CALL_IFACE_METHOD(RadientAssetManager, CreateMesh,     This, __VA_ARGS__)
#    define IRadientAssetManager_CreateMaterial(This, ...)     CALL_IFACE_METHOD(RadientAssetManager, CreateMaterial, This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
