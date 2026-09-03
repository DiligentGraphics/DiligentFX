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
/// Defines renderer-independent skeleton, skin, pose, and animation interfaces.

#include "RadientAssets.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientSkeletonPose       IRadientSkeletonPose;
typedef struct IRadientSkeletonPoseWriter IRadientSkeletonPoseWriter;

/// Invalid skeleton joint index and parent marker.
static DILIGENT_CONSTEXPR Uint32 InvalidRadientJointIndex = (Uint32)~0u;


/// Immutable description of one skeleton joint.
struct RadientSkeletonJointDesc
{
    /// Optional joint name. Names are diagnostic metadata and need not be unique.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Parent joint index, or InvalidRadientJointIndex for a root. Joint order is
    /// unrestricted; parents may appear before or after their children.
    Uint32 ParentJointIndex DEFAULT_INITIALIZER(InvalidRadientJointIndex);

    /// Joint transform relative to its parent in the skeleton's rest pose. A
    /// root joint's transform is relative to skeleton space.
    RadientTransform LocalRestTransform DEFAULT_INITIALIZER({});
};
typedef struct RadientSkeletonJointDesc RadientSkeletonJointDesc;


/// Skeleton asset description.
///
/// During creation, the name, joint descriptions, and joint names are copied.
/// The description returned by IRadientSkeletonAsset::GetDesc() and all data it
/// references remain valid for the lifetime of the skeleton.
struct RadientSkeletonDesc
{
    /// Optional skeleton name used for diagnostics. The value returned by a
    /// created asset is never null.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Array of JointCount joint descriptions. Must not be null during creation.
    const RadientSkeletonJointDesc* pJoints DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pJoints. Must not be zero during creation.
    Uint32 JointCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientSkeletonDesc RadientSkeletonDesc;


/// Immutable binding from one mesh skin-palette entry to a skeleton joint.
struct RadientSkinJointBindingDesc
{
    /// Index of the corresponding joint in the skin's skeleton.
    Uint32 SkeletonJointIndex DEFAULT_INITIALIZER(InvalidRadientJointIndex);

    /// Matrix that transforms a vertex from mesh space to the joint's bind space.
    RadientMatrix4x4 InverseBindMatrix DEFAULT_INITIALIZER({});
};
typedef struct RadientSkinJointBindingDesc RadientSkinJointBindingDesc;


/// Skin asset description.
///
/// During creation, the name and joint bindings are copied and the skeleton is
/// retained. The description returned by IRadientSkinAsset::GetDesc() and all
/// data it references remain valid for the lifetime of the skin.
struct RadientSkinDesc
{
    /// Optional skin name used for diagnostics. The value returned by a created
    /// asset is never null.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Skeleton referenced by the skin. Must not be null during creation. The
    /// created skin retains it; the pointer returned in this description is
    /// borrowed by callers.
    IRadientSkeletonAsset* pSkeleton DEFAULT_INITIALIZER(nullptr);

    /// Array of JointCount mesh skin-palette bindings. Must not be null during
    /// creation.
    const RadientSkinJointBindingDesc* pJoints DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pJoints. Must not be zero during creation.
    Uint32 JointCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientSkinDesc RadientSkinDesc;


// clang-format off

/// Interpolation applied between animation keyframes.
DILIGENT_TYPED_ENUM(RADIENT_ANIMATION_INTERPOLATION, Uint8)
{
    /// Holds the preceding keyframe value until the next keyframe.
    RADIENT_ANIMATION_INTERPOLATION_STEP = 0,

    /// Linearly interpolates vectors and spherically interpolates rotations.
    RADIENT_ANIMATION_INTERPOLATION_LINEAR,

    /// Evaluates a cubic Hermite spline using authored incoming and outgoing
    /// tangents.
    RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,

    /// Number of animation interpolation modes.
    RADIENT_ANIMATION_INTERPOLATION_COUNT
};

// clang-format on


/// One typed animation curve.
///
/// The concrete type of pValues is determined by the field containing this
/// curve: RadientFloat3 for translation and scale, and RadientQuaternion for
/// rotation. STEP and LINEAR curves contain KeyframeCount values. A
/// CUBIC_SPLINE curve contains three values per keyframe in incoming-tangent,
/// value, outgoing-tangent order. Cubic tangents are derivatives per second;
/// rotation tangents use RadientQuaternion as four-component storage and are
/// not themselves required to be unit quaternions.
struct RadientAnimationCurveDesc
{
    /// Interpolation used between keyframes.
    RADIENT_ANIMATION_INTERPOLATION Interpolation DEFAULT_INITIALIZER(RADIENT_ANIMATION_INTERPOLATION_LINEAR);

