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
/// Defines programmable material definitions and instances.

#include "RadientAssets.h"

#include "../../../DiligentCore/Primitives/interface/FlagEnum.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientMaterialDefinition            IRadientMaterialDefinition;
typedef struct IRadientMaterialInstance              IRadientMaterialInstance;
typedef struct IRadientMaterialInstanceWriter        IRadientMaterialInstanceWriter;
typedef struct IRadientSurfaceMaterialInstance       IRadientSurfaceMaterialInstance;
typedef struct IRadientSurfaceMaterialInstanceWriter IRadientSurfaceMaterialInstanceWriter;

// clang-format off

/// Concrete material definition type.
///
/// The value identifies the complete description returned by
/// IRadientMaterialDefinition::GetDesc(). After inspecting Type, the base
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

    RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_LAST = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME,
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL =
        (RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_LAST << 1u) - 1u
};
DEFINE_FLAG_ENUM_OPERATORS(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS)


/// Surface coverage and blending mode stored in a surface material instance.
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
/// by IRadientMaterialDefinition::GetDesc() remains valid for the lifetime of
/// the definition.
struct RadientMaterialDefinitionDesc
{
    /// Concrete definition type. The value determines the complete description
    /// type returned by IRadientMaterialDefinition::GetDesc().
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

    /// Optional features present in the definition. Unlit materials do not
    /// support optional metallic-roughness features.
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Features DEFAULT_INITIALIZER(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);

    /// Every SemanticTexture parameter supplied by the model and its features
    /// is accompanied by mutable SemanticTextureUVSelector (INT),
    /// SemanticTextureUVScaleAndRotation (FLOAT2X2), SemanticTextureUVBias
    /// (FLOAT2), and SemanticTextureWrapU and SemanticTextureWrapV (UINT)
    /// parameters. Wrap values use RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE.
};
typedef struct RadientStandardMaterialDefinitionCreateInfo RadientStandardMaterialDefinitionCreateInfo;


// {0C0DCA2D-FB29-445A-87D6-2BF9EFD5E9FD}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialDefinition =
    {0xc0dca2d, 0xfb29, 0x445a, {0x87, 0xd6, 0x2b, 0xf9, 0xef, 0xd5, 0xe9, 0xfd}};


// {37A5308E-825D-4D55-A5BC-B05A93A6A540}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialInstance =
    {0x37a5308e, 0x825d, 0x4d55, {0xa5, 0xbc, 0xb0, 0x5a, 0x93, 0xa6, 0xa5, 0x40}};


// {FAED9615-107D-4C29-9015-E8FF78E34F94}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialInstanceWriter =
    {0xfaed9615, 0x107d, 0x4c29, {0x90, 0x15, 0xe8, 0xff, 0x78, 0xe3, 0x4f, 0x94}};

// {510EFCE2-880F-4ABE-B46D-7E1176C7BE67}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSurfaceMaterialInstance =
    {0x510efce2, 0x880f, 0x4abe, {0xb4, 0x6d, 0x7e, 0x11, 0x76, 0xc7, 0xbe, 0x67}};

// {92B17A4B-8404-4838-B0BC-4B1772767281}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSurfaceMaterialInstanceWriter =
    {0x92b17a4b, 0x8404, 0x4838, {0xb0, 0xbc, 0x4b, 0x17, 0x72, 0x76, 0x72, 0x81}};


#define DILIGENT_INTERFACE_NAME IRadientMaterialDefinition
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialDefinitionInclusiveMethods \
    IRadientAssetInclusiveMethods;                 \
    IRadientMaterialDefinitionMethods RadientMaterialDefinition

// clang-format off

/// Immutable material definition and parameter schema.
DILIGENT_BEGIN_INTERFACE(IRadientMaterialDefinition, IRadientAsset)
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

    /// Creates a material instance initialized with the definition's default
    /// values. On success, ppInstance receives a strong reference to the instance.
    VIRTUAL RADIENT_STATUS METHOD(CreateInstance)(THIS_
                                                  IRadientMaterialInstance** ppInstance) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialDefinition_GetDesc(This)                 CALL_IFACE_METHOD(RadientMaterialDefinition, GetDesc,           This)
