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

#include "Animation/RadientAnimationBindingImpl.hpp"
#include "Animation/RadientAnimationValueType.hpp"
#include "Core/RadientValidation.hpp"
#include "Math/RadientMath.hpp"

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

using RadientValidation::CheckedMultiply;
using RadientValidation::IsAddressableArray;
using RadientValidation::IsAddressableSize;
using RadientValidation::IsSumRepresentable;

struct AnimationKeyInterval
{
    Uint32 StartKey = 0;
    Uint32 EndKey   = 0;
};

AnimationKeyInterval FindAnimationKeyInterval(const RadientAnimationSamplerDesc& Sampler,
                                              Float32                            Time) noexcept
{
    VERIFY_EXPR(Sampler.KeyframeCount != 0);
    if (Sampler.KeyframeCount == 1 || Time <= Sampler.pTimes[0])
        return {};

    const Uint32 LastKey = Sampler.KeyframeCount - 1;
    if (Time >= Sampler.pTimes[LastKey])
        return {LastKey, LastKey};

    const Float32* const pEndTime = std::upper_bound(Sampler.pTimes,
                                                     Sampler.pTimes + Sampler.KeyframeCount,
                                                     Time);

    const Uint32 EndKey = static_cast<Uint32>(pEndTime - Sampler.pTimes);
    return {EndKey - 1, EndKey};
}

struct AnimationSampleInterval
{
    Uint32  StartKey = 0;
    Uint32  EndKey   = 0;
    Float32 Factor   = 0.f;
    Float32 Duration = 0.f;
};

AnimationSampleInterval FindAnimationSampleInterval(const RadientAnimationSamplerDesc& Sampler,
                                                    Float32                            Time) noexcept
{
    const AnimationKeyInterval Keys = FindAnimationKeyInterval(Sampler, Time);
    if (Keys.StartKey == Keys.EndKey)
        return {Keys.StartKey, Keys.EndKey, 0.f, 0.f};

    const Float32 Duration = Sampler.pTimes[Keys.EndKey] - Sampler.pTimes[Keys.StartKey];
    const Float32 Factor   = (Time - Sampler.pTimes[Keys.StartKey]) / Duration;
    return {Keys.StartKey, Keys.EndKey, Factor, Duration};
}

const Uint8* GetAnimationKeyValue(const RadientAnimationSamplerDesc& Sampler,
                                  size_t                             SampleSize,
                                  Uint32                             KeyIndex) noexcept
{
    return static_cast<const Uint8*>(Sampler.pValues) + static_cast<size_t>(KeyIndex) * SampleSize;
}

const Uint8* GetCubicAnimationKeyValue(const RadientAnimationSamplerDesc& Sampler,
                                       size_t                             SampleSize,
                                       Uint32                             KeyIndex,
                                       Uint32                             CubicElement) noexcept
{
    const size_t ValueIndex = static_cast<size_t>(KeyIndex) * 3u + CubicElement;
    return static_cast<const Uint8*>(Sampler.pValues) + ValueIndex * SampleSize;
}

Float32 LoadAnimationFloat(const Uint8* pData, size_t ComponentIndex) noexcept
{
    Float32 Value = 0.f;
    std::memcpy(&Value, pData + ComponentIndex * sizeof(Value), sizeof(Value));
    return Value;
}

void StoreAnimationFloat(Uint8* pData, size_t ComponentIndex, Float32 Value) noexcept
{
    std::memcpy(pData + ComponentIndex * sizeof(Value), &Value, sizeof(Value));
}

template <typename ValueType>
ValueType LoadAnimationValue(const Uint8* pData) noexcept
{
    static_assert(std::is_trivially_copyable<ValueType>::value,
                  "Animation values must be trivially copyable");

    ValueType Value;
    std::memcpy(&Value, pData, sizeof(Value));
    return Value;
}

template <typename ValueType>
void StoreAnimationValue(void* pData, const ValueType& Value) noexcept
{
    static_assert(std::is_trivially_copyable<ValueType>::value,
                  "Animation values must be trivially copyable");

    std::memcpy(pData, &Value, sizeof(Value));
}

RadientQuaternion LoadAnimationQuaternion(const Uint8* pData, size_t ElementIndex) noexcept
{
    RadientQuaternion Value;
    std::memcpy(&Value, pData + ElementIndex * sizeof(RadientQuaternion), sizeof(RadientQuaternion));
    return Value;
}

void StoreAnimationQuaternion(Uint8* pData, size_t ElementIndex, const RadientQuaternion& Value) noexcept
{
    std::memcpy(pData + ElementIndex * sizeof(RadientQuaternion), &Value, sizeof(RadientQuaternion));
}

template <typename ValueType>
void SampleAnimationSingleValueStep(const RadientAnimationSamplerDesc& Sampler,
                                    Float32                            Time,
                                    size_t                             SampleSize,
                                    void*                              pOutput) noexcept
{
    VERIFY_EXPR(SampleSize == sizeof(ValueType));

    const Uint32 KeyIndex = FindAnimationKeyInterval(Sampler, Time).StartKey;
    std::memcpy(pOutput, GetAnimationKeyValue(Sampler, sizeof(ValueType), KeyIndex), sizeof(ValueType));
}

void SampleAnimationComponentsStep(const RadientAnimationSamplerDesc& Sampler,
                                   Float32                            Time,
                                   size_t                             SampleSize,
                                   void*                              pOutput) noexcept
{
    const Uint32 KeyIndex = FindAnimationKeyInterval(Sampler, Time).StartKey;
    std::memcpy(pOutput, GetAnimationKeyValue(Sampler, SampleSize, KeyIndex), SampleSize);
}

