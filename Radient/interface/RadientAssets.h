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
#include "RadientDataBlob.h"
#include "RadientVertexLayout.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAsset                   IRadientAsset;
typedef struct IRadientMeshAsset               IRadientMeshAsset;
typedef struct IRadientMaterialAsset           IRadientMaterialAsset;
typedef struct IRadientMaterialDefinitionAsset IRadientMaterialDefinitionAsset;
typedef struct IRadientTextureAsset            IRadientTextureAsset;
typedef struct IRadientSceneAsset              IRadientSceneAsset;
typedef struct IRadientSkeletonAsset           IRadientSkeletonAsset;
typedef struct IRadientSkinAsset               IRadientSkinAsset;
typedef struct IRadientAnimationClipAsset      IRadientAnimationClipAsset;
typedef struct IRadientMorphTargetWeights      IRadientMorphTargetWeights;
typedef struct IDeviceContext                  IDeviceContext;

typedef struct RadientStandardMaterialDefinitionCreateInfo RadientStandardMaterialDefinitionCreateInfo;
typedef struct RadientSkeletonDesc                         RadientSkeletonDesc;
typedef struct RadientSkinDesc                             RadientSkinDesc;
typedef struct RadientAnimationClipDesc                    RadientAnimationClipDesc;
typedef struct RadientMorphTargetCreateInfo                RadientMorphTargetCreateInfo;
typedef struct RadientMorphTargetDesc                      RadientMorphTargetDesc;

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
    RADIENT_ASSET_TYPE_SCENE,

    /// Material definition asset.
    RADIENT_ASSET_TYPE_MATERIAL_DEFINITION,

    /// Immutable skeleton hierarchy asset.
    RADIENT_ASSET_TYPE_SKELETON,

    /// Immutable mapping from a mesh skin palette to skeleton joints.
    RADIENT_ASSET_TYPE_SKIN,

    /// Immutable, unbound animation clip.
    RADIENT_ASSET_TYPE_ANIMATION_CLIP
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
    /// The geometry has no index buffer and is drawn directly from its vertices.
    RADIENT_INDEX_TYPE_NONE = 0,

    /// 16-bit unsigned indices.
    RADIENT_INDEX_TYPE_UINT16,

    /// 32-bit unsigned indices.
    RADIENT_INDEX_TYPE_UINT32
};

// clang-format on


/// GPU resource pool creation attributes used by the engine's asset manager.
/// These settings control resource capacity, not rendering feature availability.
struct RadientResourceManagerCreateInfo
{
    /// Initial size and growth increment of each index buffer, in bytes.
    /// Zero selects the engine default.
    Uint32 IndexBufferSize DEFAULT_INITIALIZER(4u * 1024u * 1024u);

    /// Maximum size of each index buffer, in bytes. Zero prevents growth beyond IndexBufferSize.
    Uint64 MaxIndexBufferSize DEFAULT_INITIALIZER(256ull * 1024ull * 1024ull);

    /// Initial size and growth increment of the morph-target buffer, in bytes.
    /// Zero selects the engine default.
    Uint32 MorphTargetBufferSize DEFAULT_INITIALIZER(1024u * 1024u);

    /// Maximum morph-target buffer size, in bytes. Zero prevents growth beyond MorphTargetBufferSize.
    Uint64 MaxMorphTargetBufferSize DEFAULT_INITIALIZER(256ull * 1024ull * 1024ull);

    /// Vertex capacity of each new vertex pool. Zero selects the engine default.
    /// Rounded up to a multiple of 1024 vertices.
    Uint32 VertexPoolSize DEFAULT_INITIALIZER(64u * 1024u);

    /// Starting width and height of each default texture atlas, in texels.
    /// Zero selects the engine default.
    Uint32 TextureAtlasSize DEFAULT_INITIALIZER(2048);

    /// Maximum mip-level-0 size of one atlas slice, in bytes. Zero disables this limit.
    /// Both dimensions are halved until the slice fits, without reducing either below 16.
    /// The minimum dimensions take precedence when the limit is too small.
    Uint64 TextureAtlasMipLevel0Size DEFAULT_INITIALIZER(16ull * 1024ull * 1024ull);

