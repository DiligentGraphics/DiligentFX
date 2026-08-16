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

typedef struct IRadientMaterialDefinition     IRadientMaterialDefinition;
typedef struct IRadientMaterialInstance       IRadientMaterialInstance;
typedef struct IRadientMaterialInstanceWriter IRadientMaterialInstanceWriter;

/// Stable identifier assigned to a material parameter by its authoring source.
typedef Uint64 RadientMaterialParameterID;

/// Invalid material parameter identifier.
static DILIGENT_CONSTEXPR RadientMaterialParameterID InvalidRadientMaterialParameterID = 0;

// clang-format off

/// Material execution domain.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_DOMAIN, Uint8)
{
    /// A material that shades a geometric surface.
    RADIENT_MATERIAL_DOMAIN_SURFACE = 0,

    /// A full-screen post-processing material.
    RADIENT_MATERIAL_DOMAIN_POST_PROCESS,

    /// A compute material.
    RADIENT_MATERIAL_DOMAIN_COMPUTE,

    /// Number of material domains. This value is not a valid domain.
    RADIENT_MATERIAL_DOMAIN_COUNT
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


/// Material parameter flags.
DILIGENT_TYPED_ENUM(RADIENT_MATERIAL_PARAMETER_FLAGS, Uint8)
{
    /// The parameter has no special behavior.
    RADIENT_MATERIAL_PARAMETER_FLAG_NONE = 0u,

    /// Changing the parameter may select a different compiled material variant.
    RADIENT_MATERIAL_PARAMETER_FLAG_SPECIALIZATION = 1u << 0u,

    /// Last individual material parameter flag. This value is reserved for validation.
    RADIENT_MATERIAL_PARAMETER_FLAG_LAST = RADIENT_MATERIAL_PARAMETER_FLAG_SPECIALIZATION,

    /// Bit mask containing all valid material parameter flags.
    RADIENT_MATERIAL_PARAMETER_FLAGS_ALL = (RADIENT_MATERIAL_PARAMETER_FLAG_LAST << 1u) - 1u
};
DEFINE_FLAG_ENUM_OPERATORS(RADIENT_MATERIAL_PARAMETER_FLAGS)

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


/// Immutable material definition description.
struct RadientMaterialDefinitionDesc
{
    /// Definition name used for diagnostics.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Material execution domain.
    RADIENT_MATERIAL_DOMAIN Domain DEFAULT_INITIALIZER(RADIENT_MATERIAL_DOMAIN_SURFACE);
};
typedef struct RadientMaterialDefinitionDesc RadientMaterialDefinitionDesc;


/// Material parameter metadata and default value.
struct RadientMaterialParameterDesc
{
    /// Parameter name used for authoring and reflection.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Stable identifier assigned by the material authoring source.
    RadientMaterialParameterID ID DEFAULT_INITIALIZER(InvalidRadientMaterialParameterID);

    /// Parameter type.
    RADIENT_MATERIAL_PARAMETER_TYPE Type DEFAULT_INITIALIZER(RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN);

    /// Parameter flags.
    RADIENT_MATERIAL_PARAMETER_FLAGS Flags DEFAULT_INITIALIZER(RADIENT_MATERIAL_PARAMETER_FLAG_NONE);

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


/// Generic material definition creation attributes.
struct RadientMaterialDefinitionCreateInfo
{
    /// Material definition description.
    RadientMaterialDefinitionDesc Desc DEFAULT_INITIALIZER({});

    /// Optional persistent asset identity. The definition copies the URI.
    RadientAssetReference Reference DEFAULT_INITIALIZER({});

    /// Array of ParameterCount parameter descriptions. Parameter names and IDs
    /// must be unique within the definition. The definition copies all metadata
    /// and default values and retains default textures during creation.
    const RadientMaterialParameterDesc* pParameters DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pParameters. May be zero.
    Uint32 ParameterCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMaterialDefinitionCreateInfo RadientMaterialDefinitionCreateInfo;


// {0C0DCA2D-FB29-445A-87D6-2BF9EFD5E9FD}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialDefinition =
    {0xc0dca2d, 0xfb29, 0x445a, {0x87, 0xd6, 0x2b, 0xf9, 0xef, 0xd5, 0xe9, 0xfd}};


// {37A5308E-825D-4D55-A5BC-B05A93A6A540}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialInstance =
    {0x37a5308e, 0x825d, 0x4d55, {0xa5, 0xbc, 0xb0, 0x5a, 0x93, 0xa6, 0xa5, 0x40}};


// {FAED9615-107D-4C29-9015-E8FF78E34F94}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMaterialInstanceWriter =
    {0xfaed9615, 0x107d, 0x4c29, {0x90, 0x15, 0xe8, 0xff, 0x78, 0xe3, 0x4f, 0x94}};


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
    /// for the lifetime of the definition.
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

/// Stable, thread-safe material instance.
///
/// An instance owns a versioned immutable parameter state. A writer publishes a
/// new state without changing the instance identity, so every object that shares
/// the instance observes the update.
DILIGENT_BEGIN_INTERFACE(IRadientMaterialInstance, IObject)
{
    /// Returns a borrowed pointer to the definition retained by this instance.
    VIRTUAL IRadientMaterialDefinition* METHOD(GetDefinition)(THIS) CONST PURE;

    /// Returns a monotonically increasing version of the parameter state. The
    /// version changes after every successful writer commit.
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

    /// Creates a writer initialized with the current parameter state. On success,
    /// ppWriter receives a strong reference to the writer.
    VIRTUAL RADIENT_STATUS METHOD(CreateWriter)(THIS_
                                                IRadientMaterialInstanceWriter** ppWriter) CONST PURE;

    /// Creates an independent material instance initialized with the current
    /// parameter state. Subsequent updates to either instance do not affect the
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


#define DILIGENT_INTERFACE_NAME IRadientMaterialInstanceWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMaterialInstanceWriterInclusiveMethods \
    IObjectInclusiveMethods;                           \
    IRadientMaterialInstanceWriterMethods RadientMaterialInstanceWriter

// clang-format off

/// Reusable material instance writer.
///
/// A writer is not thread-safe. It owns a private parameter copy and never
/// modifies the definition. Changes are applied atomically by Commit().
DILIGENT_BEGIN_INTERFACE(IRadientMaterialInstanceWriter, IObject)
{
    /// Replaces the complete value or value array identified by Handle. pData
    /// is copied immediately and DataSize must exactly match the parameter's
    /// native data size. Texture parameters are not accepted.
    VIRTUAL RADIENT_STATUS METHOD(SetParameter)(THIS_
                                                RadientMaterialParameterHandle Handle,
                                                const void*                    pData,
                                                Uint32                         DataSize) PURE;

    /// Replaces one texture array element identified by Handle and ArrayIndex.
    /// The writer retains pTexture; null is a valid texture value. Non-texture
    /// parameters are not accepted.
    VIRTUAL RADIENT_STATUS METHOD(SetTexture)(THIS_
                                              RadientMaterialParameterHandle Handle,
                                              Uint32                         ArrayIndex,
                                              IRadientTextureAsset*          pTexture) PURE;

    /// Atomically publishes the writer's changes to its material instance. The
    /// writer remains valid after the call. On success or RADIENT_STATUS_NO_CHANGE,
    /// pending changes are cleared and the writer is rebased on the latest instance
    /// state. On failure, pending changes are retained so the operation can be
    /// retried. Changes to different parameters are merged with commits made since
    /// this writer was created. For the same parameter, the later commit wins.
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


#include "../../../DiligentCore/Primitives/interface/DefineGlobalFuncHelperMacros.h"

/// Creates an immutable in-memory material definition. The function copies all
/// creation metadata and default values and retains default textures. On
/// success, ppDefinition receives a strong reference to the definition.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientMaterialDefinition)(
    const RadientMaterialDefinitionCreateInfo REF CreateInfo,
    IRadientMaterialDefinition**                  ppDefinition);

#include "../../../DiligentCore/Primitives/interface/UndefGlobalFuncHelperMacros.h"

DILIGENT_END_NAMESPACE // namespace Diligent