template <typename ValueType>
void SampleAnimationSingleValueLinear(const RadientAnimationSamplerDesc& Sampler,
                                      Float32                            Time,
                                      size_t                             SampleSize,
                                      void*                              pOutput) noexcept
{
    VERIFY_EXPR(SampleSize == sizeof(ValueType));

    const AnimationSampleInterval Interval    = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue = GetAnimationKeyValue(Sampler, sizeof(ValueType), Interval.StartKey);
    if (Interval.StartKey == Interval.EndKey)
    {
        std::memcpy(pOutput, pStartValue, sizeof(ValueType));
        return;
    }

    const ValueType Start = LoadAnimationValue<ValueType>(pStartValue);
    const ValueType End   = LoadAnimationValue<ValueType>(GetAnimationKeyValue(Sampler, sizeof(ValueType), Interval.EndKey));
    StoreAnimationValue(pOutput, RadientMath::Lerp(Start, End, Interval.Factor));
}

void SampleAnimationComponentsLinear(const RadientAnimationSamplerDesc& Sampler,
                                     Float32                            Time,
                                     size_t                             SampleSize,
                                     void*                              pOutput) noexcept
{
    const size_t                  ComponentCount = SampleSize / sizeof(Float32);
    const AnimationSampleInterval Interval       = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue    = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey);
    if (Interval.StartKey == Interval.EndKey)
    {
        std::memcpy(pOutput, pStartValue, SampleSize);
        return;
    }

    const Uint8* const pEndValue = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey);
    Uint8* const       pBytes    = static_cast<Uint8*>(pOutput);
    for (size_t Component = 0; Component < ComponentCount; ++Component)
    {
        const Float32 Start = LoadAnimationFloat(pStartValue, Component);
        const Float32 End   = LoadAnimationFloat(pEndValue, Component);
        StoreAnimationFloat(pBytes, Component, RadientMath::Lerp(Start, End, Interval.Factor));
    }
}

template <typename ValueType>
void SampleAnimationSingleValueCubic(const RadientAnimationSamplerDesc& Sampler,
                                     Float32                            Time,
                                     size_t                             SampleSize,
                                     void*                              pOutput) noexcept
{
    static_assert(std::is_trivially_copyable<ValueType>::value,
                  "Animation values must be trivially copyable");
    VERIFY_EXPR(SampleSize == sizeof(ValueType));

    const AnimationSampleInterval Interval    = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue = GetCubicAnimationKeyValue(Sampler, sizeof(ValueType), Interval.StartKey, 1);
    if (Interval.StartKey == Interval.EndKey)
    {
        std::memcpy(pOutput, pStartValue, sizeof(ValueType));
        return;
    }

    const Uint8* const pEndValue     = GetCubicAnimationKeyValue(Sampler, sizeof(ValueType), Interval.EndKey, 1);
    const Uint8* const pStartTangent = GetCubicAnimationKeyValue(Sampler, sizeof(ValueType), Interval.StartKey, 2);
    const Uint8* const pEndTangent   = GetCubicAnimationKeyValue(Sampler, sizeof(ValueType), Interval.EndKey, 0);

    const ValueType Start        = LoadAnimationValue<ValueType>(pStartValue);
    const ValueType StartTangent = LoadAnimationValue<ValueType>(pStartTangent);
    const ValueType End          = LoadAnimationValue<ValueType>(pEndValue);
    const ValueType EndTangent   = LoadAnimationValue<ValueType>(pEndTangent);
    const ValueType Result       = RadientMath::CubicHermite(
        Start, StartTangent, End, EndTangent, Interval.Factor, Interval.Duration);
    StoreAnimationValue(pOutput, Result);
}

void SampleAnimationComponentsCubic(const RadientAnimationSamplerDesc& Sampler,
                                    Float32                            Time,
                                    size_t                             SampleSize,
                                    void*                              pOutput) noexcept
{
    const size_t                  ComponentCount = SampleSize / sizeof(Float32);
    const AnimationSampleInterval Interval       = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue    = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 1);
    if (Interval.StartKey == Interval.EndKey)
    {
        std::memcpy(pOutput, pStartValue, SampleSize);
        return;
    }

    const Uint8* const pEndValue     = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 1);
    const Uint8* const pStartTangent = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 2);
    const Uint8* const pEndTangent   = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 0);

    Float32 StartValueWeight;
    Float32 StartTangentWeight;
    Float32 EndValueWeight;
    Float32 EndTangentWeight;
    RadientMath::GetCubicHermiteWeights(Interval.Factor,
                                        Interval.Duration,
                                        StartValueWeight,
                                        StartTangentWeight,
                                        EndValueWeight,
                                        EndTangentWeight);

    Uint8* const pBytes = static_cast<Uint8*>(pOutput);
    for (size_t Component = 0; Component < ComponentCount; ++Component)
    {
        const Float32 Value =
            LoadAnimationFloat(pStartValue, Component) * StartValueWeight +
            LoadAnimationFloat(pStartTangent, Component) * StartTangentWeight +
            LoadAnimationFloat(pEndValue, Component) * EndValueWeight +
            LoadAnimationFloat(pEndTangent, Component) * EndTangentWeight;
        StoreAnimationFloat(pBytes, Component, Value);
    }
}


template <Uint32 FixedElementCount>
Uint32 GetAnimationQuaternionElementCount(const RadientAnimationSamplerDesc& Sampler,
                                          size_t                             SampleSize) noexcept
{
    const Uint32 ElementCount = FixedElementCount != 0 ? FixedElementCount : Sampler.Value.ArraySize;
    VERIFY_EXPR(FixedElementCount == 0 || Sampler.Value.ArraySize == FixedElementCount);
    VERIFY_EXPR(SampleSize == static_cast<size_t>(ElementCount) * sizeof(RadientQuaternion));
    return ElementCount;
}