    /// Initial number of slices in each texture atlas. Zero defers storage allocation.
    Uint32 TextureAtlasSlices DEFAULT_INITIALIZER(1);

    /// Maximum number of slices in each texture atlas. Zero selects the engine default.
    Uint32 TextureAtlasMaxSlices DEFAULT_INITIALIZER(2048);
};
typedef struct RadientResourceManagerCreateInfo RadientResourceManagerCreateInfo;


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

    /// Zero-based index element offset in RadientMeshCreateInfo::pIndexBuffer.
    Uint32 FirstIndex DEFAULT_INITIALIZER(0);

    /// Number of indices in RadientMeshCreateInfo::pIndexBuffer.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);

    /// Default material for this primitive.
    IRadientMaterialAsset* pMaterial DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMeshPrimitiveCreateInfo RadientMeshPrimitiveCreateInfo;


/// CPU-side mesh creation attributes.
///
/// Vertex data is described by VertexLayout and ppVertexBuffers; index data is
/// supplied through pIndexBuffer. Radient copies the layout arrays and semantic
/// strings and retains the referenced blobs before CreateMesh returns, including
/// when uploads are asynchronous. The caller can then modify or release the
/// descriptors and its blob references. Vertex and index bytes are read without
/// copying the input buffers and remain under shared read access
/// until source processing finishes. End write access before calling CreateMesh;
/// an active writer returns RADIENT_STATUS_INVALID_OPERATION. Writes and resizing
/// are unavailable while Radient is reading a mutable blob. OnLastReaderReleased
/// can be used to recycle its storage after acquiring write access; it does not
/// signal GPU completion. Reference blobs follow their normal storage-lifetime
/// requirements. The renderer selects its storage layout and converts source
/// attributes as needed; VertexLayout describes only the supplied CPU bytes.
struct RadientMeshCreateInfo
{
    /// Mesh name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Layout of the source vertex buffers. Automatic offsets and strides are
    /// resolved before reading the data. A nonempty layout with a three-component
    /// POSITION attribute is required. JOINTS_0 and WEIGHTS_0, when present, occur
    /// together and each has four components; JOINTS_0 uses non-normalized unsigned
    /// integers or FLOAT32 components. Conversion to the renderer's storage format
    /// supports integer and FLOAT32 source components. FLOAT16 conversion is
    /// unsupported by the current renderer.
    /// The default empty layout is invalid for mesh creation.
    RadientVertexLayoutDesc VertexLayout;

    /// Array of VertexLayout.BufferCount source data blobs. Required and non-null
    /// for mesh creation. An attribute's BufferIndex selects its blob; ByteOffset
    /// is relative to the blob's first byte. Each referenced entry must be non-null
    /// and contain every attribute value through the last vertex:
    /// (VertexCount - 1) * resolved ByteStride + resolved ByteOffset + element size.
    /// Trailing padding after the final attribute value is optional. Insufficient
    /// or unaddressable storage returns RADIENT_STATUS_INVALID_ARGUMENT. Size and
    /// data are inspected under read access. Unreferenced entries are ignored and
    /// may be null. The same blob may appear in multiple entries. Radient retains
    /// the blobs, so the pointer array and caller references can be released after
    /// CreateMesh returns. Both read-only and mutable blobs are accepted. The
    /// default is nullptr.
    IRadientDataBlob* const* ppVertexBuffers DEFAULT_INITIALIZER(nullptr);

    /// Number of vertex records in every referenced buffer. Must be nonzero.
    /// This count also defines the vertex domain for indices and morph targets.
    Uint32 VertexCount DEFAULT_INITIALIZER(0);

    /// Morph targets whose attribute streams use the mesh vertex domain. Each
    /// target attribute contains VertexCount vertex deltas. Must not be null
    /// when MorphTargetCount is nonzero.
    const RadientMorphTargetCreateInfo* pMorphTargets DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pMorphTargets.
    Uint32 MorphTargetCount DEFAULT_INITIALIZER(0);

