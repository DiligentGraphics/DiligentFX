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
    VERIFY_EXPR(Sampler.KeyframeCount != 0);
    if (Sampler.KeyframeCount == 1 || Time <= Sampler.pTimes[0])
        return {};

    const Uint32 LastKey = Sampler.KeyframeCount - 1;
    if (Time >= Sampler.pTimes[LastKey])
        return {LastKey, LastKey, 0.f, 0.f};

    const Float32* const pEndTime = std::upper_bound(Sampler.pTimes,
                                                     Sampler.pTimes + Sampler.KeyframeCount,
                                                     Time);

    const Uint32  EndKey   = static_cast<Uint32>(pEndTime - Sampler.pTimes);
    const Uint32  StartKey = EndKey - 1;
    const Float32 Duration = Sampler.pTimes[EndKey] - Sampler.pTimes[StartKey];
    const Float32 Factor   = (Time - Sampler.pTimes[StartKey]) / Duration;
    return {StartKey, EndKey, Factor, Duration};
}

const Uint8* GetAnimationKeyValue(const RadientAnimationSamplerDesc& Sampler,
                                  size_t                             SampleSize,
                                  Uint32                             KeyIndex,
                                  Uint32                             CubicElement) noexcept
{
    const size_t ValueIndex = Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ?
        static_cast<size_t>(KeyIndex) * 3u + CubicElement :
        KeyIndex;
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

void SampleAnimationComponentWise(const RadientAnimationSamplerDesc& Sampler,
                                  const AnimationSampleInterval&     Interval,
                                  size_t                             SampleSize,
                                  Uint8*                             pOutput) noexcept
{
    const Uint8* const pStartValue = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 1);
    if (Interval.StartKey == Interval.EndKey || Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP)
    {
        std::memcpy(pOutput, pStartValue, SampleSize);
        return;
    }

    const Uint8* const pEndValue      = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 1);
    const size_t       ComponentCount = SampleSize / sizeof(Float32);
    if (Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR)
    {
        for (size_t Component = 0; Component < ComponentCount; ++Component)
        {
            const Float32 Start = LoadAnimationFloat(pStartValue, Component);
            const Float32 End   = LoadAnimationFloat(pEndValue, Component);
            const Float32 Value = Start * (1.f - Interval.Factor) + End * Interval.Factor;
            StoreAnimationFloat(pOutput, Component, Value);
        }
        return;
    }

    const Uint8* const pStartTangent = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 2);
    const Uint8* const pEndTangent   = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 0);

    const Float32 Factor             = Interval.Factor;
    const Float32 Factor2            = Factor * Factor;
    const Float32 Factor3            = Factor2 * Factor;
    const Float32 StartWeight        = 2.f * Factor3 - 3.f * Factor2 + 1.f;
    const Float32 StartTangentWeight = (Factor3 - 2.f * Factor2 + Factor) * Interval.Duration;
    const Float32 EndWeight          = -2.f * Factor3 + 3.f * Factor2;
    const Float32 EndTangentWeight   = (Factor3 - Factor2) * Interval.Duration;
    for (size_t Component = 0; Component < ComponentCount; ++Component)
    {
        const Float32 Value =
            LoadAnimationFloat(pStartValue, Component) * StartWeight +
            LoadAnimationFloat(pStartTangent, Component) * StartTangentWeight +
            LoadAnimationFloat(pEndValue, Component) * EndWeight +
            LoadAnimationFloat(pEndTangent, Component) * EndTangentWeight;
        StoreAnimationFloat(pOutput, Component, Value);
    }
}

void SampleAnimationQuaternions(const RadientAnimationSamplerDesc& Sampler,
                                const AnimationSampleInterval&     Interval,
                                size_t                             SampleSize,
                                Uint8*                             pOutput) noexcept
{
    const Uint8* const pStartValue = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 1);
    const Uint8* const pEndValue   = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 1);

    const Uint8* pStartTangent = nullptr;
    const Uint8* pEndTangent   = nullptr;
    if (Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE &&
        Interval.StartKey != Interval.EndKey)
    {
        pStartTangent = GetAnimationKeyValue(Sampler, SampleSize, Interval.StartKey, 2);
        pEndTangent   = GetAnimationKeyValue(Sampler, SampleSize, Interval.EndKey, 0);
    }

    for (Uint32 Element = 0; Element < Sampler.Value.ArraySize; ++Element)
    {
        const RadientQuaternion Start = LoadAnimationQuaternion(pStartValue, Element);
        RadientQuaternion       Result;
        if (Interval.StartKey == Interval.EndKey || Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP)
        {
            Result = RadientMath::Normalize(Start);
        }
        else
        {
            const RadientQuaternion End = LoadAnimationQuaternion(pEndValue, Element);
            if (Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR)
            {
                Result = RadientMath::Slerp(Start, End, Interval.Factor);
            }
            else
            {
                Result = RadientMath::CubicHermite(
                    Start,
                    LoadAnimationQuaternion(pStartTangent, Element),
                    End,
                    LoadAnimationQuaternion(pEndTangent, Element),
                    Interval.Factor,
                    Interval.Duration);
            }
        }
        StoreAnimationQuaternion(pOutput, Element, Result);
    }
}

