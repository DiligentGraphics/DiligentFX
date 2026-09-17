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

#include "Import/RadientGLTFConverter.hpp"

#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "Core/RadientValidation.hpp"
#include "Import/RadientImportedScene.hpp"
#include "Math/RadientMath.hpp"
#include "RadientAnimation.h"
#include "RadientMorphTargets.h"
#include "RadientSceneWriter.h"
#include "RadientSkinning.h"

#include "Errors.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFLoader.hpp"
#include "GraphicsAccessories.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

using RadientValidation::CheckedMultiply;
using RadientValidation::IsAddressableArray;

using SkinEntityLists  = std::vector<std::vector<RadientEntityID>>;
using NodeEntityLists  = std::vector<std::vector<RadientEntityID>>;
using LightEntityLists = std::unordered_map<Uint32, std::vector<RadientEntityID>>;

struct MorphAnimationDestinationInstance
{
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    RadientEntityID                           Entity = InvalidRadientEntityID;
};

using MorphAnimationDestinationLists = std::vector<std::vector<MorphAnimationDestinationInstance>>;

struct MeshSourceDataOwner
{
    explicit MeshSourceDataOwner(std::shared_ptr<const GLTF::Document> pDoc) :
        pDocument{std::move(pDoc)}
    {
    }

    std::shared_ptr<const GLTF::Document> pDocument;
    std::vector<Uint32>                   GeneratedIndices;
};

RadientTransform ToRadientTransform(const GLTF::Node& Node)
{
    // GLTF nodes define either a matrix or TRS. Keep the common TRS path direct
    // and only decompose a matrix when the node actually uses one.
    if (Node.Matrix != float4x4::Identity())
        return RadientMath::MatrixToTransform(RadientMath::ToRadientMatrix(Node.Matrix));

    RadientTransform Transform{};
    Transform.Position = RadientMath::ToRadientFloat3(Node.Translation);
    Transform.Rotation = RadientMath::ToRadientQuaternion(Node.Rotation);
    Transform.Scale    = RadientMath::ToRadientFloat3(Node.Scale);
    return Transform;
}

RadientCameraComponent ToRadientCamera(const GLTF::Camera& Camera)
{
    RadientCameraComponent Result{};

    if (Camera.Type == GLTF::Camera::Projection::Orthographic)
    {
        Result.Projection         = RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
        Result.HorizontalAperture = 2.f * Camera.Orthographic.XMag;
        Result.VerticalAperture   = 2.f * Camera.Orthographic.YMag;
        Result.ClippingRange      = {Camera.Orthographic.ZNear, Camera.Orthographic.ZFar};
    }
    else if (Camera.Type == GLTF::Camera::Projection::Perspective)
    {
        Result.Projection    = RADIENT_CAMERA_PROJECTION_PERSPECTIVE;
        Result.ClippingRange = {Camera.Perspective.ZNear, Camera.Perspective.ZFar};

        if (Camera.Perspective.AspectRatio > 0.f)
            Result.HorizontalAperture = Result.VerticalAperture * Camera.Perspective.AspectRatio;

        if (Camera.Perspective.YFov > 0.f)
            Result.FocalLength = Result.VerticalAperture / (2.f * std::tan(Camera.Perspective.YFov * 0.5f));
    }

    return Result;
}

RADIENT_LIGHT_TYPE ToRadientLightType(GLTF::Light::TYPE Type)
{
    switch (Type)
    {
        case GLTF::Light::TYPE::DIRECTIONAL:
            return RADIENT_LIGHT_TYPE_DIRECTIONAL;

        case GLTF::Light::TYPE::POINT:
            return RADIENT_LIGHT_TYPE_POINT;

        case GLTF::Light::TYPE::SPOT:
            return RADIENT_LIGHT_TYPE_SPOT;

        default:
            UNEXPECTED("Unexpected GLTF light type");
            return RADIENT_LIGHT_TYPE_DIRECTIONAL;
    }
}

RadientLightComponent ToRadientLight(const GLTF::Light& Light)
{
    RadientLightComponent Result{};
    Result.Type           = ToRadientLightType(Light.Type);
    Result.Color          = RadientMath::ToRadientFloat3(Light.Color);
    Result.Intensity      = Light.Intensity;
    Result.Range          = Light.Range;
    Result.InnerConeAngle = Light.InnerConeAngle;
    Result.OuterConeAngle = Light.OuterConeAngle;
    return Result;
}

bool GetNodeIndex(const GLTF::Model& Model,
                  const GLTF::Node*  pNode,
                  Uint32&            NodeIndex)
{
    if (pNode == nullptr ||
        pNode->Index < 0 ||
        static_cast<size_t>(pNode->Index) >= Model.Nodes.size() ||
        pNode != &Model.Nodes[static_cast<size_t>(pNode->Index)])
    {
        return false;
    }

    NodeIndex = static_cast<Uint32>(pNode->Index);
    return true;
}

bool GetLightIndex(const GLTF::Model& Model,
                   const GLTF::Light* pLight,
                   Uint32&            LightIndex)
{
    if (pLight == nullptr)
        return false;

    for (size_t Index = 0; Index < Model.Lights.size(); ++Index)
    {
        if (&Model.Lights[Index] != pLight)
            continue;
        LightIndex = static_cast<Uint32>(Index);
        return true;
    }

    return false;
}

struct SkinImportContext
{
    std::vector<Uint32> NodeToSkeletonJoint;
};

