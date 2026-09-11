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
/// Defines generic animation clips, destinations, bindings, and their runtime
/// registry.

#include "RadientAssets.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAnimationDestination        IRadientAnimationDestination;
typedef struct IRadientAnimationDestinationBinding IRadientAnimationDestinationBinding;
typedef struct IRadientAnimationClipAsset          IRadientAnimationClipAsset;
typedef struct IRadientAnimationBinding            IRadientAnimationBinding;
typedef struct IRadientAnimationRegistry           IRadientAnimationRegistry;
typedef struct IRadientScene                       IRadientScene;

/// UUID-sized identifier of an animation target schema contract.
///
/// Schema providers assign a stable, globally unique value to every published
/// contract. The alias gives that value animation-specific meaning while
/// reusing INTERFACE_ID storage, generation, and comparison conventions; it
/// does not imply that a schema is an object interface. A glTF animation-pointer
/// importer resolves every supported canonical pointer through a schema
/// provider into (Schema, Object, Property, array range); unknown providers or
/// properties are reported as unsupported rather than encoded as unstable
/// process-local IDs.
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

/// Schema-specific runtime element within an animation destination.
///
/// For example, the node-animation schema uses a skeleton joint index in a
/// skeleton-pose destination and a RadientEntityID in a scene-writer
/// destination.
typedef Uint64 RadientAnimationDestinationElement;

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

/// Invalid schema-specific runtime destination element. This value is reserved;
/// schemas may use every smaller Uint64 value.
static DILIGENT_CONSTEXPR RadientAnimationDestinationElement InvalidRadientAnimationDestinationElement = (Uint64)~0ull;

/// Built-in schema for animatable properties of an authored scene node.
///
/// A target's Object is the authored node identity. A binding may resolve the
/// same node to a skeleton joint, a scene entity, or another runtime node
/// representation without changing the clip. Each destination exposes only
/// the subset it supports: skeleton poses expose local transforms, while scene
/// entities also expose own visibility. Root animation is represented by the
/// same schema as every other node transform.
// {E4ADD320-EECA-439F-A6C3-1D8A25AEFBC3}
static DILIGENT_CONSTEXPR RadientAnimationSchemaID RadientNodeAnimationSchemaID =
    {0xe4add320, 0xeeca, 0x439f, {0xa6, 0xc3, 0x1d, 0x8a, 0x25, 0xae, 0xfb, 0xc3}};

/// FLOAT3[1] local translation property in RadientNodeAnimationSchemaID.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID RadientNodeTranslationProperty = 1;

/// FLOAT4[1] normalized quaternion local rotation property in
/// RadientNodeAnimationSchemaID. LINEAR interpolation uses spherical
/// interpolation; CUBIC_SPLINE results are normalized.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID RadientNodeRotationProperty = 2;

/// FLOAT3[1] local scale property in RadientNodeAnimationSchemaID.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID RadientNodeScaleProperty = 3;

/// BOOL[1] own visibility property in RadientNodeAnimationSchemaID. Zero is
/// hidden and one is visible. Only STEP interpolation is supported. Effective
/// visibility is derived by the scene from this property and ancestor
/// visibility.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID RadientNodeVisibilityProperty = 4;

/// Built-in schema for morph weights owned by an authored scene node.
///
/// A target's Object is the authored identity of the node whose mesh supplies
/// the morph targets. Keeping morph weights in a separate schema lets the same
/// source node bind its transform and weights to different destinations.
// {8E3A3B5B-2267-4E06-B94A-316746AB9F61}
static DILIGENT_CONSTEXPR RadientAnimationSchemaID RadientMorphWeightsAnimationSchemaID =
    {0x8e3a3b5b, 0x2267, 0x4e06, {0xb9, 0x4a, 0x31, 0x67, 0x46, 0xab, 0x9f, 0x61}};

/// FLOAT[N] weight property in RadientMorphWeightsAnimationSchemaID, where N
/// is the number of morph targets used by the bound node. Channels may animate
/// the complete array or disjoint ranges through FirstArrayElement. A scene
/// destination that addresses scene nodes may use the node's RadientEntityID as
/// its destination element. A destination wrapping exactly one weight array may
/// instead use element zero.
static DILIGENT_CONSTEXPR RadientAnimationPropertyID RadientMorphWeightsProperty = 1;