    /// Blob containing IndexCount tightly packed indices starting at its first byte.
    /// Required and non-null. IndexType determines the element size; the blob must
    /// contain at least IndexCount * element size bytes. Additional bytes are ignored.
    /// Insufficient or unaddressable storage returns RADIENT_STATUS_INVALID_ARGUMENT.
    /// Radient retains the blob and acquires shared read access before CreateMesh
    /// returns, without copying the source bytes. Size and data are checked under
    /// read access, which lasts until index processing finishes. Both read-only and
    /// mutable blobs are accepted; an active writer returns RADIENT_STATUS_INVALID_OPERATION.
    /// The caller may release its blob reference after CreateMesh returns. Reference
    /// blobs retain their usual external-storage lifetime requirements.
    IRadientDataBlob* pIndexBuffer DEFAULT_INITIALIZER(nullptr);

    /// Number of indices in pIndexBuffer. Must be nonzero.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);

    /// Encoding of each index in pIndexBuffer. Mesh creation requires UINT16 or
    /// UINT32; the default NONE is invalid. The renderer selects its stored format.
    RADIENT_INDEX_TYPE IndexType DEFAULT_INITIALIZER(RADIENT_INDEX_TYPE_NONE);

    /// Mesh primitives.
    const RadientMeshPrimitiveCreateInfo* pPrimitives DEFAULT_INITIALIZER(nullptr);

    /// Number of primitives.
    Uint32 PrimitiveCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMeshCreateInfo RadientMeshCreateInfo;


/// Immutable description of one geometry's stored vertex and index data.
///
/// Multiple primitives can reference the same geometry while selecting different
/// draw ranges and default materials. Counts describe the complete geometry;
/// RadientMeshPrimitiveDesc describes the range used by each primitive.
struct RadientMeshGeometryDesc
{
    /// Renderer-selected layout of the stored vertex data. Attribute offsets and
    /// buffer strides are explicit; automatic packing values do not appear here.
    /// The layout contains only active logical streams, numbered consecutively
    /// from zero. BufferIndex identifies a stream within this layout, not a GPU
    /// binding slot. ByteOffset is relative to a vertex record and excludes any
    /// GPU pool or allocation offset. The layout may differ from the source layout
    /// supplied during mesh creation, including attribute encodings and defaults.
    /// The mesh owns the arrays and semantic strings for its lifetime.
    RadientVertexLayoutDesc VertexLayout DEFAULT_INITIALIZER({});

    /// Number of vertex records in every logical stream in VertexLayout.
    /// Indices and non-indexed primitive ranges refer to this vertex domain.
    Uint32 VertexCount DEFAULT_INITIALIZER(0);

    /// Encoding of the stored indices. NONE identifies non-indexed geometry,
    /// whose primitives select vertices directly. This describes the renderer's
    /// stored representation, which may differ from the input index encoding.
    RADIENT_INDEX_TYPE IndexType DEFAULT_INITIALIZER(RADIENT_INDEX_TYPE_NONE);

    /// Total number of stored indices. Zero when IndexType is NONE.
    /// Indexed primitive ranges select elements within this index domain.
    Uint32 IndexCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMeshGeometryDesc RadientMeshGeometryDesc;


/// Immutable description of one mesh primitive's draw range and default material.
///
/// GeometryIndex selects the shared geometry that supplies vertex and index data.
/// The referenced geometry's IndexType determines whether FirstElement and
/// ElementCount count indices or vertices.
struct RadientMeshPrimitiveDesc
{
    /// Optional null-terminated primitive name, or nullptr when no name is set.
    /// The mesh owns the string, which remains valid while the mesh is retained.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Zero-based index into RadientMeshAssetDesc::pGeometries. Less than the mesh
    /// description's GeometryCount for every primitive in a loaded mesh.
    Uint32 GeometryIndex DEFAULT_INITIALIZER(0);

    /// Zero-based first index when the geometry is indexed, or first vertex when
    /// its IndexType is NONE. Relative to the referenced geometry's data, without
    /// any GPU pool or allocation offset; measured in elements, not bytes.
    Uint32 FirstElement DEFAULT_INITIALIZER(0);

    /// Number of indices when the geometry is indexed, or vertices when its
    /// IndexType is NONE. FirstElement + ElementCount does not exceed the geometry's
    /// IndexCount for indexed geometry, or VertexCount for non-indexed geometry.
    Uint32 ElementCount DEFAULT_INITIALIZER(0);