RADIENT_STATUS CreateImportedSkin(const GLTF::Model&                Model,
                                  Uint32                            SkinIndex,
                                  IRadientAssetManager&             AssetManager,
                                  RefCntAutoPtr<IRadientSkinAsset>& pSkin,
                                  SkinImportContext&                Context)
{
    VERIFY_EXPR(SkinIndex < Model.Skins.size());
    const GLTF::Skin& Skin = Model.Skins[SkinIndex];

    if (Skin.Joints.empty() || Skin.Joints.size() > std::numeric_limits<Uint32>::max())
    {
        LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " must contain at least one joint");
        return RADIENT_STATUS_INVALID_DATA;
    }

    if (!Skin.InverseBindMatrices.empty() &&
        Skin.InverseBindMatrices.size() != Skin.Joints.size())
    {
        LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains ", Skin.Joints.size(),
                          " joints, but ", Skin.InverseBindMatrices.size(), " inverse-bind matrices");
        return RADIENT_STATUS_INVALID_DATA;
    }

    // A glTF skin only lists palette joints. Preserve every ancestor as well,
    // because non-joint nodes may contribute transforms to the joint hierarchy.
    std::vector<Uint8> IncludedNodes(Model.Nodes.size(), 0);
    std::vector<Uint8> PaletteNodes(Model.Nodes.size(), 0);
    for (size_t PaletteIndex = 0; PaletteIndex < Skin.Joints.size(); ++PaletteIndex)
    {
        const GLTF::Node* pJoint = Skin.Joints[PaletteIndex];
        Uint32            JointNodeIndex;
        if (!GetNodeIndex(Model, pJoint, JointNodeIndex))
        {
            LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " references an invalid joint node at palette index ", PaletteIndex);
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (PaletteNodes[JointNodeIndex] != 0)
        {
            LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " references node ", JointNodeIndex, " more than once");
            return RADIENT_STATUS_INVALID_DATA;
        }
        PaletteNodes[JointNodeIndex] = 1;

        for (const GLTF::Node* pAncestor = pJoint; pAncestor != nullptr; pAncestor = pAncestor->Parent)
        {
            Uint32 AncestorIndex;
            if (!GetNodeIndex(Model, pAncestor, AncestorIndex))
            {
                LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains an invalid joint ancestor");
                return RADIENT_STATUS_INVALID_DATA;
            }

            if (IncludedNodes[AncestorIndex] != 0)
                break;

            IncludedNodes[AncestorIndex] = 1;
        }
    }

    std::vector<Uint32> NodeToSkeletonJoint(Model.Nodes.size(), InvalidRadientJointIndex);
    Uint32              SkeletonJointCount = 0;
    for (size_t NodeIndex = 0; NodeIndex < IncludedNodes.size(); ++NodeIndex)
    {
        if (IncludedNodes[NodeIndex] != 0)
            NodeToSkeletonJoint[NodeIndex] = SkeletonJointCount++;
    }

    std::vector<RadientSkeletonJointDesc> SkeletonJoints(SkeletonJointCount);
    for (size_t NodeIndex = 0; NodeIndex < IncludedNodes.size(); ++NodeIndex)
    {
        if (IncludedNodes[NodeIndex] == 0)
            continue;

        const GLTF::Node&         Node  = Model.Nodes[NodeIndex];
        RadientSkeletonJointDesc& Joint = SkeletonJoints[NodeToSkeletonJoint[NodeIndex]];
        Joint.Name                      = Node.Name.c_str();
        Joint.LocalRestTransform        = ToRadientTransform(Node);

        if (Node.Parent != nullptr)
        {
            Uint32 ParentNodeIndex;
            if (!GetNodeIndex(Model, Node.Parent, ParentNodeIndex) ||
                NodeToSkeletonJoint[ParentNodeIndex] == InvalidRadientJointIndex)
            {
                LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains an invalid joint parent");
                return RADIENT_STATUS_INVALID_DATA;
            }
            Joint.ParentJointIndex = NodeToSkeletonJoint[ParentNodeIndex];
        }
    }

    RadientSkeletonDesc SkeletonDesc{};
    SkeletonDesc.Name       = Skin.Name.c_str();
    SkeletonDesc.pJoints    = SkeletonJoints.data();
    SkeletonDesc.JointCount = SkeletonJointCount;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    RADIENT_STATUS                       Status = AssetManager.CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pSkeleton == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    std::vector<RadientSkinJointBindingDesc> SkinJoints(Skin.Joints.size());
    for (size_t PaletteIndex = 0; PaletteIndex < Skin.Joints.size(); ++PaletteIndex)
    {
        Uint32 JointNodeIndex;
        if (!GetNodeIndex(Model, Skin.Joints[PaletteIndex], JointNodeIndex))
        {
            UNEXPECTED("A validated GLTF skin joint became invalid");
            return RADIENT_STATUS_INVALID_DATA;
        }

        RadientSkinJointBindingDesc& Joint = SkinJoints[PaletteIndex];
        Joint.SkeletonJointIndex           = NodeToSkeletonJoint[JointNodeIndex];
        Joint.InverseBindMatrix            = RadientMath::ToRadientMatrix(
            Skin.InverseBindMatrices.empty() ?
                float4x4::Identity() :
                Skin.InverseBindMatrices[PaletteIndex]);
    }

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = Skin.Name.c_str();
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = SkinJoints.data();
    SkinDesc.JointCount = static_cast<Uint32>(SkinJoints.size());

    Status = AssetManager.CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pSkin == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    Context.NodeToSkeletonJoint = std::move(NodeToSkeletonJoint);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ExtractSkins(const GLTF::Model&               Model,
                            IRadientAssetManager*            pAssetManager,
                            RadientImport::ImportedDocument& Scene,
                            std::vector<SkinImportContext>&  Contexts)
{
    Scene.Skins.clear();
    Contexts.clear();
    if (Model.Skins.empty())
        return RADIENT_STATUS_OK;

    if (pAssetManager == nullptr)
    {
        LOG_ERROR_MESSAGE("A Radient asset manager is required to import GLTF skins");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Model.Skins.size() > std::numeric_limits<Uint32>::max())
        return RADIENT_STATUS_INVALID_DATA;

    Scene.Skins.resize(Model.Skins.size());
    Contexts.resize(Model.Skins.size());
    for (Uint32 SkinIndex = 0; SkinIndex < static_cast<Uint32>(Model.Skins.size()); ++SkinIndex)
    {
        const RADIENT_STATUS Status = CreateImportedSkin(Model,
                                                         SkinIndex,
                                                         *pAssetManager,
                                                         Scene.Skins[SkinIndex],
                                                         Contexts[SkinIndex]);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateAnimationSamplerTimes(const GLTF::AnimationSampler& Sampler,
                                             Uint32                        AnimationIndex,
                                             Uint32                        SamplerIndex)
{
    for (size_t KeyIndex = 0; KeyIndex < Sampler.Inputs.size(); ++KeyIndex)
    {
        const Float32 Time = Sampler.Inputs[KeyIndex];
        if (!RadientMath::IsFinite(Time))
        {
            LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                                " contains a non-finite keyframe time");
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (KeyIndex != 0 && Time <= Sampler.Inputs[KeyIndex - 1])
        {
            LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                                " keyframe times are not strictly increasing");
            return RADIENT_STATUS_INVALID_DATA;
        }
    }

    if (!Sampler.Inputs.empty() &&
        !RadientMath::IsFiniteNonNegative(Sampler.Inputs.back() - Sampler.Inputs.front()))
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " has an invalid time range");
        return RADIENT_STATUS_INVALID_DATA;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS GetRadientAnimationInterpolation(const GLTF::AnimationSampler&    Sampler,
                                                RADIENT_ANIMATION_INTERPOLATION& Interpolation)
{
    switch (Sampler.Interpolation)
    {
        case GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP:
            Interpolation = RADIENT_ANIMATION_INTERPOLATION_STEP;
            return RADIENT_STATUS_OK;

        case GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR:
            Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
            return RADIENT_STATUS_OK;

        case GLTF::AnimationSampler::INTERPOLATION_TYPE::CUBICSPLINE:
            Interpolation = RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
            return RADIENT_STATUS_OK;

        default:
            return RADIENT_STATUS_INVALID_DATA;
    }
}

RADIENT_STATUS GetAnimationSamplerLayout(const GLTF::AnimationSampler&    Sampler,
                                         Uint32                           AnimationIndex,
                                         Uint32                           SamplerIndex,
                                         const char*                      ValueName,
                                         Uint32                           ComponentCount,
                                         Uint32                           ArraySize,
                                         RADIENT_ANIMATION_INTERPOLATION& Interpolation,
                                         size_t&                          ValueCount)
{
    ValueCount = 0;
    if (Sampler.Inputs.empty() || Sampler.Inputs.size() > std::numeric_limits<Uint32>::max())
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " has an invalid keyframe count");
        return RADIENT_STATUS_INVALID_DATA;
    }

    if (RADIENT_FAILED(GetRadientAnimationInterpolation(Sampler, Interpolation)))
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " uses an unsupported interpolation mode");
        return RADIENT_STATUS_INVALID_DATA;
    }

    const size_t ValuesPerKey = Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ? 3u : 1u;
    if (!IsAddressableArray(Sampler.Inputs.size(), ValuesPerKey))
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " has too many ", ValueName, " values");
        return RADIENT_STATUS_INVALID_DATA;
    }

    ValueCount = Sampler.Inputs.size() * ValuesPerKey;
    if (!IsAddressableArray(ValueCount, ArraySize))
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " has too many ", ValueName, " values");
        return RADIENT_STATUS_INVALID_DATA;
    }

    const size_t ExpectedOutputElementCount = ValueCount * ArraySize;
    if (Sampler.OutputComponentCount != ComponentCount ||
        Sampler.GetOutputElementCount() != ExpectedOutputElementCount)
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                            " has an invalid number of ", ValueName, " values");
        return RADIENT_STATUS_INVALID_DATA;
    }

    return RADIENT_STATUS_OK;
}

static constexpr Uint8 NodeTranslationPropertyMask = 1u << 0u;
static constexpr Uint8 NodeRotationPropertyMask    = 1u << 1u;
static constexpr Uint8 NodeScalePropertyMask       = 1u << 2u;
static constexpr Uint8 NodeVisibilityPropertyMask  = 1u << 3u;
static constexpr Uint8 NodeTransformPropertyMask =
    NodeTranslationPropertyMask | NodeRotationPropertyMask | NodeScalePropertyMask;

static constexpr Uint8 LightColorPropertyMask          = 1u << 0u;
static constexpr Uint8 LightIntensityPropertyMask      = 1u << 1u;
static constexpr Uint8 LightRangePropertyMask          = 1u << 2u;
static constexpr Uint8 LightInnerConeAnglePropertyMask = 1u << 3u;
static constexpr Uint8 LightOuterConeAnglePropertyMask = 1u << 4u;

enum class AnimationPointerValueConstraint : Uint8
{
    None,
    UnitInterval,
    NonNegative,
    Positive,
    InnerConeAngle,
    OuterConeAngle,
};

struct AnimationPointerPropertyDesc
{
    GLTF::AnimationChannel::OBJECT_TYPE ObjectType;
    const char*                         PropertyPath;
    RadientAnimationSchemaID            Schema;
    RadientAnimationPropertyID          Property;
    RadientAnimationValueDesc           Value;
    Uint32                              ComponentCount;
    Uint8                               PropertyMask;
    const char*                         ValueName;
    AnimationPointerValueConstraint     ValueConstraint;
};

const AnimationPointerPropertyDesc* FindAnimationPointerProperty(
    const GLTF::AnimationChannel& Channel)
{
    static constexpr AnimationPointerPropertyDesc Properties[] = {
        {
            GLTF::AnimationChannel::OBJECT_TYPE::LIGHT,
            "/color",
            RadientLightAnimationSchemaID,
            RadientLightColorProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1},
            3,
            LightColorPropertyMask,
            "light color",
            AnimationPointerValueConstraint::UnitInterval,
        },
        {
            GLTF::AnimationChannel::OBJECT_TYPE::LIGHT,
            "/intensity",
            RadientLightAnimationSchemaID,
            RadientLightIntensityProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1},
            1,
            LightIntensityPropertyMask,
            "light intensity",
            AnimationPointerValueConstraint::NonNegative,
        },
        {
            GLTF::AnimationChannel::OBJECT_TYPE::LIGHT,
            "/range",
            RadientLightAnimationSchemaID,
            RadientLightRangeProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1},
            1,
            LightRangePropertyMask,
            "light range",
            AnimationPointerValueConstraint::Positive,
        },
        {
            GLTF::AnimationChannel::OBJECT_TYPE::LIGHT,
            "/spot/innerConeAngle",
            RadientLightAnimationSchemaID,
            RadientLightInnerConeAngleProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1},
            1,
            LightInnerConeAnglePropertyMask,
            "light inner-cone angle",
            AnimationPointerValueConstraint::InnerConeAngle,
        },
        {
            GLTF::AnimationChannel::OBJECT_TYPE::LIGHT,
            "/spot/outerConeAngle",
            RadientLightAnimationSchemaID,
            RadientLightOuterConeAngleProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1},
            1,
            LightOuterConeAnglePropertyMask,
            "light outer-cone angle",
            AnimationPointerValueConstraint::OuterConeAngle,
        },
        {
            GLTF::AnimationChannel::OBJECT_TYPE::NODE,
            "/extensions/KHR_node_visibility/visible",
            RadientNodeAnimationSchemaID,
            RadientNodeVisibilityProperty,
            {RADIENT_ANIMATION_VALUE_TYPE_BOOL, 1},
            1,
            NodeVisibilityPropertyMask,
            "visibility",
            AnimationPointerValueConstraint::None,
        },
    };

    for (const AnimationPointerPropertyDesc& Property : Properties)
    {
        if (Property.ObjectType == Channel.ObjectType &&
            Channel.PropertyPath == Property.PropertyPath)
        {
            return &Property;
        }
    }

    return nullptr;
}

bool IsValidGLTFFloatAnimationComponentType(VALUE_TYPE Type, bool IsNormalized) noexcept
{
    switch (Type)
    {
        case VT_INT8:
        case VT_UINT8:
        case VT_INT16:
        case VT_UINT16:
            return true;

        case VT_UINT32:
        case VT_FLOAT32:
            return !IsNormalized;

        default:
            return false;
    }
}

