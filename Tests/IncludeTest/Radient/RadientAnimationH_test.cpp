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

#include "Radient/interface/RadientAnimation.h"

#include <type_traits>

namespace
{

using namespace Diligent;

static_assert(sizeof(RADIENT_ANIMATION_VALUE_TYPE) == sizeof(Uint8), "Unexpected RADIENT_ANIMATION_VALUE_TYPE size");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN == 0, "Unexpected UNKNOWN animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_BOOL == 1, "Unexpected BOOL animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_INT == 2, "Unexpected INT animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_INT2 == 3, "Unexpected INT2 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_INT3 == 4, "Unexpected INT3 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_INT4 == 5, "Unexpected INT4 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_UINT == 6, "Unexpected UINT animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_UINT2 == 7, "Unexpected UINT2 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_UINT3 == 8, "Unexpected UINT3 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_UINT4 == 9, "Unexpected UINT4 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT == 10, "Unexpected FLOAT animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT2 == 11, "Unexpected FLOAT2 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT3 == 12, "Unexpected FLOAT3 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT4 == 13, "Unexpected FLOAT4 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2 == 14, "Unexpected FLOAT2X2 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3 == 15, "Unexpected FLOAT3X3 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4 == 16, "Unexpected FLOAT4X4 animation value type");
static_assert(RADIENT_ANIMATION_VALUE_TYPE_COUNT == 17, "Unexpected animation value type count");
static_assert(sizeof(RADIENT_ANIMATION_VALUE_SEMANTIC) == sizeof(Uint8), "Unexpected RADIENT_ANIMATION_VALUE_SEMANTIC size");
static_assert(RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN == 0, "Unexpected UNKNOWN animation value semantic");
static_assert(RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE == 1, "Unexpected COMPONENT_WISE animation value semantic");
static_assert(RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION == 2, "Unexpected NORMALIZED_QUATERNION animation value semantic");
static_assert(RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT == 3, "Unexpected animation value semantic count");
static_assert(std::is_same<RadientAnimationSchemaID, INTERFACE_ID>::value, "RadientAnimationSchemaID must use INTERFACE_ID storage");
static_assert(sizeof(RadientAnimationSchemaID) == sizeof(INTERFACE_ID), "Unexpected RadientAnimationSchemaID size");
static_assert(std::is_same<RadientAnimationPropertyID, Uint64>::value, "RadientAnimationPropertyID must use Uint64 storage");
static_assert(sizeof(RadientAnimationPropertyID) == sizeof(Uint64), "Unexpected RadientAnimationPropertyID size");
static_assert(std::is_same<RadientAnimationObjectID, Uint64>::value, "RadientAnimationObjectID must use Uint64 storage");
static_assert(sizeof(RadientAnimationObjectID) == sizeof(Uint64), "Unexpected RadientAnimationObjectID size");
static_assert(std::is_same<RadientAnimationDestinationElement, Uint64>::value, "RadientAnimationDestinationElement must use Uint64 storage");
static_assert(sizeof(RadientAnimationDestinationElement) == sizeof(Uint64), "Unexpected RadientAnimationDestinationElement size");
static_assert(InvalidRadientAnimationPropertyID == 0, "Unexpected invalid animation property ID");
static_assert(InvalidRadientAnimationObject == static_cast<Uint64>(~0ull), "Unexpected invalid animation object ID");
static_assert(InvalidRadientAnimationTargetIndex == static_cast<Uint32>(~0u), "Unexpected invalid animation target index");
static_assert(InvalidRadientAnimationSamplerIndex == static_cast<Uint32>(~0u), "Unexpected invalid animation sampler index");
static_assert(InvalidRadientAnimationDestinationElement == static_cast<Uint64>(~0ull), "Unexpected invalid animation destination element");
static_assert(RadientNodeAnimationSchemaID.Data1 == 0xe4add320 &&
                  RadientNodeAnimationSchemaID.Data2 == 0xeeca &&
                  RadientNodeAnimationSchemaID.Data3 == 0x439f &&
                  RadientNodeAnimationSchemaID.Data4[0] == 0xa6 &&
                  RadientNodeAnimationSchemaID.Data4[1] == 0xc3 &&
                  RadientNodeAnimationSchemaID.Data4[2] == 0x1d &&
                  RadientNodeAnimationSchemaID.Data4[3] == 0x8a &&
                  RadientNodeAnimationSchemaID.Data4[4] == 0x25 &&
                  RadientNodeAnimationSchemaID.Data4[5] == 0xae &&
                  RadientNodeAnimationSchemaID.Data4[6] == 0xfb &&
                  RadientNodeAnimationSchemaID.Data4[7] == 0xc3,
              "Unexpected node-animation schema ID");