// clang-format off

/// Interpolation applied between animation keyframes.
DILIGENT_TYPED_ENUM(RADIENT_ANIMATION_INTERPOLATION, Uint8)
{
    /// Holds the preceding keyframe value until the next keyframe.
    RADIENT_ANIMATION_INTERPOLATION_STEP = 0,

    /// Applies the target property's linear interpolation semantics. Numeric
    /// values are normally interpolated component-wise; a rotation property
    /// may instead use spherical interpolation of quaternion storage.
    RADIENT_ANIMATION_INTERPOLATION_LINEAR,

    /// Evaluates a cubic Hermite spline using authored incoming and outgoing
    /// tangents. Tangents are derivatives per second; the target property's
    /// schema defines any additional semantic interpretation.
    RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,

    /// Sentinel equal to the number of animation interpolation modes. This is
    /// not a valid interpolation mode.
    RADIENT_ANIMATION_INTERPOLATION_COUNT
};


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


/// Semantic interpolation applied to a resolved property's native values.
DILIGENT_TYPED_ENUM(RADIENT_ANIMATION_VALUE_SEMANTIC, Uint8)
{
    /// Unbound or unspecified value semantic. A destination uses this value for
    /// a property that it does not expose.
    RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN = 0,

    /// Interpolates every scalar component independently.
    RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,

    /// Treats each FLOAT4 element as a normalized quaternion. LINEAR uses
    /// shortest-path spherical interpolation. CUBIC_SPLINE evaluates the
    /// component-wise Hermite curve and normalizes each result.
    RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION,

    /// Sentinel equal to the number of value semantics. This is not a valid
    /// RadientAnimationResolvedPropertyDesc::Semantic value.
    RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT
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
/// Binding evaluation holds the nearest endpoint value outside each sampler's
/// key interval. Looping, ping-pong, playback-rate handling, and time wrapping
/// belong to the caller or a future player.
///
/// Example (C++): Object 17 is an authored node identity, while both zero
/// indices refer to tables in Clip.
///
/// \code
/// const Float32 Times[] = {0.f, 1.f};
/// const RadientFloat3 Translations[] = {{0.f, 0.f, 0.f},
///                                        {0.f, 2.f, 0.f}};
///
/// RadientAnimationTargetDesc Target{};
/// Target.Schema = RadientNodeAnimationSchemaID;
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
/// Channel.Property          = RadientNodeTranslationProperty;
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


/// Property range that a binding asks one destination to resolve.
///
/// Bindings derive these records from clip channels and destination mappings;
/// applications normally do not create them directly. For example, a node
/// rotation channel mapped to skeleton joint 42 requests
/// RadientNodeAnimationSchemaID, element 42,
/// RadientNodeRotationProperty, FirstArrayElement 0, and FLOAT4[1].
struct RadientAnimationPropertyBindingDesc
{
    /// Schema of the symbolic clip target that owns Property.
    RadientAnimationSchemaID Schema DEFAULT_INITIALIZER(InvalidRadientAnimationSchemaID);

    /// Schema-specific runtime element selected by the destination mapping.
    /// Must not equal InvalidRadientAnimationDestinationElement.
    RadientAnimationDestinationElement DestinationElement DEFAULT_INITIALIZER(InvalidRadientAnimationDestinationElement);

    /// Property identifier in Schema's namespace.
    RadientAnimationPropertyID Property DEFAULT_INITIALIZER(InvalidRadientAnimationPropertyID);

    /// First complete array element written by this property range. Together
    /// with Value.ArraySize, this identifies the half-open destination range.
    Uint32 FirstArrayElement DEFAULT_INITIALIZER(0);

    /// Native type and number of array elements written by the channel.
    RadientAnimationValueDesc Value;
};
typedef struct RadientAnimationPropertyBindingDesc RadientAnimationPropertyBindingDesc;


/// Destination result for one resolved property range.
struct RadientAnimationResolvedPropertyDesc
{
    /// Semantic used to interpolate a bound property's native values. For
    /// example, RadientNodeRotationProperty resolves to
    /// RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION, while node
    /// translation, scale, visibility, RadientMorphWeightsProperty, and
    /// ordinary numeric properties resolve to
    /// RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE.
    /// The same accepted (Schema, Property) contract must resolve to the same
    /// non-UNKNOWN semantic for every element and destination implementation.
    /// UNKNOWN means that the destination did not bind this property; COUNT is
    /// always invalid.
    /// NORMALIZED_QUATERNION requires FLOAT4 storage; binding creation rejects
    /// incompatible semantic/type pairs.
    RADIENT_ANIMATION_VALUE_SEMANTIC Semantic DEFAULT_INITIALIZER(RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
};
typedef struct RadientAnimationResolvedPropertyDesc RadientAnimationResolvedPropertyDesc;


/// Maps one symbolic clip target to an element of one aggregate destination.
///
/// ClipTargetIndex selects a RadientAnimationTargetDesc, while
/// DestinationElement selects the corresponding runtime object inside the
/// destination. Repeating ClipTargetIndex with different elements intentionally
/// fans one authored target out to multiple runtime instances.
struct RadientAnimationDestinationMappingDesc
{
    /// Zero-based index into the bound clip's target table. It must be less
    /// than RadientAnimationClipDesc::TargetCount.
    Uint32 ClipTargetIndex DEFAULT_INITIALIZER(InvalidRadientAnimationTargetIndex);

    /// Schema-specific element within its containing pDestination. A node
    /// target uses a zero-based joint index for a skeleton-pose destination or
    /// a RadientEntityID for a scene-writer destination. Other schemas and
    /// destinations define their own element identities. Must not equal
    /// InvalidRadientAnimationDestinationElement.
    RadientAnimationDestinationElement DestinationElement DEFAULT_INITIALIZER(InvalidRadientAnimationDestinationElement);
};
typedef struct RadientAnimationDestinationMappingDesc RadientAnimationDestinationMappingDesc;


/// One aggregate runtime destination and all clip targets mapped into it.
struct RadientAnimationDestinationDesc
{
    /// Aggregate object receiving the mapped property updates. A skeleton pose
    /// exposes one destination for all of its joints; a scene writer exposes
    /// one for all entities in its scene. On successful compilation, the
    /// returned destination binding retains this interface. The pointer must
    /// not be null.
    IRadientAnimationDestination* pDestination DEFAULT_INITIALIZER(nullptr);

    /// Array of MappingCount symbolic-target-to-element mappings. It must not
    /// be null and is copied by CreateBinding().
    const RadientAnimationDestinationMappingDesc* pMappings DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pMappings. Destination descriptors with no
    /// mappings are invalid; omit the complete descriptor instead.
    Uint32 MappingCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationDestinationDesc RadientAnimationDestinationDesc;


/// Description used to compile an animation binding.
///
/// Creation copies both descriptor-array levels and asks every destination to
/// resolve every channel contributed by its mapped targets. A destination may
/// bind only the subset of schema properties that it exposes. Unsupported
/// properties are omitted while compiling the binding and incur no evaluation
/// cost. Each nonempty child binding retains its destination; the outer binding
/// retains the children. Unmapped clip targets are ignored. A zero-destination
/// or completely unbound binding is valid and evaluates to
/// RADIENT_STATUS_NO_CHANGE.
///
/// Each pDestination must occur in exactly one destination descriptor. Exact
/// duplicate mappings and overlapping accepted writes to the same resolved
/// property range are invalid. Unsupported requests do not participate in
/// overlap checks. A clip target may appear in multiple mappings to support
/// fanout. Different targets may map to one element when their accepted property
/// ranges do not overlap. Aliasing through two different destination interface
/// pointers cannot be detected and is the caller's responsibility; evaluation
/// applies destination descriptors in order.
///
/// Evaluation is a sparse overwrite: destination state outside the clip's
/// channel ranges is preserved. Blending, additive animation, base-pose
/// restoration, and root-motion extraction are outside the binding contract
/// and are handled by the caller or a future player/mixer.
///
/// Example (C++): a 1,000-joint skeleton uses one destination descriptor, one
/// retained pose destination, and a compact mapping array. Evaluation brackets
/// the complete pose with one BeginUpdate()/EndUpdate() pair, not one virtual
/// call per joint or transform component. ClipTargetForJoint is
/// importer-produced data that maps the clip's authored node identities to
/// skeleton joint indices.
///
/// \code
/// std::vector<RadientAnimationDestinationMappingDesc> JointMappings(JointCount);
/// for (Uint32 Joint = 0; Joint < JointCount; ++Joint)
/// {
///     JointMappings[Joint].ClipTargetIndex    = ClipTargetForJoint[Joint];
///     JointMappings[Joint].DestinationElement = Joint;
/// }
///
/// IRadientAnimationDestination* pPoseDestination = nullptr;
/// pPose->QueryInterface(IID_RadientAnimationDestination, &pPoseDestination);
///
/// RadientAnimationDestinationDesc PoseDestination{};
/// PoseDestination.pDestination = pPoseDestination;
/// PoseDestination.pMappings    = JointMappings.data();
/// PoseDestination.MappingCount = static_cast<Uint32>(JointMappings.size());
///
/// RadientAnimationBindingDesc BindingDesc{};
/// BindingDesc.pDestinations    = &PoseDestination;
/// BindingDesc.DestinationCount = 1;
///
/// IRadientAnimationBinding* pBinding = nullptr;
/// const RADIENT_STATUS Status = pClip->CreateBinding(BindingDesc, &pBinding);
/// pPoseDestination->Release(); // The binding retained it on success.
/// \endcode
///
/// For a scene-node transform, the corresponding core fields are:
///
/// \code
/// IRadientAnimationDestination* pSceneDestination = nullptr;
/// pSceneWriter->QueryInterface(IID_RadientAnimationDestination,
///                              &pSceneDestination);
///
/// RadientAnimationDestinationMappingDesc RootMapping{};
/// RootMapping.ClipTargetIndex    = RootTargetIndex;
/// RootMapping.DestinationElement = RootEntity;
///
/// RadientAnimationDestinationDesc SceneDestination{};
/// SceneDestination.pDestination = pSceneDestination;
/// SceneDestination.pMappings    = &RootMapping;
/// SceneDestination.MappingCount = 1;
/// \endcode
///
/// SceneDestination replaces PoseDestination in the complete binding example
/// above. Release pSceneDestination after CreateBinding() returns, just like
/// pPoseDestination.
///
/// Root animation is the same mapping with the root joint or root entity;
/// root-motion extraction and redirection remain caller or future player/mixer
/// policies.
struct RadientAnimationBindingDesc
{
    /// Array of DestinationCount aggregate destinations. It must be null
    /// exactly when DestinationCount is zero. CreateBinding() copies the array.
    const RadientAnimationDestinationDesc* pDestinations DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pDestinations.
    Uint32 DestinationCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationBindingDesc RadientAnimationBindingDesc;


/// Parameters for evaluating a compiled animation binding.
struct RadientAnimationEvaluateInfo
{
    /// Finite clip-local time in seconds. Each sampler holds its first value
    /// before its first key and its last value after its last key. Looping,
    /// ping-pong, playback-rate handling, and time wrapping belong to the
    /// caller or a future player.
    Float32 Time DEFAULT_INITIALIZER(0.f);

    /// Passed to each destination binding's EndUpdate() after all values for
    /// that destination have been sampled. True requests one
    /// destination-specific derived-state update for the complete property
    /// batch. For a skeleton pose, this propagates global transforms once; for
    /// a scene writer, it commits pending derived scene state once.
    Bool UpdateDerivedState DEFAULT_INITIALIZER(True);
};
typedef struct RadientAnimationEvaluateInfo RadientAnimationEvaluateInfo;


// {65F04A12-2D6B-4313-9907-9F670A116244}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationDestination =
    {0x65f04a12, 0x2d6b, 0x4313, {0x99, 0x7, 0x9f, 0x67, 0xa, 0x11, 0x62, 0x44}};

// {5EFE5484-10DA-4899-8C74-B162C6D9ABD4}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationDestinationBinding =
    {0x5efe5484, 0x10da, 0x4899, {0x8c, 0x74, 0xb1, 0x62, 0xc6, 0xd9, 0xab, 0xd4}};

// {CF8BE652-BE67-46C8-AC57-F016048B46EA}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationClipAsset =
    {0xcf8be652, 0xbe67, 0x46c8, {0xac, 0x57, 0xf0, 0x16, 0x4, 0x8b, 0x46, 0xea}};

// {F228417D-614D-4EAB-A51C-2EE97CB4AC20}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAnimationBinding =
    {0xf228417d, 0x614d, 0x4eab, {0xa5, 0x1c, 0x2e, 0xe9, 0x7c, 0xb4, 0xac, 0x20}};


#define DILIGENT_INTERFACE_NAME IRadientAnimationDestination
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAnimationDestinationInclusiveMethods \
    IObjectInclusiveMethods;                         \
    IRadientAnimationDestinationMethods RadientAnimationDestination

// clang-format off

/// Factory for destination-specific compiled animation bindings.
///
/// This interface is the only runtime extension point required by the generic
/// animation system. Radient-created skeleton poses expose it through
/// QueryInterface() for node translation, rotation, and scale properties.
/// Radient-created scene writers expose those properties plus node own
/// visibility for scene entities.
/// Radient-created morph-target weight objects expose it for morph-weight
/// array ranges. Custom destinations may implement it for material, light,
/// camera, application, or extension properties. The interface is externally
/// synchronized.
///
/// Destination binding creation is a cold operation. The returned object owns
/// any optimized element/property lookup tables and releases them with ordinary
/// IObject lifetime management. Applying values uses one hot
/// BeginUpdate()/EndUpdate() virtual-call pair on that object per evaluation,
/// never one call per element or property.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationDestination, IObject)
{
    /// Compiles the supported subset of an ordered property-range batch.
    ///
    /// pProperties contains PropertyCount requests derived from every channel
    /// mapped to this destination descriptor. pResolvedProperties contains the
    /// same number of output records in corresponding order. PropertyCount must
    /// be nonzero and both pointers must be non-null. ppBinding must be non-null
    /// and *ppBinding must be null.
    ///
    /// The implementation first resets every resolved output to UNKNOWN semantic.
    /// On success, every supported request receives the semantic needed to
    /// interpolate it, while unsupported schema/property requests remain UNKNOWN.
    /// The output index of a supported request is its zero-based ordinal among
    /// supported results in request order. ppBinding receives a strong reference
    /// to an IRadientAnimationDestinationBinding that retains this destination
    /// and owns the compiled plan for the supported requests. This method does
    /// not retain either caller-owned array.
    ///
    /// For example, if three requests resolve to COMPONENT_WISE, UNKNOWN, and
    /// NORMALIZED_QUATERNION, BeginUpdate() returns two output addresses: slot 0
    /// for the first request and slot 1 for the third. The unsupported middle
    /// request has no output slot.
    ///
    /// Unsupported requests are not failures and do not receive destination
    /// storage. All other validation remains all-or-nothing: the implementation
    /// rejects malformed requests, incompatible layouts for supported properties,
    /// and requests that resolve to overlapping physical storage, even when
    /// different schemas or property IDs alias that storage. Outputs are
    /// unspecified on failure other than RADIENT_STATUS_UNSUPPORTED, which leaves
    /// every result reset to its unbound state.
    ///
    /// Returns RADIENT_STATUS_UNSUPPORTED without creating a binding when none
    /// of the requests use a schema/property exposed by this destination,
    /// RADIENT_STATUS_NOT_FOUND when a referenced runtime element for a
    /// supported request no longer exists, and RADIENT_STATUS_INVALID_ARGUMENT
    /// for malformed input or a layout incompatible with a supported property.
    ///
    /// On failure, *ppBinding remains null. Implementations return only
    /// RADIENT_STATUS_OK or a negative status; other nonnegative statuses are
    /// not valid for this method.
    ///
    /// \return RADIENT_STATUS_OK when at least one request was compiled,
    ///         RADIENT_STATUS_UNSUPPORTED when no request was supported, or
    ///         another negative RADIENT_STATUS value on failure.
    VIRTUAL RADIENT_STATUS METHOD(CreateBinding)(THIS_
                                                  const RadientAnimationPropertyBindingDesc* pProperties,
                                                  Uint32                                     PropertyCount,
                                                  RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
                                                  IRadientAnimationDestinationBinding**      ppBinding) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationDestination_CreateBinding(This, ...) CALL_IFACE_METHOD(RadientAnimationDestination, CreateBinding, This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientAnimationDestinationBinding
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAnimationDestinationBindingInclusiveMethods \
    IObjectInclusiveMethods;                                \
    IRadientAnimationDestinationBindingMethods RadientAnimationDestinationBinding

// clang-format off

/// Destination-owned compiled plan for one ordered property batch.
///
/// The object retains its parent destination and owns every destination-specific
/// lookup table or snapshot required by the plan. It is externally synchronized.
/// If an element is destroyed after binding, it must never be silently replaced
/// by a newly created element; BeginUpdate() returns RADIENT_STATUS_NOT_FOUND,
/// and the caller recreates the outer binding.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationDestinationBinding, IObject)
{
    /// Begins one direct-write update and returns its ordered output addresses.
    ///
    /// ppOutputs must be non-null. On success, *ppOutputs receives a borrowed,
    /// non-null array containing exactly one writable address for every property
    /// accepted when this object was created, in supported-request order.
    /// Each address is non-null, naturally aligned for the property's native
    /// type, and exposes the complete tightly packed byte range described by the
    /// corresponding RadientAnimationPropertyBindingDesc::Value. Property ranges
    /// do not overlap because CreateBinding() rejects aliases.
    ///
    /// The caller writes one complete value to every returned address before
    /// calling EndUpdate(). Values must satisfy the resolved schema contract;
    /// the destination is not required to validate or normalize them. State
    /// outside the bound property ranges is not writable through the returned
    /// array and is preserved.
    ///
    /// Implementations may expose destination storage directly or return
    /// destination-owned staging storage for properties that cannot be written
    /// in place. Implementations may refresh or pin relocatable storage during
    /// this call. The output array and every address in it remain valid only
    /// until the matching EndUpdate() returns and must not be retained by the
    /// caller.
    /// While the bracket is open, the caller must not mutate the underlying
    /// destination except by writing the returned property ranges; in particular,
    /// it must not destroy elements or perform operations that may relocate the
    /// exposed storage.
    ///
    /// A successful call opens an update bracket and must be followed by exactly
    /// one EndUpdate() call before BeginUpdate() is called again. On failure, no
    /// bracket is opened and *ppOutputs is set to null. Implementations return
    /// only RADIENT_STATUS_OK or a negative status; other nonnegative statuses
    /// are not valid for this method.
    ///
    /// \return RADIENT_STATUS_OK when writable outputs were acquired,
    ///         RADIENT_STATUS_NOT_FOUND when a bound element no longer exists,
    ///         or another negative status on failure.
    VIRTUAL RADIENT_STATUS METHOD(BeginUpdate)(THIS_
                                               void* const** ppOutputs) PURE;

    /// Ends the active direct-write update.
    ///
    /// The caller must invoke this method exactly once after each successful
    /// BeginUpdate(), after writing every output value. UpdateDerivedState
    /// requests one destination-specific derived-state update after the complete
    /// property batch has been written. For example, a skeleton-pose destination
    /// propagates global transforms once for the complete joint batch when it is
    /// true, while a scene-writer destination commits pending scene-derived state
    /// once. When false, the destination must preserve the primary writes and may
    /// leave derived state dirty for a later aggregate update.
    ///
    /// A destination that returned staging storage from BeginUpdate() publishes
    /// those values during this call. Primary writes are not transactional: a
    /// negative return value may leave some or all values applied, and rollback
    /// is not required. The active bracket is closed and all output addresses
    /// expire when this method returns, regardless of its status.
    ///
    /// Applying several bindings to the same underlying state is ordered
    /// overwrite, not blending. A future player may instead bind clips to a
    /// mixer or root-motion collector that implements
    /// IRadientAnimationDestination.
    /// Implementations return only RADIENT_STATUS_OK or a negative status;
    /// other nonnegative statuses are not valid for this method.
    ///
    /// \return RADIENT_STATUS_OK when the update was completed, or a negative
    ///         status on failure.
    VIRTUAL RADIENT_STATUS METHOD(EndUpdate)(THIS_
                                             Bool UpdateDerivedState) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationDestinationBinding_BeginUpdate(This, ...) CALL_IFACE_METHOD(RadientAnimationDestinationBinding, BeginUpdate, This, __VA_ARGS__)
#    define IRadientAnimationDestinationBinding_EndUpdate(This, ...)   CALL_IFACE_METHOD(RadientAnimationDestinationBinding, EndUpdate,   This, __VA_ARGS__)

#endif

// clang-format on


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

    /// Compiles BindingDesc against this clip. The method invokes the
    /// destination's CreateBinding() once for each destination descriptor and
    /// omits destinations that return RADIENT_STATUS_UNSUPPORTED. On success,
    /// ppBinding receives a strong reference to a binding that retains this clip
    /// and every nonempty compiled destination binding. The caller retains
    /// ownership of BindingDesc and its arrays. ppBinding must not be null and
    /// *ppBinding must be null. On failure, *ppBinding remains null and every
    /// destination binding created earlier in the operation is released.
    ///
    /// \return RADIENT_STATUS_OK when the binding was compiled,
    ///         RADIENT_STATUS_INVALID_ARGUMENT when the descriptor or output
    ///         pointer is invalid, or a destination failure other than
    ///         RADIENT_STATUS_UNSUPPORTED.
    VIRTUAL RADIENT_STATUS METHOD(CreateBinding)(THIS_
                                                  const RadientAnimationBindingDesc REF BindingDesc,
                                                  IRadientAnimationBinding**            ppBinding) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationClipAsset_GetDesc(This)            CALL_IFACE_METHOD(RadientAnimationClipAsset, GetDesc,       This)
#    define IRadientAnimationClipAsset_CreateBinding(This, ...) CALL_IFACE_METHOD(RadientAnimationClipAsset, CreateBinding, This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientAnimationBinding
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAnimationBindingInclusiveMethods \
    IObjectInclusiveMethods;                     \
    IRadientAnimationBindingMethods RadientAnimationBinding

// clang-format off

/// Compiled connection between an immutable clip and runtime destinations.
///
/// A binding owns destination-specific sampling and write plans and may keep
/// reusable scratch memory for values shared by multiple destinations. Evaluate
/// performs no schema, object, or property lookup and samples directly into
/// destination outputs when possible. The binding and all of its destinations
/// are externally synchronized and must not be accessed concurrently. If
/// evaluation of a multi-destination binding fails, updates already applied to
/// earlier destinations are not rolled back.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationBinding, IObject)
{
    /// Returns a borrowed pointer to the clip retained by this binding.
    VIRTUAL IRadientAnimationClipAsset* METHOD(GetClip)(THIS) CONST PURE;

    /// Samples the clip and writes all compiled destinations according to Info.
    /// Each distinct (sampler, value-semantic) pair is sampled once, and each
    /// compiled destination binding receives one BeginUpdate()/EndUpdate() pair
    /// in descriptor order. A successful BeginUpdate() is always paired with
    /// EndUpdate(), and all of its outputs are written before EndUpdate().
    /// No schema, object, property, or interface lookup occurs during this
    /// method. Info.Time must be finite.
    ///
    /// Evaluation stops at the first destination failure. Updates already
    /// applied to earlier destinations are not rolled back, and later
    /// destinations are not called.
    ///
    /// \return RADIENT_STATUS_OK when the nonempty binding was evaluated,
    ///         RADIENT_STATUS_NO_CHANGE when the binding is empty,
    ///         RADIENT_STATUS_INVALID_ARGUMENT when Info is invalid, or a
    ///         destination failure.
    VIRTUAL RADIENT_STATUS METHOD(Evaluate)(THIS_
                                            const RadientAnimationEvaluateInfo REF Info) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationBinding_GetClip(This)       CALL_IFACE_METHOD(RadientAnimationBinding, GetClip,  This)
#    define IRadientAnimationBinding_Evaluate(This, ...) CALL_IFACE_METHOD(RadientAnimationBinding, Evaluate, This, __VA_ARGS__)

#endif

// clang-format on


/// One animation clip and its unique compiled bindings.
struct RadientAnimationRegistryEntry
{
    /// Clip shared by every binding in ppBindings. The registry retains the
    /// clip; this pointer is borrowed and is never null.
    IRadientAnimationClipAsset* pClip DEFAULT_INITIALIZER(nullptr);

    /// Array of BindingCount unique compiled bindings. The registry retains
    /// every binding; the array and its borrowed pointers remain valid until
    /// the registry is modified.
    IRadientAnimationBinding* const* ppBindings DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in ppBindings. Registry entries never have zero
    /// bindings.
    Uint32 BindingCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationRegistryEntry RadientAnimationRegistryEntry;


/// Current animation registry contents.
struct RadientAnimationRegistryState
{
    /// Revision incremented whenever the registry contents change. Like other
    /// Radient revision counters, it wraps to zero after its maximum value.
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

/// Externally owned mapping from animation clips to compiled bindings.
///
/// A registry is associated with one scene and retains that scene. It does not
/// modify the scene or automatically observe entity destruction. The code that
/// adds or removes scene content is responsible for updating the registry's
/// private entity associations.
///
/// Entity associations are lifetime tags, not animation destinations. The
/// registry never inspects a binding's compiled destinations or assumes that
/// an entity owns them. For example, one skeleton-pose binding may be associated
/// with every mesh entity that shares the pose. The binding remains registered
/// until its last entity association is removed.
/// Public state is intentionally a bulk-playback view: it enumerates every
/// instantiated binding for a clip but does not expose the private lifetime
/// associations or identify a particular scene instance.
///
/// The interface is externally synchronized. Applications must not call its
/// methods concurrently without their own synchronization.
DILIGENT_BEGIN_INTERFACE(IRadientAnimationRegistry, IObject)
{
    /// Returns the scene associated with this registry. The returned pointer is
    /// borrowed and remains valid for the registry lifetime.
    VIRTUAL IRadientScene* METHOD(GetScene)(THIS) CONST PURE;

    /// Adds unique binding-to-entity associations.
    ///
    /// pBinding must be non-null and return a non-null clip. Every entity must
    /// belong to the registry's scene. The operation is atomic with respect to
    /// argument validation: no association is added if any new entity does not
    /// exist. Repeated entities and existing associations are ignored. The
    /// registry retains pBinding and the clip returned by pBinding->GetClip().
    /// Returns RADIENT_STATUS_NO_CHANGE when EntityCount is zero or every
    /// association already exists.
    VIRTUAL RADIENT_STATUS METHOD(AddAnimationBinding)(THIS_
                                                       IRadientAnimationBinding* pBinding,
                                                       const RadientEntityID*    pEntities,
                                                       Uint32                    EntityCount) PURE;

    /// Removes the specified binding-to-entity associations. Missing
    /// associations are ignored. Returns RADIENT_STATUS_NO_CHANGE when the
    /// registry is not modified. A binding is removed when its last
    /// entity association is removed; a clip entry is removed when its last
    /// binding is removed.
    VIRTUAL RADIENT_STATUS METHOD(RemoveAnimationBinding)(THIS_
                                                          IRadientAnimationBinding* pBinding,
                                                          const RadientEntityID*    pEntities,
                                                          Uint32                    EntityCount) PURE;

    /// Removes an entity from every animation entry. This method only updates
    /// the registry and never destroys or otherwise modifies the scene entity.
    /// Returns RADIENT_STATUS_NO_CHANGE when the entity is not registered.
    VIRTUAL RADIENT_STATUS METHOD(RemoveEntity)(THIS_
                                                RadientEntityID Entity) PURE;

    /// Removes a clip and all binding-to-entity associations grouped under it.
    /// Returns RADIENT_STATUS_NO_CHANGE when the clip is not registered.
    VIRTUAL RADIENT_STATUS METHOD(RemoveAnimationClip)(THIS_
                                                       IRadientAnimationClipAsset* pClip) PURE;

    /// Returns the current registry contents. The returned reference remains
    /// valid for the registry lifetime. Its arrays are invalidated by the next
    /// successful registry mutation.
    VIRTUAL const RadientAnimationRegistryState REF METHOD(GetState)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAnimationRegistry_GetScene(This)                    CALL_IFACE_METHOD(RadientAnimationRegistry, GetScene,                  This)
#    define IRadientAnimationRegistry_AddAnimationBinding(This, ...)    CALL_IFACE_METHOD(RadientAnimationRegistry, AddAnimationBinding,       This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveAnimationBinding(This, ...) CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveAnimationBinding,    This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveEntity(This, ...)           CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveEntity,              This, __VA_ARGS__)
#    define IRadientAnimationRegistry_RemoveAnimationClip(This, ...)    CALL_IFACE_METHOD(RadientAnimationRegistry, RemoveAnimationClip,       This, __VA_ARGS__)
#    define IRadientAnimationRegistry_GetState(This)                    CALL_IFACE_METHOD(RadientAnimationRegistry, GetState,                  This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