    /// Default material assigned to this primitive, or nullptr when none is set.
    /// Does not include material overrides applied to scene instances. The mesh
    /// retains the material; this pointer remains valid while the mesh is retained.
    IRadientMaterialAsset* pMaterial DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMeshPrimitiveDesc RadientMeshPrimitiveDesc;


/// Immutable description of a mesh's stored geometry, primitives, and morph targets.
///
/// Available after successful CPU loading, independently of GPU upload completion.
/// Before loading succeeds, all counts are zero and all array pointers are null.
/// The mesh owns the descriptor arrays and referenced strings and retains the
/// default materials. All referenced data remains valid while the mesh is retained.
struct RadientMeshAssetDesc
{
    /// Array of GeometryCount descriptions of stored vertex and index data.
    /// Primitive GeometryIndex values select elements of this array. Null when
    /// GeometryCount is zero. The mesh owns this array and all nested layouts.
    const RadientMeshGeometryDesc* pGeometries DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pGeometries. Multiple primitives may share a geometry.
    Uint32 GeometryCount DEFAULT_INITIALIZER(0);

    /// Array of PrimitiveCount primitive descriptions, including geometry indices,
    /// draw ranges, and default materials. Null when PrimitiveCount is zero.
    /// The mesh owns this array and its names and retains its non-null materials.
    const RadientMeshPrimitiveDesc* pPrimitives DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pPrimitives.
    Uint32 PrimitiveCount DEFAULT_INITIALIZER(0);

    /// Array of MorphTargetCount morph-target descriptions. The array, its
    /// attribute descriptions, and all referenced strings remain valid while
    /// the mesh asset is retained. Null when MorphTargetCount is zero.
    const RadientMorphTargetDesc* pMorphTargets DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pMorphTargets.
    Uint32 MorphTargetCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMeshAssetDesc RadientMeshAssetDesc;


/// Typed color formats for texture data and texture asset descriptions.
/// UNORM components represent values in [0, 1]; SNORM components represent values in [-1, 1].
/// The formats accepted as decoded input are listed in RadientTextureData::Format.
DILIGENT_TYPED_ENUM(RADIENT_TEXTURE_FORMAT, Uint8){
    /// Unknown format.
    RADIENT_TEXTURE_FORMAT_UNKNOWN = 0,

    /// One 8-bit unsigned normalized component.
    RADIENT_TEXTURE_FORMAT_R8_UNORM,

    /// Two 8-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RG8_UNORM,

    /// Four 8-bit unsigned normalized components.
    RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,

    /// Four 8-bit components with sRGB-encoded RGB and unsigned normalized alpha.
    RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB,

    /// One 8-bit signed normalized component.
    RADIENT_TEXTURE_FORMAT_R8_SNORM,

    /// Two 8-bit signed normalized components.
    RADIENT_TEXTURE_FORMAT_RG8_SNORM,

    /// Four 8-bit signed normalized components.
    RADIENT_TEXTURE_FORMAT_RGBA8_SNORM,

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

    /// One 16-bit signed normalized component.
    RADIENT_TEXTURE_FORMAT_R16_SNORM,

    /// Two 16-bit signed normalized components.
    RADIENT_TEXTURE_FORMAT_RG16_SNORM,

    /// Four 16-bit signed normalized components.
    RADIENT_TEXTURE_FORMAT_RGBA16_SNORM,

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
    RADIENT_TEXTURE_FORMAT_RGBA32_FLOAT,

    /// BC1 block compression for RGB and optional one-bit alpha, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 8 bytes.
    RADIENT_TEXTURE_FORMAT_BC1_UNORM,

    /// BC1 block compression for RGB and optional one-bit alpha, with sRGB-encoded RGB and unsigned normalized alpha.
    /// Each 4x4 pixel block occupies 8 bytes.
    RADIENT_TEXTURE_FORMAT_BC1_UNORM_SRGB,

    /// BC2 block compression for RGB and explicit four-bit alpha, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC2_UNORM,

    /// BC2 block compression for RGB and explicit four-bit alpha, with sRGB-encoded RGB and unsigned normalized alpha.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC2_UNORM_SRGB,

    /// BC3 block compression for RGB and interpolated alpha, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC3_UNORM,

    /// BC3 block compression for RGB and interpolated alpha, with sRGB-encoded RGB and unsigned normalized alpha.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC3_UNORM_SRGB,

    /// BC4 block compression for one R component, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 8 bytes.
    RADIENT_TEXTURE_FORMAT_BC4_UNORM,

    /// BC4 block compression for one R component, with signed normalized components.
    /// Each 4x4 pixel block occupies 8 bytes.
    RADIENT_TEXTURE_FORMAT_BC4_SNORM,

    /// BC5 block compression for R and G components, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC5_UNORM,

    /// BC5 block compression for R and G components, with signed normalized components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC5_SNORM,

    /// BC6H block compression for RGB components, with unsigned half-precision floating-point components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC6H_UF16,

    /// BC6H block compression for RGB components, with signed half-precision floating-point components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC6H_SF16,

    /// BC7 block compression for RGB and alpha components, with unsigned normalized components.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC7_UNORM,

    /// BC7 block compression for RGB and alpha components, with sRGB-encoded RGB and unsigned normalized alpha.
    /// Each 4x4 pixel block occupies 16 bytes.
    RADIENT_TEXTURE_FORMAT_BC7_UNORM_SRGB,
};

/// Immutable description of a loaded texture asset.
/// Describes the logical image after decoding and mip generation. The dimensions,
/// format, and mip count are independent of GPU allocation, atlas placement, and
/// mip residency. All members have their default values until CPU loading succeeds.
struct RadientTextureAssetDesc
{
    /// Width of mip 0, in pixels, for each array layer or cube face.
    /// Nonzero after CPU loading succeeds; defaults to zero.
    Uint32 Width DEFAULT_INITIALIZER(0);