void NormalizeAnimationQuaternions(const Uint8* pSource,
                                   Uint32       ElementCount,
                                   Uint8*       pOutput) noexcept
{
    for (Uint32 Element = 0; Element < ElementCount; ++Element)
    {
        const RadientQuaternion Result = RadientMath::Normalize(LoadAnimationQuaternion(pSource, Element));
        StoreAnimationQuaternion(pOutput, Element, Result);
    }
}

template <Uint32 FixedElementCount>
void SampleAnimationQuaternionsStep(const RadientAnimationSamplerDesc& Sampler,
                                    Float32                            Time,
                                    size_t                             SampleSize,
                                    void*                              pOutput) noexcept
{
    const Uint32 ElementCount = GetAnimationQuaternionElementCount<FixedElementCount>(Sampler, SampleSize);
    const Uint32 KeyIndex     = FindAnimationKeyInterval(Sampler, Time).StartKey;
    NormalizeAnimationQuaternions(GetAnimationKeyValue(Sampler, SampleSize, KeyIndex),
                                  ElementCount,
                                  static_cast<Uint8*>(pOutput));
}

template <Uint32 FixedElementCount>
void SampleAnimationQuaternionsLinear(const RadientAnimationSamplerDesc& Sampler,
                                      Float32                            Time,
                                      size_t                             SampleSize,
                                      void*                              pOutput) noexcept
{
    const Uint32 ElementCount =
        GetAnimationQuaternionElementCount<FixedElementCount>(Sampler, SampleSize);
    const AnimationSampleInterval Interval    = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey);
    Uint8* const                  pBytes      = static_cast<Uint8*>(pOutput);
    if (Interval.StartKey == Interval.EndKey)
    {
        NormalizeAnimationQuaternions(pStartValue, ElementCount, pBytes);
        return;
    }

    const Uint8* const pEndValue = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey);
    for (Uint32 Element = 0; Element < ElementCount; ++Element)
    {
        const RadientQuaternion Result = RadientMath::Slerp(
            LoadAnimationQuaternion(pStartValue, Element),
            LoadAnimationQuaternion(pEndValue, Element),
            Interval.Factor);
        StoreAnimationQuaternion(pBytes, Element, Result);
    }
}

template <Uint32 FixedElementCount>
void SampleAnimationQuaternionsCubic(const RadientAnimationSamplerDesc& Sampler,
                                     Float32                            Time,
                                     size_t                             SampleSize,
                                     void*                              pOutput) noexcept
{
    const Uint32 ElementCount =
        GetAnimationQuaternionElementCount<FixedElementCount>(Sampler, SampleSize);
    const AnimationSampleInterval Interval = FindAnimationSampleInterval(Sampler, Time);
    const Uint8* const            pStartValue =
        GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 1);
    Uint8* const pBytes = static_cast<Uint8*>(pOutput);
    if (Interval.StartKey == Interval.EndKey)
    {
        NormalizeAnimationQuaternions(pStartValue, ElementCount, pBytes);
        return;
    }

    const Uint8* const pEndValue     = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 1);
    const Uint8* const pStartTangent = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 2);
    const Uint8* const pEndTangent   = GetCubicAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 0);

    for (Uint32 Element = 0; Element < ElementCount; ++Element)
    {
        const RadientQuaternion Result = RadientMath::CubicHermite(
            LoadAnimationQuaternion(pStartValue, Element),
            LoadAnimationQuaternion(pStartTangent, Element),
            LoadAnimationQuaternion(pEndValue, Element),
            LoadAnimationQuaternion(pEndTangent, Element),
            Interval.Factor,
            Interval.Duration);
        StoreAnimationQuaternion(pBytes, Element, Result);
    }
}

using AnimationSampleKernel = void (*)(const RadientAnimationSamplerDesc& Sampler,
                                       Float32                            Time,
                                       size_t                             SampleSize,
                                       void*                              pOutput) noexcept;

template <typename ValueType>
AnimationSampleKernel GetAnimationSingleValueSampleKernel(
    RADIENT_ANIMATION_INTERPOLATION Interpolation) noexcept
{
    switch (Interpolation)
    {
        case RADIENT_ANIMATION_INTERPOLATION_STEP:
            return &SampleAnimationSingleValueStep<ValueType>;

        case RADIENT_ANIMATION_INTERPOLATION_LINEAR:
            return &SampleAnimationSingleValueLinear<ValueType>;

        case RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE:
            return &SampleAnimationSingleValueCubic<ValueType>;

        default:
            return nullptr;
    }
}

AnimationSampleKernel GetAnimationSampleKernel(const RadientAnimationSamplerDesc& Sampler,
                                               RADIENT_ANIMATION_VALUE_SEMANTIC   Semantic) noexcept
{
    if (Semantic == RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION)
    {
        const bool SingleQuaternion = Sampler.Value.ArraySize == 1;
        switch (Sampler.Interpolation)
        {
            case RADIENT_ANIMATION_INTERPOLATION_STEP:
                return SingleQuaternion ?
                    &SampleAnimationQuaternionsStep<1> :
                    &SampleAnimationQuaternionsStep<0>;

            case RADIENT_ANIMATION_INTERPOLATION_LINEAR:
                return SingleQuaternion ?
                    &SampleAnimationQuaternionsLinear<1> :
                    &SampleAnimationQuaternionsLinear<0>;

            case RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE:
                return SingleQuaternion ?
                    &SampleAnimationQuaternionsCubic<1> :
                    &SampleAnimationQuaternionsCubic<0>;

            default:
                return nullptr;
        }
    }

    if (Semantic != RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE)
        return nullptr;

    if (Sampler.Value.ArraySize == 1)
    {
        switch (Sampler.Value.Type)
        {
            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT:
                return GetAnimationSingleValueSampleKernel<Float32>(Sampler.Interpolation);

            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT2:
                return GetAnimationSingleValueSampleKernel<RadientFloat2>(Sampler.Interpolation);

            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3:
                return GetAnimationSingleValueSampleKernel<RadientFloat3>(Sampler.Interpolation);

            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4:
                return GetAnimationSingleValueSampleKernel<RadientFloat4>(Sampler.Interpolation);

            default:
                break;
        }
    }

    switch (Sampler.Interpolation)
    {
        case RADIENT_ANIMATION_INTERPOLATION_STEP:
            return &SampleAnimationComponentsStep;

        case RADIENT_ANIMATION_INTERPOLATION_LINEAR:
            return &SampleAnimationComponentsLinear;

        case RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE:
            return &SampleAnimationComponentsCubic;

        default:
            return nullptr;
    }
}

