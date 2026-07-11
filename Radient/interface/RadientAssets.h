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
#include "RadientAssetResolver.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAsset         IRadientAsset;
typedef struct IRadientMeshAsset     IRadientMeshAsset;
typedef struct IRadientMaterialAsset IRadientMaterialAsset;
typedef struct IRadientTextureAsset  IRadientTextureAsset;
typedef struct IRadientSceneAsset    IRadientSceneAsset;
typedef struct IDeviceContext        IDeviceContext;

// clang-format off

/// Asset type.
DILIGENT_TYPED_ENUM(RADIENT_ASSET_TYPE, Uint8)
{
    /// Mesh asset.
    RADIENT_ASSET_TYPE_MESH = 0,

    /// Material asset.
    RADIENT_ASSET_TYPE_MATERIAL,

    /// Texture asset.
    RADIENT_ASSET_TYPE_TEXTURE,

    /// Imported scene/model asset.
    RADIENT_ASSET_TYPE_SCENE
};

/// Authored scene/model source format.
DILIGENT_TYPED_ENUM(RADIENT_SCENE_FORMAT, Uint8)
{
    /// Infer the source format from the URI or source metadata.
    RADIENT_SCENE_FORMAT_AUTO = 0,

    /// GL Transmission Format (.gltf/.glb).
    RADIENT_SCENE_FORMAT_GLTF
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

    /// Optional asset resolver used to obtain bytes for URI-backed assets.
    /// If null, Radient uses a default filesystem resolver.
    IRadientAssetResolver* pAssetResolver DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAssetManagerCreateInfo RadientAssetManagerCreateInfo;


/// Four 8-bit color channels in RGBA order.
struct RadientColorRGBA8
{
    Uint8 r DEFAULT_INITIALIZER(0);
    Uint8 g DEFAULT_INITIALIZER(0);
    Uint8 b DEFAULT_INITIALIZER(0);
    Uint8 a DEFAULT_INITIALIZER(255);
};
typedef struct RadientColorRGBA8 RadientColorRGBA8;


/// Four bone indices.
struct RadientBoneIndices4
{
    Uint16 x DEFAULT_INITIALIZER(0);
    Uint16 y DEFAULT_INITIALIZER(0);
    Uint16 z DEFAULT_INITIALIZER(0);
    Uint16 w DEFAULT_INITIALIZER(0);
};
typedef struct RadientBoneIndices4 RadientBoneIndices4;


/// CPU-side mesh primitive creation attributes.
struct RadientMeshPrimitiveCreateInfo
{
    /// Optional primitive name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// First index in RadientMeshCreateInfo::pIndices.
    Uint32 FirstIndex DEFAULT_INITIALIZER(0);

    /// Number of indices in RadientMeshCreateInfo::pIndices.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);

    /// Default material for this primitive.
    IRadientMaterialAsset* pMaterial DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMeshPrimitiveCreateInfo RadientMeshPrimitiveCreateInfo;


/// CPU-side mesh creation attributes.
struct RadientMeshCreateInfo
{
    /// Mesh name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Vertex positions. Required when VertexCount is not zero.
    const RadientFloat3* pPositions DEFAULT_INITIALIZER(nullptr);

    /// Vertex normals.
    const RadientFloat3* pNormals DEFAULT_INITIALIZER(nullptr);

    /// Vertex tangents.
    const RadientFloat4* pTangents DEFAULT_INITIALIZER(nullptr);

    /// Primary texture coordinates.
    const RadientFloat2* pTexCoords0 DEFAULT_INITIALIZER(nullptr);

    /// Primary vertex colors as 8-bit RGBA values.
    const RadientColorRGBA8* pColors0 DEFAULT_INITIALIZER(nullptr);

    /// Four bone indices matching pBoneWeights0.
    const RadientBoneIndices4* pBoneIndices0 DEFAULT_INITIALIZER(nullptr);

    /// Four bone weights matching pBoneIndices0.
    const RadientFloat4* pBoneWeights0 DEFAULT_INITIALIZER(nullptr);

    /// Number of vertices.
    Uint32 VertexCount DEFAULT_INITIALIZER(0);

    /// Index data. Type is controlled by IndexType.
    const void* pIndices DEFAULT_INITIALIZER(nullptr);

    /// Number of indices.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);

    /// Index type.
    RADIENT_INDEX_TYPE IndexType DEFAULT_INITIALIZER(RADIENT_INDEX_TYPE_NONE);

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
    IRadientTextureAsset* pBaseColorTexture DEFAULT_INITIALIZER(nullptr);

    /// Optional metallic-roughness texture.
    IRadientTextureAsset* pMetallicRoughnessTexture DEFAULT_INITIALIZER(nullptr);

    /// Optional normal texture.
    IRadientTextureAsset* pNormalTexture DEFAULT_INITIALIZER(nullptr);

    /// Optional occlusion texture.
    IRadientTextureAsset* pOcclusionTexture DEFAULT_INITIALIZER(nullptr);

    /// Optional emissive texture.
    IRadientTextureAsset* pEmissiveTexture DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMaterialCreateInfo RadientMaterialCreateInfo;


/// Texture load attributes.
/// Optional callback used to release memory passed through RadientTextureLoadInfo::pData or
/// RadientTextureLoadInfo::pTextureData->pData.
/// The callback is invoked when Radient no longer needs the source memory.
/// The callback may be invoked from any thread.
typedef void (*RadientTextureReleaseDataCallbackType)(const void* pData, Uint64 DataSize, void* pUserData);

/// Texture format.
DILIGENT_TYPED_ENUM(RADIENT_TEXTURE_FORMAT, Uint8){
    /// Unknown format.
    RADIENT_TEXTURE_FORMAT_UNKNOWN = 0,

    /// One 8-bit unsigned normalized component.
    RADIENT_TEXTURE_FORMAT_R8_UNORM,

    /// Two 8-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RG8_UNORM,

    /// Four 8-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,

    /// Four 8-bit unsigned normalized components with sRGB-encoded color data.
    RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB,

    /// One 8-bit unsigned integer component.
    RADIENT_TEXTURE_FORMAT_R8_UINT,

    /// Two 8-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RG8_UINT,

    /// Four 8-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RGBA8_UINT,

    /// One 8-bit signed integer component.
    RADIENT_TEXTURE_FORMAT_R8_SINT,

    /// Two 8-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RG8_SINT,

    /// Four 8-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RGBA8_SINT,

    /// One 16-bit unsigned normalized component.
    RADIENT_TEXTURE_FORMAT_R16_UNORM,

    /// Two 16-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RG16_UNORM,

    /// Four 16-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RGBA16_UNORM,

    /// One 16-bit unsigned integer component.
    RADIENT_TEXTURE_FORMAT_R16_UINT,

    /// Two 16-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RG16_UINT,

    /// Four 16-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RGBA16_UINT,

    /// One 16-bit signed integer component.
    RADIENT_TEXTURE_FORMAT_R16_SINT,

    /// Two 16-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RG16_SINT,

    /// Four 16-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RGBA16_SINT,

    /// One 32-bit unsigned integer component.
    RADIENT_TEXTURE_FORMAT_R32_UINT,

    /// Two 32-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RG32_UINT,

    /// Four 32-bit unsigned integer components.
    RADIENT_TEXTURE_FORMAT_RGBA32_UINT,

    /// One 32-bit signed integer component.
    RADIENT_TEXTURE_FORMAT_R32_SINT,

    /// Two 32-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RG32_SINT,

    /// Four 32-bit signed integer components.
    RADIENT_TEXTURE_FORMAT_RGBA32_SINT,

    /// One 32-bit floating-point component.
    RADIENT_TEXTURE_FORMAT_R32_FLOAT,

    /// Two 32-bit floating-point components.
    RADIENT_TEXTURE_FORMAT_RG32_FLOAT,

    /// Four 32-bit floating-point components.
    RADIENT_TEXTURE_FORMAT_RGBA32_FLOAT};

/// Texture source data.
struct RadientTextureData
{
    /// Texture width in pixels.
    Uint32 Width DEFAULT_INITIALIZER(0);

    /// Texture height in pixels.
    Uint32 Height DEFAULT_INITIALIZER(0);

    /// Texture format.
    RADIENT_TEXTURE_FORMAT Format DEFAULT_INITIALIZER(RADIENT_TEXTURE_FORMAT_UNKNOWN);

    /// Pointer to mip 0 pixel data.
    const void* pData DEFAULT_INITIALIZER(nullptr);

    /// Row stride, in bytes. If zero, Radient derives tightly packed stride from Format and Width.
    /// Stride must be at least the active row size.
    Uint32 Stride DEFAULT_INITIALIZER(0);
};
typedef struct RadientTextureData RadientTextureData;

struct RadientTextureLoadInfo
{
    /// Source URI. For memory-backed textures, this is optional and may be used as the texture identity
    /// for asset cache lookup and debugging.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional URI of the asset that references URI. Relative URI sources are resolved against this
    /// value by the active asset resolver.
    const Char* BaseURI DEFAULT_INITIALIZER(nullptr);

    /// Optional pointer to encoded texture data.
    const void* pData DEFAULT_INITIALIZER(nullptr);

    /// Size of the encoded texture data, in bytes.
    Uint64 DataSize DEFAULT_INITIALIZER(0);

    /// Optional pointer to texture data. Only 2D texture data is currently supported.
    /// Mip 0 data must be provided; Radient always generates mip levels.
    const RadientTextureData* pTextureData DEFAULT_INITIALIZER(nullptr);

    /// Optional callback to release pData or pTextureData->pData when Radient no longer needs it.
    /// For pTextureData, DataSize is the minimum source span described by RadientTextureData::pData.
    /// If this callback is null and memory-backed source data is not null, Radient makes an internal copy of the data.
    /// The callback may be invoked from any thread.
    RadientTextureReleaseDataCallbackType ReleaseData DEFAULT_INITIALIZER(nullptr);

    /// User data passed to ReleaseData.
    void* pReleaseDataUserData DEFAULT_INITIALIZER(nullptr);

    /// Interpret the texture as sRGB.
    Bool IsSRGB DEFAULT_INITIALIZER(False);
};
typedef struct RadientTextureLoadInfo RadientTextureLoadInfo;


/// Scene/model load attributes.
struct RadientSceneLoadInfo
{
    /// Source URI. The scheme may identify a local file, remote resource, or memory-backed source.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Source format. AUTO infers the format from the URI.
    RADIENT_SCENE_FORMAT Format DEFAULT_INITIALIZER(RADIENT_SCENE_FORMAT_AUTO);
};
typedef struct RadientSceneLoadInfo RadientSceneLoadInfo;

// {81E53AF7-3CBD-4750-ACA9-72D301E8E286}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAsset =
    {0x81e53af7, 0x3cbd, 0x4750, {0xac, 0xa9, 0x72, 0xd3, 0x1, 0xe8, 0xe2, 0x86}};

// {DADF2017-67FE-485A-802D-98C607831509}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshAsset =
    {0xdadf2017, 0x67fe, 0x485a, {0x80, 0x2d, 0x98, 0xc6, 0x7, 0x83, 0x15, 0x9}};

// {D73346B3-A9F9-4FBB-9803-8B60C6D60E4A}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialAsset =
    {0xd73346b3, 0xa9f9, 0x4fbb, {0x98, 0x3, 0x8b, 0x60, 0xc6, 0xd6, 0xe, 0x4a}};

// {A24C6739-3521-4517-9D5E-0A0D2C8F4BC3}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientTextureAsset =
    {0xa24c6739, 0x3521, 0x4517, {0x9d, 0x5e, 0xa, 0xd, 0x2c, 0x8f, 0x4b, 0xc3}};

// {E7A3CF57-50F4-4D57-BC23-A75814DC6B93}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSceneAsset =
    {0xe7a3cf57, 0x50f4, 0x4d57, {0xbc, 0x23, 0xa7, 0x58, 0x14, 0xdc, 0x6b, 0x93}};

// {F7333555-956A-4CE1-83EB-BAC3D2E9E0BD}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetManager =
    {0xf7333555, 0x956a, 0x4ce1, {0x83, 0xeb, 0xba, 0xc3, 0xd2, 0xe9, 0xe0, 0xbd}};


#define DILIGENT_INTERFACE_NAME IRadientAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAssetInclusiveMethods \
    IObjectInclusiveMethods;          \
    IRadientAssetMethods RadientAsset

// clang-format off

/// Common base interface for all Radient assets.
DILIGENT_BEGIN_INTERFACE(IRadientAsset, IObject)
{
    /// Returns the asset reference identity.
    VIRTUAL const RadientAssetReference REF METHOD(GetReference)(THIS) CONST PURE;

    /// Returns the concrete asset type.
    VIRTUAL RADIENT_ASSET_TYPE METHOD(GetType)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_CPP_INTERFACE

/// Mesh asset.
struct IRadientMeshAsset : public IRadientAsset
{
};

/// Material asset.
struct IRadientMaterialAsset : public IRadientAsset
{
};

/// Texture asset.
struct IRadientTextureAsset : public IRadientAsset
{
};

/// Imported scene/model asset.
struct IRadientSceneAsset : public IRadientAsset
{
};

#endif


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
    ///
    /// The returned status reports asset payload creation and source-data processing. A successful
    /// status does not guarantee that all GPU upload work has completed.
    VIRTUAL RADIENT_STATUS METHOD(CreateMesh)(THIS_
                                              const RadientMeshCreateInfo REF MeshCI,
                                              IRadientMeshAsset**             ppMesh) PURE;

    /// Creates a material asset.
    VIRTUAL RADIENT_STATUS METHOD(CreateMaterial)(THIS_
                                                  const RadientMaterialCreateInfo REF MaterialCI,
                                                  IRadientMaterialAsset**             ppMaterial) PURE;

    /// Starts loading a texture asset from a URI or texture data.
    ///
    /// The returned status reports source loading and GPU upload scheduling. A successful status
    /// does not guarantee that the texture is already available for sampling.
    /// Returns RADIENT_STATUS_PENDING when loading continues asynchronously.
    VIRTUAL RADIENT_STATUS METHOD(LoadTexture)(THIS_
                                               const RadientTextureLoadInfo REF LoadInfo,
                                               IRadientTextureAsset**           ppTexture) PURE;

    /// Starts loading an authored scene asset from a URI.
    ///
    /// The returned status reports scene loading and GPU upload scheduling. A successful status
    /// does not guarantee that all GPU resources referenced by the scene are ready.
    /// Returns RADIENT_STATUS_PENDING when loading continues asynchronously.
    VIRTUAL RADIENT_STATUS METHOD(LoadScene)(THIS_
                                             const RadientSceneLoadInfo REF LoadInfo,
                                             IRadientSceneAsset**           ppScene) PURE;

    /// Blocks the calling thread until asset payload loading and GPU upload scheduling have completed.
    ///
    /// This does not wait for the render thread to execute queued GPU upload callbacks. Resource-specific
    /// accessors may still report that GPU resources are not ready after this method returns RADIENT_STATUS_OK.
    /// This is intended for tests and explicit synchronization points only; normal rendering/import code should avoid it.
    VIRTUAL RADIENT_STATUS METHOD(WaitForAssetLoad)(THIS_
                                                    IRadientAsset* pAsset) PURE;

    /// Permanently stops the asset manager's internal GPU upload work.
    ///
    /// The method must be called before destroying a GPU-backed asset manager. pContext must be the
    /// device context used by the renderer/update path and must not be used concurrently while Stop()
    /// is executing.
    VIRTUAL RADIENT_STATUS METHOD(Stop)(THIS_
                                        IDeviceContext* pContext) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetManager_GetDesc(This)                 CALL_IFACE_METHOD(RadientAssetManager, GetDesc,        This)
#    define IRadientAssetManager_CreateMesh(This, ...)         CALL_IFACE_METHOD(RadientAssetManager, CreateMesh,     This, __VA_ARGS__)
#    define IRadientAssetManager_CreateMaterial(This, ...)     CALL_IFACE_METHOD(RadientAssetManager, CreateMaterial, This, __VA_ARGS__)
#    define IRadientAssetManager_LoadTexture(This, ...)        CALL_IFACE_METHOD(RadientAssetManager, LoadTexture,    This, __VA_ARGS__)
#    define IRadientAssetManager_LoadScene(This, ...)          CALL_IFACE_METHOD(RadientAssetManager, LoadScene,      This, __VA_ARGS__)
#    define IRadientAssetManager_WaitForAssetLoad(This, ...)   CALL_IFACE_METHOD(RadientAssetManager, WaitForAssetLoad, This, __VA_ARGS__)
#    define IRadientAssetManager_Stop(This, ...)               CALL_IFACE_METHOD(RadientAssetManager, Stop,           This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
