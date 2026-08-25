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
/// Defines programmable material definition and material assets.

#include "RadientAssets.h"

#include "../../../DiligentCore/Primitives/interface/FlagEnum.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"

#if DILIGENT_CPP_INTERFACE
#    include <type_traits>
#endif

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientMaterialDefinitionAsset IRadientMaterialDefinitionAsset;
typedef struct IRadientMaterialWriter          IRadientMaterialWriter;
typedef struct IRadientSurfaceMaterialAsset    IRadientSurfaceMaterialAsset;
typedef struct IRadientSurfaceMaterialWriter   IRadientSurfaceMaterialWriter;

// clang-format off

/// Concrete material definition type.
///
/// The value identifies the complete description returned by
/// IRadientMaterialDefinitionAsset::GetDesc(). After inspecting Type, the base
/// description may be cast to the corresponding concrete description.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_DEFINITION_TYPE, Uint8)
{
    /// A material that shades a geometric surface.
    RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE = 0,

    /// A full-screen post-processing material.
    RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS,

    /// A compute material.
    RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE,

    /// Number of material definition types. This value is not a valid type.
    RADIENT_MATERIAL_DEFINITION_TYPE_COUNT
};


/// Material parameter type.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_PARAMETER_TYPE, Uint8)
{
    /// Unknown or invalid parameter type.
    RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN = 0,

    /// One Bool value.
    RADIENT_MATERIAL_PARAMETER_TYPE_BOOL,

    /// One signed 32-bit integer.
    RADIENT_MATERIAL_PARAMETER_TYPE_INT,

    /// Two signed 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_INT2,

    /// Three signed 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_INT3,

    /// Four signed 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_INT4,

    /// One unsigned 32-bit integer.
    RADIENT_MATERIAL_PARAMETER_TYPE_UINT,

    /// Two unsigned 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_UINT2,

    /// Three unsigned 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_UINT3,

    /// Four unsigned 32-bit integers.
    RADIENT_MATERIAL_PARAMETER_TYPE_UINT4,

    /// One 32-bit floating-point value.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT,

    /// Two 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,

    /// Three 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3,

    /// Four 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4,

    /// A 2 by 2 matrix of 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,

    /// A 3 by 3 matrix of 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3,

    /// A 4 by 4 matrix of 32-bit floating-point values.
    RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4,

    /// A Radient texture asset. Texture arrays use ArraySize greater than one.
    RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,

    /// Number of material parameter types. This value is not a valid type.
    RADIENT_MATERIAL_PARAMETER_TYPE_COUNT
};


/// Texture coordinate address mode stored in a material parameter.
///
/// The supported numeric values match the corresponding Diligent
/// TEXTURE_ADDRESS_MODE values.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE, Uint32)
{
    /// Repeat the texture at every integer boundary.
    RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP = 1,

    /// Clamp texture coordinates to the [0, 1] range.
    RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP = 3,
};


/// Renderer-visible surface shading model.
///
/// The model identifies the lighting closure implemented by a surface
/// definition. A renderer uses it to select compatible shader logic.
DILIGENT_TYPED_ENUM(RADIENT_SURFACE_SHADING_MODEL, Uint8)
{
    /// Metallic-roughness physically based material.
    RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS = 0,

    /// Specular-glossiness physically based material.
    RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS,

    /// Unlit material whose output is the base color.
    RADIENT_SURFACE_SHADING_MODEL_UNLIT,

    /// Number of surface shading models. This value is not a valid model.
    RADIENT_SURFACE_SHADING_MODEL_COUNT
};


/// Optional renderer-visible features provided by a surface material definition.
DILIGENT_TYPED_ENUM(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS, Uint32)
{
    /// No optional material features.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE = 0u,

    /// Layer a dielectric clear coat over the base material.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT = 1u << 0u,

    /// Enable the cloth-like sheen lobe.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN = 1u << 1u,

    /// Enable anisotropic specular reflection.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY = 1u << 2u,

    /// Enable thin-film iridescence.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE = 1u << 3u,

    /// Enable light transmission through the surface.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION = 1u << 4u,

    /// Enable volume thickness and attenuation. Transmission must also be enabled.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME = 1u << 5u,

    /// Enable dielectric specular weight and color control.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR = 1u << 6u,

    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_LAST = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR,
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL =
        (RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_LAST << 1u) - 1u
};
DEFINE_FLAG_ENUM_OPERATORS(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS)