bool IsValidAnimationPointerKeyValue(AnimationPointerValueConstraint Constraint,
                                     Float32                         Value) noexcept
{
    switch (Constraint)
    {
        case AnimationPointerValueConstraint::None:
            return true;

        case AnimationPointerValueConstraint::UnitInterval:
            return RadientMath::IsFiniteNonNegative(Value) && Value <= 1.f;

        case AnimationPointerValueConstraint::NonNegative:
            return RadientMath::IsFiniteNonNegative(Value);

        case AnimationPointerValueConstraint::Positive:
            return RadientMath::IsFinitePositive(Value);

        case AnimationPointerValueConstraint::InnerConeAngle:
            return RadientMath::IsFiniteNonNegative(Value) && Value < PI_F * 0.5f;

        case AnimationPointerValueConstraint::OuterConeAngle:
            return RadientMath::IsFinitePositive(Value) && Value <= PI_F * 0.5f;

        default:
            UNEXPECTED("Unexpected animation-pointer value constraint");
            return false;
    }
}

bool ValidateAnimationPointerKeyValues(const std::vector<Uint8>&        Values,
                                       RADIENT_ANIMATION_INTERPOLATION  Interpolation,
                                       const RadientAnimationValueDesc& Value,
                                       Uint32                           ComponentCount,
                                       AnimationPointerValueConstraint  Constraint,
                                       Uint32                           AnimationIndex,
                                       Uint32                           SamplerIndex,
                                       const char*                      ValueName)
{
    if (Constraint == AnimationPointerValueConstraint::None)
        return true;

    const size_t ValuesPerKey =
        Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ? 3u : 1u;
    VERIFY_EXPR(Value.ArraySize != 0);
    const size_t ArraySize = Value.ArraySize;
    VERIFY_EXPR(ArraySize <= std::numeric_limits<size_t>::max() / ValuesPerKey);
    const size_t ElementsPerKey = ValuesPerKey * ArraySize;
    const size_t ElementSize    = static_cast<size_t>(ComponentCount) * sizeof(Float32);
    VERIFY_EXPR(ElementSize != 0 && Values.size() % ElementSize == 0);
    const size_t ElementCount = Values.size() / ElementSize;

    for (size_t ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
    {
        if (ValuesPerKey == 3u)
        {
            const size_t ElementWithinKey = ElementIndex % ElementsPerKey;
            if (ElementWithinKey < ArraySize ||
                ElementWithinKey >= 2u * ArraySize)
            {
                continue;
            }
        }

        for (Uint32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
        {
            const size_t FlatComponentIndex = ElementIndex * ComponentCount + ComponentIndex;
            Float32      Component;
            std::memcpy(&Component,
                        Values.data() + FlatComponentIndex * sizeof(Component),
                        sizeof(Component));
            if (!IsValidAnimationPointerKeyValue(Constraint, Component))
            {
                LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SamplerIndex,
                                    " contains an invalid ", ValueName,
                                    " key value at component index ", FlatComponentIndex);
                return false;
            }
        }
    }

    return true;
}

struct AnimationClipSamplerStorage
{
    Uint32                          SourceSamplerIndex = InvalidRadientAnimationSamplerIndex;
    RadientAnimationValueDesc       Value;
    RADIENT_ANIMATION_INTERPOLATION Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    std::vector<Float32>            Times;
    std::vector<Uint8>              ConvertedValues;
};

struct SourceAnimationSamplerMapping
{
    Uint32                    ClipSamplerIndex = InvalidRadientAnimationSamplerIndex;
    RadientAnimationValueDesc Value;
    bool                      UsesFloatConversion = false;
};

using SourceAnimationSamplerMappings = std::vector<std::vector<SourceAnimationSamplerMapping>>;

RADIENT_STATUS GetOrCreateAnimationClipSampler(const GLTF::Animation&                    Animation,
                                               Uint32                                    AnimationIndex,
                                               Uint32                                    SourceSamplerIndex,
                                               const char*                               ValueName,
                                               const RadientAnimationValueDesc&          Value,
                                               Uint32                                    ComponentCount,
                                               bool                                      AllowFloatConversion,
                                               AnimationPointerValueConstraint           ValueConstraint,
                                               SourceAnimationSamplerMappings&           SourceMappings,
                                               std::vector<AnimationClipSamplerStorage>& Samplers,
                                               Uint32&                                   ClipSamplerIndex)
{
    VERIFY_EXPR(SourceSamplerIndex < Animation.Samplers.size());
    VERIFY_EXPR(SourceSamplerIndex < SourceMappings.size());

    const GLTF::AnimationSampler&               Sampler  = Animation.Samplers[SourceSamplerIndex];
    std::vector<SourceAnimationSamplerMapping>& Mappings = SourceMappings[SourceSamplerIndex];
    for (const SourceAnimationSamplerMapping& Mapping : Mappings)
    {
        if (Mapping.Value.Type == Value.Type &&
            Mapping.Value.ArraySize == Value.ArraySize &&
            (AllowFloatConversion || !Mapping.UsesFloatConversion))
        {
            VERIFY_EXPR(Mapping.ClipSamplerIndex < Samplers.size());
            const AnimationClipSamplerStorage& Storage = Samplers[Mapping.ClipSamplerIndex];
            const std::vector<Uint8>&          Values  = Storage.ConvertedValues.empty() ?
                Sampler.OutputData :
                Storage.ConvertedValues;
            if (!ValidateAnimationPointerKeyValues(
                    Values, Storage.Interpolation, Value, ComponentCount, ValueConstraint,
                    AnimationIndex, SourceSamplerIndex, ValueName))
            {
                return RADIENT_STATUS_INVALID_DATA;
            }

            ClipSamplerIndex = Mapping.ClipSamplerIndex;
            return RADIENT_STATUS_OK;
        }
    }

    if (Samplers.size() >= std::numeric_limits<Uint32>::max())
        return RADIENT_STATUS_INVALID_DATA;

    RADIENT_ANIMATION_INTERPOLATION Interpolation;
    size_t                          ValueCount   = 0;
    const RADIENT_STATUS            LayoutStatus = GetAnimationSamplerLayout(
        Sampler,
        AnimationIndex,
        SourceSamplerIndex,
        ValueName,
        ComponentCount,
        Value.ArraySize,
        Interpolation,
        ValueCount);
    if (RADIENT_FAILED(LayoutStatus))
        return LayoutStatus;
    VERIFY_EXPR(ValueCount != 0);

    std::vector<Uint8> ConvertedValues;
    bool               UsesFloatConversion = false;
    if (Value.Type == RADIENT_ANIMATION_VALUE_TYPE_BOOL)
    {
        if (Sampler.OutputValueType != VT_UINT8)
        {
            LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                " has an incompatible ", ValueName, " output component type");
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (Interpolation != RADIENT_ANIMATION_INTERPOLATION_STEP)
        {
            LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                " must use STEP interpolation for ", ValueName);
            return RADIENT_STATUS_INVALID_DATA;
        }

        ConvertedValues.resize(Sampler.OutputData.size());
        std::transform(Sampler.OutputData.begin(), Sampler.OutputData.end(),
                       ConvertedValues.begin(),
                       [](Uint8 SourceValue) { return SourceValue != 0 ? Uint8{1} : Uint8{0}; });
    }
    else
    {
        if (AllowFloatConversion &&
            !IsValidGLTFFloatAnimationComponentType(
                Sampler.OutputValueType, Sampler.OutputIsNormalized))
        {
            LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                " has an incompatible ", ValueName, " output component type");
            return RADIENT_STATUS_INVALID_DATA;
        }

        const bool UseSourceValues =
            Sampler.OutputValueType == VT_FLOAT32 && !Sampler.OutputIsNormalized;
        if (!UseSourceValues)
        {
            if (!AllowFloatConversion || Sampler.OutputValueType == VT_FLOAT32)
            {
                LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                    " has an incompatible ", ValueName, " output component type");
                return RADIENT_STATUS_INVALID_DATA;
            }

            Uint64 ConvertedComponentCount = 0;
            Uint64 ConvertedValueDataSize  = 0;
            if (!CheckedMultiply(static_cast<Uint64>(Sampler.GetOutputElementCount()),
                                 ComponentCount, ConvertedComponentCount) ||
                !CheckedMultiply(ConvertedComponentCount, sizeof(Float32), ConvertedValueDataSize) ||
                !RadientValidation::IsAddressableSize(ConvertedValueDataSize))
            {
                LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                    " has too many ", ValueName, " values to convert");
                return RADIENT_STATUS_INVALID_DATA;
            }

            ConvertedValues.resize(static_cast<size_t>(ConvertedValueDataSize));
            if (!Sampler.ConvertOutputData(
                    VT_FLOAT32, ConvertedValues.data(), ConvertedValues.size()))
            {
                LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                    " has an incompatible ", ValueName, " output component type");
                return RADIENT_STATUS_INVALID_DATA;
            }
            UsesFloatConversion = true;
        }

        const std::vector<Uint8>& FloatValues         = UseSourceValues ? Sampler.OutputData : ConvertedValues;
        const size_t              ComponentValueCount = FloatValues.size() / sizeof(Float32);
        for (size_t ComponentIndex = 0; ComponentIndex < ComponentValueCount; ++ComponentIndex)
        {
            Float32 Component;
            std::memcpy(&Component,
                        FloatValues.data() + ComponentIndex * sizeof(Component),
                        sizeof(Component));
            if (!RadientMath::IsFinite(Component))
            {
                LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " sampler ", SourceSamplerIndex,
                                    " contains a non-finite ", ValueName, " component at index ",
                                    ComponentIndex);
                return RADIENT_STATUS_INVALID_DATA;
            }
        }

        if (!ValidateAnimationPointerKeyValues(
                FloatValues, Interpolation, Value, ComponentCount, ValueConstraint,
                AnimationIndex, SourceSamplerIndex, ValueName))
        {
            return RADIENT_STATUS_INVALID_DATA;
        }
    }

    const RADIENT_STATUS TimeStatus = ValidateAnimationSamplerTimes(
        Sampler, AnimationIndex, SourceSamplerIndex);
    if (RADIENT_FAILED(TimeStatus))
        return TimeStatus;

    ClipSamplerIndex                     = static_cast<Uint32>(Samplers.size());
    AnimationClipSamplerStorage& Storage = Samplers.emplace_back();
    Storage.SourceSamplerIndex           = SourceSamplerIndex;
    Storage.Value                        = Value;
    Storage.Interpolation                = Interpolation;
    Storage.Times                        = Sampler.Inputs;
    Storage.ConvertedValues              = std::move(ConvertedValues);

    Mappings.push_back({ClipSamplerIndex, Value, UsesFloatConversion});
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS CreateImportedAnimationClip(
    const GLTF::Model&                                        Model,
    const GLTF::Animation&                                    Animation,
    Uint32                                                    AnimationIndex,
    const std::vector<SkinImportContext>&                     SkinContexts,
    IRadientAssetManager&                                     AssetManager,
    RefCntAutoPtr<IRadientAnimationClipAsset>&                pResult,
    std::vector<RadientImport::ImportedAnimationSkinMapping>& SkinMappings)
{
    pResult.Release();
    SkinMappings.clear();

    std::vector<Uint32>                      NodeToTarget(Model.Nodes.size(), InvalidRadientAnimationTargetIndex);
    std::vector<Uint32>                      NodeToMorphTarget(Model.Nodes.size(), InvalidRadientAnimationTargetIndex);
    std::vector<Uint32>                      LightToTarget(Model.Lights.size(), InvalidRadientAnimationTargetIndex);
    std::vector<RadientAnimationTargetDesc>  Targets;
    std::vector<Uint8>                       TargetPropertyMasks;
    std::vector<AnimationClipSamplerStorage> SamplerStorage;
    std::vector<RadientAnimationChannelDesc> Channels;
    SourceAnimationSamplerMappings           SourceSamplerMappings(Animation.Samplers.size());
    Targets.reserve(Animation.Channels.size());
    TargetPropertyMasks.reserve(Animation.Channels.size());
    SamplerStorage.reserve(Animation.Samplers.size());
    Channels.reserve(Animation.Channels.size());

    for (const GLTF::AnimationChannel& Channel : Animation.Channels)
    {
        if (Channel.SamplerIndex >= Animation.Samplers.size())
        {
            LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                " channel that references invalid sampler ", Channel.SamplerIndex);
            continue;
        }

        if (Channel.PathType == GLTF::AnimationChannel::PATH_TYPE::UNKNOWN)
        {
            LOG_WARNING_MESSAGE("Skipping unsupported channel path in GLTF animation ", AnimationIndex);
            continue;
        }

        const AnimationPointerPropertyDesc* pPointerProperty = nullptr;
        if (Channel.PathType == GLTF::AnimationChannel::PATH_TYPE::POINTER)
        {
            pPointerProperty = FindAnimationPointerProperty(Channel);
            if (pPointerProperty == nullptr)
            {
                LOG_WARNING_MESSAGE("Skipping unsupported animation-pointer target '",
                                    Channel.PropertyPath, "' in GLTF animation ", AnimationIndex);
                continue;
            }
        }

        RadientAnimationSchemaID   Schema;
        RadientAnimationPropertyID Property;
        RadientAnimationValueDesc  Value;
        Uint32                     ComponentCount;
        Uint8                      PropertyMask;
        const char*                ValueName;
        Uint32                     ObjectIndex;
        const char*                ObjectName;
        std::vector<Uint32>*       pObjectToTarget;
        const GLTF::Node*          pNode = nullptr;
        if (pPointerProperty != nullptr)
        {
            Schema         = pPointerProperty->Schema;
            Property       = pPointerProperty->Property;
            Value          = pPointerProperty->Value;
            ComponentCount = pPointerProperty->ComponentCount;
            PropertyMask   = pPointerProperty->PropertyMask;
            ValueName      = pPointerProperty->ValueName;

            switch (pPointerProperty->ObjectType)
            {
                case GLTF::AnimationChannel::OBJECT_TYPE::NODE:
                    pNode = Channel.GetNode();
                    if (!GetNodeIndex(Model, pNode, ObjectIndex))
                    {
                        LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                            " channel that references an invalid target node");
                        continue;
                    }
                    ObjectName      = Model.Nodes[ObjectIndex].Name.c_str();
                    pObjectToTarget = &NodeToTarget;
                    break;

                case GLTF::AnimationChannel::OBJECT_TYPE::LIGHT:
                {
                    const GLTF::Light* const pLight =
                        static_cast<const GLTF::Light*>(Channel.pObject);
                    if (!GetLightIndex(Model, pLight, ObjectIndex))
                    {
                        LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                            " channel that references an invalid target light");
                        continue;
                    }
                    if ((Property == RadientLightRangeProperty &&
                         ((pLight->Type != GLTF::Light::TYPE::POINT &&
                           pLight->Type != GLTF::Light::TYPE::SPOT) ||
                          !RadientMath::IsFinitePositive(pLight->Range))) ||
                        ((Property == RadientLightInnerConeAngleProperty ||
                          Property == RadientLightOuterConeAngleProperty) &&
                         pLight->Type != GLTF::Light::TYPE::SPOT))
                    {
                        LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex, ' ',
                                            ValueName, " channel because the property is not defined for the target light");
                        continue;
                    }
                    ObjectName      = Model.Lights[ObjectIndex].Name.c_str();
                    pObjectToTarget = &LightToTarget;
                    break;
                }

                default:
                    UNEXPECTED("Unsupported animation-pointer object type");
                    continue;
            }
        }
        else
        {
            pNode = Channel.GetNode();
            if (!GetNodeIndex(Model, pNode, ObjectIndex))
            {
                LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                    " channel that references an invalid target node");
                continue;
            }
            ObjectName = Model.Nodes[ObjectIndex].Name.c_str();

            switch (Channel.PathType)
            {
                case GLTF::AnimationChannel::PATH_TYPE::TRANSLATION:
                    Schema          = RadientNodeAnimationSchemaID;
                    Property        = RadientNodeTranslationProperty;
                    Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
                    Value.ArraySize = 1;
                    ComponentCount  = 3;
                    PropertyMask    = NodeTranslationPropertyMask;
                    ValueName       = "translation";
                    pObjectToTarget = &NodeToTarget;
                    break;

                case GLTF::AnimationChannel::PATH_TYPE::ROTATION:
                    Schema          = RadientNodeAnimationSchemaID;
                    Property        = RadientNodeRotationProperty;
                    Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT4;
                    Value.ArraySize = 1;
                    ComponentCount  = 4;
                    PropertyMask    = NodeRotationPropertyMask;
                    ValueName       = "rotation";
                    pObjectToTarget = &NodeToTarget;
                    break;

                case GLTF::AnimationChannel::PATH_TYPE::SCALE:
                    Schema          = RadientNodeAnimationSchemaID;
                    Property        = RadientNodeScaleProperty;
                    Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
                    Value.ArraySize = 1;
                    ComponentCount  = 3;
                    PropertyMask    = NodeScalePropertyMask;
                    ValueName       = "scale";
                    pObjectToTarget = &NodeToTarget;
                    break;

                case GLTF::AnimationChannel::PATH_TYPE::WEIGHTS:
                {
                    if (pNode->pMesh == nullptr)
                    {
                        LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                            " morph-weight channel for a node without a mesh");
                        continue;
                    }

                    const size_t MorphTargetCount = pNode->pMesh->GetMorphTargetCount();
                    if (MorphTargetCount == 0 || MorphTargetCount > std::numeric_limits<Uint32>::max())
                    {
                        LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                            " morph-weight channel for a node without addressable morph targets");
                        continue;
                    }

                    Schema          = RadientMorphWeightsAnimationSchemaID;
                    Property        = RadientMorphWeightsProperty;
                    Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
                    Value.ArraySize = static_cast<Uint32>(MorphTargetCount);
                    ComponentCount  = 1;
                    PropertyMask    = 1u;
                    ValueName       = "morph-weight";
                    pObjectToTarget = &NodeToMorphTarget;
                    break;
                }

                default:
                    LOG_WARNING_MESSAGE("Skipping unsupported channel path in GLTF animation ", AnimationIndex);
                    continue;
            }
        }

        Uint32 ClipTargetIndex = (*pObjectToTarget)[ObjectIndex];
        if (ClipTargetIndex != InvalidRadientAnimationTargetIndex &&
            (TargetPropertyMasks[ClipTargetIndex] & PropertyMask) != 0)
        {
            LOG_WARNING_MESSAGE("Skipping duplicate ", ValueName, " channel in GLTF animation ",
                                AnimationIndex);
            continue;
        }

        if ((ClipTargetIndex == InvalidRadientAnimationTargetIndex &&
             Targets.size() >= std::numeric_limits<Uint32>::max()) ||
            Channels.size() >= std::numeric_limits<Uint32>::max())
        {
            LOG_WARNING_MESSAGE("Skipping GLTF animation ", AnimationIndex,
                                " because its target or channel table is too large");
            return RADIENT_STATUS_NO_CHANGE;
        }

        Uint32               ClipSamplerIndex = InvalidRadientAnimationSamplerIndex;
        const RADIENT_STATUS SamplerStatus    = GetOrCreateAnimationClipSampler(
            Animation,
            AnimationIndex,
            Channel.SamplerIndex,
            ValueName,
            Value,
            ComponentCount,
            pPointerProperty != nullptr,
            pPointerProperty != nullptr ?
                pPointerProperty->ValueConstraint :
                AnimationPointerValueConstraint::None,
            SourceSamplerMappings,
            SamplerStorage,
            ClipSamplerIndex);
        if (RADIENT_FAILED(SamplerStatus))
            continue;

        if (ClipTargetIndex == InvalidRadientAnimationTargetIndex)
        {
            ClipTargetIndex                    = static_cast<Uint32>(Targets.size());
            (*pObjectToTarget)[ObjectIndex]    = ClipTargetIndex;
            RadientAnimationTargetDesc& Target = Targets.emplace_back();
            Target.Schema                      = Schema;
            Target.Object                      = static_cast<RadientAnimationObjectID>(ObjectIndex);
            Target.Name                        = ObjectName;
            TargetPropertyMasks.push_back(0);
        }

        TargetPropertyMasks[ClipTargetIndex] = static_cast<Uint8>(
            TargetPropertyMasks[ClipTargetIndex] | PropertyMask);

        RadientAnimationChannelDesc& ChannelDesc = Channels.emplace_back();
        ChannelDesc.TargetIndex                  = ClipTargetIndex;
        ChannelDesc.Property                     = Property;
        ChannelDesc.FirstArrayElement            = 0;
        ChannelDesc.SamplerIndex                 = ClipSamplerIndex;
    }

    if (Channels.empty())
    {
        if (!Animation.Channels.empty())
        {
            LOG_WARNING_MESSAGE("GLTF animation '", Animation.Name,
                                "' does not contain usable channels and was not imported");
        }
        return RADIENT_STATUS_NO_CHANGE;
    }

    Float32 AnimationStart = +(std::numeric_limits<Float32>::max)();
    Float32 AnimationEnd   = -(std::numeric_limits<Float32>::max)();
    for (AnimationClipSamplerStorage& Storage : SamplerStorage)
    {
        VERIFY_EXPR(!Storage.Times.empty());
        AnimationStart = std::min(AnimationStart, Storage.Times.front());
        AnimationEnd   = std::max(AnimationEnd, Storage.Times.back());
    }

    const Float32 Duration = AnimationEnd - AnimationStart;
    if (!RadientMath::IsFiniteNonNegative(Duration))
    {
        LOG_WARNING_MESSAGE("GLTF animation ", AnimationIndex, " has an invalid time range");
        return RADIENT_STATUS_NO_CHANGE;
    }

    for (AnimationClipSamplerStorage& Storage : SamplerStorage)
    {
        std::transform(Storage.Times.begin(), Storage.Times.end(), Storage.Times.begin(),
                       [AnimationStart](Float32 Time) { return Time - AnimationStart; });
    }

    std::vector<RadientAnimationSamplerDesc> Samplers(SamplerStorage.size());
    for (Uint32 SamplerIndex = 0; SamplerIndex < static_cast<Uint32>(SamplerStorage.size()); ++SamplerIndex)
    {
        const AnimationClipSamplerStorage& Storage = SamplerStorage[SamplerIndex];
        const GLTF::AnimationSampler&      Source  = Animation.Samplers[Storage.SourceSamplerIndex];
        const std::vector<Uint8>&          Values  = Storage.ConvertedValues.empty() ?
            Source.OutputData :
            Storage.ConvertedValues;

        RadientAnimationSamplerDesc& Desc = Samplers[SamplerIndex];
        Desc.Value                        = Storage.Value;
        Desc.Interpolation                = Storage.Interpolation;
        Desc.pTimes                       = Storage.Times.data();
        Desc.pValues                      = Values.data();
        Desc.ValueDataSize                = static_cast<Uint64>(Values.size());
        Desc.KeyframeCount                = static_cast<Uint32>(Storage.Times.size());
    }

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = Animation.Name.c_str();
    ClipDesc.Duration     = Duration;
    ClipDesc.pTargets     = Targets.data();
    ClipDesc.TargetCount  = static_cast<Uint32>(Targets.size());
    ClipDesc.pSamplers    = Samplers.data();
    ClipDesc.SamplerCount = static_cast<Uint32>(Samplers.size());
    ClipDesc.pChannels    = Channels.data();
    ClipDesc.ChannelCount = static_cast<Uint32>(Channels.size());

    const RADIENT_STATUS ClipStatus = AssetManager.CreateAnimationClip(ClipDesc, pResult.GetAddressOfEmpty());
    if (ClipStatus == RADIENT_STATUS_INVALID_ARGUMENT)
    {
        LOG_WARNING_MESSAGE("Skipping GLTF animation '", Animation.Name,
                            "' because its clip description is invalid");
        return RADIENT_STATUS_NO_CHANGE;
    }
    if (ClipStatus != RADIENT_STATUS_OK || pResult == nullptr)
        return RADIENT_FAILED(ClipStatus) ? ClipStatus : RADIENT_STATUS_FAILED;

    std::vector<RadientImport::ImportedAnimationSkinMapping> PendingSkinMappings;
    PendingSkinMappings.reserve(SkinContexts.size());
    for (Uint32 SkinIndex = 0; SkinIndex < static_cast<Uint32>(SkinContexts.size()); ++SkinIndex)
    {
        const SkinImportContext&                    SkinContext = SkinContexts[SkinIndex];
        RadientImport::ImportedAnimationSkinMapping Mapping;
        Mapping.SkinIndex = SkinIndex;
        Mapping.JointMappings.reserve(Targets.size());
        for (Uint32 ClipTargetIndex = 0; ClipTargetIndex < static_cast<Uint32>(Targets.size()); ++ClipTargetIndex)
        {
            const RadientAnimationTargetDesc& Target = Targets[ClipTargetIndex];
            if (Target.Schema != RadientNodeAnimationSchemaID ||
                (TargetPropertyMasks[ClipTargetIndex] & NodeTransformPropertyMask) == 0)
                continue;

            const Uint32 NodeIndex  = static_cast<Uint32>(Target.Object);
            const Uint32 JointIndex = SkinContext.NodeToSkeletonJoint[NodeIndex];
            if (JointIndex == InvalidRadientJointIndex)
                continue;

            RadientAnimationDestinationMappingDesc& JointMapping = Mapping.JointMappings.emplace_back();
            JointMapping.ClipTargetIndex                         = ClipTargetIndex;
            JointMapping.DestinationElement                      = JointIndex;
        }

        if (!Mapping.JointMappings.empty())
            PendingSkinMappings.emplace_back(std::move(Mapping));
    }

    SkinMappings = std::move(PendingSkinMappings);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ExtractAnimations(const GLTF::Model&                    Model,
                                 IRadientAssetManager*                 pAssetManager,
                                 const std::vector<SkinImportContext>& SkinContexts,
                                 RadientImport::ImportedDocument&      Scene)
{
    Scene.Animations.clear();
    if (Model.Animations.empty())
        return RADIENT_STATUS_OK;
    if (pAssetManager == nullptr || SkinContexts.size() != Model.Skins.size() || Scene.Skins.size() != Model.Skins.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (Model.Animations.size() > std::numeric_limits<Uint32>::max())
    {
        LOG_WARNING_MESSAGE("Skipping GLTF animations because the animation table is too large");
        return RADIENT_STATUS_OK;
    }

    for (const SkinImportContext& SkinContext : SkinContexts)
    {
        if (SkinContext.NodeToSkeletonJoint.size() != Model.Nodes.size())
            return RADIENT_STATUS_INVALID_DATA;
    }

    Scene.Animations.reserve(Model.Animations.size());
    for (Uint32 AnimationIndex = 0; AnimationIndex < static_cast<Uint32>(Model.Animations.size()); ++AnimationIndex)
    {
        const GLTF::Animation& Animation = Model.Animations[AnimationIndex];

        RadientImport::ImportedAnimation ImportedAnimation;

        const RADIENT_STATUS ClipStatus = CreateImportedAnimationClip(
            Model,
            Animation,
            AnimationIndex,
            SkinContexts,
            *pAssetManager,
            ImportedAnimation.pClip,
            ImportedAnimation.SkinMappings);
        if (ClipStatus == RADIENT_STATUS_NO_CHANGE)
            continue;
        if (RADIENT_FAILED(ClipStatus))
            return ClipStatus;
        VERIFY_EXPR(ImportedAnimation.pClip != nullptr);
        Scene.Animations.emplace_back(std::move(ImportedAnimation));
    }

    return RADIENT_STATUS_OK;
}

Uint32 FindSkinIndex(const GLTF::Model& Model, const GLTF::Skin* pSkin)
{
    for (size_t SkinIndex = 0; SkinIndex < Model.Skins.size(); ++SkinIndex)
    {
        if (&Model.Skins[SkinIndex] == pSkin)
            return static_cast<Uint32>(SkinIndex);
    }

    return RadientImport::InvalidImportedSkinIndex;
}

RADIENT_STATUS CreateNode(IRadientSceneWriter&                              Writer,
                          const RadientImport::ImportedDocument&            Scene,
                          Uint32                                            NodeIndex,
                          RadientEntityID                                   Parent,
                          const RadientMatrix4x4&                           ParentDocumentMatrix,
                          std::vector<RefCntAutoPtr<IRadientSkeletonPose>>& SkinPoses,
                          NodeEntityLists*                                  pNodeEntities,
                          LightEntityLists*                                 pLightEntities,
                          SkinEntityLists*                                  pSkinEntities,
                          MorphAnimationDestinationLists*                   pMorphAnimationDestinations)
{
    if (NodeIndex >= Scene.Nodes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientImport::ImportedNode& Node               = Scene.Nodes[NodeIndex];
    const std::string                  FallbackName       = std::string{"GLTF Node "} + std::to_string(NodeIndex);
    const RadientMatrix4x4             NodeDocumentMatrix = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(Node.Transform), ParentDocumentMatrix);

    RadientEntityDesc NodeDesc{};
    NodeDesc.Name      = !Node.Name.empty() ? Node.Name.c_str() : FallbackName.c_str();
    NodeDesc.Parent    = Parent;
    NodeDesc.Flags     = Node.Visible ? RADIENT_ENTITY_FLAG_VISIBLE : RADIENT_ENTITY_FLAG_NONE;
    NodeDesc.Transform = Node.Transform;

    RadientEntityID NodeEntity = InvalidRadientEntityID;
    RADIENT_STATUS  Status     = Writer.CreateEntity(NodeDesc, NodeEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (pNodeEntities != nullptr)
    {
        VERIFY_EXPR(NodeIndex < pNodeEntities->size());
        (*pNodeEntities)[NodeIndex].push_back(NodeEntity);
    }

    if (Node.Camera)
    {
        Status = Writer.SetCamera(NodeEntity, *Node.Camera);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (Node.Light)
    {
        Status = Writer.SetLight(NodeEntity, *Node.Light);
        if (RADIENT_FAILED(Status))
            return Status;

        if (pLightEntities != nullptr &&
            Node.LightIndex != RadientImport::InvalidImportedLightIndex)
        {
            (*pLightEntities)[Node.LightIndex].push_back(NodeEntity);
        }
    }

    if (Node.pMesh != nullptr)
    {
        RadientMeshComponent Mesh{};
        Mesh.pMesh = Node.pMesh;
        Status     = Writer.SetMesh(NodeEntity, Mesh);
        if (RADIENT_FAILED(Status))
            return Status;

        const Uint32 MorphTargetCount = Node.pMesh->GetDesc().MorphTargetCount;
        if (MorphTargetCount != 0)
        {
            RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
            Status = Node.pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty());
            if (RADIENT_FAILED(Status) || pWeights == nullptr)
                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

            if (!Node.MorphWeights.empty())
            {
                if (Node.MorphWeights.size() != MorphTargetCount)
                {
                    LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' provides ", Node.MorphWeights.size(),
                                        " morph weights for a mesh with ", MorphTargetCount,
                                        " targets; applying the available weights");
                }

                const Uint32 WeightCount = static_cast<Uint32>(std::min(
                    Node.MorphWeights.size(),
                    size_t{MorphTargetCount}));
                if (WeightCount != 0)
                {
                    Status = pWeights->SetWeights(0, WeightCount, Node.MorphWeights.data());
                    if (RADIENT_FAILED(Status))
                        return Status;
                }
            }

            Status = Writer.SetMorph(NodeEntity, RadientMorphComponent{pWeights});
            if (RADIENT_FAILED(Status))
                return Status;

            if (pMorphAnimationDestinations != nullptr)
            {
                VERIFY_EXPR(NodeIndex < pMorphAnimationDestinations->size());
                MorphAnimationDestinationInstance& Instance =
                    (*pMorphAnimationDestinations)[NodeIndex].emplace_back();
                Instance.pWeights = pWeights;
                Instance.Entity   = NodeEntity;
            }
        }
        else if (!Node.MorphWeights.empty())
        {
            LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name,
                                "' provides morph weights for a mesh without imported morph targets; ignoring the weights");
        }

        if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
        {
            if (Node.SkinIndex >= Scene.Skins.size() || Scene.Skins[Node.SkinIndex] == nullptr)
            {
                LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references unavailable skin ",
                                    Node.SkinIndex, "; rendering the mesh without skinning");
            }
            else
            {
                IRadientSkinAsset* const     pSkin     = Scene.Skins[Node.SkinIndex];
                IRadientSkeletonAsset* const pSkeleton = pSkin->GetDesc().pSkeleton;
                if (pSkeleton == nullptr)
                {
                    LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references skin ",
                                        Node.SkinIndex, " without a skeleton; rendering the mesh without skinning");
                }
                else
                {
                    RadientMatrix4x4 SkeletonToMeshTransform;
                    const bool       NodeDocumentMatrixIsIdentity = (NodeDocumentMatrix == SkeletonToMeshTransform);
                    if (!NodeDocumentMatrixIsIdentity &&
                        !RadientMath::TryInverseMatrix(NodeDocumentMatrix, SkeletonToMeshTransform))
                    {
                        LOG_WARNING_MESSAGE("Imported skinned node '", NodeDesc.Name,
                                            "' has a non-invertible document transform; rendering the mesh without skinning");
                    }
                    else
                    {
                        RefCntAutoPtr<IRadientSkeletonPose>& pPose = SkinPoses[Node.SkinIndex];
                        if (pPose == nullptr)
                        {
                            Status = pSkeleton->CreatePose(pPose.GetAddressOfEmpty());
                            if (RADIENT_FAILED(Status) || pPose == nullptr)
                                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
                        }

                        RadientSkinComponent SkinComponent{};
                        SkinComponent.pSkin                   = pSkin;
                        SkinComponent.pPose                   = pPose;
                        SkinComponent.SkeletonToMeshTransform = SkeletonToMeshTransform;
                        Status                                = Writer.SetSkin(NodeEntity, SkinComponent);
                        if (RADIENT_FAILED(Status))
                            return Status;

                        if (pSkinEntities != nullptr)
                        {
                            VERIFY_EXPR(Node.SkinIndex < pSkinEntities->size());
                            (*pSkinEntities)[Node.SkinIndex].push_back(NodeEntity);
                        }
                    }
                }
            }
        }

        RadientMeshRendererComponent Renderer{};
        Status = Writer.SetMeshRenderer(NodeEntity, Renderer);
        if (RADIENT_FAILED(Status))
            return Status;
    }
    else if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
    {
        LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references skin ", Node.SkinIndex,
                            " but has no mesh; ignoring the skin reference");
    }

    for (Uint32 ChildIndex : Node.Children)
    {
        Status = CreateNode(Writer, Scene, ChildIndex, NodeEntity, NodeDocumentMatrix,
                            SkinPoses, pNodeEntities, pLightEntities, pSkinEntities,
                            pMorphAnimationDestinations);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

void RegisterAnimationDestination(
    IRadientAnimationClipAsset&            Clip,
    const RadientAnimationDestinationDesc& DestinationDesc,
    const RadientEntityID*                 pEntities,
    Uint32                                 EntityCount,
    const char*                            DestinationDescription,
    IRadientAnimationRegistry&             Registry)
{
    const RadientAnimationClipDesc& ClipDesc = Clip.GetDesc();

    RadientAnimationBindingDesc BindingDesc{};
    BindingDesc.pDestinations    = &DestinationDesc;
    BindingDesc.DestinationCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    RADIENT_STATUS                          Status = Clip.CreateBinding(BindingDesc, pBinding.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK || pBinding == nullptr)
    {
        LOG_WARNING_MESSAGE("Skipping ", DestinationDescription, " binding for imported animation clip '",
                            ClipDesc.Name, "' because it could not be created");
        return;
    }

    Status = Registry.AddAnimationBinding(pBinding, pEntities, EntityCount);
    if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE)
        return;

    LOG_WARNING_MESSAGE("Skipping ", DestinationDescription, " binding for imported animation clip '",
                        ClipDesc.Name, "' because it could not be registered");

    // AddAnimationBinding implementations may have changed part of the
    // registry before reporting a failure. Remove this binding as a
    // best-effort cleanup without disturbing earlier bindings.
    const RADIENT_STATUS CleanupStatus = Registry.RemoveAnimationBinding(
        pBinding, pEntities, EntityCount);
    if (CleanupStatus != RADIENT_STATUS_OK && CleanupStatus != RADIENT_STATUS_NO_CHANGE)
    {
        LOG_WARNING_MESSAGE("Unable to clean up the failed ", DestinationDescription,
                            " animation binding for clip '", ClipDesc.Name, "'");
    }
}