static_assert(RadientNodeTranslationProperty == 1, "Unexpected node-translation property ID");
static_assert(RadientNodeRotationProperty == 2, "Unexpected node-rotation property ID");
static_assert(RadientNodeScaleProperty == 3, "Unexpected node-scale property ID");
static_assert(RadientMorphWeightsAnimationSchemaID.Data1 == 0x8e3a3b5b &&
                  RadientMorphWeightsAnimationSchemaID.Data2 == 0x2267 &&
                  RadientMorphWeightsAnimationSchemaID.Data3 == 0x4e06 &&
                  RadientMorphWeightsAnimationSchemaID.Data4[0] == 0xb9 &&
                  RadientMorphWeightsAnimationSchemaID.Data4[1] == 0x4a &&
                  RadientMorphWeightsAnimationSchemaID.Data4[2] == 0x31 &&
                  RadientMorphWeightsAnimationSchemaID.Data4[3] == 0x67 &&
                  RadientMorphWeightsAnimationSchemaID.Data4[4] == 0x46 &&
                  RadientMorphWeightsAnimationSchemaID.Data4[5] == 0xab &&
                  RadientMorphWeightsAnimationSchemaID.Data4[6] == 0x9f &&
                  RadientMorphWeightsAnimationSchemaID.Data4[7] == 0x61,
              "Unexpected morph-weights animation schema ID");