struct PendingAnimationDestination
{
    IRadientAnimationDestination*                    pDestination = nullptr;
    std::vector<RadientAnimationPropertyBindingDesc> Properties;
    std::vector<Uint32>                              SamplerIndices;
};

struct AnimationDestinationMappingKey
{
    Uint32                             ClipTargetIndex    = InvalidRadientAnimationTargetIndex;
    RadientAnimationDestinationElement DestinationElement = InvalidRadientAnimationDestinationElement;
    Uint32                             MappingIndex       = 0;

    bool operator<(const AnimationDestinationMappingKey& Rhs) const noexcept
    {
        if (ClipTargetIndex != Rhs.ClipTargetIndex)
            return ClipTargetIndex < Rhs.ClipTargetIndex;
        if (DestinationElement != Rhs.DestinationElement)
            return DestinationElement < Rhs.DestinationElement;
        return MappingIndex < Rhs.MappingIndex;
    }
};

struct BoundAnimationPropertyRange
{
    RadientAnimationSchemaID           Schema       = InvalidRadientAnimationSchemaID;
    RadientAnimationDestinationElement Element      = InvalidRadientAnimationDestinationElement;
    RadientAnimationPropertyID         Property     = InvalidRadientAnimationPropertyID;
    Uint64                             First        = 0;
    Uint64                             End          = 0;
    Uint32                             RequestIndex = 0;

    bool operator<(const BoundAnimationPropertyRange& Rhs) const noexcept
    {
        if (Schema != Rhs.Schema)
            return Schema < Rhs.Schema;
        if (Element != Rhs.Element)
            return Element < Rhs.Element;
        if (Property != Rhs.Property)
            return Property < Rhs.Property;
        if (First != Rhs.First)
            return First < Rhs.First;
        if (End != Rhs.End)
            return End < Rhs.End;
        return RequestIndex < Rhs.RequestIndex;
    }
};

bool HaveSameBoundAnimationProperty(const BoundAnimationPropertyRange& Lhs,
                                    const BoundAnimationPropertyRange& Rhs) noexcept
{
    return Lhs.Schema == Rhs.Schema &&
        Lhs.Element == Rhs.Element &&
        Lhs.Property == Rhs.Property;
}