void RegisterSceneAnimations(
    const RadientImport::ImportedDocument&                  Scene,
    const std::vector<RefCntAutoPtr<IRadientSkeletonPose>>& SkinPoses,
    IRadientAnimationDestination*                           pSceneDestination,
    const NodeEntityLists&                                  NodeEntities,
    RadientEntityID                                         SceneRootEntity,
    const LightEntityLists&                                 LightEntities,
    const SkinEntityLists&                                  SkinEntities,
    const MorphAnimationDestinationLists&                   MorphAnimationDestinations,
    IRadientAnimationRegistry&                              Registry)
{
    for (const RadientImport::ImportedAnimation& ImportedAnimation : Scene.Animations)
    {
        if (ImportedAnimation.pClip == nullptr)
        {
            LOG_WARNING_MESSAGE("Skipping a null imported animation clip");
            continue;
        }

        const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
        for (const RadientImport::ImportedAnimationSkinMapping& SkinMapping : ImportedAnimation.SkinMappings)
        {
            if (SkinMapping.SkinIndex >= SkinPoses.size() || SkinMapping.SkinIndex >= SkinEntities.size())
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because its skin mapping is invalid");
                continue;
            }

            IRadientSkeletonPose* const         pPose    = SkinPoses[SkinMapping.SkinIndex];
            const std::vector<RadientEntityID>& Entities = SkinEntities[SkinMapping.SkinIndex];
            if (pPose == nullptr || Entities.empty())
                continue;

            RefCntAutoPtr<IRadientAnimationDestination> pDestination;
            pPose->QueryInterface(IID_RadientAnimationDestination, pDestination.GetAddressOfEmpty());
            if (pDestination == nullptr)
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because the skeleton pose does not expose an animation destination");
                continue;
            }

            RadientAnimationDestinationDesc DestinationDesc{};
            DestinationDesc.pDestination = pDestination;
            DestinationDesc.pMappings    = SkinMapping.JointMappings.data();
            DestinationDesc.MappingCount = static_cast<Uint32>(SkinMapping.JointMappings.size());

            RegisterAnimationDestination(
                *ImportedAnimation.pClip,
                DestinationDesc,
                Entities.data(),
                static_cast<Uint32>(Entities.size()),
                "an instantiated skeleton pose",
                Registry);
        }

        for (Uint32 ClipTargetIndex = 0; ClipTargetIndex < ClipDesc.TargetCount; ++ClipTargetIndex)
        {
            const RadientAnimationTargetDesc& Target = ClipDesc.pTargets[ClipTargetIndex];
            if (Target.Schema != RadientMorphWeightsAnimationSchemaID)
                continue;

            if (Target.Object >= MorphAnimationDestinations.size())
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because a morph target references an invalid source node");
                continue;
            }

            const Uint32 NodeIndex = static_cast<Uint32>(Target.Object);
            for (const MorphAnimationDestinationInstance& Instance :
                 MorphAnimationDestinations[NodeIndex])
            {
                if (Instance.pWeights == nullptr || Instance.Entity == InvalidRadientEntityID)
                    continue;

                RefCntAutoPtr<IRadientAnimationDestination> pDestination;
                Instance.pWeights->QueryInterface(
                    IID_RadientAnimationDestination, pDestination.GetAddressOfEmpty());
                if (pDestination == nullptr)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' because the morph weights do not expose an animation destination");
                    continue;
                }

                const RadientAnimationDestinationMappingDesc WeightMapping{
                    ClipTargetIndex,
                    0,
                };
                RadientAnimationDestinationDesc DestinationDesc{};
                DestinationDesc.pDestination = pDestination;
                DestinationDesc.pMappings    = &WeightMapping;
                DestinationDesc.MappingCount = 1;

                RegisterAnimationDestination(
                    *ImportedAnimation.pClip,
                    DestinationDesc,
                    &Instance.Entity,
                    1,
                    "instantiated morph weights",
                    Registry);
            }
        }

        std::vector<bool> HasScenePropertyChannel(ClipDesc.TargetCount, false);
        for (Uint32 ChannelIndex = 0; ChannelIndex < ClipDesc.ChannelCount; ++ChannelIndex)
        {
            const RadientAnimationChannelDesc& Channel = ClipDesc.pChannels[ChannelIndex];
            const RadientAnimationTargetDesc&  Target  = ClipDesc.pTargets[Channel.TargetIndex];
            if (Target.Schema == RadientLightAnimationSchemaID)
            {
                HasScenePropertyChannel[Channel.TargetIndex] = true;
            }
            else if (Target.Schema == RadientNodeAnimationSchemaID)
            {
                switch (Channel.Property)
                {
                    case RadientNodeTranslationProperty:
                    case RadientNodeRotationProperty:
                    case RadientNodeScaleProperty:
                    case RadientNodeVisibilityProperty:
                        HasScenePropertyChannel[Channel.TargetIndex] = true;
                        break;

                    default:
                        break;
                }
            }
        }

        std::vector<RadientAnimationDestinationMappingDesc> NodeMappings;
        std::vector<RadientAnimationDestinationMappingDesc> LightMappings;
        NodeMappings.reserve(ClipDesc.TargetCount);
        LightMappings.reserve(ClipDesc.TargetCount);
        bool       NodeMappingOverflow  = false;
        bool       LightMappingOverflow = false;
        const auto AppendEntityMappings =
            [](const std::vector<RadientEntityID>&                  Entities,
               Uint32                                               ClipTargetIndex,
               std::vector<RadientAnimationDestinationMappingDesc>& Mappings,
               bool&                                                MappingOverflow) {
                if (MappingOverflow)
                    return;

                for (RadientEntityID Entity : Entities)
                {
                    if (Entity == InvalidRadientEntityID)
                        continue;
                    if (Mappings.size() >= std::numeric_limits<Uint32>::max())
                    {
                        MappingOverflow = true;
                        return;
                    }

                    RadientAnimationDestinationMappingDesc& Mapping = Mappings.emplace_back();
                    Mapping.ClipTargetIndex                         = ClipTargetIndex;
                    Mapping.DestinationElement                      = Entity;
                }
            };

        for (Uint32 ClipTargetIndex = 0; ClipTargetIndex < ClipDesc.TargetCount; ++ClipTargetIndex)
        {
            const RadientAnimationTargetDesc& Target = ClipDesc.pTargets[ClipTargetIndex];
            if (!HasScenePropertyChannel[ClipTargetIndex])
                continue;

            if (Target.Schema == RadientNodeAnimationSchemaID)
            {
                if (Target.Object >= NodeEntities.size())
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' target because it references an invalid source node");
                    continue;
                }

                AppendEntityMappings(
                    NodeEntities[static_cast<size_t>(Target.Object)],
                    ClipTargetIndex,
                    NodeMappings,
                    NodeMappingOverflow);
            }
            else if (Target.Schema == RadientLightAnimationSchemaID)
            {
                if (Target.Object >= std::numeric_limits<Uint32>::max())
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' target because it references an invalid source light");
                    continue;
                }

                const auto LightIt = LightEntities.find(static_cast<Uint32>(Target.Object));
                if (LightIt == LightEntities.end())
                    continue;

                AppendEntityMappings(
                    LightIt->second,
                    ClipTargetIndex,
                    LightMappings,
                    LightMappingOverflow);
            }
        }

        const auto RegisterSceneMappings =
            [&](const std::vector<RadientAnimationDestinationMappingDesc>& Mappings,
                bool                                                       MappingOverflow,
                const char*                                                DestinationDescription) {
                if (MappingOverflow)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' ", DestinationDescription,
                                        " binding because it contains too many mappings");
                    return;
                }
                if (Mappings.empty())
                    return;
                if (pSceneDestination == nullptr)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' ", DestinationDescription,
                                        " binding because the scene writer does not expose an animation destination");
                    return;
                }

                RadientAnimationDestinationDesc DestinationDesc{};
                DestinationDesc.pDestination = pSceneDestination;
                DestinationDesc.pMappings    = Mappings.data();
                DestinationDesc.MappingCount = static_cast<Uint32>(Mappings.size());

                RegisterAnimationDestination(
                    *ImportedAnimation.pClip,
                    DestinationDesc,
                    &SceneRootEntity,
                    1,
                    DestinationDescription,
                    Registry);
            };

        // Keep independently mutable component families in separate bindings. A
        // deleted light can then invalidate its binding without suppressing node
        // transforms or visibility from the same clip.
        RegisterSceneMappings(NodeMappings, NodeMappingOverflow, "instantiated node properties");
        RegisterSceneMappings(LightMappings, LightMappingOverflow, "instantiated light properties");
    }
}