void SampleAnimationValue(const RadientAnimationSamplerDesc& Sampler,
                          RADIENT_ANIMATION_VALUE_SEMANTIC   Semantic,
                          Float32                            Time,
                          size_t                             SampleSize,
                          void*                              pOutput) noexcept
{
    const AnimationSampleInterval Interval = FindAnimationSampleInterval(Sampler, Time);
    Uint8* const                  pBytes   = static_cast<Uint8*>(pOutput);
    if (Semantic == RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION)
        SampleAnimationQuaternions(Sampler, Interval, SampleSize, pBytes);
    else
        SampleAnimationComponentWise(Sampler, Interval, SampleSize, pBytes);
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
                                                 const RadientAnimationBindingDesc&        Binding,
                                                 std::vector<PendingAnimationDestination>& PendingDestinations)
{
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

            MappingKeys.push_back({Mapping.ClipTargetIndex, Mapping.DestinationElement, MappingIndex});
            for (Uint32 ChannelIndex = 0; ChannelIndex < Clip.ChannelCount; ++ChannelIndex)
            {
                if (Clip.pChannels[ChannelIndex].TargetIndex == Mapping.ClipTargetIndex)
                {
                    if (!IsSumRepresentable<Uint32>(PropertyCount64, 1u))
                    {
                        LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                          " contains too many mapped properties");
                        return RADIENT_STATUS_INVALID_ARGUMENT;
                    }
                    ++PropertyCount64;
                }
            }
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
        std::vector<BoundAnimationPropertyRange> PropertyRanges;
        PropertyRanges.reserve(PropertyCount);

        for (Uint32 MappingIndex = 0; MappingIndex < Destination.MappingCount; ++MappingIndex)
        {
            const RadientAnimationDestinationMappingDesc& Mapping = Destination.pMappings[MappingIndex];
            const RadientAnimationTargetDesc&             Target  = Clip.pTargets[Mapping.ClipTargetIndex];
            for (Uint32 ChannelIndex = 0; ChannelIndex < Clip.ChannelCount; ++ChannelIndex)
            {
                const RadientAnimationChannelDesc& Channel = Clip.pChannels[ChannelIndex];
                if (Channel.TargetIndex != Mapping.ClipTargetIndex)
                    continue;

                const RadientAnimationSamplerDesc&  Sampler = Clip.pSamplers[Channel.SamplerIndex];
                RadientAnimationPropertyBindingDesc Property;
                Property.Schema             = Target.Schema;
                Property.DestinationElement = Mapping.DestinationElement;
                Property.Property           = Channel.Property;
                Property.FirstArrayElement  = Channel.FirstArrayElement;
                Property.Value              = Sampler.Value;

                const Uint32 RequestIndex = static_cast<Uint32>(Pending.Properties.size());
                Pending.Properties.push_back(Property);
                Pending.SamplerIndices.push_back(Channel.SamplerIndex);
                PropertyRanges.push_back({Property.Schema,
                                          Property.DestinationElement,
                                          Property.Property,
                                          Property.FirstArrayElement,
                                          static_cast<Uint64>(Property.FirstArrayElement) + Property.Value.ArraySize,
                                          RequestIndex});
            }
        }

        std::sort(PropertyRanges.begin(), PropertyRanges.end());
        for (size_t RangeIndex = 1; RangeIndex < PropertyRanges.size(); ++RangeIndex)
        {
            const BoundAnimationPropertyRange& Previous = PropertyRanges[RangeIndex - 1];
            const BoundAnimationPropertyRange& Current  = PropertyRanges[RangeIndex];
            if (HaveSameBoundAnimationProperty(Previous, Current) && Current.First < Previous.End)
            {
                LOG_ERROR_MESSAGE("Radient animation binding destination ", DestinationIndex,
                                  " property requests ", Previous.RequestIndex, " and ", Current.RequestIndex,
                                  " address overlapping ranges");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        PendingDestinations.emplace_back(std::move(Pending));
    }

    return RADIENT_STATUS_OK;
}