static_assert(RadientMorphWeightsProperty == 1, "Unexpected morph-weights property ID");
static_assert(std::is_standard_layout<RadientAnimationValueDesc>::value, "RadientAnimationValueDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationValueDesc>::value, "RadientAnimationValueDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationSamplerDesc>::value, "RadientAnimationSamplerDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationSamplerDesc>::value, "RadientAnimationSamplerDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationTargetDesc>::value, "RadientAnimationTargetDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationTargetDesc>::value, "RadientAnimationTargetDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationChannelDesc>::value, "RadientAnimationChannelDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationChannelDesc>::value, "RadientAnimationChannelDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationClipDesc>::value, "RadientAnimationClipDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationClipDesc>::value, "RadientAnimationClipDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationPropertyBindingDesc>::value, "RadientAnimationPropertyBindingDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationPropertyBindingDesc>::value, "RadientAnimationPropertyBindingDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationResolvedPropertyDesc>::value, "RadientAnimationResolvedPropertyDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationResolvedPropertyDesc>::value, "RadientAnimationResolvedPropertyDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationPropertyUpdateDesc>::value, "RadientAnimationPropertyUpdateDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationPropertyUpdateDesc>::value, "RadientAnimationPropertyUpdateDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationApplyInfo>::value, "RadientAnimationApplyInfo must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationApplyInfo>::value, "RadientAnimationApplyInfo must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationDestinationMappingDesc>::value, "RadientAnimationDestinationMappingDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationDestinationMappingDesc>::value, "RadientAnimationDestinationMappingDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationDestinationDesc>::value, "RadientAnimationDestinationDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationDestinationDesc>::value, "RadientAnimationDestinationDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationBindingDesc>::value, "RadientAnimationBindingDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationBindingDesc>::value, "RadientAnimationBindingDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationEvaluateInfo>::value, "RadientAnimationEvaluateInfo must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationEvaluateInfo>::value, "RadientAnimationEvaluateInfo must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationTarget>::value, "RadientAnimationTarget must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationTarget>::value, "RadientAnimationTarget must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationRegistryEntry>::value, "RadientAnimationRegistryEntry must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationRegistryEntry>::value, "RadientAnimationRegistryEntry must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationRegistryState>::value, "RadientAnimationRegistryState must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationRegistryState>::value, "RadientAnimationRegistryState must be trivially copyable");

constexpr RadientAnimationValueDesc DefaultValue{};
static_assert(DefaultValue.Type == RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN, "Unexpected RadientAnimationValueDesc type default value");
static_assert(DefaultValue.ArraySize == 1, "Unexpected RadientAnimationValueDesc array-size default value");

constexpr RadientAnimationSamplerDesc DefaultSampler{};
static_assert(DefaultSampler.Value.Type == RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN, "Unexpected RadientAnimationSamplerDesc value type default value");
static_assert(DefaultSampler.Value.ArraySize == 1, "Unexpected RadientAnimationSamplerDesc array-size default value");
static_assert(DefaultSampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR, "Unexpected RadientAnimationSamplerDesc interpolation default value");
static_assert(DefaultSampler.pTimes == nullptr, "Unexpected RadientAnimationSamplerDesc time pointer default value");
static_assert(DefaultSampler.pValues == nullptr, "Unexpected RadientAnimationSamplerDesc value pointer default value");
static_assert(DefaultSampler.ValueDataSize == 0, "Unexpected RadientAnimationSamplerDesc value-data size default value");
static_assert(DefaultSampler.KeyframeCount == 0, "Unexpected RadientAnimationSamplerDesc keyframe count default value");

constexpr RadientAnimationTargetDesc DefaultClipTarget{};
static_assert((DefaultClipTarget.Schema.Data1 == InvalidRadientAnimationSchemaID.Data1 &&
               DefaultClipTarget.Schema.Data2 == InvalidRadientAnimationSchemaID.Data2 &&
               DefaultClipTarget.Schema.Data3 == InvalidRadientAnimationSchemaID.Data3 &&
               DefaultClipTarget.Schema.Data4[0] == InvalidRadientAnimationSchemaID.Data4[0] &&
               DefaultClipTarget.Schema.Data4[1] == InvalidRadientAnimationSchemaID.Data4[1] &&
               DefaultClipTarget.Schema.Data4[2] == InvalidRadientAnimationSchemaID.Data4[2] &&
               DefaultClipTarget.Schema.Data4[3] == InvalidRadientAnimationSchemaID.Data4[3] &&
               DefaultClipTarget.Schema.Data4[4] == InvalidRadientAnimationSchemaID.Data4[4] &&
               DefaultClipTarget.Schema.Data4[5] == InvalidRadientAnimationSchemaID.Data4[5] &&
               DefaultClipTarget.Schema.Data4[6] == InvalidRadientAnimationSchemaID.Data4[6] &&
               DefaultClipTarget.Schema.Data4[7] == InvalidRadientAnimationSchemaID.Data4[7]),
              "Unexpected RadientAnimationTargetDesc schema default value");
static_assert(DefaultClipTarget.Object == InvalidRadientAnimationObject, "Unexpected RadientAnimationTargetDesc object default value");
static_assert(DefaultClipTarget.Name == nullptr, "Unexpected RadientAnimationTargetDesc name default value");

constexpr RadientAnimationChannelDesc DefaultChannel{};
static_assert(DefaultChannel.TargetIndex == InvalidRadientAnimationTargetIndex, "Unexpected RadientAnimationChannelDesc target index default value");
static_assert(DefaultChannel.Property == InvalidRadientAnimationPropertyID, "Unexpected RadientAnimationChannelDesc property default value");
static_assert(DefaultChannel.FirstArrayElement == 0, "Unexpected RadientAnimationChannelDesc first array element default value");
static_assert(DefaultChannel.SamplerIndex == InvalidRadientAnimationSamplerIndex, "Unexpected RadientAnimationChannelDesc sampler index default value");

constexpr RadientAnimationClipDesc DefaultClip{};
static_assert(DefaultClip.Name == nullptr, "Unexpected RadientAnimationClipDesc name default value");
static_assert(DefaultClip.Duration == 0.f, "Unexpected RadientAnimationClipDesc duration default value");
static_assert(DefaultClip.pTargets == nullptr, "Unexpected RadientAnimationClipDesc target pointer default value");
static_assert(DefaultClip.TargetCount == 0, "Unexpected RadientAnimationClipDesc target count default value");
static_assert(DefaultClip.pSamplers == nullptr, "Unexpected RadientAnimationClipDesc sampler pointer default value");
static_assert(DefaultClip.SamplerCount == 0, "Unexpected RadientAnimationClipDesc sampler count default value");
static_assert(DefaultClip.pChannels == nullptr, "Unexpected RadientAnimationClipDesc channel pointer default value");
static_assert(DefaultClip.ChannelCount == 0, "Unexpected RadientAnimationClipDesc channel count default value");

constexpr RadientAnimationPropertyBindingDesc DefaultPropertyBinding{};
static_assert((DefaultPropertyBinding.Schema.Data1 == InvalidRadientAnimationSchemaID.Data1 &&
               DefaultPropertyBinding.Schema.Data2 == InvalidRadientAnimationSchemaID.Data2 &&
               DefaultPropertyBinding.Schema.Data3 == InvalidRadientAnimationSchemaID.Data3 &&
               DefaultPropertyBinding.Schema.Data4[0] == InvalidRadientAnimationSchemaID.Data4[0] &&
               DefaultPropertyBinding.Schema.Data4[1] == InvalidRadientAnimationSchemaID.Data4[1] &&
               DefaultPropertyBinding.Schema.Data4[2] == InvalidRadientAnimationSchemaID.Data4[2] &&
               DefaultPropertyBinding.Schema.Data4[3] == InvalidRadientAnimationSchemaID.Data4[3] &&
               DefaultPropertyBinding.Schema.Data4[4] == InvalidRadientAnimationSchemaID.Data4[4] &&
               DefaultPropertyBinding.Schema.Data4[5] == InvalidRadientAnimationSchemaID.Data4[5] &&
               DefaultPropertyBinding.Schema.Data4[6] == InvalidRadientAnimationSchemaID.Data4[6] &&
               DefaultPropertyBinding.Schema.Data4[7] == InvalidRadientAnimationSchemaID.Data4[7]),
              "Unexpected RadientAnimationPropertyBindingDesc schema default value");
static_assert(DefaultPropertyBinding.DestinationElement == InvalidRadientAnimationDestinationElement, "Unexpected RadientAnimationPropertyBindingDesc destination element default value");
static_assert(DefaultPropertyBinding.Property == InvalidRadientAnimationPropertyID, "Unexpected RadientAnimationPropertyBindingDesc property default value");
static_assert(DefaultPropertyBinding.FirstArrayElement == 0, "Unexpected RadientAnimationPropertyBindingDesc first array element default value");
static_assert(DefaultPropertyBinding.Value.Type == RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN, "Unexpected RadientAnimationPropertyBindingDesc value type default value");
static_assert(DefaultPropertyBinding.Value.ArraySize == 1, "Unexpected RadientAnimationPropertyBindingDesc array-size default value");

constexpr RadientAnimationResolvedPropertyDesc DefaultResolvedProperty{};
static_assert(DefaultResolvedProperty.Semantic == RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN, "Unexpected RadientAnimationResolvedPropertyDesc semantic default value");

constexpr RadientAnimationPropertyUpdateDesc DefaultPropertyUpdate{};
static_assert(DefaultPropertyUpdate.pValue == nullptr, "Unexpected RadientAnimationPropertyUpdateDesc value pointer default value");
static_assert(DefaultPropertyUpdate.ValueDataSize == 0, "Unexpected RadientAnimationPropertyUpdateDesc value-data size default value");

constexpr RadientAnimationApplyInfo DefaultApplyInfo{};
static_assert(DefaultApplyInfo.pUpdates == nullptr, "Unexpected RadientAnimationApplyInfo update pointer default value");
static_assert(DefaultApplyInfo.UpdateCount == 0, "Unexpected RadientAnimationApplyInfo update count default value");
static_assert(DefaultApplyInfo.UpdateDerivedState == True, "Unexpected RadientAnimationApplyInfo derived-state default value");

constexpr RadientAnimationDestinationMappingDesc DefaultDestinationMapping{};
static_assert(DefaultDestinationMapping.ClipTargetIndex == InvalidRadientAnimationTargetIndex, "Unexpected RadientAnimationDestinationMappingDesc target index default value");
static_assert(DefaultDestinationMapping.DestinationElement == InvalidRadientAnimationDestinationElement, "Unexpected RadientAnimationDestinationMappingDesc destination element default value");

constexpr RadientAnimationDestinationDesc DefaultDestination{};
static_assert(DefaultDestination.pDestination == nullptr, "Unexpected RadientAnimationDestinationDesc destination pointer default value");
static_assert(DefaultDestination.pMappings == nullptr, "Unexpected RadientAnimationDestinationDesc mapping pointer default value");
static_assert(DefaultDestination.MappingCount == 0, "Unexpected RadientAnimationDestinationDesc mapping count default value");

constexpr RadientAnimationBindingDesc DefaultBinding{};
static_assert(DefaultBinding.pDestinations == nullptr, "Unexpected RadientAnimationBindingDesc destination pointer default value");
static_assert(DefaultBinding.DestinationCount == 0, "Unexpected RadientAnimationBindingDesc destination count default value");

constexpr RadientAnimationEvaluateInfo DefaultEvaluateInfo{};
static_assert(DefaultEvaluateInfo.Time == 0.0, "Unexpected RadientAnimationEvaluateInfo time default value");
static_assert(DefaultEvaluateInfo.UpdateDerivedState == True, "Unexpected RadientAnimationEvaluateInfo derived-state default value");

constexpr RadientAnimationTarget DefaultTarget{};
static_assert(DefaultTarget.pPose == nullptr, "Unexpected RadientAnimationTarget pose default value");

constexpr RadientAnimationRegistryEntry DefaultEntry{};
static_assert(DefaultEntry.pAnimation == nullptr, "Unexpected RadientAnimationRegistryEntry animation default value");
static_assert(DefaultEntry.pTargets == nullptr, "Unexpected RadientAnimationRegistryEntry targets default value");
static_assert(DefaultEntry.TargetCount == 0, "Unexpected RadientAnimationRegistryEntry target count default value");

constexpr RadientAnimationRegistryState DefaultState{};
static_assert(DefaultState.Revision == 0, "Unexpected RadientAnimationRegistryState revision default value");
static_assert(DefaultState.pEntries == nullptr, "Unexpected RadientAnimationRegistryState entries default value");
static_assert(DefaultState.EntryCount == 0, "Unexpected RadientAnimationRegistryState entry count default value");

} // namespace