RADIENT_STATUS BuildPendingAnimationDestinations(const RadientAnimationClipDesc&           Clip,
                                                 const RadientAnimationClipChannelIndex&   ChannelIndex,
                                                 const RadientAnimationBindingDesc&        Binding,
                                                 std::vector<PendingAnimationDestination>& PendingDestinations)
{
    VERIFY_EXPR(ChannelIndex.TargetCount == Clip.TargetCount);
    VERIFY_EXPR(ChannelIndex.ChannelCount == Clip.ChannelCount);
    VERIFY_EXPR(ChannelIndex.pTargetOffsets != nullptr);
    VERIFY_EXPR(ChannelIndex.ChannelCount == 0 || ChannelIndex.pChannelIndices != nullptr);

    if ((Binding.DestinationCount == 0) != (Binding.pDestinations == nullptr))
    {
        LOG_ERROR_MESSAGE("Radient animation binding destination count and pointer do not form a valid pair");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (!IsAddressableArray(Binding.DestinationCount, sizeof(RadientAnimationDestinationDesc)) ||
        !IsAddressableArray(Binding.DestinationCount, sizeof(PendingAnimationDestination)))
    {
        LOG_ERROR_MESSAGE("Radient animation binding destination table size overflows addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    PendingDestinations.reserve(Binding.DestinationCount);
    for (Uint32 DestinationIndex = 0; DestinationIndex < Binding.DestinationCount; ++DestinationIndex)
    {
        const RadientAnimationDestinationDesc& Destination = Binding.pDestinations[DestinationIndex];
        if (Destination.pDestination == nullptr)
        {
            LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex, " is null");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        for (Uint32 PreviousIndex = 0; PreviousIndex < DestinationIndex; ++PreviousIndex)
        {
            if (Binding.pDestinations[PreviousIndex].pDestination == Destination.pDestination)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destinations ", PreviousIndex, " and ", DestinationIndex,
                                  " reference the same destination interface");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }
        if (Destination.MappingCount == 0 || Destination.pMappings == nullptr)
        {
            LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                              " must contain at least one mapping");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (!IsAddressableArray(Destination.MappingCount, sizeof(RadientAnimationDestinationMappingDesc)) ||
            !IsAddressableArray(Destination.MappingCount, sizeof(AnimationDestinationMappingKey)))
        {
            LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                              " mapping table size overflows addressable memory");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        std::vector<AnimationDestinationMappingKey> MappingKeys;
        MappingKeys.reserve(Destination.MappingCount);
        Uint64 PropertyCount64 = 0;
        for (Uint32 MappingIndex = 0; MappingIndex < Destination.MappingCount; ++MappingIndex)
        {
            const RadientAnimationDestinationMappingDesc& Mapping = Destination.pMappings[MappingIndex];
            if (Mapping.ClipTargetIndex >= Clip.TargetCount)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                  " mapping ", MappingIndex, " references invalid clip target ", Mapping.ClipTargetIndex);
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            if (Mapping.DestinationElement == InvalidRadientAnimationDestinationElement)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                  " mapping ", MappingIndex, " references an invalid destination element");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }

            MappingKeys.push_back({Mapping.ClipTargetIndex, Mapping.DestinationElement, MappingIndex});
            const Uint32 FirstChannel = ChannelIndex.pTargetOffsets[Mapping.ClipTargetIndex];
            const Uint32 EndChannel   = ChannelIndex.pTargetOffsets[Mapping.ClipTargetIndex + 1u];
            VERIFY_EXPR(FirstChannel <= EndChannel && EndChannel <= Clip.ChannelCount);
            const Uint32 MappedChannelCount = EndChannel - FirstChannel;
            if (!IsSumRepresentable<Uint32>(PropertyCount64, MappedChannelCount))
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                  " contains too many mapped properties");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            PropertyCount64 += MappedChannelCount;
        }

        std::sort(MappingKeys.begin(), MappingKeys.end());
        for (size_t MappingIndex = 1; MappingIndex < MappingKeys.size(); ++MappingIndex)
        {
            const AnimationDestinationMappingKey& Previous = MappingKeys[MappingIndex - 1];
            const AnimationDestinationMappingKey& Current  = MappingKeys[MappingIndex];
            if (Previous.ClipTargetIndex == Current.ClipTargetIndex &&
                Previous.DestinationElement == Current.DestinationElement)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                  " mappings ", Previous.MappingIndex, " and ", Current.MappingIndex,
                                  " are duplicates");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        const Uint32 PropertyCount = static_cast<Uint32>(PropertyCount64);
        if (PropertyCount == 0 ||
            !IsAddressableArray(PropertyCount, sizeof(RadientAnimationPropertyBindingDesc)) ||
            !IsAddressableArray(PropertyCount, sizeof(Uint32)) ||
            !IsAddressableArray(PropertyCount, sizeof(BoundAnimationPropertyRange)))
        {
            LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                              " property table size overflows addressable memory");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        PendingAnimationDestination Pending;
        Pending.pDestination = Destination.pDestination;
        Pending.Properties.reserve(PropertyCount);
        Pending.SamplerIndices.reserve(PropertyCount);

        for (Uint32 MappingIndex = 0; MappingIndex < Destination.MappingCount; ++MappingIndex)
        {
            const RadientAnimationDestinationMappingDesc& Mapping = Destination.pMappings[MappingIndex];
            const RadientAnimationTargetDesc&             Target  = Clip.pTargets[Mapping.ClipTargetIndex];
            const Uint32 FirstChannel = ChannelIndex.pTargetOffsets[Mapping.ClipTargetIndex];
            const Uint32 EndChannel   = ChannelIndex.pTargetOffsets[Mapping.ClipTargetIndex + 1u];
            for (Uint32 ChannelOffset = FirstChannel; ChannelOffset < EndChannel; ++ChannelOffset)
            {
                const Uint32 ChannelDescIndex = ChannelIndex.pChannelIndices[ChannelOffset];
                VERIFY_EXPR(ChannelDescIndex < Clip.ChannelCount);
                const RadientAnimationChannelDesc& Channel = Clip.pChannels[ChannelDescIndex];
                VERIFY_EXPR(Channel.TargetIndex == Mapping.ClipTargetIndex);

                const RadientAnimationSamplerDesc&  Sampler = Clip.pSamplers[Channel.SamplerIndex];
                RadientAnimationPropertyBindingDesc Property;
                Property.Schema             = Target.Schema;
                Property.DestinationElement = Mapping.DestinationElement;
                Property.Property           = Channel.Property;
                Property.FirstArrayElement  = Channel.FirstArrayElement;
                Property.Value              = Sampler.Value;

                Pending.Properties.push_back(Property);
                Pending.SamplerIndices.push_back(Channel.SamplerIndex);
            }
        }

        PendingDestinations.emplace_back(std::move(Pending));
    }

    return RADIENT_STATUS_OK;
}

struct AnimationSampleJob
{
    Uint32                SamplerIndex     = InvalidRadientAnimationSamplerIndex;
    Uint32                DestinationCount = 0;
    size_t                OutputOffset     = 0;
    size_t                OutputSize       = 0;
    AnimationSampleKernel pKernel          = nullptr;
};

constexpr Uint32 InvalidAnimationSampleJobIndex = ~Uint32{0};
constexpr Uint32 ValidAnimationValueSemanticCount =
    static_cast<Uint32>(RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT) - 1u;

struct AnimationDestinationWriteJob
{
    Uint32 SampleJobIndex   = InvalidAnimationSampleJobIndex;
    Uint32 FirstOutputIndex = 0;
};

struct CompiledAnimationDestination
{
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    std::vector<AnimationDestinationWriteJob>          WriteJobs;
};

struct AnimationPropertySemantic
{
    RadientAnimationSchemaID         Schema   = InvalidRadientAnimationSchemaID;
    RadientAnimationPropertyID       Property = InvalidRadientAnimationPropertyID;
    RADIENT_ANIMATION_VALUE_SEMANTIC Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
};

struct AnimationDestinationFirstOutput
{
    Uint32 Generation       = 0;
    Uint32 FirstOutputIndex = 0;
};