/// Surface coverage and blending mode stored in a surface material asset.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_SURFACE_MODE, Uint32)
{
    /// The material is fully opaque.
    RADIENT_MATERIAL_SURFACE_MODE_OPAQUE = 0,

    /// Fragments below AlphaCutoff are discarded.
    RADIENT_MATERIAL_SURFACE_MODE_MASKED,

    /// The material uses alpha blending.
    RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT,

    /// Number of surface modes. This value is not a valid mode.
    RADIENT_MATERIAL_SURFACE_MODE_COUNT
};


// clang-format on


/// Opaque parameter handle resolved by a material definition.
///
/// A handle is only valid for the definition that created it. Applications should
/// resolve handles once and use them for runtime updates instead of repeatedly
/// looking parameters up by name.
struct RadientMaterialParameterHandle
{
    /// Opaque identity of the material definition that created the handle.
    /// Applications must not modify this value.
    RadientHandle Definition DEFAULT_INITIALIZER(InvalidRadientHandle);

    /// Zero-based parameter index in the originating definition.
    /// Applications must not modify this value.
    Uint32 Index DEFAULT_INITIALIZER(~Uint32{0});

    /// Reserved for future use. Must be zero.
    Uint32 Reserved DEFAULT_INITIALIZER(0);

#if DILIGENT_CPP_INTERFACE
    /// Returns true if the handle is initialized.
    explicit operator bool() const
    {
        return Definition != InvalidRadientHandle && Index != ~Uint32{0};
    }

    /// Compares two parameter handles.
    bool operator==(const RadientMaterialParameterHandle& Rhs) const
    {
        return Definition == Rhs.Definition && Index == Rhs.Index;
    }

    /// Compares two parameter handles.
    bool operator!=(const RadientMaterialParameterHandle& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientMaterialParameterHandle RadientMaterialParameterHandle;


/// Material parameter metadata and default value.
struct RadientMaterialParameterDesc
{
    /// Parameter name used for authoring and reflection.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Parameter type.
    RADIENT_MATERIAL_PARAMETER_TYPE Type DEFAULT_INITIALIZER(RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN);

    /// Number of values or textures in the parameter array. Must not be zero.
    Uint32 ArraySize DEFAULT_INITIALIZER(1);

    /// Optional default value for scalar, vector, and matrix parameters.
    /// The data must contain ArraySize consecutive values in the native
    /// representation specified by Type. The definition copies the complete
    /// value array during creation. A null pointer initializes all bytes to zero.
    const void* pDefaultValue DEFAULT_INITIALIZER(nullptr);

    /// Optional default texture. This field is only used by texture parameters
    /// with ArraySize equal to one. Texture arrays default to null entries.
    IRadientTextureAsset* pDefaultTexture DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMaterialParameterDesc RadientMaterialParameterDesc;


/// Immutable material definition description.
///
/// Material definitions copy the description, parameter metadata, default
/// values, and asset reference when they are created. The description returned
/// by IRadientMaterialDefinitionAsset::GetDesc() remains valid for the lifetime of
/// the definition.
struct RadientMaterialDefinitionDesc
{
    /// Concrete definition type. The value determines the complete description
    /// type returned by IRadientMaterialDefinitionAsset::GetDesc().
    RADIENT_MATERIAL_DEFINITION_TYPE Type DEFAULT_INITIALIZER(RADIENT_MATERIAL_DEFINITION_TYPE_COUNT);

    /// Definition name used for diagnostics.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Optional persistent asset identity. The definition copies the URI.
    RadientAssetReference Reference DEFAULT_INITIALIZER({});

    /// Number of elements in pParameters. May be zero.
    Uint32 ParameterCount DEFAULT_INITIALIZER(0);

    /// Array of ParameterCount parameter descriptions. Parameter names must be
    /// unique within the definition. The definition copies all metadata and
    /// default values and retains default textures during creation.
    const RadientMaterialParameterDesc* pParameters DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMaterialDefinitionDesc RadientMaterialDefinitionDesc;


// clang-format off

/// Surface material definition description.
///
/// ShadingModel and Features describe the surface closure exposed to every
/// renderer. Built-in and future programmable surface definitions use the same
/// contract even when they produce its inputs differently.
struct RadientSurfaceMaterialDefinitionDesc DILIGENT_DERIVE(RadientMaterialDefinitionDesc)

    /// Surface shading model.
    RADIENT_SURFACE_SHADING_MODEL ShadingModel DEFAULT_INITIALIZER(RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS);

    /// Optional surface features provided by the definition.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Features DEFAULT_INITIALIZER(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);

#if DILIGENT_CPP_INTERFACE
    constexpr RadientSurfaceMaterialDefinitionDesc() noexcept
    {
        Type = RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE;
    }
#endif
};
typedef struct RadientSurfaceMaterialDefinitionDesc RadientSurfaceMaterialDefinitionDesc;


/// Post-processing material definition description.
struct RadientPostProcessMaterialDefinitionDesc DILIGENT_DERIVE(RadientMaterialDefinitionDesc)
#if DILIGENT_CPP_INTERFACE
    constexpr RadientPostProcessMaterialDefinitionDesc() noexcept
    {
        Type = RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS;
    }
#endif
};
typedef struct RadientPostProcessMaterialDefinitionDesc RadientPostProcessMaterialDefinitionDesc;


/// Compute material definition description.
struct RadientComputeMaterialDefinitionDesc DILIGENT_DERIVE(RadientMaterialDefinitionDesc)
#if DILIGENT_CPP_INTERFACE
    constexpr RadientComputeMaterialDefinitionDesc() noexcept
    {
        Type = RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE;
    }
#endif
};
typedef struct RadientComputeMaterialDefinitionDesc RadientComputeMaterialDefinitionDesc;

// clang-format on


/// Built-in standard material definition creation attributes.
///
/// ShadingModel and Features define the immutable parameter schema, including its
/// texture parameters. Every standard material declares the texture semantics
/// required by its model and enabled features.
/// The resulting definition exposes the generalized
/// RadientSurfaceMaterialDefinitionDesc renderer contract.
/// Compatible descriptions may resolve to the same cached definition.
/// Canonical parameter names and their value semantics are declared in
/// RadientStandardMaterialParameters.h.
struct RadientStandardMaterialDefinitionCreateInfo
{
    /// Surface shading model implemented by the built-in definition.
    RADIENT_SURFACE_SHADING_MODEL ShadingModel DEFAULT_INITIALIZER(RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS);

