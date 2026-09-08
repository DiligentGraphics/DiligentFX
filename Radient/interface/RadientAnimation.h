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
/// Defines generic animation clip and legacy animation-to-pose registry interfaces.

#include "RadientScene.h"
#include "RadientSkinning.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAnimationClipAsset IRadientAnimationClipAsset;
typedef struct IRadientAnimationRegistry  IRadientAnimationRegistry;

/// UUID-sized identifier of an animation target schema contract.
///
/// Schema providers assign a stable, globally unique value to every published
/// contract. The alias gives that value animation-specific meaning while
/// reusing INTERFACE_ID storage, generation, and comparison conventions; it
/// does not imply that a schema is an object interface.
typedef INTERFACE_ID RadientAnimationSchemaID;

/// Property identifier scoped by a Radient animation target schema.
///
/// A schema provider assigns stable meanings and value layouts to these IDs.
/// IDs are compared only within the same schema; zero is reserved as invalid.
typedef Uint64 RadientAnimationPropertyID;

/// Stable authored-object identifier scoped by an animation target schema.
///
/// This is not a process-wide identity, runtime entity ID, or pointer. For
/// example, an importer may use a source node index and let each scene-instance
/// binding resolve that index to its own runtime destination.
typedef Uint64 RadientAnimationObjectID;