bool AppendAnimationScratchBlock(size_t  Size,
                                 size_t  Alignment,
                                 size_t& ScratchSize,
                                 size_t& Offset) noexcept
{
    VERIFY_EXPR(Alignment != 0 && (Alignment & (Alignment - 1u)) == 0);
    const size_t AlignmentMask = Alignment - 1u;
    if (!IsSumRepresentable<size_t>(ScratchSize, AlignmentMask))
        return false;

    Offset = (ScratchSize + AlignmentMask) & ~AlignmentMask;
    if (!IsSumRepresentable<size_t>(Offset, Size))
        return false;

    ScratchSize = Offset + Size;
    return true;
}

RADIENT_STATUS GetOrCreateAnimationSampleJob(const RadientAnimationClipDesc&     Clip,
                                             Uint32                              SamplerIndex,
                                             RADIENT_ANIMATION_VALUE_SEMANTIC    Semantic,
                                             std::vector<Uint32>&                SampleJobLookup,
                                             std::vector<AnimationSampleJob>&    SampleJobs,
                                             Uint32&                             SampleJobIndex)
{
    VERIFY_EXPR(SamplerIndex < Clip.SamplerCount);
    VERIFY_EXPR(Semantic > RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN &&
                Semantic < RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT);
    const size_t LookupIndex = static_cast<size_t>(SamplerIndex) * ValidAnimationValueSemanticCount +
        (static_cast<size_t>(Semantic) - 1u);
    VERIFY_EXPR(LookupIndex < SampleJobLookup.size());

    const Uint32 ExistingSampleJobIndex = SampleJobLookup[LookupIndex];
    if (ExistingSampleJobIndex != InvalidAnimationSampleJobIndex)
    {
        VERIFY_EXPR(ExistingSampleJobIndex < SampleJobs.size());
        SampleJobIndex = ExistingSampleJobIndex;
        return RADIENT_STATUS_OK;
    }

    if (!IsSumRepresentable<Uint32>(SampleJobs.size(), 1u))
    {
        LOG_ERROR_MESSAGE("Radient animation binding contains too many sampling jobs");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RadientAnimationSamplerDesc& Sampler = Clip.pSamplers[SamplerIndex];
    const AnimationSampleKernel        pKernel = GetAnimationSampleKernel(Sampler, Semantic);
    if (pKernel == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to compile a Radient animation sampling kernel");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const AnimationValueTypeInfo TypeInfo     = GetAnimationValueTypeInfo(Sampler.Value.Type);
    Uint64                       OutputSize64 = 0;
    if (!CheckedMultiply(TypeInfo.NativeSize, Sampler.Value.ArraySize, OutputSize64) ||
        !IsAddressableSize(OutputSize64))
    {
        LOG_ERROR_MESSAGE("Radient animation binding sampling output size overflows addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    SampleJobIndex = static_cast<Uint32>(SampleJobs.size());
    SampleJobs.push_back({SamplerIndex,
                          0,
                          0,
                          static_cast<size_t>(OutputSize64),
                          pKernel});
    SampleJobLookup[LookupIndex] = SampleJobIndex;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateAnimationPropertySemantic(const RadientAnimationPropertyBindingDesc& Property,
                                                 RADIENT_ANIMATION_VALUE_SEMANTIC           Semantic,
                                                 std::vector<AnimationPropertySemantic>&    PropertySemantics)
{
    if (Semantic <= RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN ||
        Semantic >= RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT)
    {
        LOG_ERROR_MESSAGE("Radient animation destination returned an invalid value semantic");
        return RADIENT_STATUS_INVALID_OPERATION;
    }
    if (Semantic == RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION &&
        Property.Value.Type != RADIENT_ANIMATION_VALUE_TYPE_FLOAT4)
    {
        LOG_ERROR_MESSAGE("Radient animation destination returned quaternion semantics for a non-FLOAT4 property");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    for (const AnimationPropertySemantic& Existing : PropertySemantics)
    {
        if (Existing.Schema == Property.Schema && Existing.Property == Property.Property)
        {
            if (Existing.Semantic != Semantic)
            {
                LOG_ERROR_MESSAGE("Radient animation destinations returned inconsistent semantics for one schema property");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
            return RADIENT_STATUS_OK;
        }
    }

    PropertySemantics.push_back({Property.Schema, Property.Property, Semantic});
    return RADIENT_STATUS_OK;
}

class RadientAnimationBindingImpl final : public ObjectBase<IRadientAnimationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationBinding>;

    RadientAnimationBindingImpl(IReferenceCounters*                       pRefCounters,
                                IRadientAnimationClipAsset*               pClip,
                                std::vector<AnimationSampleJob>           SampleJobs,
                                std::vector<Uint32>                       SharedSampleJobIndices,
                                std::vector<CompiledAnimationDestination> Destinations,
                                size_t                                    ScratchSize) :
        TBase{pRefCounters},
        m_pClip{pClip},
        m_pClipDesc{&m_pClip->GetDesc()},
        m_SampleJobs{std::move(SampleJobs)},
        m_SharedSampleJobIndices{std::move(SharedSampleJobIndices)},
        m_Destinations{std::move(Destinations)}
    {
        VERIFY_EXPR(m_pClip != nullptr);

        const size_t ScratchWordCount = ScratchSize == 0 ?
            0 :
            1 + (ScratchSize - 1) / sizeof(std::max_align_t);
        m_Scratch.resize(ScratchWordCount);
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationBinding, TBase)

    virtual IRadientAnimationClipAsset* DILIGENT_CALL_TYPE GetClip() const override final
    {
        return m_pClip;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Evaluate(const RadientAnimationEvaluateInfo& Info) override final
    {
        if (!RadientMath::IsFinite(Info.Time))
        {
            LOG_ERROR_MESSAGE("Radient animation evaluation time must be finite");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const RadientAnimationClipDesc& Clip     = *m_pClipDesc;
        Uint8* const                    pScratch = reinterpret_cast<Uint8*>(m_Scratch.data());
        for (const Uint32 SampleJobIndex : m_SharedSampleJobIndices)
        {
            const AnimationSampleJob& SampleJob = m_SampleJobs[SampleJobIndex];

            SampleJob.pKernel(Clip.pSamplers[SampleJob.SamplerIndex],
                              Info.Time,
                              SampleJob.OutputSize,
                              pScratch + SampleJob.OutputOffset);
        }

        for (CompiledAnimationDestination& Destination : m_Destinations)
        {
            void* const*         ppOutputs   = nullptr;
            const RADIENT_STATUS BeginStatus = Destination.pBinding->BeginUpdate(&ppOutputs);
            if (BeginStatus != RADIENT_STATUS_OK)
            {
                if (BeginStatus < 0)
                    return BeginStatus;

                LOG_ERROR_MESSAGE("Radient animation destination binding returned an invalid success status from BeginUpdate");
                return RADIENT_STATUS_INVALID_OPERATION;
            }

            VERIFY_EXPR(ppOutputs != nullptr);

            for (Uint32 OutputIndex = 0; OutputIndex < static_cast<Uint32>(Destination.WriteJobs.size()); ++OutputIndex)
            {
                const AnimationDestinationWriteJob& WriteJob  = Destination.WriteJobs[OutputIndex];
                const AnimationSampleJob&           SampleJob = m_SampleJobs[WriteJob.SampleJobIndex];
                void* const                         pOutput   = ppOutputs[OutputIndex];
                VERIFY_EXPR(pOutput != nullptr);

                if (OutputIndex != WriteJob.FirstOutputIndex)
                {
                    const void* const pFirstOutput = ppOutputs[WriteJob.FirstOutputIndex];
                    VERIFY_EXPR(pFirstOutput != nullptr);
                    if (pOutput != pFirstOutput)
                        std::memcpy(pOutput, pFirstOutput, SampleJob.OutputSize);
                    continue;
                }

                if (SampleJob.DestinationCount > 1)
                {
                    std::memcpy(pOutput,
                                pScratch + SampleJob.OutputOffset,
                                SampleJob.OutputSize);
                }
                else
                {
                    SampleJob.pKernel(Clip.pSamplers[SampleJob.SamplerIndex],
                                      Info.Time,
                                      SampleJob.OutputSize,
                                      pOutput);
                }
            }

            const RADIENT_STATUS EndStatus = Destination.pBinding->EndUpdate(Info.UpdateDerivedState);
            if (EndStatus != RADIENT_STATUS_OK)
            {
                if (EndStatus < 0)
                    return EndStatus;

                LOG_ERROR_MESSAGE("Radient animation destination binding returned an invalid success status from EndUpdate");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
        }

        return m_Destinations.empty() ? RADIENT_STATUS_NO_CHANGE : RADIENT_STATUS_OK;
    }

private:
    const RefCntAutoPtr<IRadientAnimationClipAsset> m_pClip;
    const RadientAnimationClipDesc* const           m_pClipDesc;
    const std::vector<AnimationSampleJob>           m_SampleJobs;
    const std::vector<Uint32>                       m_SharedSampleJobIndices;
    std::vector<CompiledAnimationDestination>       m_Destinations;
    std::vector<std::max_align_t>                   m_Scratch;
};

} // namespace

RADIENT_STATUS CreateRadientAnimationBinding(IRadientAnimationClipAsset*             pClip,
                                             const RadientAnimationClipChannelIndex& ChannelIndex,
                                             const RadientAnimationBindingDesc&      BindingDesc,
                                             IRadientAnimationBinding**              ppBinding)
{
    VERIFY_EXPR(pClip != nullptr);
    VERIFY_EXPR(ppBinding != nullptr && *ppBinding == nullptr);

    const RadientAnimationClipDesc& Clip = pClip->GetDesc();

    std::vector<PendingAnimationDestination> PendingDestinations;
    const RADIENT_STATUS                     DescriptorStatus =
        BuildPendingAnimationDestinations(Clip, ChannelIndex, BindingDesc, PendingDestinations);
    if (DescriptorStatus != RADIENT_STATUS_OK)
        return DescriptorStatus;

    Uint64 SampleJobLookupCount = 0;
    if ((!PendingDestinations.empty() &&
         !CheckedMultiply(Clip.SamplerCount,
                          ValidAnimationValueSemanticCount,
                          SampleJobLookupCount)) ||
        !IsAddressableArray(SampleJobLookupCount, sizeof(Uint32)))
    {
        LOG_ERROR_MESSAGE("Radient animation binding sampling job lookup size overflows addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::vector<Uint32> SampleJobLookup(static_cast<size_t>(SampleJobLookupCount),
                                        InvalidAnimationSampleJobIndex);
    std::vector<AnimationSampleJob>               SampleJobs;
    std::vector<CompiledAnimationDestination>     Destinations;
    std::vector<AnimationPropertySemantic>        PropertySemantics;
    std::vector<AnimationDestinationFirstOutput>  DestinationFirstOutputs;

    Destinations.reserve(PendingDestinations.size());
    Uint32 DestinationGeneration = 0;
    for (PendingAnimationDestination& Pending : PendingDestinations)
    {
        ++DestinationGeneration;
        VERIFY_EXPR(DestinationGeneration != 0);

        std::vector<RadientAnimationResolvedPropertyDesc>  ResolvedProperties(Pending.Properties.size());
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pDestinationBinding;
        const RADIENT_STATUS                               DestinationStatus = Pending.pDestination->CreateBinding(
            Pending.Properties.data(),
            static_cast<Uint32>(Pending.Properties.size()),
            ResolvedProperties.data(),
            pDestinationBinding.GetAddressOfEmpty());

        if (DestinationStatus == RADIENT_STATUS_UNSUPPORTED)
        {
            if (pDestinationBinding)
            {
                LOG_ERROR_MESSAGE("Radient animation destination created a binding while reporting that no properties are supported");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
            for (const RadientAnimationResolvedPropertyDesc& Resolved : ResolvedProperties)
            {
                if (Resolved.Semantic != RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN)
                {
                    LOG_ERROR_MESSAGE("Radient animation destination resolved a property while reporting that no properties are supported");
                    return RADIENT_STATUS_INVALID_OPERATION;
                }
            }
            continue;
        }

        if (DestinationStatus != RADIENT_STATUS_OK)
        {
            if (pDestinationBinding)
            {
                LOG_ERROR_MESSAGE("Radient animation destination created a binding while reporting failure");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
            if (DestinationStatus > RADIENT_STATUS_OK)
            {
                LOG_ERROR_MESSAGE("Radient animation destination returned an invalid success status while creating a binding");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
            return DestinationStatus;
        }
        if (!pDestinationBinding)
        {
            LOG_ERROR_MESSAGE("Radient animation destination returned success without creating a binding");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        CompiledAnimationDestination Destination;
        Destination.pBinding = std::move(pDestinationBinding);
        Destination.WriteJobs.reserve(Pending.Properties.size());
        std::vector<BoundAnimationPropertyRange> PropertyRanges;
        PropertyRanges.reserve(Pending.Properties.size());
        for (size_t PropertyIndex = 0; PropertyIndex < Pending.Properties.size(); ++PropertyIndex)
        {
            const RadientAnimationPropertyBindingDesc&  Property = Pending.Properties[PropertyIndex];
            const RadientAnimationResolvedPropertyDesc& Resolved = ResolvedProperties[PropertyIndex];
            if (Resolved.Semantic == RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN)
                continue;

            const RADIENT_ANIMATION_VALUE_SEMANTIC Semantic = Resolved.Semantic;

            const RADIENT_STATUS SemanticStatus =
                ValidateAnimationPropertySemantic(Property, Semantic, PropertySemantics);
            if (SemanticStatus != RADIENT_STATUS_OK)
                return SemanticStatus;

            PropertyRanges.push_back({Property.Schema,
                                      Property.DestinationElement,
                                      Property.Property,
                                      Property.FirstArrayElement,
                                      static_cast<Uint64>(Property.FirstArrayElement) + Property.Value.ArraySize,
                                      static_cast<Uint32>(PropertyIndex)});

            Uint32               SampleJobIndex = 0;
            const RADIENT_STATUS SampleJobStatus =
                GetOrCreateAnimationSampleJob(Clip,
                                              Pending.SamplerIndices[PropertyIndex],
                                              Semantic,
                                              SampleJobLookup,
                                              SampleJobs,
                                              SampleJobIndex);
            if (SampleJobStatus != RADIENT_STATUS_OK)
                return SampleJobStatus;

            const Uint32 OutputIndex = static_cast<Uint32>(Destination.WriteJobs.size());
            if (DestinationFirstOutputs.size() < SampleJobs.size())
                DestinationFirstOutputs.resize(SampleJobs.size());

            AnimationDestinationFirstOutput& FirstOutput = DestinationFirstOutputs[SampleJobIndex];
            if (FirstOutput.Generation != DestinationGeneration)
            {
                FirstOutput.Generation       = DestinationGeneration;
                FirstOutput.FirstOutputIndex = OutputIndex;
                ++SampleJobs[SampleJobIndex].DestinationCount;
            }

            Destination.WriteJobs.push_back({SampleJobIndex, FirstOutput.FirstOutputIndex});
        }

        if (Destination.WriteJobs.empty())
        {
            LOG_ERROR_MESSAGE("Radient animation destination returned success without resolving any properties");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        std::sort(PropertyRanges.begin(), PropertyRanges.end());
        for (size_t RangeIndex = 1; RangeIndex < PropertyRanges.size(); ++RangeIndex)
        {
            const BoundAnimationPropertyRange& Previous = PropertyRanges[RangeIndex - 1];
            const BoundAnimationPropertyRange& Current  = PropertyRanges[RangeIndex];
            if (HaveSameBoundAnimationProperty(Previous, Current) && Current.First < Previous.End)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination property requests ", Previous.RequestIndex,
                                  " and ", Current.RequestIndex, " address overlapping ranges");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        Destinations.emplace_back(std::move(Destination));
    }

    std::vector<Uint32> SharedSampleJobIndices;
    size_t              ScratchSize = 0;
    for (Uint32 SampleJobIndex = 0; SampleJobIndex < static_cast<Uint32>(SampleJobs.size()); ++SampleJobIndex)
    {
        AnimationSampleJob& SampleJob = SampleJobs[SampleJobIndex];
        if (SampleJob.DestinationCount <= 1)
            continue;

        SharedSampleJobIndices.push_back(SampleJobIndex);
        const AnimationValueTypeInfo TypeInfo = GetAnimationValueTypeInfo(
            Clip.pSamplers[SampleJob.SamplerIndex].Value.Type);
        if (!AppendAnimationScratchBlock(SampleJob.OutputSize,
                                         TypeInfo.NativeAlignment,
                                         ScratchSize,
                                         SampleJob.OutputOffset))
        {
            LOG_ERROR_MESSAGE("Radient animation binding shared sampling scratch size overflows addressable memory");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    RefCntAutoPtr<RadientAnimationBindingImpl> pBinding{MakeNewRCObj<RadientAnimationBindingImpl>()(
        pClip,
        std::move(SampleJobs),
        std::move(SharedSampleJobIndices),
        std::move(Destinations),
        ScratchSize)};
    *ppBinding = pBinding.Detach();
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