    /// Optional features present in the definition. Built-in optional features
    /// are only supported by metallic-roughness materials.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Features DEFAULT_INITIALIZER(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);

    /// Every SemanticTexture parameter supplied by the model and its features
    /// is accompanied by mutable SemanticTextureUVSelector (INT),
    /// SemanticTextureUVScaleAndRotation (FLOAT2X2), SemanticTextureUVBias
    /// (FLOAT2), and SemanticTextureWrapU and SemanticTextureWrapV (UINT)
    /// parameters. Wrap values use RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE.
};
typedef struct RadientStandardMaterialDefinitionCreateInfo RadientStandardMaterialDefinitionCreateInfo;


// {FD30372C-A009-425B-A649-1AE74E33EA46}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialDefinitionAsset =
    {0xfd30372c, 0xa009, 0x425b, {0xa6, 0x49, 0x1a, 0xe7, 0x4e, 0x33, 0xea, 0x46}};

// {7999AEDF-3DDA-4A5F-BB0C-ADF73AEDA6F2}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialAsset =
    {0x7999aedf, 0x3dda, 0x4a5f, {0xbb, 0xc, 0xad, 0xf7, 0x3a, 0xed, 0xa6, 0xf2}};


// {829FEEEE-46E6-4BDB-9ED4-AAEF34C522C4}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSurfaceMaterialAsset =
    {0x829feeee, 0x46e6, 0x4bdb, {0x9e, 0xd4, 0xaa, 0xef, 0x34, 0xc5, 0x22, 0xc4}};

// {39C207AF-A516-4BAB-85BE-715E325E58D3}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialWriter =
    {0x39c207af, 0xa516, 0x4bab, {0x85, 0xbe, 0x71, 0x5e, 0x32, 0x5e, 0x58, 0xd3}};

// {D0D1D20B-8C59-4CEE-8DD7-8FE8151CA89F}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSurfaceMaterialWriter =
    {0xd0d1d20b, 0x8c59, 0x4cee, {0x8d, 0xd7, 0x8f, 0xe8, 0x15, 0x1c, 0xa8, 0x9f}};


#define DILIGENT_INTERFACE_NAME IRadientMaterialDefinitionAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialDefinitionAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;                      \
    IRadientMaterialDefinitionAssetMethods RadientMaterialDefinitionAsset

// clang-format off

/// Immutable material definition asset and parameter schema.
DILIGENT_BEGIN_INTERFACE(IRadientMaterialDefinitionAsset, IRadientAsset)
{
    /// Returns the immutable definition description. The reference remains valid
    /// for the lifetime of the definition. Its Type identifies the concrete
    /// description type to which the returned reference may be cast.
    VIRTUAL const RadientMaterialDefinitionDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Returns the definition loading status. Generic definitions created from
    /// in-memory metadata are immediately ready.
    VIRTUAL RADIENT_STATUS METHOD(GetStatus)(THIS) CONST PURE;

    /// Returns the number of parameters in the definition.
    VIRTUAL Uint32 METHOD(GetParameterCount)(THIS) CONST PURE;

    /// Returns the metadata for the parameter at Index. The reference remains
    /// valid for the lifetime of the definition. Index must be less than
    /// GetParameterCount().
    VIRTUAL const RadientMaterialParameterDesc REF METHOD(GetParameterDesc)(THIS_
                                                                            Uint32 Index) CONST PURE;

    /// Resolves a zero-based parameter index to a definition-specific runtime
    /// handle. On failure, pHandle is set to an invalid handle.
    VIRTUAL RADIENT_STATUS METHOD(GetParameterHandle)(THIS_
                                                      Uint32                          Index,
                                                      RadientMaterialParameterHandle* pHandle) CONST PURE;

    /// Resolves an exact, case-sensitive parameter name to a definition-specific
    /// runtime handle. On failure, pHandle is set to an invalid handle.
    VIRTUAL RADIENT_STATUS METHOD(FindParameter)(THIS_
                                                 const Char*                     Name,
                                                 RadientMaterialParameterHandle* pHandle) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialDefinitionAsset_GetDesc(This)                 CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, GetDesc,           This)
#    define IRadientMaterialDefinitionAsset_GetStatus(This)               CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, GetStatus,         This)
#    define IRadientMaterialDefinitionAsset_GetParameterCount(This)       CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, GetParameterCount, This)
#    define IRadientMaterialDefinitionAsset_GetParameterDesc(This, ...)   CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, GetParameterDesc,  This, __VA_ARGS__)
#    define IRadientMaterialDefinitionAsset_GetParameterHandle(This, ...) CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, GetParameterHandle, This, __VA_ARGS__)
#    define IRadientMaterialDefinitionAsset_FindParameter(This, ...)      CALL_IFACE_METHOD(RadientMaterialDefinitionAsset, FindParameter,      This, __VA_ARGS__)
#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientMaterialAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;            \
    IRadientMaterialAssetMethods RadientMaterialAsset

// clang-format off

/// Definition-backed material asset with writable initialization state.
///
/// Writers may be used to initialize the asset before its load status, GPU
/// resource status, or render view is first queried. Runtime mutation after one
/// of those queries is not currently supported. This restriction is not
/// enforced by the API. Material assets and their writers are not thread-safe.
/// The caller is responsible for synchronizing all access to them.
DILIGENT_BEGIN_INTERFACE(IRadientMaterialAsset, IRadientAsset)
{
    /// Returns a borrowed pointer to the definition retained by this asset.
    VIRTUAL IRadientMaterialDefinitionAsset* METHOD(GetDefinition)(THIS) CONST PURE;

    /// Returns a monotonically increasing version of the complete material
    /// state. The version changes after every commit that modifies at least one
    /// parameter or specialized material property such as surface mode.
    VIRTUAL Uint64 METHOD(GetVersion)(THIS) CONST PURE;

    /// Copies the complete scalar, vector, matrix, or value array identified by
    /// Handle to pData. DataSize must exactly match the parameter's native data
    /// size. Texture parameters are not accepted.
    VIRTUAL RADIENT_STATUS METHOD(GetParameter)(THIS_
                                                RadientMaterialParameterHandle Handle,
                                                void*                          pData,
                                                Uint32                         DataSize) CONST PURE;

    /// Returns the texture identified by Handle and ArrayIndex. When the texture
    /// is not null, the method calls AddRef() and the caller must call Release().
    /// A null pointer is a valid texture value. On failure, ppTexture is set to null.
    VIRTUAL RADIENT_STATUS METHOD(GetTexture)(THIS_
                                              RadientMaterialParameterHandle Handle,
                                              Uint32                         ArrayIndex,
                                              IRadientTextureAsset**         ppTexture) CONST PURE;

    /// Creates a reusable writer with no pending changes. On success, ppWriter
    /// receives a strong reference to the writer.
    VIRTUAL RADIENT_STATUS METHOD(CreateWriter)(THIS_
                                                IRadientMaterialWriter** ppWriter) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialAsset_GetDefinition(This)     CALL_IFACE_METHOD(RadientMaterialAsset, GetDefinition, This)
#    define IRadientMaterialAsset_GetVersion(This)        CALL_IFACE_METHOD(RadientMaterialAsset, GetVersion,    This)
#    define IRadientMaterialAsset_GetParameter(This, ...) CALL_IFACE_METHOD(RadientMaterialAsset, GetParameter,  This, __VA_ARGS__)
#    define IRadientMaterialAsset_GetTexture(This, ...)   CALL_IFACE_METHOD(RadientMaterialAsset, GetTexture,    This, __VA_ARGS__)
#    define IRadientMaterialAsset_CreateWriter(This, ...) CALL_IFACE_METHOD(RadientMaterialAsset, CreateWriter,  This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSurfaceMaterialAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSurfaceMaterialAssetInclusiveMethods \
    IRadientMaterialAssetInclusiveMethods;           \
    IRadientSurfaceMaterialAssetMethods RadientSurfaceMaterialAsset

// clang-format off

/// Material asset that shades a geometric surface.
DILIGENT_BEGIN_INTERFACE(IRadientSurfaceMaterialAsset, IRadientMaterialAsset)
{
    /// Returns the surface coverage and blending mode.
    VIRTUAL RADIENT_MATERIAL_SURFACE_MODE METHOD(GetSurfaceMode)(THIS) CONST PURE;

    /// Returns the alpha cutoff used by masked surfaces. The default is 0.5.
    VIRTUAL Float32 METHOD(GetAlphaCutoff)(THIS) CONST PURE;

    /// Returns True when both sides of the surface are rendered.
    VIRTUAL Bool METHOD(IsDoubleSided)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSurfaceMaterialAsset_GetSurfaceMode(This) CALL_IFACE_METHOD(RadientSurfaceMaterialAsset, GetSurfaceMode, This)
#    define IRadientSurfaceMaterialAsset_GetAlphaCutoff(This) CALL_IFACE_METHOD(RadientSurfaceMaterialAsset, GetAlphaCutoff, This)
#    define IRadientSurfaceMaterialAsset_IsDoubleSided(This)  CALL_IFACE_METHOD(RadientSurfaceMaterialAsset, IsDoubleSided,  This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientMaterialWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialWriterInclusiveMethods \
    IObjectInclusiveMethods;                   \
    IRadientMaterialWriterMethods RadientMaterialWriter

// clang-format off

/// Reusable material initialization writer.
///
/// A writer records every value explicitly assigned through its setter methods.
/// Commit() publishes complete non-texture parameters, individual texture array
/// elements, and any specialized material properties exposed by a derived writer. If
/// multiple writers modify the same value, the last commit replaces that complete
/// value. Commits must complete before the asset's load status, GPU resource
/// status, or render view is first queried. The writer and its material asset are
/// not thread-safe and must not be accessed concurrently with Commit().
DILIGENT_BEGIN_INTERFACE(IRadientMaterialWriter, IObject)
{
    /// Replaces the complete value or value array identified by Handle. pData
    /// is copied immediately and DataSize must exactly match the parameter's
    /// native data size. Texture parameters are not accepted. The first assignment
    /// is always recorded, even if the material currently contains the supplied
    /// value. Returns RADIENT_STATUS_NO_CHANGE only if this writer already has an
    /// identical pending assignment.
    VIRTUAL RADIENT_STATUS METHOD(SetParameter)(THIS_
                                                RadientMaterialParameterHandle Handle,
                                                const void*                    pData,
                                                Uint32                         DataSize) PURE;

#if DILIGENT_CPP_INTERFACE
    /// Replaces the complete value or value array identified by Handle, inferring
    /// the native data size from Value.
    template <typename ValueType>
    RADIENT_STATUS SetParameter(RadientMaterialParameterHandle Handle,
                                const ValueType&                Value)
    {
        static_assert(std::is_trivially_copyable<ValueType>::value,
                      "Material parameter values must be trivially copyable");
        static_assert(!std::is_pointer<ValueType>::value,
                      "Pass the material parameter value itself, not a pointer");
        return SetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value)));
    }
#endif