/// Invalid animation schema identifier. A target must use a schema ID other
/// than this all-zero value.
static DILIGENT_CONSTEXPR RadientAnimationSchemaID InvalidRadientAnimationSchemaID =
    {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

/// Invalid animation property identifier. This value cannot name a property.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID InvalidRadientAnimationPropertyID = 0;

/// Invalid animation source-object identifier. All other values, including
/// zero, are available to a schema.
static DILIGENT_CONSTEXPR RadientAnimationObjectID InvalidRadientAnimationObject = (Uint64)~0ull;

/// Invalid index into an animation clip target table. This value can never be a
/// valid RadientAnimationChannelDesc::TargetIndex.
static DILIGENT_CONSTEXPR Uint32 InvalidRadientAnimationTargetIndex = (Uint32)~0u;

/// Invalid index into an animation clip sampler table. This value can never be
/// a valid RadientAnimationChannelDesc::SamplerIndex.
static DILIGENT_CONSTEXPR Uint32 InvalidRadientAnimationSamplerIndex = (Uint32)~0u;


// clang-format off

/// Native type of one animation value array element.
DILIGENT_TYPED_ENUM(RADIENT_ANIMATION_VALUE_TYPE, Uint8)
{
    /// Invalid or unspecified value type.
    RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN = 0,

    /// Boolean scalar stored as one Uint8 byte; zero is false and one is true.
    RADIENT_ANIMATION_VALUE_TYPE_BOOL,

    /// One signed 32-bit integer.
    RADIENT_ANIMATION_VALUE_TYPE_INT,

    /// Two signed 32-bit integers in x, y order.
    RADIENT_ANIMATION_VALUE_TYPE_INT2,

    /// Three signed 32-bit integers in x, y, z order.
    RADIENT_ANIMATION_VALUE_TYPE_INT3,

    /// Four signed 32-bit integers in x, y, z, w order.
    RADIENT_ANIMATION_VALUE_TYPE_INT4,

    /// One unsigned 32-bit integer.
    RADIENT_ANIMATION_VALUE_TYPE_UINT,

    /// Two unsigned 32-bit integers in x, y order.
    RADIENT_ANIMATION_VALUE_TYPE_UINT2,

    /// Three unsigned 32-bit integers in x, y, z order.
    RADIENT_ANIMATION_VALUE_TYPE_UINT3,

    /// Four unsigned 32-bit integers in x, y, z, w order.
    RADIENT_ANIMATION_VALUE_TYPE_UINT4,

    /// One 32-bit floating-point value.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT,

    /// Two 32-bit floating-point values in x, y order.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT2,

    /// Three 32-bit floating-point values in x, y, z order.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,

    /// Four 32-bit floating-point values in x, y, z, w order.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,

    /// One row-major 2x2 matrix of 32-bit floating-point values.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2,

    /// One row-major 3x3 matrix of 32-bit floating-point values.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3,

    /// One row-major 4x4 matrix of 32-bit floating-point values.
    RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4,

    /// Sentinel equal to the number of animation value types. This is not a
    /// valid RadientAnimationValueDesc::Type.
    RADIENT_ANIMATION_VALUE_TYPE_COUNT
};

// clang-format on


/// Type and fixed array length of a sampled animation value.
///
/// ArraySize is the number of native values, not the number of scalar
/// components. For example, one RadientFloat3 has Type FLOAT3 and ArraySize 1;
/// ten morph weights have Type FLOAT and ArraySize 10. Semantic meaning is
/// defined by the target schema and property: for example, a rotation sampler
/// uses FLOAT4 storage, while its property defines quaternion interpolation.
struct RadientAnimationValueDesc
{
    /// Native storage type of each element in the sampled array. The target
    /// schema gives this storage its semantic meaning; for example, FLOAT4 may
    /// represent a quaternion, color, or ordinary four-component vector.
    /// UNKNOWN and COUNT are invalid for a clip sampler.
    RADIENT_ANIMATION_VALUE_TYPE Type DEFAULT_INITIALIZER(RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN);

    /// Number of complete Type values produced at each keyframe. This is not
    /// the number of scalar components: FLOAT3 with ArraySize 1 is one vector,
    /// while FLOAT with ArraySize 10 is an array of ten scalars. Must be
    /// nonzero in a clip sampler.
    Uint32 ArraySize DEFAULT_INITIALIZER(1);
};
typedef struct RadientAnimationValueDesc RadientAnimationValueDesc;


/// Immutable keyframe sampler stored by an animation clip.
///
/// pTimes contains KeyframeCount strictly increasing, finite clip-local times.
/// STEP and LINEAR samplers contain KeyframeCount arrays described by Value in
/// pValues. CUBIC_SPLINE samplers contain three arrays per keyframe in
/// incoming-tangent, value, outgoing-tangent order. Values use the native type
/// selected by Value.Type and arrays are tightly packed. ValueDataSize must
/// exactly equal the native value size multiplied by Value.ArraySize,
/// KeyframeCount, and three for CUBIC_SPLINE (one otherwise). Boolean and
/// integer samplers only support STEP interpolation. Floating-point components
/// must be finite. A one-key CUBIC_SPLINE sampler is valid and represents the
/// key's central value; its tangent arrays are still present. Cubic tangents
/// are derivatives per second in the same native storage type as the value.
///
/// For example, this sampler stores two linearly interpolated FLOAT3 positions:
///
///     const Float32 Times[] = {0.f, 1.f};
///     const RadientFloat3 Positions[] = {{0.f, 0.f, 0.f},
///                                        {2.f, 1.f, 0.f}};
///     const RadientAnimationSamplerDesc PositionSampler =
///     {
///         {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1},
///         RADIENT_ANIMATION_INTERPOLATION_LINEAR,
///         Times,
///         Positions,
///         sizeof(Positions),
///         2
///     };
///
/// With Type FLOAT3, ArraySize 2, and KeyframeCount 2, LINEAR pValues
/// contains four consecutive FLOAT3 values: key0[0], key0[1], key1[0],
/// key1[1]. CUBIC_SPLINE contains twelve: key0 incoming[2], value[2],
/// outgoing[2], followed by the same three arrays for key1.
struct RadientAnimationSamplerDesc
{
    /// Type and fixed array length produced by this sampler.
    RadientAnimationValueDesc Value;

    /// Interpolation applied between keyframes. Boolean and signed or unsigned
    /// integer values require STEP. A target property's schema determines
    /// semantic details; for example, a rotation property may interpret FLOAT4
    /// LINEAR as a spherical interpolation rather than component-wise
    /// interpolation.
    RADIENT_ANIMATION_INTERPOLATION Interpolation DEFAULT_INITIALIZER(RADIENT_ANIMATION_INTERPOLATION_LINEAR);

    /// Array of KeyframeCount times in seconds. The pointer must be non-null
    /// when KeyframeCount is nonzero. Times must be finite, strictly increasing,
    /// and in the inclusive range [0, RadientAnimationClipDesc::Duration]. The
    /// first and last times need not equal the clip boundaries. Creation copies
    /// this array before returning.
    const Float32* pTimes DEFAULT_INITIALIZER(nullptr);

    /// Tightly packed keyframe data. STEP and LINEAR store one Value array per
    /// key. CUBIC_SPLINE stores incoming-tangent, central-value, and
    /// outgoing-tangent arrays for every key. The pointer must be non-null for
    /// a nonempty sampler and is copied during clip creation.
    const void* pValues DEFAULT_INITIALIZER(nullptr);

    /// Exact size of pValues in bytes. This must equal the native byte size of
    /// Value.Type multiplied by Value.ArraySize, KeyframeCount, and three for
    /// CUBIC_SPLINE (one for STEP or LINEAR).
    Uint64 ValueDataSize DEFAULT_INITIALIZER(0);

    /// Number of entries in pTimes and logical keys in pValues. Must be
    /// nonzero. CUBIC_SPLINE also permits a single key.
    Uint32 KeyframeCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationSamplerDesc RadientAnimationSamplerDesc;


/// Symbolic, unbound target slot in an animation clip.
///
/// Object is a stable source-object identity in the namespace identified by
/// Schema, allowing the same clip to resolve against different instances.
/// The target index identifies this record within the clip. Name is optional,
/// is stored as an owned non-null string, and is used only for diagnostics;
/// bindings must not depend on it. Each (Schema, Object) pair must be unique in
/// a clip. The same Object value under different schemas identifies distinct
/// targets.
struct RadientAnimationTargetDesc
{
    /// Identifier of the schema that owns the property-ID namespace, defines
    /// property semantics, and resolves Object during binding.
    /// InvalidRadientAnimationSchemaID is not permitted. This identifies a
    /// schema contract, not the IID of the runtime object eventually updated
    /// by animation.
    RadientAnimationSchemaID Schema DEFAULT_INITIALIZER(InvalidRadientAnimationSchemaID);

    /// Stable authored-object identifier in Schema's namespace. This is source
    /// identity, not a runtime RadientEntityID or pointer. A binder resolves it
    /// independently for every scene instance. Every value, including zero, is
    /// valid except InvalidRadientAnimationObject.
    RadientAnimationObjectID Object DEFAULT_INITIALIZER(InvalidRadientAnimationObject);

    /// Optional diagnostic source-object name. Creation copies the string and
    /// stores an empty string when this pointer is null. Name is not part of
    /// target identity and is never used to resolve a binding.
    const Char* Name DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAnimationTargetDesc RadientAnimationTargetDesc;


/// Connects one clip sampler to a property of one symbolic target.
///
/// The sampler value type and array length must be compatible with the target
/// property. FirstArrayElement enables a sampler to address a contiguous range
/// within an array property, such as all or part of a morph-weight array.
/// Creation validates indices, range overflow, overlap, and consistent native
/// types among ranges of one target/property. Whether the property exists and
/// accepts the sampler layout is intentionally validated later, when binding.
struct RadientAnimationChannelDesc
{
    /// Zero-based index into RadientAnimationClipDesc::pTargets. Must be less
    /// than RadientAnimationClipDesc::TargetCount.
    Uint32 TargetIndex DEFAULT_INITIALIZER(InvalidRadientAnimationTargetIndex);

    /// Nonzero property identifier interpreted in the namespace of
    /// RadientAnimationClipDesc::pTargets[TargetIndex].Schema. Property IDs
    /// only need to be unique and stable within their schema; the same numeric
    /// ID may mean something else in another schema.
    RadientAnimationPropertyID Property DEFAULT_INITIALIZER(InvalidRadientAnimationPropertyID);

    /// First destination array element addressed by this channel. The channel
    /// covers the half-open range [FirstArrayElement, FirstArrayElement +
    /// RadientAnimationClipDesc::pSamplers[SamplerIndex].Value.ArraySize). This
    /// is normally zero for a non-array property. Elements are complete native
    /// values, so one FLOAT3 occupies one array element rather than three.
    ///
    /// For example, a FLOAT sampler with ArraySize 4 and FirstArrayElement 8
    /// addresses morph weights [8, 12). Another channel may address [12, 16),
    /// but [10, 14) would overlap and is invalid.
    Uint32 FirstArrayElement DEFAULT_INITIALIZER(0);

    /// Zero-based index into RadientAnimationClipDesc::pSamplers. Must be less
    /// than RadientAnimationClipDesc::SamplerCount. Multiple compatible
    /// channels may share one sampler by using the same index.
    Uint32 SamplerIndex DEFAULT_INITIALIZER(InvalidRadientAnimationSamplerIndex);
};
typedef struct RadientAnimationChannelDesc RadientAnimationChannelDesc;


/// Immutable, unbound animation clip description.
///
/// Creation copies the name and all target, sampler, channel, time, and value
/// data. TargetIndex and SamplerIndex values in channels index the corresponding
/// tables below. Duration and all keyframe times are expressed in seconds. An
/// empty clip is valid only when all three table counts are zero and all three
/// table pointers are null. A nonempty clip contains at least one record in
/// every table, and every target and sampler must be referenced by a channel.
/// Target records must have unique (Schema, Object) identities. Channels for
/// one target and property may address adjacent or disjoint ranges, but their
/// ranges must not overlap and must use one native value type.
/// Destination-schema compatibility is established when binding.
///
/// The clip does not define playback behavior before the first key or after the
/// last key. Clamp, loop, and other time policies belong to the player that
/// samples the clip.
///
/// Example (C++): the node-animation schema provider publishes
/// NodeAnimationSchemaID and NodeTranslationProperty. Object 17 is an
/// authored node identity, while both zero indices refer to tables in Clip.
///
/// \code
/// const Float32 Times[] = {0.f, 1.f};
/// const RadientFloat3 Translations[] = {{0.f, 0.f, 0.f},
///                                        {0.f, 2.f, 0.f}};
///
/// RadientAnimationTargetDesc Target{};
/// Target.Schema = NodeAnimationSchemaID;
/// Target.Object = 17;
/// Target.Name   = "Root";
///
/// RadientAnimationSamplerDesc Sampler{};
/// Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
/// Sampler.Value.ArraySize = 1; // One FLOAT3, not three array elements.
/// Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
/// Sampler.pTimes          = Times;
/// Sampler.pValues         = Translations;
/// Sampler.ValueDataSize   = sizeof(Translations);
/// Sampler.KeyframeCount   = 2;
///
/// RadientAnimationChannelDesc Channel{};
/// Channel.TargetIndex       = 0; // Target above.
/// Channel.Property          = NodeTranslationProperty;
/// Channel.FirstArrayElement = 0;
/// Channel.SamplerIndex      = 0; // Sampler above.
///
/// RadientAnimationClipDesc Clip{};
/// Clip.Name         = "Root rise";
/// Clip.Duration     = 1.f;
/// Clip.pTargets     = &Target;
/// Clip.TargetCount  = 1;
/// Clip.pSamplers    = &Sampler;
/// Clip.SamplerCount = 1;
/// Clip.pChannels    = &Channel;
/// Clip.ChannelCount = 1;
///
/// IRadientAnimationClipAsset* pClip = nullptr;
/// const RADIENT_STATUS Status = pAssetManager->CreateAnimationClip(Clip, &pClip);
/// \endcode
///
/// All strings and arrays in this example may use stack storage because
/// CreateAnimationClip copies them before returning. The caller owns the strong
/// reference returned through pClip and releases it in the usual way. C callers
/// initialize descriptors with `{0}`, explicitly assign every required member
/// (C initialization does not apply C++ DEFAULT_INITIALIZER values), and invoke:
///
/// \code
/// IRadientAssetManager_CreateAnimationClip(pAssetManager, &Clip, &pClip);
/// \endcode
struct RadientAnimationClipDesc
{
    /// Optional diagnostic clip name. Creation copies the string. The asset's
    /// stored description contains an owned, non-null string, using "" when
    /// this input pointer is null.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Finite, non-negative duration in seconds. Every sampler time must be in
    /// the inclusive range [0, Duration], but Duration need not equal the final
    /// key time. When Duration is zero, every sampler must contain exactly one
    /// key at time zero.
    Float32 Duration DEFAULT_INITIALIZER(0.f);

    /// Array of TargetCount symbolic targets. Must be null exactly when
    /// TargetCount is zero. Creation copies the array and all target names.
    const RadientAnimationTargetDesc* pTargets DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pTargets. Every target in a nonempty clip must be
    /// referenced by at least one channel.
    Uint32 TargetCount DEFAULT_INITIALIZER(0);

    /// Array of SamplerCount typed keyframe samplers. Must be null exactly when
    /// SamplerCount is zero. Creation copies the descriptors and their time and
    /// value arrays.
    const RadientAnimationSamplerDesc* pSamplers DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pSamplers. Every sampler in a nonempty clip must
    /// be referenced by at least one channel.
    Uint32 SamplerCount DEFAULT_INITIALIZER(0);

    /// Array of ChannelCount target-property connections. Must be null exactly
    /// when ChannelCount is zero. Creation copies the array.
    const RadientAnimationChannelDesc* pChannels DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pChannels. A nonempty clip must contain at least
    /// one channel.
    Uint32 ChannelCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationClipDesc RadientAnimationClipDesc;


// {CF8BE652-BE67-46C8-AC57-F016048B46EA}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationClipAsset =
    {0xcf8be652, 0xbe67, 0x46c8, {0xac, 0x57, 0xf0, 0x16, 0x4, 0x8b, 0x46, 0xea}};


#define DILIGENT_INTERFACE_NAME IRadientAnimationClipAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAnimationClipAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;                 \
    IRadientAnimationClipAssetMethods RadientAnimationClipAsset

// clang-format off

/// Immutable, unbound collection of typed animation channels.
///
/// The asset owns clip data but does not retain or address a scene. A binding
/// step resolves each (Schema, Object) pair for a particular runtime instance
/// and may compile the channels into optimized destination-specific batches.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationClipAsset, IRadientAsset)
{
    /// Returns the immutable clip description. The reference and all data it
    /// references remain valid for the lifetime of the clip asset.
    VIRTUAL const RadientAnimationClipDesc REF METHOD(GetDesc)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationClipAsset_GetDesc(This) CALL_IFACE_METHOD(RadientAnimationClipAsset, GetDesc, This)

#endif

// clang-format on


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