    /// Height of mip 0, in pixels, for each array layer or cube face.
    /// One for 1D textures. Nonzero after CPU loading succeeds; defaults to zero.
    Uint32 Height DEFAULT_INITIALIZER(0);

    /// Loaded image format, including any channel expansion or encoding conversion
    /// performed during loading. This is independent of the storage format and of
    /// the linear or sRGB sampling view selected by a material.
    /// RADIENT_TEXTURE_FORMAT_UNKNOWN if CPU loading has not succeeded or the loaded
    /// format has no corresponding public enum value. Defaults to UNKNOWN.
    RADIENT_TEXTURE_FORMAT Format DEFAULT_INITIALIZER(RADIENT_TEXTURE_FORMAT_UNKNOWN);

    /// Number of mip levels per array layer or cube face in the loaded or generated
    /// logical image, including mip 0.
    /// This count does not indicate how many levels are currently resident on the GPU
    /// or available through a texture atlas. Nonzero after CPU loading succeeds;
    /// defaults to zero.
    Uint32 MipLevels DEFAULT_INITIALIZER(0);
};
typedef struct RadientTextureAssetDesc RadientTextureAssetDesc;


/// Decoded mip 0 data for a 2D texture. The descriptor is copied by LoadTexture.
struct RadientTextureData
{
    /// Texture width in pixels. Must be nonzero; defaults to zero.
    Uint32 Width DEFAULT_INITIALIZER(0);

    /// Texture height in pixels. Must be nonzero; defaults to zero.
    Uint32 Height DEFAULT_INITIALIZER(0);

    /// Pixel format. Accepts all uncompressed RADIENT_TEXTURE_FORMAT values, including SNORM.
    /// Compressed BC formats are only supported through encoded texture sources.
    /// Must not be RADIENT_TEXTURE_FORMAT_UNKNOWN, which is the default.
    RADIENT_TEXTURE_FORMAT Format DEFAULT_INITIALIZER(RADIENT_TEXTURE_FORMAT_UNKNOWN);