    /// Replaces one texture array element identified by Handle and ArrayIndex.
    /// Commit() publishes only modified texture elements, so independent element
    /// updates from different writers are preserved. The writer retains pTexture;
    /// null is a valid texture value. Non-texture parameters are not accepted. The
    /// first assignment is always recorded, even if the material currently contains
    /// pTexture. Returns RADIENT_STATUS_NO_CHANGE only if this writer already has an
    /// identical pending assignment.
    VIRTUAL RADIENT_STATUS METHOD(SetTexture)(THIS_
                                              RadientMaterialParameterHandle Handle,
                                              Uint32                         ArrayIndex,
                                              IRadientTextureAsset*          pTexture) PURE;

    /// Applies the writer's pending changes to its material asset as one logical
    /// update. This is the authoritative check for whether the assignments change
    /// the material state. The writer remains valid after the call. On success or
    /// RADIENT_STATUS_NO_CHANGE, pending changes are cleared. On failure, pending
    /// changes are retained so the operation can be retried.
    VIRTUAL RADIENT_STATUS METHOD(Commit)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialWriter_SetParameter(This, ...) CALL_IFACE_METHOD(RadientMaterialWriter, SetParameter, This, __VA_ARGS__)
#    define IRadientMaterialWriter_SetTexture(This, ...)   CALL_IFACE_METHOD(RadientMaterialWriter, SetTexture,   This, __VA_ARGS__)
#    define IRadientMaterialWriter_Commit(This)            CALL_IFACE_METHOD(RadientMaterialWriter, Commit,       This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSurfaceMaterialWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSurfaceMaterialWriterInclusiveMethods \
    IRadientMaterialWriterInclusiveMethods;           \
    IRadientSurfaceMaterialWriterMethods RadientSurfaceMaterialWriter

// clang-format off

/// Writer for mutable surface-material state.
DILIGENT_BEGIN_INTERFACE(IRadientSurfaceMaterialWriter, IRadientMaterialWriter)
{
    /// Sets the surface coverage and blending mode. Returns RADIENT_STATUS_NO_CHANGE
    /// only if this writer already has an identical pending assignment.
    VIRTUAL RADIENT_STATUS METHOD(SetSurfaceMode)(THIS_
                                                  RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) PURE;

    /// Sets the alpha cutoff used by masked surfaces. Returns RADIENT_STATUS_NO_CHANGE
    /// only if this writer already has an identical pending assignment.
    VIRTUAL RADIENT_STATUS METHOD(SetAlphaCutoff)(THIS_
                                                  Float32 AlphaCutoff) PURE;

    /// Controls whether both sides of the surface are rendered. Returns
    /// RADIENT_STATUS_NO_CHANGE only if this writer already has an identical pending
    /// assignment.
    VIRTUAL RADIENT_STATUS METHOD(SetDoubleSided)(THIS_
                                                  Bool DoubleSided) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSurfaceMaterialWriter_SetSurfaceMode(This, ...) CALL_IFACE_METHOD(RadientSurfaceMaterialWriter, SetSurfaceMode, This, __VA_ARGS__)
#    define IRadientSurfaceMaterialWriter_SetAlphaCutoff(This, ...) CALL_IFACE_METHOD(RadientSurfaceMaterialWriter, SetAlphaCutoff, This, __VA_ARGS__)
#    define IRadientSurfaceMaterialWriter_SetDoubleSided(This, ...) CALL_IFACE_METHOD(RadientSurfaceMaterialWriter, SetDoubleSided, This, __VA_ARGS__)

#endif

// clang-format on


DILIGENT_END_NAMESPACE // namespace Diligent