#    define IRadientMaterialDefinition_GetStatus(This)               CALL_IFACE_METHOD(RadientMaterialDefinition, GetStatus,         This)
#    define IRadientMaterialDefinition_GetParameterCount(This)       CALL_IFACE_METHOD(RadientMaterialDefinition, GetParameterCount, This)
#    define IRadientMaterialDefinition_GetParameterDesc(This, ...)   CALL_IFACE_METHOD(RadientMaterialDefinition, GetParameterDesc,  This, __VA_ARGS__)
#    define IRadientMaterialDefinition_GetParameterHandle(This, ...) CALL_IFACE_METHOD(RadientMaterialDefinition, GetParameterHandle, This, __VA_ARGS__)
#    define IRadientMaterialDefinition_FindParameter(This, ...)      CALL_IFACE_METHOD(RadientMaterialDefinition, FindParameter,      This, __VA_ARGS__)
#    define IRadientMaterialDefinition_CreateInstance(This, ...) CALL_IFACE_METHOD(RadientMaterialDefinition, CreateInstance, This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientMaterialInstance
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialInstanceInclusiveMethods \
    IObjectInclusiveMethods;                     \
    IRadientMaterialInstanceMethods RadientMaterialInstance

// clang-format off

/// Mutable material instance.
///
/// Every object that shares an instance observes changes committed through one
/// of its writers. Material instances and their writers are not thread-safe. The
/// caller is responsible for synchronizing all access to them and must ensure
/// that an instance is not accessed concurrently with a writer commit.
DILIGENT_BEGIN_INTERFACE(IRadientMaterialInstance, IObject)
{
    /// Returns a borrowed pointer to the definition retained by this instance.
    VIRTUAL IRadientMaterialDefinition* METHOD(GetDefinition)(THIS) CONST PURE;

    /// Returns a monotonically increasing version of the complete instance
    /// state. The version changes after every commit that modifies at least one
    /// parameter or specialized instance property such as surface mode.
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
                                                IRadientMaterialInstanceWriter** ppWriter) CONST PURE;

    /// Creates an independent material instance initialized with the complete
    /// current state. Subsequent updates to either instance do not affect the
    /// other. On success, ppInstance receives a strong reference to the clone.
    VIRTUAL RADIENT_STATUS METHOD(Clone)(THIS_
                                         IRadientMaterialInstance** ppInstance) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialInstance_GetDefinition(This)     CALL_IFACE_METHOD(RadientMaterialInstance, GetDefinition, This)
#    define IRadientMaterialInstance_GetVersion(This)        CALL_IFACE_METHOD(RadientMaterialInstance, GetVersion,    This)
#    define IRadientMaterialInstance_GetParameter(This, ...) CALL_IFACE_METHOD(RadientMaterialInstance, GetParameter,  This, __VA_ARGS__)
#    define IRadientMaterialInstance_GetTexture(This, ...)   CALL_IFACE_METHOD(RadientMaterialInstance, GetTexture,    This, __VA_ARGS__)
#    define IRadientMaterialInstance_CreateWriter(This, ...) CALL_IFACE_METHOD(RadientMaterialInstance, CreateWriter,  This, __VA_ARGS__)
#    define IRadientMaterialInstance_Clone(This, ...)        CALL_IFACE_METHOD(RadientMaterialInstance, Clone,         This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSurfaceMaterialInstance
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSurfaceMaterialInstanceInclusiveMethods \
    IRadientMaterialInstanceInclusiveMethods;           \
    IRadientSurfaceMaterialInstanceMethods RadientSurfaceMaterialInstance

// clang-format off

/// Material instance that shades a geometric surface.
DILIGENT_BEGIN_INTERFACE(IRadientSurfaceMaterialInstance, IRadientMaterialInstance)
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

#    define IRadientSurfaceMaterialInstance_GetSurfaceMode(This)  CALL_IFACE_METHOD(RadientSurfaceMaterialInstance, GetSurfaceMode,  This)
#    define IRadientSurfaceMaterialInstance_GetAlphaCutoff(This)  CALL_IFACE_METHOD(RadientSurfaceMaterialInstance, GetAlphaCutoff,  This)
#    define IRadientSurfaceMaterialInstance_IsDoubleSided(This)   CALL_IFACE_METHOD(RadientSurfaceMaterialInstance, IsDoubleSided,   This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientMaterialInstanceWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialInstanceWriterInclusiveMethods \
    IObjectInclusiveMethods;                           \
    IRadientMaterialInstanceWriterMethods RadientMaterialInstanceWriter

// clang-format off