Uint32 GetDefaultSceneIndex(const GLTF::Model& Model)
{
    return Model.DefaultSceneId >= 0 && static_cast<size_t>(Model.DefaultSceneId) < Model.Scenes.size() ?
        static_cast<Uint32>(Model.DefaultSceneId) :
        0;
}

RefCntAutoPtr<IRadientMeshAsset> GetRadientMeshAsset(const GLTF::Mesh& Mesh)
{
    return RefCntAutoPtr<IRadientMeshAsset>{Mesh.pUserData.RawPtr(), IID_RadientMeshAsset};
}

Uint32 GetDefaultSceneIndex(const RadientImport::ImportedDocument& Scene)
{
    return Scene.DefaultSceneId < Scene.Scenes.size() ? Scene.DefaultSceneId : 0u;
}

RADIENT_STATUS ResolveSceneIndex(const RadientImport::ImportedDocument& Scene,
                                 Uint32                                 RequestedSceneIndex,
                                 Uint32&                                SceneIndex)
{
    if (RequestedSceneIndex == InvalidRadientSceneIndex)
    {
        SceneIndex = GetDefaultSceneIndex(Scene);
        return RADIENT_STATUS_OK;
    }

    if (RequestedSceneIndex >= Scene.Scenes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    SceneIndex = RequestedSceneIndex;
    return RADIENT_STATUS_OK;
}

} // namespace

