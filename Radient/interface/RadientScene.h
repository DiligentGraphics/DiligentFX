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
/// Defines Radient scene graph and ECS interfaces.

#include "RadientTypes.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"
#include "../../../DiligentCore/Primitives/interface/FlagEnum.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Entity flags.
DILIGENT_TYPED_ENUM(RADIENT_ENTITY_FLAGS, Uint32)
{
    RADIENT_ENTITY_FLAG_NONE = 0u,

    /// Entity's own visibility flag. Effective visibility also depends on parent entities.
    RADIENT_ENTITY_FLAG_VISIBLE = 1u << 0u,

    RADIENT_ENTITY_FLAG_LAST = RADIENT_ENTITY_FLAG_VISIBLE
};
DEFINE_FLAG_ENUM_OPERATORS(RADIENT_ENTITY_FLAGS)


/// Camera projection mode.
DILIGENT_TYPED_ENUM(RADIENT_CAMERA_PROJECTION, Uint8)
{
    /// Perspective projection.
    RADIENT_CAMERA_PROJECTION_PERSPECTIVE = 0,

    /// Orthographic projection.
    RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC
};


/// Light type.
DILIGENT_TYPED_ENUM(RADIENT_LIGHT_TYPE, Uint8)
{
    /// Directional light.
    RADIENT_LIGHT_TYPE_DIRECTIONAL = 0,

    /// Point light.
    RADIENT_LIGHT_TYPE_POINT,
    
    /// Spot light.
    RADIENT_LIGHT_TYPE_SPOT
};

// clang-format on


/// Scene description.
struct RadientSceneDesc
{
    /// Scene name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientSceneDesc RadientSceneDesc;


/// Entity creation attributes.
struct RadientEntityDesc
{
    /// Optional entity name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Optional parent entity.
    RadientEntityID Parent DEFAULT_INITIALIZER(InvalidRadientEntityID);

    /// Entity flags.
    RADIENT_ENTITY_FLAGS Flags DEFAULT_INITIALIZER(RADIENT_ENTITY_FLAG_VISIBLE);

    /// Initial local transform.
    RadientTransform Transform DEFAULT_INITIALIZER({});
};
typedef struct RadientEntityDesc RadientEntityDesc;


/// Camera component.
struct RadientCameraComponent
{
    /// Camera projection mode.
    RADIENT_CAMERA_PROJECTION Projection DEFAULT_INITIALIZER(RADIENT_CAMERA_PROJECTION_PERSPECTIVE);

    /// Horizontal aperture in scene units.
    Float32 HorizontalAperture DEFAULT_INITIALIZER(2.0955f);

    /// Vertical aperture in scene units.
    Float32 VerticalAperture DEFAULT_INITIALIZER(1.52908f);

    /// Horizontal aperture offset in the same units as HorizontalAperture.
    Float32 HorizontalApertureOffset DEFAULT_INITIALIZER(0.f);

    /// Vertical aperture offset in the same units as VerticalAperture.
    Float32 VerticalApertureOffset DEFAULT_INITIALIZER(0.f);

    /// Perspective focal length in scene units.
    Float32 FocalLength DEFAULT_INITIALIZER(5.f);

    /// Near and far clipping distances in scene units.
    RadientFloat2 ClippingRange DEFAULT_INITIALIZER({0.1f, 1000.f});

    /// Lens aperture for depth-of-field effects. A value of 0 disables depth of field.
    Float32 FStop DEFAULT_INITIALIZER(0.f);

    /// Distance from the camera to the focus plane in scene units.
    Float32 FocusDistance DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientCameraComponent RadientCameraComponent;


/// Mesh component.
struct RadientMeshComponent
{
    /// Mesh asset reference.
    RadientAssetReference Mesh DEFAULT_INITIALIZER({});
};
typedef struct RadientMeshComponent RadientMeshComponent;


/// Mesh renderer component.
struct RadientMeshRendererComponent
{
    /// Per-renderer visibility mask.
    Uint64 VisibilityMask DEFAULT_INITIALIZER(~0ull);
};
typedef struct RadientMeshRendererComponent RadientMeshRendererComponent;


/// Light component.
struct RadientLightComponent
{
    /// Light type.
    RADIENT_LIGHT_TYPE Type DEFAULT_INITIALIZER(RADIENT_LIGHT_TYPE_DIRECTIONAL);