    /// Required blob containing mip 0 pixel data, starting at byte zero.
    /// Accepts read-only or mutable blobs. Its size must cover every active source row:
    /// (Height - 1) * effective stride + active row size. The effective stride is Stride
    /// when nonzero, otherwise the active row size derived from Format and Width.
    /// The final row does not require trailing padding; extra bytes are ignored.
    /// The read pointer must be aligned to the format's component size (1, 2, or 4 bytes).
    /// Insufficient size or component misalignment returns RADIENT_STATUS_INVALID_ARGUMENT.
    /// LoadTexture retains the blob and acquires read access before returning, then
    /// consumes the pixels directly without copying or repacking the source rows.
    /// Read access lasts while loading or upload preparation references these pixels.
    /// Other readers are allowed; writes and resizing remain blocked until all readers
    /// finish. An active writer causes LoadTexture to return RADIENT_STATUS_INVALID_OPERATION.
    /// The caller may release its blob reference after LoadTexture returns. For REFERENCE
    /// storage, the bytes remain alive and unchanged for the blob's entire lifetime;
    /// RadientDataBlobCreateInfo::OnDestroy can release their owner. Last-reader callbacks
    /// may run during LoadTexture or later on a worker thread; they do not indicate GPU
    /// upload completion. Defaults to nullptr, which is invalid for decoded texture input.
    IRadientDataBlob* pDataBlob DEFAULT_INITIALIZER(nullptr);

    /// Row stride, in bytes. If zero, Radient derives tightly packed stride from Format and Width.
    /// Stride must be at least the active row size and, when Height is greater than one,
    /// a multiple of the format's component size, so each row remains component-aligned.
    Uint32 Stride DEFAULT_INITIALIZER(0);
};
typedef struct RadientTextureData RadientTextureData;

/// Texture load attributes. Selects encoded bytes, decoded pixels, or a URI source.
struct RadientTextureLoadInfo
{
    /// Source URI. For memory-backed textures, this is optional and may be used as the texture identity
    /// for asset cache lookup and debugging.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional URI of the asset that references URI. Relative URI sources are resolved against this
    /// value by the active asset resolver.
    const Char* BaseURI DEFAULT_INITIALIZER(nullptr);

    /// Optional blob containing the complete encoded texture, starting at byte zero.
    /// Accepts read-only or mutable blobs. The blob must be non-empty and its size must fit in size_t.
    /// Mutually exclusive with pTextureData. Radient retains the blob and acquires read access during
    /// LoadTexture(), before returning. The data pointer and size are checked within this read scope.
    /// The encoded bytes are consumed directly; LoadTexture() does not copy them.
    /// Read access remains active until Radient no longer needs the source bytes, including any
    /// decoder references used during upload preparation. The caller may release its blob reference
    /// after LoadTexture() returns. For REFERENCE storage, the caller keeps the referenced bytes alive
    /// and unchanged throughout the blob's lifetime; OnDestroy can release their owner. Other readers
    /// may access the blob while Radient is reading it. Mutable blobs cannot be written or resized
    /// until all readers finish. Finish any write access before calling LoadTexture(); an active writer
    /// causes the call to return RADIENT_STATUS_INVALID_OPERATION without creating a texture asset.
    /// If acquiring read access fails, no matching EndRead() or last-reader callback is performed.
    /// Once acquired, access is released on completion or failure. OnLastReaderReleased, if set,
    /// may run before LoadTexture() returns or later on a worker thread. This notification reports
    /// that all readers have finished; it does not indicate GPU upload completion or end the lifetime
    /// requirement for REFERENCE storage. See RadientDataBlobCreateInfo for callback details.
    IRadientDataBlob* pDataBlob DEFAULT_INITIALIZER(nullptr);

