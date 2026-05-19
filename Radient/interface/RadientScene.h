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

    /// Entity participates in rendering.
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
    RadientComponentTypeID CustomType DEFAULT_INITIALIZER(InvalidRadientComponentTypeID);

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

    /// Creates an entity.
    VIRTUAL RadientEntityID METHOD(CreateEntity)(THIS_
                                                 const RadientEntityDesc REF Desc) PURE;

    /// Destroys an entity and its owned components.
    VIRTUAL void METHOD(DestroyEntity)(THIS_
                                       RadientEntityID Entity) PURE;

    /// Checks if the entity is alive.
    VIRTUAL Bool METHOD(IsEntityAlive)(THIS_
                                       RadientEntityID Entity) CONST PURE;

    /// Sets entity flags.
    VIRTUAL void METHOD(SetEntityFlags)(THIS_
                                        RadientEntityID      Entity,
                                        RADIENT_ENTITY_FLAGS Flags) PURE;

    /// Gets entity flags.
    VIRTUAL Bool METHOD(GetEntityFlags)(THIS_
                                        RadientEntityID          Entity,
                                        RADIENT_ENTITY_FLAGS REF Flags) CONST PURE;

    /// Sets entity visibility.
    VIRTUAL void METHOD(SetEntityVisible)(THIS_
                                          RadientEntityID Entity,
                                          Bool            Visible) PURE;

    /// Returns true if the entity is visible.
    VIRTUAL Bool METHOD(IsEntityVisible)(THIS_
                                         RadientEntityID Entity) CONST PURE;

    /// Sets the entity parent.
    VIRTUAL void METHOD(SetParent)(THIS_
                                   RadientEntityID Entity,
                                   RadientEntityID Parent,
                                   Bool            KeepWorldTransform DEFAULT_VALUE(True)) PURE;

    /// Returns the entity parent, or InvalidRadientEntityID.
    VIRTUAL RadientEntityID METHOD(GetParent)(THIS_
                                              RadientEntityID Entity) CONST PURE;

    /// Sets local transform.
    VIRTUAL void METHOD(SetLocalTransform)(THIS_
                                           RadientEntityID            Entity,
                                           const RadientTransform REF Transform) PURE;

    /// Gets local transform.
    VIRTUAL Bool METHOD(GetLocalTransform)(THIS_
                                           RadientEntityID      Entity,
                                           RadientTransform REF Transform) CONST PURE;

    /// Gets world transform matrix.
    VIRTUAL Bool METHOD(GetWorldMatrix)(THIS_
                                        RadientEntityID      Entity,
                                        RadientMatrix4x4 REF Matrix) CONST PURE;

    /// Adds or updates a camera component.
    VIRTUAL void METHOD(SetCamera)(THIS_
                                   RadientEntityID                  Entity,
                                   const RadientCameraComponent REF Camera) PURE;

    /// Adds or updates a mesh component.
    VIRTUAL void METHOD(SetMesh)(THIS_
                                 RadientEntityID                Entity,
                                 const RadientMeshComponent REF Mesh) PURE;

    /// Adds or updates a mesh renderer component.
    VIRTUAL void METHOD(SetMeshRenderer)(THIS_
                                         RadientEntityID                        Entity,
                                         const RadientMeshRendererComponent REF Renderer) PURE;

    /// Adds or updates a light component.
    VIRTUAL void METHOD(SetLight)(THIS_
                                  RadientEntityID                 Entity,
                                  const RadientLightComponent REF Light) PURE;

    /// Adds or updates a custom serialized component.
    VIRTUAL void METHOD(SetCustomComponentData)(THIS_
                                                RadientEntityID                       Entity,
                                                const RadientCustomComponentData REF Component) PURE;

    /// Removes a component from the entity.
    VIRTUAL void METHOD(RemoveComponent)(THIS_
                                         RadientEntityID        Entity,
                                         RADIENT_COMPONENT_TYPE Type,
                                         RadientComponentTypeID CustomType DEFAULT_VALUE(InvalidRadientComponentTypeID)) PURE;

    /// Returns true when the entity has the requested component.
    VIRTUAL Bool METHOD(HasComponent)(THIS_
                                      RadientEntityID        Entity,
                                      RADIENT_COMPONENT_TYPE Type,
                                      RadientComponentTypeID CustomType DEFAULT_VALUE(InvalidRadientComponentTypeID)) CONST PURE;

    /// Returns current scene revision.
    VIRTUAL RadientRevision METHOD(GetRevision)(THIS) CONST PURE;

    /// Commits pending scene changes to the active backend.
    VIRTUAL void METHOD(CommitChanges)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientScene_GetDesc(This)                    CALL_IFACE_METHOD(RadientScene, GetDesc,           This)