namespace RadientGLTFConverter
{

MeshVertexSourceResult CreateMeshVertexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                              const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                              const std::shared_ptr<const GLTF::Document>& pDocument)
{
    if (pDocument == nullptr)
    {
        MeshVertexSourceResult Result;
        Result.Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return Result;
    }

    const int* pPositionAccessor = GltfPrimitive.GetAttribute(GLTF::PositionAttributeName);
    if (pPositionAccessor == nullptr)
        return {};

    const auto PositionData = GLTF::GetGltfDataInfo(GltfModel, *pPositionAccessor);
    if (PositionData.pData == nullptr ||
        PositionData.ByteStride <= 0 ||
        PositionData.Count == 0 ||
        PositionData.Count > (std::numeric_limits<Uint32>::max)())
    {
        return {};
    }

    std::vector<RadientMeshVertexSource::SourceAttribute> SourceAttributes;
    std::vector<RefCntAutoPtr<IRadientDataBlob>>          SourceBlobs;
    SourceAttributes.reserve(GLTF::DefaultVertexAttributes.size());
    SourceBlobs.reserve(GLTF::DefaultVertexAttributes.size());

    const Uint32 VertexCount = static_cast<Uint32>(PositionData.Count);

    for (size_t AttribIndex = 0; AttribIndex < GLTF::DefaultVertexAttributes.size(); ++AttribIndex)
    {
        const GLTF::VertexAttributeDesc& DstAttrib = GLTF::DefaultVertexAttributes[AttribIndex];
        const int*                       pAccessor = GltfPrimitive.GetAttribute(DstAttrib.Name);
        if (pAccessor == nullptr)
            continue;

        const auto GltfData = GLTF::GetGltfDataInfo(GltfModel, *pAccessor);
        if (GltfData.pData == nullptr ||
            GltfData.ByteStride <= 0 ||
            GltfData.Count != VertexCount)
        {
            return {};
        }

        const VALUE_TYPE ComponentType = GltfData.Accessor.GetComponentType();
        const int        NumComponents = GltfData.Accessor.GetNumComponents();
        const Uint32     ComponentSize = GetValueSize(ComponentType);
        if (ComponentSize == 0 || NumComponents < 1 || NumComponents > 4)
            return {};

        const Uint64 ElementSize = Uint64{ComponentSize} * static_cast<Uint32>(NumComponents);
        Uint64       DataSize    = 0;
        if (static_cast<Uint64>(GltfData.ByteStride) < ElementSize ||
            !CheckedMultiply(VertexCount - 1u, static_cast<Uint64>(GltfData.ByteStride), DataSize) ||
            DataSize > (std::numeric_limits<Uint64>::max)() - ElementSize)
        {
            return {};
        }
        DataSize += ElementSize;

        const auto View   = GltfModel.GetBufferView(GltfData.Accessor.GetBufferViewId());
        const auto Buffer = GltfModel.GetBuffer(View.GetBufferId());
        if (!RadientValidation::IsValidSubrange(GltfData.Accessor.GetByteOffset(), DataSize, View.View.byteLength) ||
            !RadientValidation::IsValidSubrange(View.GetByteOffset(), View.View.byteLength, Buffer.Buffer.data.size()))
        {
            return {};
        }

        auto                      pDocumentOwner = std::make_unique<std::shared_ptr<const GLTF::Document>>(pDocument);
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.pData     = GltfData.pData;
        BlobCI.Size      = DataSize;
        BlobCI.pUserData = pDocumentOwner.get();
        BlobCI.OnDestroy = [](void* pUserData) {
            delete static_cast<std::shared_ptr<const GLTF::Document>*>(pUserData);
        };
        RefCntAutoPtr<IRadientDataBlob> pBlob;
        if (CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob) != RADIENT_STATUS_OK)
            return {};
        pDocumentOwner.release();

        RadientMeshVertexSource::SourceAttribute& SrcAttrib = SourceAttributes.emplace_back();
        SrcAttrib.Name                                      = DstAttrib.Name;
        SrcAttrib.Type                                      = ComponentType;
        SrcAttrib.NumComponents                             = static_cast<Uint8>(NumComponents);
        SrcAttrib.IsNormalized                              = GltfData.Accessor.IsNormalized();
        SrcAttrib.pDataBlob                                 = pBlob;
        SrcAttrib.Stride                                    = static_cast<Uint32>(GltfData.ByteStride);
        SourceBlobs.push_back(std::move(pBlob));
    }

    if (SourceAttributes.empty())
        return {};

    float3 BBMin;
    float3 BBMax;
    if (!GLTF::ComputePrimitiveBoundingBox(PositionData, BBMin, BBMax))
        return {};

    RadientMeshVertexSource::CreateInfo VertexCI;
    VertexCI.pAttributes    = SourceAttributes.data();
    VertexCI.AttributeCount = static_cast<Uint32>(SourceAttributes.size());
    VertexCI.VertexCount    = VertexCount;

    std::unique_ptr<RadientMeshVertexSource> pSource = std::make_unique<RadientMeshVertexSource>(VertexCI);
    if (pSource == nullptr || pSource->GetStatus() != RADIENT_STATUS_OK)
        return {};

    MeshVertexSourceResult Result;
    Result.Status  = RADIENT_STATUS_OK;
    Result.pSource = std::move(pSource);
    Result.BBMin   = BBMin;
    Result.BBMax   = BBMax;
    return Result;
}