struct AnimationSampleJob
{
    Uint32                           SamplerIndex = InvalidRadientAnimationSamplerIndex;
    RADIENT_ANIMATION_VALUE_SEMANTIC Semantic     = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
    size_t                           OutputOffset = 0;
    size_t                           OutputSize   = 0;
};

struct CompiledAnimationDestination
{
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    std::vector<Uint32>                                SampleJobIndices;
    std::vector<RadientAnimationPropertyUpdateDesc>    Updates;
};

struct AnimationPropertySemantic
{
    RadientAnimationSchemaID         Schema   = InvalidRadientAnimationSchemaID;
    RadientAnimationPropertyID       Property = InvalidRadientAnimationPropertyID;
    RADIENT_ANIMATION_VALUE_SEMANTIC Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
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

RADIENT_STATUS GetOrCreateAnimationSampleJob(const RadientAnimationClipDesc&  Clip,
                                             Uint32                           SamplerIndex,
                                             RADIENT_ANIMATION_VALUE_SEMANTIC Semantic,
                                             std::vector<AnimationSampleJob>& SampleJobs,
                                             size_t&                          ScratchSize,
                                             Uint32&                          SampleJobIndex)
{
    for (size_t Index = 0; Index < SampleJobs.size(); ++Index)
    {
        if (SampleJobs[Index].SamplerIndex == SamplerIndex && SampleJobs[Index].Semantic == Semantic)
        {
            SampleJobIndex = static_cast<Uint32>(Index);
            return RADIENT_STATUS_OK;
        }
    }

    if (!IsSumRepresentable<Uint32>(SampleJobs.size(), 1u))
    {
        LOG_ERROR_MESSAGE("Radient animation binding contains too many sampling jobs");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RadientAnimationSamplerDesc& Sampler      = Clip.pSamplers[SamplerIndex];
    const AnimationValueTypeInfo       TypeInfo     = GetAnimationValueTypeInfo(Sampler.Value.Type);
    Uint64                             OutputSize64 = 0;
    if (!CheckedMultiply(TypeInfo.NativeSize, Sampler.Value.ArraySize, OutputSize64) ||
        !IsAddressableSize(OutputSize64))
    {
        LOG_ERROR_MESSAGE("Radient animation binding sampling output size overflows addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const size_t OutputSize   = static_cast<size_t>(OutputSize64);
    size_t       OutputOffset = 0;
    if (!AppendAnimationScratchBlock(OutputSize, TypeInfo.NativeAlignment, ScratchSize, OutputOffset))
    {
        LOG_ERROR_MESSAGE("Radient animation binding sampling scratch size overflows addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    SampleJobIndex = static_cast<Uint32>(SampleJobs.size());
    SampleJobs.push_back({SamplerIndex, Semantic, OutputOffset, OutputSize});
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
                                std::vector<CompiledAnimationDestination> Destinations,
                                size_t                                    ScratchSize) :
        TBase{pRefCounters},
        m_pClip{pClip},
        m_pClipDesc{&m_pClip->GetDesc()},
        m_SampleJobs{std::move(SampleJobs)},
        m_Destinations{std::move(Destinations)}
    {
        VERIFY_EXPR(m_pClip != nullptr);

        const size_t ScratchWordCount = ScratchSize == 0 ?
            0 :
            1 + (ScratchSize - 1) / sizeof(std::max_align_t);
        m_Scratch.resize(ScratchWordCount);

        Uint8* const pScratch = reinterpret_cast<Uint8*>(m_Scratch.data());
        for (CompiledAnimationDestination& Destination : m_Destinations)
        {
            VERIFY_EXPR(Destination.Updates.size() == Destination.SampleJobIndices.size());
            for (size_t UpdateIndex = 0; UpdateIndex < Destination.Updates.size(); ++UpdateIndex)
            {
                const AnimationSampleJob& SampleJob            = m_SampleJobs[Destination.SampleJobIndices[UpdateIndex]];
                Destination.Updates[UpdateIndex].pValue        = pScratch + SampleJob.OutputOffset;
                Destination.Updates[UpdateIndex].ValueDataSize = static_cast<Uint64>(SampleJob.OutputSize);
            }
        }
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
        for (const AnimationSampleJob& SampleJob : m_SampleJobs)
        {
            SampleAnimationValue(Clip.pSamplers[SampleJob.SamplerIndex],
                                 SampleJob.Semantic,
                                 Info.Time,
                                 SampleJob.OutputSize,
                                 pScratch + SampleJob.OutputOffset);
        }

        bool AnyDestinationApplied = false;
        for (CompiledAnimationDestination& Destination : m_Destinations)
        {
            const RadientAnimationApplyInfo ApplyInfo{
                Destination.Updates.data(),
                static_cast<Uint32>(Destination.Updates.size()),
                Info.UpdateDerivedState,
            };
            const RADIENT_STATUS Status = Destination.pBinding->ApplyProperties(ApplyInfo);
            if (Status < 0)
                return Status;

            if (Status == RADIENT_STATUS_OK)
            {
                AnyDestinationApplied = true;
            }
            else if (Status != RADIENT_STATUS_NO_CHANGE)
            {
                LOG_ERROR_MESSAGE("Radient animation destination binding returned an invalid success status");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
        }

        return AnyDestinationApplied ? RADIENT_STATUS_OK : RADIENT_STATUS_NO_CHANGE;
    }

private:
    const RefCntAutoPtr<IRadientAnimationClipAsset> m_pClip;
    const RadientAnimationClipDesc* const           m_pClipDesc;
    const std::vector<AnimationSampleJob>           m_SampleJobs;
    std::vector<CompiledAnimationDestination>       m_Destinations;
    std::vector<std::max_align_t>                   m_Scratch;
};

} // namespace

RADIENT_STATUS CreateRadientAnimationBinding(IRadientAnimationClipAsset*        pClip,
                                             const RadientAnimationBindingDesc& BindingDesc,
                                             IRadientAnimationBinding**         ppBinding)
{
    VERIFY_EXPR(pClip != nullptr);
    VERIFY_EXPR(ppBinding != nullptr && *ppBinding == nullptr);

    const RadientAnimationClipDesc& Clip = pClip->GetDesc();

    std::vector<PendingAnimationDestination> PendingDestinations;
    const RADIENT_STATUS                     DescriptorStatus =
        BuildPendingAnimationDestinations(Clip, BindingDesc, PendingDestinations);
    if (DescriptorStatus != RADIENT_STATUS_OK)
        return DescriptorStatus;

    std::vector<AnimationSampleJob>           SampleJobs;
    std::vector<CompiledAnimationDestination> Destinations;
    std::vector<AnimationPropertySemantic>    PropertySemantics;
    size_t                                    ScratchSize = 0;

    Destinations.reserve(PendingDestinations.size());
    for (PendingAnimationDestination& Pending : PendingDestinations)
    {
        std::vector<RadientAnimationResolvedPropertyDesc>  ResolvedProperties(Pending.Properties.size());
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pDestinationBinding;
        const RADIENT_STATUS                               DestinationStatus = Pending.pDestination->CreateBinding(
            Pending.Properties.data(),
            static_cast<Uint32>(Pending.Properties.size()),
            ResolvedProperties.data(),
            pDestinationBinding.GetAddressOfEmpty());

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
        Destination.SampleJobIndices.reserve(Pending.Properties.size());
        Destination.Updates.resize(Pending.Properties.size());
        for (size_t PropertyIndex = 0; PropertyIndex < Pending.Properties.size(); ++PropertyIndex)
        {
            const RadientAnimationPropertyBindingDesc& Property = Pending.Properties[PropertyIndex];
            const RADIENT_ANIMATION_VALUE_SEMANTIC     Semantic = ResolvedProperties[PropertyIndex].Semantic;

            const RADIENT_STATUS SemanticStatus =
                ValidateAnimationPropertySemantic(Property, Semantic, PropertySemantics);
            if (SemanticStatus != RADIENT_STATUS_OK)
                return SemanticStatus;

            Uint32               SampleJobIndex = 0;
            const RADIENT_STATUS SampleJobStatus =
                GetOrCreateAnimationSampleJob(Clip,
                                              Pending.SamplerIndices[PropertyIndex],
                                              Semantic,
                                              SampleJobs,
                                              ScratchSize,
                                              SampleJobIndex);
            if (SampleJobStatus != RADIENT_STATUS_OK)
                return SampleJobStatus;

            Destination.SampleJobIndices.push_back(SampleJobIndex);
        }

        Destinations.emplace_back(std::move(Destination));
    }

    RefCntAutoPtr<RadientAnimationBindingImpl> pBinding{MakeNewRCObj<RadientAnimationBindingImpl>()(
        pClip, std::move(SampleJobs), std::move(Destinations), ScratchSize)};
    *ppBinding = pBinding.Detach();
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