/// Reusable material instance writer.
///
/// A writer records only values changed through its setter methods. Commit()
/// publishes complete non-texture parameters, individual texture array elements,
/// and any specialized instance properties exposed by a derived writer. If
/// multiple writers modify the same value, the last commit replaces that complete
/// value. The writer and its material instance are not thread-safe and must not be
/// accessed concurrently with Commit().
DILIGENT_BEGIN_INTERFACE(IRadientMaterialInstanceWriter, IObject)
{
    /// Replaces the complete value or value array identified by Handle. pData
    /// is copied immediately and DataSize must exactly match the parameter's
    /// native data size. Texture parameters are not accepted. Returns
    /// RADIENT_STATUS_NO_CHANGE if the writer's effective value already equals
    /// the supplied value.
    VIRTUAL RADIENT_STATUS METHOD(SetParameter)(THIS_
                                                RadientMaterialParameterHandle Handle,
                                                const void*                    pData,
                                                Uint32                         DataSize) PURE;

    /// Replaces one texture array element identified by Handle and ArrayIndex.
    /// Commit() publishes only modified texture elements, so independent element
    /// updates from different writers are preserved. The writer retains pTexture;
    /// null is a valid texture value. Non-texture parameters are not accepted.
    /// Returns RADIENT_STATUS_NO_CHANGE if the writer's effective texture already
    /// equals pTexture.
    VIRTUAL RADIENT_STATUS METHOD(SetTexture)(THIS_
                                              RadientMaterialParameterHandle Handle,
                                              Uint32                         ArrayIndex,
                                              IRadientTextureAsset*          pTexture) PURE;

    /// Applies the writer's pending changes to its material instance as one logical
    /// update. The writer remains valid after the call. On success or
    /// RADIENT_STATUS_NO_CHANGE, pending changes are cleared. On failure, pending
    /// changes are retained so the operation can be retried.
    VIRTUAL RADIENT_STATUS METHOD(Commit)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMaterialInstanceWriter_SetParameter(This, ...) CALL_IFACE_METHOD(RadientMaterialInstanceWriter, SetParameter, This, __VA_ARGS__)
#    define IRadientMaterialInstanceWriter_SetTexture(This, ...)   CALL_IFACE_METHOD(RadientMaterialInstanceWriter, SetTexture,   This, __VA_ARGS__)
#    define IRadientMaterialInstanceWriter_Commit(This)            CALL_IFACE_METHOD(RadientMaterialInstanceWriter, Commit,       This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSurfaceMaterialInstanceWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSurfaceMaterialInstanceWriterInclusiveMethods \
    IRadientMaterialInstanceWriterInclusiveMethods;           \
    IRadientSurfaceMaterialInstanceWriterMethods RadientSurfaceMaterialInstanceWriter

// clang-format off

/// Writer for mutable surface-material state.
DILIGENT_BEGIN_INTERFACE(IRadientSurfaceMaterialInstanceWriter, IRadientMaterialInstanceWriter)
{
    /// Sets the surface coverage and blending mode. Returns
    /// RADIENT_STATUS_NO_CHANGE when the writer's effective value already equals
    /// SurfaceMode.
    VIRTUAL RADIENT_STATUS METHOD(SetSurfaceMode)(THIS_
                                                  RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) PURE;

    /// Sets the alpha cutoff used by masked surfaces. Returns
    /// RADIENT_STATUS_NO_CHANGE when the writer's effective value already equals
    /// AlphaCutoff.
    VIRTUAL RADIENT_STATUS METHOD(SetAlphaCutoff)(THIS_
                                                  Float32 AlphaCutoff) PURE;

    /// Controls whether both sides of the surface are rendered. Returns
    /// RADIENT_STATUS_NO_CHANGE when the writer's effective value already equals
    /// DoubleSided.
    VIRTUAL RADIENT_STATUS METHOD(SetDoubleSided)(THIS_
                                                  Bool DoubleSided) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSurfaceMaterialInstanceWriter_SetSurfaceMode(This, ...)  CALL_IFACE_METHOD(RadientSurfaceMaterialInstanceWriter, SetSurfaceMode,  This, __VA_ARGS__)
#    define IRadientSurfaceMaterialInstanceWriter_SetAlphaCutoff(This, ...)  CALL_IFACE_METHOD(RadientSurfaceMaterialInstanceWriter, SetAlphaCutoff,  This, __VA_ARGS__)
#    define IRadientSurfaceMaterialInstanceWriter_SetDoubleSided(This, ...)  CALL_IFACE_METHOD(RadientSurfaceMaterialInstanceWriter, SetDoubleSided,  This, __VA_ARGS__)

#endif

// clang-format on


DILIGENT_END_NAMESPACE // namespace Diligent
