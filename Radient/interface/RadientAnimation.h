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
/// Defines animation-to-pose registry interfaces.

#include "RadientScene.h"
#include "RadientSkinning.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAnimationRegistry IRadientAnimationRegistry;

/// One unique skeleton pose targeted by an animation.
struct RadientAnimationTarget
{
    /// Pose resolved from the registered entities' RadientSkinComponent data.
    /// The registry retains the pose; this pointer is borrowed and is never
    /// null in a registry state entry.
    IRadientSkeletonPose* pPose DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAnimationTarget RadientAnimationTarget;


/// One animation and the unique poses to which it may be applied.
struct RadientAnimationRegistryEntry
{
    /// Animation shared by every target in pTargets. The registry retains the
    /// animation; this pointer is borrowed and is never null.
    IRadientSkeletonAnimationAsset* pAnimation DEFAULT_INITIALIZER(nullptr);

    /// Array of TargetCount unique pose targets. The pointer and its elements
    /// remain valid until the registry is modified.
    const RadientAnimationTarget* pTargets DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pTargets. Registry entries never have zero targets.
    Uint32 TargetCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationRegistryEntry RadientAnimationRegistryEntry;


/// Current animation registry contents.
struct RadientAnimationRegistryState
{
    /// Monotonic revision incremented whenever the registry contents change.
    RadientRevision Revision DEFAULT_INITIALIZER(0);

    /// Array of EntryCount animation entries. The pointer and all transitively
    /// referenced arrays remain valid until the registry is modified.
    const RadientAnimationRegistryEntry* pEntries DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pEntries.
    Uint32 EntryCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationRegistryState RadientAnimationRegistryState;



// {8025970E-65E2-4406-AC01-0A2D6F8ACCA3}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationRegistry =
    {0x8025970e, 0x65e2, 0x4406, {0xac, 0x1, 0xa, 0x2d, 0x6f, 0x8a, 0xcc, 0xa3}};


#define DILIGENT_INTERFACE_NAME IRadientAnimationRegistry
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAnimationRegistryInclusiveMethods \
    IObjectInclusiveMethods;                      \
    IRadientAnimationRegistryMethods RadientAnimationRegistry

// clang-format off

/// Externally owned mapping from skeleton animations to unique skeleton poses.
///
/// A registry is associated with one scene and retains that scene. It does not
/// modify the scene or automatically observe entity destruction. The code that
/// adds or removes scene content is responsible for updating its private entity
/// associations.
///
/// The interface is externally synchronized. Applications must not call its
/// methods concurrently without their own synchronization.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationRegistry, IObject)
{
    /// Returns the scene associated with this registry. The returned pointer is
    /// borrowed and remains valid for the registry lifetime.
    VIRTUAL IRadientScene* METHOD(GetScene)(THIS) CONST PURE;

    /// Adds unique animation-to-entity associations.
    ///
    /// The registry resolves each entity's RadientSkinComponent once, retains
    /// its pose, and verifies that the pose and pAnimation target the same
    /// skeleton. Entities sharing a pose produce one public animation target;
    /// the target remains registered until its last entity association is
    /// removed. The operation is atomic: no associations are added if any
    /// entity is invalid, has no skin component or pose, or targets another
    /// skeleton. Existing associations are ignored. Returns
    /// RADIENT_STATUS_NO_CHANGE when EntityCount is zero or every association
    /// already exists.
    VIRTUAL RADIENT_STATUS METHOD(AddAnimatedEntities)(THIS_
                                                       IRadientSkeletonAnimationAsset* pAnimation,
                                                       const RadientEntityID*          pEntities,
                                                       Uint32                          EntityCount) PURE;

    /// Removes the specified animation-to-entity associations. Missing
    /// associations are ignored. Returns RADIENT_STATUS_NO_CHANGE when the
    /// registry is not modified. An animation entry is removed when its last
    /// target is removed.
    VIRTUAL RADIENT_STATUS METHOD(RemoveAnimatedEntities)(THIS_
                                                          IRadientSkeletonAnimationAsset* pAnimation,
                                                          const RadientEntityID*          pEntities,
                                                          Uint32                          EntityCount) PURE;

    /// Removes an entity from every animation entry. This method only updates
    /// the registry and never destroys or otherwise modifies the scene entity.
    /// Returns RADIENT_STATUS_NO_CHANGE when the entity is not registered.
    VIRTUAL RADIENT_STATUS METHOD(RemoveEntity)(THIS_
                                                RadientEntityID Entity) PURE;

    /// Removes an animation and all of its target associations. Returns
    /// RADIENT_STATUS_NO_CHANGE when the animation is not registered.
    VIRTUAL RADIENT_STATUS METHOD(RemoveAnimation)(THIS_
                                                   IRadientSkeletonAnimationAsset* pAnimation) PURE;

    /// Returns the current registry contents. The returned reference remains
    /// valid for the registry lifetime. Its arrays are invalidated by the next
    /// successful registry mutation.
    VIRTUAL const RadientAnimationRegistryState REF METHOD(GetState)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationRegistry_GetScene(This)                    CALL_IFACE_METHOD(RadientAnimationRegistry, GetScene,               This)
#    define IRadientAnimationRegistry_AddAnimatedEntities(This, ...)    CALL_IFACE_METHOD(RadientAnimationRegistry, AddAnimatedEntities,    This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveAnimatedEntities(This, ...) CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveAnimatedEntities, This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveEntity(This, ...)           CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveEntity,           This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveAnimation(This, ...)        CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveAnimation,        This, __VA_ARGS__)
#    define IRadientAnimationRegistry_GetState(This)                    CALL_IFACE_METHOD(RadientAnimationRegistry, GetState,               This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