    /// Strictly increasing clip-local keyframe times, in seconds. Must contain
    /// KeyframeCount finite values in the inclusive range [0, clip duration].
    const Float32* pTimes DEFAULT_INITIALIZER(nullptr);

    /// Curve values in the format described above.
    const void* pValues DEFAULT_INITIALIZER(nullptr);

    /// Number of keyframes. Zero denotes an absent curve, in which case pTimes
    /// and pValues must both be null.
    Uint32 KeyframeCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientAnimationCurveDesc RadientAnimationCurveDesc;


/// Animation curves targeting one skeleton joint.
///
/// An absent component curve preserves that component of the joint's rest
/// transform. At least one component curve must be present. A clip may contain
/// at most one track for each skeleton joint.
struct RadientSkeletonAnimationTrackDesc
{
    /// Index of the target joint in the clip's skeleton.
    Uint32 SkeletonJointIndex DEFAULT_INITIALIZER(InvalidRadientJointIndex);

    /// Local-space translation curve with RadientFloat3 values.
    RadientAnimationCurveDesc Translation;

    /// Local-space rotation curve with RadientQuaternion values. Authored
    /// keyframe values must be normalized; evaluated rotations are normalized.
    RadientAnimationCurveDesc Rotation;

    /// Local-space scale curve with RadientFloat3 values.
    RadientAnimationCurveDesc Scale;
};
typedef struct RadientSkeletonAnimationTrackDesc RadientSkeletonAnimationTrackDesc;


/// Immutable skeleton animation clip description.
///
/// During creation, the name, tracks, keyframe times, and curve values are
/// copied, and the skeleton is retained. The description returned by
/// IRadientSkeletonAnimationAsset::GetDesc() and all referenced data remain
/// valid for the lifetime of the animation asset.
struct RadientSkeletonAnimationDesc
{
    /// Optional clip name used for diagnostics. The value returned by a created
    /// asset is never null.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Skeleton targeted by every track. Must not be null during creation. The
    /// created animation retains it; the pointer returned in this description
    /// is borrowed by callers.
    IRadientSkeletonAsset* pSkeleton DEFAULT_INITIALIZER(nullptr);

    /// Array of TrackCount joint animation tracks. Must not be null when
    /// TrackCount is nonzero.
    const RadientSkeletonAnimationTrackDesc* pTracks DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pTracks.
    Uint32 TrackCount DEFAULT_INITIALIZER(0);