MeshIndexSourceResult CreateMeshIndexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                            const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                            const std::shared_ptr<const GLTF::Document>& pDocument,
                                            Uint32                                       VertexCount)
{
    if (pDocument == nullptr)
    {
        MeshIndexSourceResult Result;
        Result.Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return Result;
    }

    std::shared_ptr<MeshSourceDataOwner> pOwner = std::make_shared<MeshSourceDataOwner>(pDocument);
    RadientMeshIndexSource::CreateInfo   IndexCI;
    Uint32                               IndexCount    = 0;
    const int                            IndexAccessor = GltfPrimitive.GetIndicesId();
    if (IndexAccessor >= 0)
    {
        const auto GltfIndexData = GLTF::GetGltfDataInfo(GltfModel, IndexAccessor);
        if (GltfIndexData.pData == nullptr ||
            GltfIndexData.ByteStride <= 0)
        {
            return {};
        }

        const VALUE_TYPE IndexType       = GltfIndexData.Accessor.GetComponentType();
        const Uint32     IndexValueSize  = GetValueSize(IndexType);
        const Uint32     IndexByteStride = static_cast<Uint32>(GltfIndexData.ByteStride);
        if (!RadientMeshIndexSource::IsSupportedIndexType(IndexType) ||
            IndexValueSize == 0 ||
            IndexByteStride != IndexValueSize)
        {
            return {};
        }

        IndexCI.pData = GltfIndexData.pData;
        IndexCI.Type  = IndexType;
        IndexCount    = static_cast<Uint32>(GltfIndexData.Count);
    }
    else
    {
        pOwner->GeneratedIndices.resize(VertexCount);
        for (Uint32 Index = 0; Index < VertexCount; ++Index)
            pOwner->GeneratedIndices[Index] = Index;

        IndexCI.pData = pOwner->GeneratedIndices.data();
        IndexCI.Type  = VT_UINT32;
        IndexCount    = VertexCount;
    }

    if (IndexCount == 0)
        return {};

    IndexCI.IndexCount       = IndexCount;
    IndexCI.pSourceDataOwner = pOwner;

    std::unique_ptr<RadientMeshIndexSource> pSource = std::make_unique<RadientMeshIndexSource>(IndexCI);
    if (pSource == nullptr || pSource->GetStatus() != RADIENT_STATUS_OK)
        return {};

    MeshIndexSourceResult Result;
    Result.Status  = RADIENT_STATUS_OK;
    Result.pSource = std::move(pSource);
    return Result;
}