#    define IRadientScene_CreateEntity(This, ...)           CALL_IFACE_METHOD(RadientScene, CreateEntity,      This, __VA_ARGS__)
#    define IRadientScene_DestroyEntity(This, ...)          CALL_IFACE_METHOD(RadientScene, DestroyEntity,     This, __VA_ARGS__)
#    define IRadientScene_IsEntityAlive(This, ...)          CALL_IFACE_METHOD(RadientScene, IsEntityAlive,     This, __VA_ARGS__)
#    define IRadientScene_SetEntityFlags(This, ...)         CALL_IFACE_METHOD(RadientScene, SetEntityFlags,    This, __VA_ARGS__)
#    define IRadientScene_GetEntityFlags(This, ...)         CALL_IFACE_METHOD(RadientScene, GetEntityFlags,    This, __VA_ARGS__)
#    define IRadientScene_SetEntityVisible(This, ...)       CALL_IFACE_METHOD(RadientScene, SetEntityVisible,  This, __VA_ARGS__)
#    define IRadientScene_IsEntityVisible(This, ...)        CALL_IFACE_METHOD(RadientScene, IsEntityVisible,   This, __VA_ARGS__)
#    define IRadientScene_SetParent(This, ...)              CALL_IFACE_METHOD(RadientScene, SetParent,         This, __VA_ARGS__)
#    define IRadientScene_GetParent(This, ...)              CALL_IFACE_METHOD(RadientScene, GetParent,         This, __VA_ARGS__)
#    define IRadientScene_SetLocalTransform(This, ...)      CALL_IFACE_METHOD(RadientScene, SetLocalTransform, This, __VA_ARGS__)
#    define IRadientScene_GetLocalTransform(This, ...)      CALL_IFACE_METHOD(RadientScene, GetLocalTransform, This, __VA_ARGS__)
#    define IRadientScene_GetWorldMatrix(This, ...)         CALL_IFACE_METHOD(RadientScene, GetWorldMatrix,    This, __VA_ARGS__)
#    define IRadientScene_SetCamera(This, ...)              CALL_IFACE_METHOD(RadientScene, SetCamera,         This, __VA_ARGS__)
#    define IRadientScene_SetMesh(This, ...)                CALL_IFACE_METHOD(RadientScene, SetMesh,           This, __VA_ARGS__)
#    define IRadientScene_SetMeshRenderer(This, ...)        CALL_IFACE_METHOD(RadientScene, SetMeshRenderer,   This, __VA_ARGS__)
#    define IRadientScene_SetLight(This, ...)               CALL_IFACE_METHOD(RadientScene, SetLight,          This, __VA_ARGS__)
#    define IRadientScene_SetCustomComponentData(This, ...) CALL_IFACE_METHOD(RadientScene, SetCustomComponentData,  This, __VA_ARGS__)
#    define IRadientScene_RemoveComponent(This, ...)        CALL_IFACE_METHOD(RadientScene, RemoveComponent,   This, __VA_ARGS__)
#    define IRadientScene_HasComponent(This, ...)           CALL_IFACE_METHOD(RadientScene, HasComponent,      This, __VA_ARGS__)
#    define IRadientScene_GetRevision(This)                 CALL_IFACE_METHOD(RadientScene, GetRevision,       This)
#    define IRadientScene_CommitChanges(This)               CALL_IFACE_METHOD(RadientScene, CommitChanges,     This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