    /// Light color.
    RadientFloat3 Color DEFAULT_INITIALIZER({1.f, 1.f, 1.f});

    /// Light intensity.
    Float32 Intensity DEFAULT_INITIALIZER(1.f);

    /// Exposure multiplier as a power of 2.
    Float32 Exposure DEFAULT_INITIALIZER(0.f);

    /// Diffuse contribution multiplier.
    Float32 Diffuse DEFAULT_INITIALIZER(1.f);

    /// Specular contribution multiplier.
    Float32 Specular DEFAULT_INITIALIZER(1.f);

    /// Normalizes power by the light area or angular size.
    Bool Normalize DEFAULT_INITIALIZER(False);

    /// Enables ColorTemperature.
    Bool EnableColorTemperature DEFAULT_INITIALIZER(False);

    /// Color temperature in degrees Kelvin.
    Float32 ColorTemperature DEFAULT_INITIALIZER(6500.f);

    /// Radius for sphere and point-style lights, in scene units.
    Float32 Radius DEFAULT_INITIALIZER(0.5f);

    /// Angular diameter for distant lights, in degrees.
    Float32 Angle DEFAULT_INITIALIZER(0.53f);

    /// Shaping cone angle in degrees.
    Float32 ShapingConeAngle DEFAULT_INITIALIZER(90.f);

    /// Shaping cone softness.
    Float32 ShapingConeSoftness DEFAULT_INITIALIZER(0.f);

    /// Shaping focus.
    Float32 ShapingFocus DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientLightComponent RadientLightComponent;


/// Generic custom component payload.
struct RadientCustomComponentData
{
    /// Custom component type identifier.
    RadientComponentTypeID ComponentType DEFAULT_INITIALIZER(InvalidRadientComponentTypeID);

    /// Optional component name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Optional serialized data schema identifier.
    const Char* Schema DEFAULT_INITIALIZER(nullptr);

    /// Serialized data schema version.
    Uint32 Version DEFAULT_INITIALIZER(0);

    /// Serialized component bytes.
    const void* pData DEFAULT_INITIALIZER(nullptr);