    /// Optional pointer to decoded texture data, mutually exclusive with pDataBlob.
    /// Only 2D texture data is currently supported. Mip 0 data must be provided; Radient always
    /// generates mip levels. The descriptor is copied during LoadTexture(), and its pDataBlob
    /// is retained with read access while the pixels are needed; see RadientTextureData::pDataBlob.
    const RadientTextureData* pTextureData DEFAULT_INITIALIZER(nullptr);

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


/// Immutable description of an imported scene asset.
struct RadientSceneAssetDesc
{
    /// Array of AnimationClipCount animation clips imported with the scene.
    /// The array and its pointers remain valid while the scene asset is
    /// retained. Clips preserve source-animation order, so the array index is
    /// the stable identity within the scene even when names are empty or
    /// duplicated. Individual clip name, duration, targets, and channels are
    /// available through IRadientAnimationClipAsset::GetDesc(). May be null
    /// when AnimationClipCount is zero.
    IRadientAnimationClipAsset* const* ppAnimationClips DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in ppAnimationClips.
    Uint32 AnimationClipCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientSceneAssetDesc RadientSceneAssetDesc;

// {81E53AF7-3CBD-4750-ACA9-72D301E8E286}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAsset =
    {0x81e53af7, 0x3cbd, 0x4750, {0xac, 0xa9, 0x72, 0xd3, 0x1, 0xe8, 0xe2, 0x86}};

// {DADF2017-67FE-485A-802D-98C607831509}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshAsset =
    {0xdadf2017, 0x67fe, 0x485a, {0x80, 0x2d, 0x98, 0xc6, 0x7, 0x83, 0x15, 0x9}};

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

#define DILIGENT_INTERFACE_NAME IRadientMeshAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMeshAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;        \
    IRadientMeshAssetMethods RadientMeshAsset

// clang-format off

/// Immutable mesh asset with geometry, primitive, and morph-target descriptions.
DILIGENT_BEGIN_INTERFACE(IRadientMeshAsset, IRadientAsset)
{
    /// Returns the immutable mesh description. The description is empty until
    /// CPU loading completes successfully; GPU upload completion is not required.
    /// The returned reference, nested arrays, names, and material pointers remain
    /// valid while the mesh asset is retained.
    VIRTUAL const RadientMeshAssetDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Creates mutable weights initialized to the mesh morph-target defaults.
    /// On success, ppWeights receives a strong reference. Meshes without morph
    /// targets produce a valid zero-length weight object.
    VIRTUAL RADIENT_STATUS METHOD(CreateMorphTargetWeights)(THIS_
                                                            IRadientMorphTargetWeights** ppWeights) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMeshAsset_GetDesc(This)                       CALL_IFACE_METHOD(RadientMeshAsset, GetDesc,                  This)
#    define IRadientMeshAsset_CreateMorphTargetWeights(This, ...) CALL_IFACE_METHOD(RadientMeshAsset, CreateMorphTargetWeights, This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientTextureAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientTextureAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;           \
    IRadientTextureAssetMethods RadientTextureAsset

// clang-format off

/// A texture asset and its immutable logical image description.
DILIGENT_BEGIN_INTERFACE(IRadientTextureAsset, IRadientAsset)
{
    /// Returns the immutable texture description. The description is empty until
    /// CPU loading completes successfully; GPU resources and upload completion
    /// are not required. Decode failures leave the description empty. Once CPU
    /// loading succeeds, the description remains available even if GPU creation
    /// or upload later fails or is cancelled, and its members do not change.
    /// The returned reference remains valid while the texture asset is retained.
    VIRTUAL const RadientTextureAssetDesc REF METHOD(GetDesc)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientTextureAsset_GetDesc(This) CALL_IFACE_METHOD(RadientTextureAsset, GetDesc, This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSceneAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSceneAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;         \
    IRadientSceneAssetMethods RadientSceneAsset

// clang-format off

/// Imported scene/model asset and its immutable animation catalog.
DILIGENT_BEGIN_INTERFACE(IRadientSceneAsset, IRadientAsset)
{
    /// Returns the immutable scene asset description. The description is empty
    /// until scene loading completes successfully. The returned reference and
    /// all data it references remain valid while the scene asset is retained.
    VIRTUAL const RadientSceneAssetDesc REF METHOD(GetDesc)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSceneAsset_GetDesc(This) CALL_IFACE_METHOD(RadientSceneAsset, GetDesc, This)

#endif

// clang-format on


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

    /// Creates an immutable skeleton hierarchy asset. The description and all
    /// joint names and transforms are copied before this method returns. On
    /// success, ppSkeleton receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreateSkeleton)(THIS_
                                                  const RadientSkeletonDesc REF SkeletonDesc,
                                                  IRadientSkeletonAsset**       ppSkeleton) PURE;

    /// Creates an immutable skin asset that maps a mesh's joint palette to a
    /// skeleton. The skin retains the skeleton and copies all joint mappings
    /// and inverse-bind matrices. On success, ppSkin receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreateSkin)(THIS_
                                              const RadientSkinDesc REF SkinDesc,
                                              IRadientSkinAsset**       ppSkin) PURE;

    /// Creates an immutable, unbound animation clip.
    ///
    /// The manager validates structural and native-storage invariants and
    /// copies the name, descriptor tables, target names, keyframe times, and
    /// value bytes before returning. It does not resolve target schemas or
    /// validate property-specific value compatibility; those checks occur when
    /// the clip is bound to a runtime instance.
    ///
    /// \param [in] ClipDesc - Clip description to validate and copy. Its data
    ///                        only needs to remain valid for this call.
    /// \param [out] ppClip  - Address of a null pointer that receives a strong
    ///                        reference on success. It remains null on failure.
    ///
    /// \return RADIENT_STATUS_OK on success, RADIENT_STATUS_INVALID_ARGUMENT
    ///         for a malformed description or output pointer,
    ///         RADIENT_STATUS_INVALID_OPERATION after Stop(), or
    ///         RADIENT_STATUS_FAILED if storage creation fails.
    VIRTUAL RADIENT_STATUS METHOD(CreateAnimationClip)(THIS_
                                                       const RadientAnimationClipDesc REF ClipDesc,
                                                       IRadientAnimationClipAsset**       ppClip) PURE;

    /// Creates or retrieves a cached built-in standard material definition asset.
    /// Compatible descriptions may return the same immutable asset. On success,
    /// ppDefinition receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreateStandardMaterialDefinition)(THIS_
                                                                    const RadientStandardMaterialDefinitionCreateInfo REF DefinitionCI,
                                                                    IRadientMaterialDefinitionAsset**                          ppDefinition) PURE;

    /// Creates a mutable material asset initialized with Definition's default
    /// parameter values. Definition must be a compatible Radient material
    /// definition asset. On success, ppMaterial receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreateMaterial)(THIS_
                                                  IRadientMaterialDefinitionAsset* pDefinition,
                                                  IRadientMaterialAsset**          ppMaterial) PURE;