RADIENT_STATUS ExtractSceneGraph(const GLTF::Model&               GLTFModel,
                                 RadientImport::ImportedDocument& Scene,
                                 IRadientAssetManager*            pAssetManager)
{
    std::vector<SkinImportContext> SkinContexts;
    RADIENT_STATUS                 Status = ExtractSkins(GLTFModel, pAssetManager, Scene, SkinContexts);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = ExtractAnimations(GLTFModel, pAssetManager, SkinContexts, Scene);
    if (RADIENT_FAILED(Status))
        return Status;

    Scene.DefaultSceneId = GetDefaultSceneIndex(GLTFModel);

    Scene.Nodes.clear();
    Scene.Nodes.resize(GLTFModel.Nodes.size());
    for (const GLTF::Node& SrcNode : GLTFModel.Nodes)
    {
        if (SrcNode.Index < 0 || static_cast<size_t>(SrcNode.Index) >= Scene.Nodes.size())
            return RADIENT_STATUS_INVALID_DATA;

        RadientImport::ImportedNode& DstNode = Scene.Nodes[static_cast<size_t>(SrcNode.Index)];
        DstNode.Name                         = SrcNode.Name;
        DstNode.Transform                    = ToRadientTransform(SrcNode);
        DstNode.Visible                      = SrcNode.Visible ? True : False;

        if (SrcNode.pMesh != nullptr)
        {
            DstNode.pMesh = GetRadientMeshAsset(*SrcNode.pMesh);
            if (DstNode.pMesh == nullptr)
                return RADIENT_STATUS_INVALID_DATA;

            DstNode.MorphWeights = SrcNode.Weights;
        }

        if (SrcNode.pSkin != nullptr)
        {
            if (SrcNode.pMesh == nullptr)
            {
                LOG_ERROR_MESSAGE("GLTF node ", SrcNode.Index, " references a skin without a mesh");
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstNode.SkinIndex = FindSkinIndex(GLTFModel, SrcNode.pSkin);
            if (DstNode.SkinIndex == RadientImport::InvalidImportedSkinIndex)
            {
                LOG_ERROR_MESSAGE("GLTF node ", SrcNode.Index, " references an invalid skin");
                return RADIENT_STATUS_INVALID_DATA;
            }
        }

        if (SrcNode.pCamera != nullptr)
        {
            DstNode.Camera = ToRadientCamera(*SrcNode.pCamera);
        }

        if (SrcNode.pLight != nullptr)
        {
            Uint32 LightIndex;
            if (GetLightIndex(GLTFModel, SrcNode.pLight, LightIndex))
            {
                DstNode.Light      = ToRadientLight(*SrcNode.pLight);
                DstNode.LightIndex = LightIndex;
            }
            else
            {
                LOG_WARNING_MESSAGE("GLTF node ", SrcNode.Index,
                                    " references an invalid light; ignoring the light");
            }
        }

        DstNode.Children.reserve(SrcNode.Children.size());
        for (const GLTF::Node* pChild : SrcNode.Children)
        {
            if (pChild == nullptr ||
                pChild->Index < 0 ||
                static_cast<size_t>(pChild->Index) >= Scene.Nodes.size())
            {
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstNode.Children.push_back(static_cast<Uint32>(pChild->Index));
        }
    }

    Scene.Scenes.clear();
    Scene.Scenes.resize(GLTFModel.Scenes.size());
    for (size_t SceneIndex = 0; SceneIndex < GLTFModel.Scenes.size(); ++SceneIndex)
    {
        const GLTF::Scene&            SrcScene = GLTFModel.Scenes[SceneIndex];
        RadientImport::ImportedScene& DstScene = Scene.Scenes[SceneIndex];
        DstScene.Name                          = SrcScene.Name;
        DstScene.RootNodes.reserve(SrcScene.RootNodes.size());

        for (const GLTF::Node* pRootNode : SrcScene.RootNodes)
        {
            if (pRootNode == nullptr ||
                pRootNode->Index < 0 ||
                static_cast<size_t>(pRootNode->Index) >= Scene.Nodes.size())
            {
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstScene.RootNodes.push_back(static_cast<Uint32>(pRootNode->Index));
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS InstantiateSceneGraph(const RadientImport::ImportedDocument& Scene,
                                     Uint32                                 SceneIndex,
                                     IRadientSceneWriter&                   Writer,
                                     RadientEntityID                        RootEntity,
                                     IRadientAnimationRegistry*             pAnimationRegistry)
{
    Uint32         ResolvedSceneIndex = 0;
    RADIENT_STATUS Status             = ResolveSceneIndex(Scene, SceneIndex, ResolvedSceneIndex);
    if (RADIENT_FAILED(Status))
        return Status;

    if (ResolvedSceneIndex < Scene.Scenes.size())
    {
        std::vector<RefCntAutoPtr<IRadientSkeletonPose>> SkinPoses(Scene.Skins.size());
        NodeEntityLists                                  NodeEntities;
        LightEntityLists                                 LightEntities;
        SkinEntityLists                                  SkinEntities;
        MorphAnimationDestinationLists                   MorphAnimationDestinations;
        RefCntAutoPtr<IRadientAnimationDestination>      pSceneAnimationDestination;
        if (pAnimationRegistry != nullptr)
        {
            NodeEntities.resize(Scene.Nodes.size());
            SkinEntities.resize(Scene.Skins.size());
            MorphAnimationDestinations.resize(Scene.Nodes.size());
            Writer.QueryInterface(
                IID_RadientAnimationDestination,
                pSceneAnimationDestination.GetAddressOfEmpty());
        }

        for (Uint32 NodeIndex : Scene.Scenes[ResolvedSceneIndex].RootNodes)
        {
            Status = CreateNode(Writer, Scene, NodeIndex, RootEntity, RadientMatrix4x4{}, SkinPoses,
                                pAnimationRegistry != nullptr ? &NodeEntities : nullptr,
                                pAnimationRegistry != nullptr ? &LightEntities : nullptr,
                                pAnimationRegistry != nullptr ? &SkinEntities : nullptr,
                                pAnimationRegistry != nullptr ? &MorphAnimationDestinations : nullptr);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        if (pAnimationRegistry != nullptr)
        {
            RegisterSceneAnimations(
                Scene, SkinPoses, pSceneAnimationDestination, NodeEntities, RootEntity,
                LightEntities, SkinEntities, MorphAnimationDestinations, *pAnimationRegistry);
        }
    }

    return RADIENT_STATUS_OK;
}

} // namespace RadientGLTFConverter

} // namespace Diligent