void RadientAnimation_CPP_UseDestinationInterface(Diligent::IRadientAnimationDestination* pDestination)
{
    using namespace Diligent;

    RadientAnimationPropertyBindingDesc  Property{};
    RadientAnimationResolvedPropertyDesc ResolvedProperty{};
    IRadientAnimationDestinationBinding* pDestinationBinding = nullptr;

    const RADIENT_STATUS Status = pDestination->CreateBinding(&Property, 1, &ResolvedProperty, &pDestinationBinding);

    (void)Status;
}

void RadientAnimation_CPP_UseDestinationBindingInterface(Diligent::IRadientAnimationDestinationBinding* pDestinationBinding)
{
    using namespace Diligent;

    RadientAnimationPropertyUpdateDesc PropertyUpdate{};
    RadientAnimationApplyInfo          ApplyInfo{};

    ApplyInfo.pUpdates          = &PropertyUpdate;
    ApplyInfo.UpdateCount       = 1;
    const RADIENT_STATUS Status = pDestinationBinding->ApplyProperties(ApplyInfo);

    (void)Status;
}

void RadientAnimation_CPP_UseClipInterface(Diligent::IRadientAnimationClipAsset* pClip)
{
    using namespace Diligent;

    const RadientAnimationClipDesc& Desc = pClip->GetDesc();
    RadientAnimationBindingDesc     BindingDesc{};
    IRadientAnimationBinding*       pBinding = nullptr;
    RADIENT_STATUS                  Status   = pClip->CreateBinding(BindingDesc, &pBinding);

    (void)Desc;
    (void)Status;
}

void RadientAnimation_CPP_UseBindingInterface(Diligent::IRadientAnimationBinding* pBinding)
{
    using namespace Diligent;

    RadientAnimationEvaluateInfo Info{};
    (void)pBinding->GetClip();
    const RADIENT_STATUS Status = pBinding->Evaluate(Info);

    (void)Status;
}

void RadientAnimation_CPP_UseInterface(Diligent::IRadientAnimationRegistry*      pRegistry,
                                       Diligent::IRadientSkeletonAnimationAsset* pAnimation)
{
    using namespace Diligent;

    const RadientEntityID Entity = 1;
    (void)pRegistry->GetScene();
    RADIENT_STATUS Status = pRegistry->AddAnimatedEntities(pAnimation, &Entity, 1);
    Status                = pRegistry->RemoveAnimatedEntities(pAnimation, &Entity, 1);
    Status                = pRegistry->RemoveEntity(Entity);
    Status                = pRegistry->RemoveAnimation(pAnimation);
    (void)pRegistry->GetState();

    (void)Status;
}