    /// Size of pData in bytes.
    Uint32 DataSize DEFAULT_INITIALIZER(0);
};
typedef struct RadientCustomComponentData RadientCustomComponentData;


// {AD6BA9AB-1FA8-466D-9A1E-5B7ADF1F0562}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientScene =
    {0xad6ba9ab, 0x1fa8, 0x466d, {0x9a, 0x1e, 0x5b, 0x7a, 0xdf, 0x1f, 0x5, 0x62}};


#define DILIGENT_INTERFACE_NAME IRadientScene
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSceneInclusiveMethods \
    IObjectInclusiveMethods;          \
    IRadientSceneMethods RadientScene

// clang-format off

/// Scene graph and ECS storage interface.
DILIGENT_BEGIN_INTERFACE(IRadientScene, IObject)
{
    /// Returns the scene description.
    VIRTUAL const RadientSceneDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Returns RADIENT_STATUS_OK if the entity is alive.
    VIRTUAL RADIENT_STATUS METHOD(IsEntityAlive)(THIS_
                                                 RadientEntityID Entity) CONST PURE;

    /// Gets entity flags.
    VIRTUAL RADIENT_STATUS METHOD(GetEntityFlags)(THIS_
                                                  RadientEntityID          Entity,
                                                  RADIENT_ENTITY_FLAGS REF Flags) CONST PURE;

    /// Gets entity's own visibility flag.
    VIRTUAL RADIENT_STATUS METHOD(GetEntityOwnVisibility)(THIS_
                                                          RadientEntityID Entity,
                                                          Bool REF        Visible) CONST PURE;

    /// Gets effective entity visibility, accounting for parent visibility.
    VIRTUAL RADIENT_STATUS METHOD(GetEntityEffectiveVisibility)(THIS_
                                                                RadientEntityID Entity,
                                                                Bool REF        Visible) CONST PURE;

    /// Gets the entity parent, or InvalidRadientEntityID for a root entity.
    VIRTUAL RADIENT_STATUS METHOD(GetParent)(THIS_
                                             RadientEntityID     Entity,
                                             RadientEntityID REF Parent) CONST PURE;

    /// Returns the number of child entities.
    VIRTUAL RADIENT_STATUS METHOD(GetChildCount)(THIS_
                                                 RadientEntityID Entity,
                                                 Uint32 REF      ChildCount) CONST PURE;

    /// Gets child entities starting from StartChild.
    VIRTUAL RADIENT_STATUS METHOD(GetChildren)(THIS_
                                               RadientEntityID  Entity,
                                               Uint32           StartChild,
                                               Uint32           ChildCount,
                                               RadientEntityID* pChildren,
                                               Uint32 REF       NumChildrenWritten) CONST PURE;

    /// Gets local transform.
    VIRTUAL RADIENT_STATUS METHOD(GetLocalTransform)(THIS_
                                                     RadientEntityID      Entity,
                                                     RadientTransform REF Transform) CONST PURE;

    /// Gets world transform matrix.
    VIRTUAL RADIENT_STATUS METHOD(GetWorldMatrix)(THIS_
                                                  RadientEntityID      Entity,
                                                  RadientMatrix4x4 REF Matrix) CONST PURE;

    /// Checks if the entity has the requested component.
    VIRTUAL RADIENT_STATUS METHOD(HasComponent)(THIS_
                                                RadientEntityID        Entity,
                                                RadientComponentTypeID ComponentType,
                                                Bool REF               HasComponent) CONST PURE;

    /// Returns current scene revision.
    VIRTUAL RadientRevision METHOD(GetRevision)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientScene_GetDesc(This)                         CALL_IFACE_METHOD(RadientScene, GetDesc,                     This)
#    define IRadientScene_IsEntityAlive(This, ...)               CALL_IFACE_METHOD(RadientScene, IsEntityAlive,               This, __VA_ARGS__)
#    define IRadientScene_GetEntityFlags(This, ...)              CALL_IFACE_METHOD(RadientScene, GetEntityFlags,              This, __VA_ARGS__)
#    define IRadientScene_GetEntityOwnVisibility(This, ...)      CALL_IFACE_METHOD(RadientScene, GetEntityOwnVisibility,      This, __VA_ARGS__)
#    define IRadientScene_GetEntityEffectiveVisibility(This, ...) CALL_IFACE_METHOD(RadientScene, GetEntityEffectiveVisibility, This, __VA_ARGS__)
#    define IRadientScene_GetParent(This, ...)                   CALL_IFACE_METHOD(RadientScene, GetParent,                   This, __VA_ARGS__)
#    define IRadientScene_GetChildCount(This, ...)               CALL_IFACE_METHOD(RadientScene, GetChildCount,               This, __VA_ARGS__)
#    define IRadientScene_GetChildren(This, ...)                 CALL_IFACE_METHOD(RadientScene, GetChildren,                 This, __VA_ARGS__)
#    define IRadientScene_GetLocalTransform(This, ...)           CALL_IFACE_METHOD(RadientScene, GetLocalTransform,           This, __VA_ARGS__)
#    define IRadientScene_GetWorldMatrix(This, ...)              CALL_IFACE_METHOD(RadientScene, GetWorldMatrix,              This, __VA_ARGS__)
#    define IRadientScene_HasComponent(This, ...)                CALL_IFACE_METHOD(RadientScene, HasComponent,                This, __VA_ARGS__)
#    define IRadientScene_GetRevision(This)                      CALL_IFACE_METHOD(RadientScene, GetRevision,                 This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
