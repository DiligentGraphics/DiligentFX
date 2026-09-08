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
static_assert(std::is_same<RadientAnimationSchemaID, INTERFACE_ID>::value, "RadientAnimationSchemaID must use INTERFACE_ID storage");
static_assert(sizeof(RadientAnimationSchemaID) == sizeof(INTERFACE_ID), "Unexpected RadientAnimationSchemaID size");
static_assert(sizeof(RadientAnimationPropertyID) == sizeof(Uint64), "Unexpected RadientAnimationPropertyID size");
static_assert(sizeof(RadientAnimationObjectID) == sizeof(Uint64), "Unexpected RadientAnimationObjectID size");
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

void RadientAnimation_CPP_UseClipInterface(Diligent::IRadientAnimationClipAsset* pClip)
{
    using namespace Diligent;

    const RadientAnimationClipDesc& Desc = pClip->GetDesc();
    (void)Desc;
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