    /// Clip duration in seconds. Must be finite and non-negative. Keyframe
    /// times must not exceed this value.
    Float32 Duration DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientSkeletonAnimationDesc RadientSkeletonAnimationDesc;


// {27F67251-D762-4793-8348-285ED0F6F1FE}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSkeletonAsset =
    {0x27f67251, 0xd762, 0x4793, {0x83, 0x48, 0x28, 0x5e, 0xd0, 0xf6, 0xf1, 0xfe}};

// {347A14A7-4356-4534-90C7-38FD1CFB12E1}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSkinAsset =
    {0x347a14a7, 0x4356, 0x4534, {0x90, 0xc7, 0x38, 0xfd, 0x1c, 0xfb, 0x12, 0xe1}};

// {202AA878-12A6-4CB6-BF93-9DFC7EF96CFB}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSkeletonPose =
    {0x202aa878, 0x12a6, 0x4cb6, {0xbf, 0x93, 0x9d, 0xfc, 0x7e, 0xf9, 0x6c, 0xfb}};

// {63C66C9E-B531-47D1-B841-990107F06112}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSkeletonPoseWriter =
    {0x63c66c9e, 0xb531, 0x47d1, {0xb8, 0x41, 0x99, 0x1, 0x7, 0xf0, 0x61, 0x12}};

// {CE8E3FDB-C668-4468-9BEF-8EDD0EB3084E}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSkeletonAnimationAsset =
    {0xce8e3fdb, 0xc668, 0x4468, {0x9b, 0xef, 0x8e, 0xdd, 0xe, 0xb3, 0x8, 0x4e}};


#define DILIGENT_INTERFACE_NAME IRadientSkeletonAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSkeletonAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;            \
    IRadientSkeletonAssetMethods RadientSkeletonAsset

// clang-format off

/// Immutable joint hierarchy and rest pose.
DILIGENT_BEGIN_INTERFACE(IRadientSkeletonAsset, IRadientAsset)
{
    /// Returns the immutable skeleton description. The reference and all data
    /// it references remain valid for the lifetime of the skeleton.
    VIRTUAL const RadientSkeletonDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Creates a skeleton pose initialized to the rest pose. On success, ppPose
    /// receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreatePose)(THIS_
                                              IRadientSkeletonPose** ppPose) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSkeletonAsset_GetDesc(This)         CALL_IFACE_METHOD(RadientSkeletonAsset, GetDesc,    This)
#    define IRadientSkeletonAsset_CreatePose(This, ...) CALL_IFACE_METHOD(RadientSkeletonAsset, CreatePose, This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSkinAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSkinAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;        \
    IRadientSkinAssetMethods RadientSkinAsset

// clang-format off

/// Immutable mapping from a mesh skin palette to a skeleton.
DILIGENT_BEGIN_INTERFACE(IRadientSkinAsset, IRadientAsset)
{
    /// Returns the immutable skin description. The reference and all data it
    /// references remain valid for the lifetime of the skin.
    VIRTUAL const RadientSkinDesc REF METHOD(GetDesc)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSkinAsset_GetDesc(This) CALL_IFACE_METHOD(RadientSkinAsset, GetDesc, This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSkeletonPose
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSkeletonPoseInclusiveMethods \
    IObjectInclusiveMethods;                 \
    IRadientSkeletonPoseMethods RadientSkeletonPose

// clang-format off

/// Mutable local and global transforms for one skeleton.
///
/// The pose retains its skeleton. A pose and all writers created from it are not
/// thread-safe and must not be accessed concurrently.
DILIGENT_BEGIN_INTERFACE(IRadientSkeletonPose, IObject)
{
    /// Returns a borrowed pointer to the skeleton retained by the pose.
    VIRTUAL IRadientSkeletonAsset* METHOD(GetSkeleton)(THIS) CONST PURE;

    /// Returns the monotonically increasing version of the global transforms.
    /// Deferred local-transform commits do not advance the version until
    /// UpdateGlobalTransforms() is called.
    VIRTUAL Uint64 METHOD(GetVersion)(THIS) CONST PURE;

    /// Copies JointCount local transforms beginning at FirstJoint.
    VIRTUAL RADIENT_STATUS METHOD(GetJointLocalTransforms)(THIS_
                                                           Uint32            FirstJoint,
                                                           Uint32            JointCount,
                                                           RadientTransform* pTransforms) CONST PURE;

    /// Copies JointCount skeleton-space matrices beginning at FirstJoint.
    /// Returns RADIENT_STATUS_PENDING when committed local transforms have not
    /// yet been propagated through the skeleton hierarchy.
    VIRTUAL RADIENT_STATUS METHOD(GetJointGlobalMatrices)(THIS_
                                                          Uint32             FirstJoint,
                                                          Uint32             JointCount,
                                                          RadientMatrix4x4*   pMatrices) CONST PURE;

    /// Computes the skinning matrices for pSkin and writes
    /// pSkin->GetDesc().JointCount elements to pMatrices. Each output matrix
    /// combines the skin joint's inverse-bind matrix with the corresponding
    /// skeleton-space pose matrix and therefore transforms mesh bind-space
    /// vertices into skeleton space. pSkin must reference this pose's skeleton.
    ///
    /// If TransposeMatrices is true, every matrix is transposed before it is
    /// written to pMatrices.
    ///
    /// If UpdateGlobalMatrices is true, dirty global matrices are updated
    /// before computing the result. Otherwise, the method returns
    /// RADIENT_STATUS_PENDING while global matrices are dirty. The output is
    /// not modified unless the method returns RADIENT_STATUS_OK.
    ///
    VIRTUAL RADIENT_STATUS METHOD(ComputeSkinningMatrices)(THIS_
                                                           IRadientSkinAsset* pSkin,
                                                           RadientMatrix4x4*  pMatrices,
                                                           Bool               TransposeMatrices DEFAULT_VALUE(False),
                                                           Bool               UpdateGlobalMatrices DEFAULT_VALUE(True)) PURE;

    /// Propagates committed local transforms through the skeleton hierarchy and
    /// advances the pose version. Returns RADIENT_STATUS_NO_CHANGE when the
    /// global transforms are already current.
    VIRTUAL RADIENT_STATUS METHOD(UpdateGlobalTransforms)(THIS) PURE;

    /// Creates a reusable writer initialized from the current pose.
    /// On success, ppWriter receives a strong reference.
    VIRTUAL RADIENT_STATUS METHOD(CreateWriter)(THIS_
                                                IRadientSkeletonPoseWriter** ppWriter) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSkeletonPose_GetSkeleton(This)                  CALL_IFACE_METHOD(RadientSkeletonPose, GetSkeleton,             This)
#    define IRadientSkeletonPose_GetVersion(This)                   CALL_IFACE_METHOD(RadientSkeletonPose, GetVersion,              This)
#    define IRadientSkeletonPose_GetJointLocalTransforms(This, ...) CALL_IFACE_METHOD(RadientSkeletonPose, GetJointLocalTransforms, This, __VA_ARGS__)
#    define IRadientSkeletonPose_GetJointGlobalMatrices(This, ...)  CALL_IFACE_METHOD(RadientSkeletonPose, GetJointGlobalMatrices,  This, __VA_ARGS__)
#    define IRadientSkeletonPose_ComputeSkinningMatrices(This, ...) CALL_IFACE_METHOD(RadientSkeletonPose, ComputeSkinningMatrices, This, __VA_ARGS__)
#    define IRadientSkeletonPose_UpdateGlobalTransforms(This)       CALL_IFACE_METHOD(RadientSkeletonPose, UpdateGlobalTransforms,   This)
#    define IRadientSkeletonPose_CreateWriter(This, ...)            CALL_IFACE_METHOD(RadientSkeletonPose, CreateWriter,            This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSkeletonPoseWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSkeletonPoseWriterInclusiveMethods \
    IObjectInclusiveMethods;                       \
    IRadientSkeletonPoseWriterMethods RadientSkeletonPoseWriter

// clang-format off

/// Reusable writer for a complete skeleton pose.
///
/// A writer owns a private transform set initialized from the pose when the
/// writer is created. SetJointLocalTransforms() modifies that private set, and
/// Commit() copies the complete set into the pose. Global-transform propagation
/// may be performed immediately or deferred. A pose and all writers created from
/// it are not thread-safe and must not be accessed concurrently.
DILIGENT_BEGIN_INTERFACE(IRadientSkeletonPoseWriter, IObject)
{
    /// Replaces JointCount local transforms beginning at FirstJoint. Input data
    /// is copied immediately without validation or normalization. The caller is
    /// responsible for providing valid transforms with normalized rotations.
    /// Returns RADIENT_STATUS_NO_CHANGE when JointCount is zero.
    VIRTUAL RADIENT_STATUS METHOD(SetJointLocalTransforms)(THIS_
                                                           Uint32                  FirstJoint,
                                                           Uint32                  JointCount,
                                                           const RadientTransform* pTransforms) PURE;

    /// Replaces the writer's complete private transform set with the skeleton's
    /// rest pose. Returns RADIENT_STATUS_NO_CHANGE when it already matches.
    VIRTUAL RADIENT_STATUS METHOD(ResetToRestPose)(THIS) PURE;

    /// Applies the writer's complete pose. If UpdateGlobalTransforms is true,
    /// the committed local transforms are immediately propagated through the
    /// skeleton hierarchy. Otherwise, the application must subsequently call
    /// IRadientSkeletonPose::UpdateGlobalTransforms(). Returns
    /// RADIENT_STATUS_NO_CHANGE when there are no pending changes. The writer
    /// remains reusable.
    VIRTUAL RADIENT_STATUS METHOD(Commit)(THIS_
                                          Bool UpdateGlobalTransforms) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSkeletonPoseWriter_SetJointLocalTransforms(This, ...) CALL_IFACE_METHOD(RadientSkeletonPoseWriter, SetJointLocalTransforms, This, __VA_ARGS__)
#    define IRadientSkeletonPoseWriter_ResetToRestPose(This)              CALL_IFACE_METHOD(RadientSkeletonPoseWriter, ResetToRestPose,        This)
#    define IRadientSkeletonPoseWriter_Commit(This, ...)                  CALL_IFACE_METHOD(RadientSkeletonPoseWriter, Commit,                 This, __VA_ARGS__)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientSkeletonAnimationAsset
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSkeletonAnimationAssetInclusiveMethods \
    IRadientAssetInclusiveMethods;                     \
    IRadientSkeletonAnimationAssetMethods RadientSkeletonAnimationAsset

// clang-format off

/// Immutable animation clip targeting one skeleton.
DILIGENT_BEGIN_INTERFACE(IRadientSkeletonAnimationAsset, IRadientAsset)
{
    /// Returns the immutable animation description. The reference and all data
    /// it references remain valid for the lifetime of the animation asset.
    VIRTUAL const RadientSkeletonAnimationDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Evaluates the clip at an absolute clip-local time into pPose. Time must
    /// be finite, is expressed in seconds, and is clamped to
    /// [0, GetDesc().Duration]. Joints whose animation has not started, and
    /// joints without animation tracks, are restored to their rest transforms.
    /// Active tracks are evaluated, while tracks that have ended leave their
    /// joints unchanged so the last evaluated pose is preserved. The active
    /// interval of a track spans from the earliest first key to the latest last
    /// key among its curves. If UpdateGlobalTransforms is true, global
    /// transforms are updated once before the method returns. Otherwise, the
    /// caller must subsequently call
    /// IRadientSkeletonPose::UpdateGlobalTransforms(). pPose must target the
    /// exact skeleton returned by GetDesc().pSkeleton.
    VIRTUAL RADIENT_STATUS METHOD(Evaluate)(THIS_
                                             Float64               Time,
                                             IRadientSkeletonPose* pPose,
                                             Bool                  UpdateGlobalTransforms) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSkeletonAnimationAsset_GetDesc(This)        CALL_IFACE_METHOD(RadientSkeletonAnimationAsset, GetDesc,  This)
#    define IRadientSkeletonAnimationAsset_Evaluate(This, ...)  CALL_IFACE_METHOD(RadientSkeletonAnimationAsset, Evaluate, This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
