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
/// Defines renderer-independent morph-target data and weight interfaces.

#include "RadientAssets.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

/// Standard semantic for morph-target position deltas.
static DILIGENT_CONSTEXPR Char RadientMorphTargetPositionSemantic[] = "POSITION";

/// Standard semantic for morph-target normal deltas.
static DILIGENT_CONSTEXPR Char RadientMorphTargetNormalSemantic[] = "NORMAL";

/// Standard semantic for morph-target tangent deltas. Tangent handedness is not
/// morphed, so each value contains three components.
static DILIGENT_CONSTEXPR Char RadientMorphTargetTangentSemantic[] = "TANGENT";


/// Immutable description of one vertex attribute in a morph target.
///
/// Morph attributes are identified by case-sensitive semantic strings so that
/// applications may define custom vertex attributes. Standard POSITION,
/// NORMAL, and TANGENT attributes contain three components per vertex.
struct RadientMorphTargetAttributeDesc
{
    /// Case-sensitive semantic of the destination vertex attribute. Radient
    /// copies the string during mesh creation.
    const Char* Semantic DEFAULT_INITIALIZER(nullptr);

    /// Number of components in each vertex delta. Must be in [1, 4]. Standard
    /// POSITION, NORMAL, and TANGENT semantics require three components.
    Uint32 ComponentCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMorphTargetAttributeDesc RadientMorphTargetAttributeDesc;


/// Creation data for one morph-target vertex attribute.
struct RadientMorphTargetAttributeCreateInfo
{
    /// Tightly packed canonical floating-point deltas. The array contains
    /// RadientMeshCreateInfo::VertexCount times the corresponding attribute
    /// description's ComponentCount values. Radient copies the data during
    /// mesh creation.
    const Float32* pDeltas DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMorphTargetAttributeCreateInfo RadientMorphTargetAttributeCreateInfo;


/// Immutable description of one mesh morph target.
///
/// Attributes omitted from the array contribute zero deltas. Attribute
/// semantics within one target must be unique. Morph target weights are not
/// implicitly clamped to any range.
struct RadientMorphTargetDesc
{
    /// Optional target name. Radient copies the string during mesh creation.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Array of AttributeCount morph-target attributes. Must not be null when
    /// AttributeCount is nonzero.
    const RadientMorphTargetAttributeDesc* pAttributes DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pAttributes.
    Uint32 AttributeCount DEFAULT_INITIALIZER(0);

    /// Default weight used when creating mutable weights for the mesh. Must be
    /// finite. The value is not otherwise clamped or restricted.
    Float32 DefaultWeight DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientMorphTargetDesc RadientMorphTargetDesc;


/// Morph-target creation attributes.
///
/// Radient copies Desc, all metadata it references, and the attribute creation
/// data during mesh creation.
struct RadientMorphTargetCreateInfo
{
    /// Complete immutable description of the target to create.
    RadientMorphTargetDesc Desc DEFAULT_INITIALIZER({});

    /// Array of Desc.AttributeCount creation-data records corresponding by
    /// index to Desc.pAttributes. Must not be null when Desc.AttributeCount is
    /// nonzero.
    const RadientMorphTargetAttributeCreateInfo* pAttributeData DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMorphTargetCreateInfo RadientMorphTargetCreateInfo;


// {EC771A6E-4653-4DE9-961C-6651EF05092C}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMorphTargetWeights =
    {0xec771a6e, 0x4653, 0x4de9, {0x96, 0x1c, 0x66, 0x51, 0xef, 0x5, 0x9, 0x2c}};


#define DILIGENT_INTERFACE_NAME IRadientMorphTargetWeights
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMorphTargetWeightsInclusiveMethods \
    IObjectInclusiveMethods;                       \
    IRadientMorphTargetWeightsMethods RadientMorphTargetWeights

// clang-format off

/// Mutable weights for the morph targets owned by one mesh.
///
/// The weight count is fixed at creation. The object retains its mesh and is
/// not thread-safe; callers must not read and write it concurrently. Weight
/// values are copied without clamping or per-value validation. The caller is
/// responsible for providing finite values.
DILIGENT_BEGIN_INTERFACE(IRadientMorphTargetWeights, IObject)
{
    /// Returns a borrowed pointer to the mesh that defines the targets.
    VIRTUAL IRadientMeshAsset* METHOD(GetMesh)(THIS) CONST PURE;

    /// Returns the monotonically increasing weight revision. Every successful
    /// nonempty SetWeights() call advances the revision, including assignments
    /// whose values happen to equal the current values.
    VIRTUAL Uint64 METHOD(GetVersion)(THIS) CONST PURE;

    /// Returns the fixed number of weights and mesh morph targets.
    VIRTUAL Uint32 METHOD(GetWeightCount)(THIS) CONST PURE;

    /// Returns a borrowed pointer to GetWeightCount() values, or null when the
    /// count is zero. The pointer remains valid for the object lifetime, while
    /// its contents may be changed by SetWeights().
    VIRTUAL const Float32* METHOD(GetWeights)(THIS) CONST PURE;

    /// Replaces WeightCount values beginning at FirstTarget. The input is
    /// copied immediately. Returns RADIENT_STATUS_NO_CHANGE when WeightCount is
    /// zero and RADIENT_STATUS_INVALID_ARGUMENT when the range is invalid or
    /// pWeights is null for a nonempty range.
    VIRTUAL RADIENT_STATUS METHOD(SetWeights)(THIS_
                                               Uint32         FirstTarget,
                                               Uint32         WeightCount,
                                               const Float32* pWeights) PURE;

    /// Restores every weight to the default declared by its mesh morph target.
    /// A nonempty reset advances the revision even when all current values
    /// already equal their defaults. Returns RADIENT_STATUS_NO_CHANGE when the
    /// mesh has no morph targets.
    VIRTUAL RADIENT_STATUS METHOD(ResetToDefaults)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMorphTargetWeights_GetMesh(This)         CALL_IFACE_METHOD(RadientMorphTargetWeights, GetMesh,        This)
#    define IRadientMorphTargetWeights_GetVersion(This)      CALL_IFACE_METHOD(RadientMorphTargetWeights, GetVersion,     This)
#    define IRadientMorphTargetWeights_GetWeightCount(This)  CALL_IFACE_METHOD(RadientMorphTargetWeights, GetWeightCount, This)
#    define IRadientMorphTargetWeights_GetWeights(This)      CALL_IFACE_METHOD(RadientMorphTargetWeights, GetWeights,     This)
#    define IRadientMorphTargetWeights_SetWeights(This, ...) CALL_IFACE_METHOD(RadientMorphTargetWeights, SetWeights,     This, __VA_ARGS__)
#    define IRadientMorphTargetWeights_ResetToDefaults(This) CALL_IFACE_METHOD(RadientMorphTargetWeights, ResetToDefaults, This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