    /// Starts loading a texture asset from a URI or texture data.
    ///
    /// The returned status reports source loading and GPU upload scheduling. A successful status
    /// does not guarantee that the texture is already available for sampling.
    /// Returns RADIENT_STATUS_PENDING when loading continues asynchronously.
    /// Encoded input in LoadInfo.pDataBlob or decoded input in LoadInfo.pTextureData->pDataBlob
    /// is retained with read access until the source bytes are no longer needed. An active writer
    /// prevents loading and returns RADIENT_STATUS_INVALID_OPERATION. The caller may release its
    /// blob reference after the call. Rejected loads leave ownership with the caller.
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
#    define IRadientAssetManager_CreateSkeleton(This, ...)     CALL_IFACE_METHOD(RadientAssetManager, CreateSkeleton, This, __VA_ARGS__)
#    define IRadientAssetManager_CreateSkin(This, ...)         CALL_IFACE_METHOD(RadientAssetManager, CreateSkin,     This, __VA_ARGS__)
#    define IRadientAssetManager_CreateAnimationClip(This, ...) CALL_IFACE_METHOD(RadientAssetManager, CreateAnimationClip, This, __VA_ARGS__)
#    define IRadientAssetManager_CreateStandardMaterialDefinition(This, ...) CALL_IFACE_METHOD(RadientAssetManager, CreateStandardMaterialDefinition, This, __VA_ARGS__)
#    define IRadientAssetManager_CreateMaterial(This, ...)     CALL_IFACE_METHOD(RadientAssetManager, CreateMaterial, This, __VA_ARGS__)
#    define IRadientAssetManager_LoadTexture(This, ...)        CALL_IFACE_METHOD(RadientAssetManager, LoadTexture,    This, __VA_ARGS__)
#    define IRadientAssetManager_LoadScene(This, ...)          CALL_IFACE_METHOD(RadientAssetManager, LoadScene,      This, __VA_ARGS__)
#    define IRadientAssetManager_WaitForAssetLoad(This, ...)   CALL_IFACE_METHOD(RadientAssetManager, WaitForAssetLoad, This, __VA_ARGS__)
#    define IRadientAssetManager_Stop(This, ...)               CALL_IFACE_METHOD(RadientAssetManager, Stop,           This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
