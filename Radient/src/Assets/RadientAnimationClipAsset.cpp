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
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Core/RadientValidation.hpp"
#include "Math/RadientMath.hpp"

#include "RadientAnimation.h"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
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

bool GetExpectedValueDataSize(const RadientAnimationSamplerDesc& Sampler,
                              Uint32                             NativeValueSize,
                              Uint64&                            ExpectedSize) noexcept
{
    ExpectedSize = NativeValueSize;
    if (!CheckedMultiply(ExpectedSize, Sampler.Value.ArraySize, ExpectedSize) ||
        !CheckedMultiply(ExpectedSize, Sampler.KeyframeCount, ExpectedSize))
    {
        return false;
    }

    if (Sampler.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE &&
        !CheckedMultiply(ExpectedSize, 3u, ExpectedSize))
    {
        return false;
    }

    return true;
}

struct AnimationTargetKey
{
    RadientAnimationSchemaID Schema      = InvalidRadientAnimationSchemaID;
    RadientAnimationObjectID Object      = InvalidRadientAnimationObject;
    Uint32                   TargetIndex = InvalidRadientAnimationTargetIndex;

    bool operator<(const AnimationTargetKey& Rhs) const noexcept
    {
        if (Schema != Rhs.Schema)
            return Schema < Rhs.Schema;
        if (Object != Rhs.Object)
            return Object < Rhs.Object;
        return TargetIndex < Rhs.TargetIndex;
    }
};

bool HaveSameTargetIdentity(const AnimationTargetKey& Lhs, const AnimationTargetKey& Rhs) noexcept
{
    return Lhs.Object == Rhs.Object && Lhs.Schema == Rhs.Schema;
}

struct AnimationChannelRange
{
    Uint32                       TargetIndex  = InvalidRadientAnimationTargetIndex;
    RadientAnimationPropertyID   Property     = InvalidRadientAnimationPropertyID;
    RADIENT_ANIMATION_VALUE_TYPE Type         = RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN;
    Uint32                       First        = 0;
    Uint32                       End          = 0;
    Uint32                       ChannelIndex = 0;

    bool operator<(const AnimationChannelRange& Rhs) const noexcept
    {
        if (TargetIndex != Rhs.TargetIndex)
            return TargetIndex < Rhs.TargetIndex;
        if (Property != Rhs.Property)
            return Property < Rhs.Property;
        if (First != Rhs.First)
            return First < Rhs.First;
        if (End != Rhs.End)
            return End < Rhs.End;
        return ChannelIndex < Rhs.ChannelIndex;
    }
};

bool HaveSameTargetProperty(const AnimationChannelRange& Lhs,
                            const AnimationChannelRange& Rhs) noexcept
{
    return Lhs.TargetIndex == Rhs.TargetIndex && Lhs.Property == Rhs.Property;
}

RADIENT_STATUS ValidateAnimationClipDesc(const RadientAnimationClipDesc& Desc)
{
    if (!RadientMath::IsFiniteNonNegative(Desc.Duration))
    {
        LOG_ERROR_MESSAGE("Radient animation clip duration must be finite and non-negative");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if ((Desc.TargetCount == 0) != (Desc.pTargets == nullptr))
    {
        LOG_ERROR_MESSAGE("Radient animation clip target count and pointer do not form a valid pair");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if ((Desc.SamplerCount == 0) != (Desc.pSamplers == nullptr))
    {
        LOG_ERROR_MESSAGE("Radient animation clip sampler count and pointer do not form a valid pair");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if ((Desc.ChannelCount == 0) != (Desc.pChannels == nullptr))
    {
        LOG_ERROR_MESSAGE("Radient animation clip channel count and pointer do not form a valid pair");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (!IsAddressableArray(Desc.TargetCount, sizeof(RadientAnimationTargetDesc)) ||
        !IsAddressableArray(Desc.TargetCount, sizeof(AnimationTargetKey)) ||
        !IsAddressableArray(Desc.SamplerCount, sizeof(RadientAnimationSamplerDesc)) ||
        !IsAddressableArray(Desc.ChannelCount, sizeof(RadientAnimationChannelDesc)) ||
        !IsAddressableArray(Desc.ChannelCount, sizeof(AnimationChannelRange)))
    {
        LOG_ERROR_MESSAGE("Radient animation clip table sizes overflow addressable memory");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const bool Empty = Desc.TargetCount == 0 && Desc.SamplerCount == 0 && Desc.ChannelCount == 0;
    if (Empty)
        return RADIENT_STATUS_OK;

    if (Desc.TargetCount == 0 || Desc.SamplerCount == 0 || Desc.ChannelCount == 0)
    {
        LOG_ERROR_MESSAGE("A nonempty Radient animation clip must contain targets, samplers, and channels");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::vector<AnimationTargetKey> TargetKeys;
    TargetKeys.reserve(Desc.TargetCount);
    for (Uint32 TargetIndex = 0; TargetIndex < Desc.TargetCount; ++TargetIndex)
    {
        const RadientAnimationTargetDesc& Target = Desc.pTargets[TargetIndex];
        if (Target.Schema == InvalidRadientAnimationSchemaID)
        {
            LOG_ERROR_MESSAGE("Radient animation clip target ", TargetIndex, " uses an invalid schema identifier");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Target.Object == InvalidRadientAnimationObject)
        {
            LOG_ERROR_MESSAGE("Radient animation clip target ", TargetIndex, " uses an invalid source object identifier");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        TargetKeys.push_back({Target.Schema, Target.Object, TargetIndex});
    }

    std::sort(TargetKeys.begin(), TargetKeys.end());
    for (size_t Index = 1; Index < TargetKeys.size(); ++Index)
    {
        if (HaveSameTargetIdentity(TargetKeys[Index - 1], TargetKeys[Index]))
        {
            LOG_ERROR_MESSAGE("Radient animation clip targets ", TargetKeys[Index - 1].TargetIndex,
                              " and ", TargetKeys[Index].TargetIndex,
                              " use the same schema and source object identifier");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    for (Uint32 SamplerIndex = 0; SamplerIndex < Desc.SamplerCount; ++SamplerIndex)
    {
        const RadientAnimationSamplerDesc& Sampler  = Desc.pSamplers[SamplerIndex];
        const AnimationValueTypeInfo       TypeInfo = GetAnimationValueTypeInfo(Sampler.Value.Type);
        if (TypeInfo.NativeSize == 0)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " uses an invalid value type");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.Value.ArraySize == 0)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " has an empty value array");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.Interpolation >= RADIENT_ANIMATION_INTERPOLATION_COUNT)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " uses an invalid interpolation mode");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (TypeInfo.IsDiscrete && Sampler.Interpolation != RADIENT_ANIMATION_INTERPOLATION_STEP)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                              " must use STEP interpolation for a Boolean or integer value type");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.KeyframeCount == 0)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " does not contain keyframes");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.pTimes == nullptr)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " keyframe times must not be null");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.pValues == nullptr)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " values must not be null");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (!IsAddressableArray(Sampler.KeyframeCount, sizeof(Float32)))
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " time-data size overflows addressable memory");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        Uint64 ExpectedValueDataSize = 0;
        if (!GetExpectedValueDataSize(Sampler, TypeInfo.NativeSize, ExpectedValueDataSize) ||
            !IsAddressableSize(ExpectedValueDataSize))
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " value-data size overflows addressable memory");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Sampler.ValueDataSize != ExpectedValueDataSize)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                              " value-data size is ", Sampler.ValueDataSize,
                              " bytes, but ", ExpectedValueDataSize, " bytes are required");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        for (Uint32 KeyIndex = 0; KeyIndex < Sampler.KeyframeCount; ++KeyIndex)
        {
            const Float32 Time = Sampler.pTimes[KeyIndex];
            if (!RadientMath::IsFiniteNonNegative(Time) || Time > Desc.Duration)
            {
                LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                                  " keyframe ", KeyIndex, " has a time outside the clip duration");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            if (KeyIndex != 0 && Time <= Sampler.pTimes[KeyIndex - 1])
            {
                LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                                  " keyframe times must be strictly increasing");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        if (Sampler.Value.Type == RADIENT_ANIMATION_VALUE_TYPE_BOOL)
        {
            const Uint8* const pValues = static_cast<const Uint8*>(Sampler.pValues);
            for (Uint64 ValueIndex = 0; ValueIndex < Sampler.ValueDataSize; ++ValueIndex)
            {
                if (pValues[ValueIndex] > 1)
                {
                    LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                                      " contains a Boolean value other than zero or one at index ", ValueIndex);
                    return RADIENT_STATUS_INVALID_ARGUMENT;
                }
            }
        }
        else if (TypeInfo.IsFloatingPoint)
        {
            const Uint8* const pValueBytes    = static_cast<const Uint8*>(Sampler.pValues);
            const Uint64       ComponentCount = Sampler.ValueDataSize / sizeof(Float32);
            for (Uint64 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
            {
                Float32 Value = 0.f;
                std::memcpy(&Value, pValueBytes + ComponentIndex * sizeof(Float32), sizeof(Value));
                if (!RadientMath::IsFinite(Value))
                {
                    LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex,
                                      " contains a non-finite floating-point component at index ", ComponentIndex);
                    return RADIENT_STATUS_INVALID_ARGUMENT;
                }
            }
        }
    }

    std::vector<Uint8>                 ReferencedTargets(Desc.TargetCount, 0);
    std::vector<Uint8>                 ReferencedSamplers(Desc.SamplerCount, 0);
    std::vector<AnimationChannelRange> ChannelRanges;
    ChannelRanges.reserve(Desc.ChannelCount);
    for (Uint32 ChannelIndex = 0; ChannelIndex < Desc.ChannelCount; ++ChannelIndex)
    {
        const RadientAnimationChannelDesc& Channel = Desc.pChannels[ChannelIndex];
        if (Channel.TargetIndex >= Desc.TargetCount)
        {
            LOG_ERROR_MESSAGE("Radient animation clip channel ", ChannelIndex, " references invalid target ", Channel.TargetIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Channel.SamplerIndex >= Desc.SamplerCount)
        {
            LOG_ERROR_MESSAGE("Radient animation clip channel ", ChannelIndex, " references invalid sampler ", Channel.SamplerIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Channel.Property == InvalidRadientAnimationPropertyID)
        {
            LOG_ERROR_MESSAGE("Radient animation clip channel ", ChannelIndex, " uses an invalid property identifier");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const RadientAnimationSamplerDesc& Sampler = Desc.pSamplers[Channel.SamplerIndex];
        if (!IsSumRepresentable<Uint32>(Channel.FirstArrayElement, Sampler.Value.ArraySize))
        {
            LOG_ERROR_MESSAGE("Radient animation clip channel ", ChannelIndex, " array range overflows Uint32");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        ReferencedTargets[Channel.TargetIndex]   = 1;
        ReferencedSamplers[Channel.SamplerIndex] = 1;
        ChannelRanges.push_back({Channel.TargetIndex,
                                 Channel.Property,
                                 Sampler.Value.Type,
                                 Channel.FirstArrayElement,
                                 Channel.FirstArrayElement + Sampler.Value.ArraySize,
                                 ChannelIndex});
    }

    for (Uint32 TargetIndex = 0; TargetIndex < Desc.TargetCount; ++TargetIndex)
    {
        if (ReferencedTargets[TargetIndex] == 0)
        {
            LOG_ERROR_MESSAGE("Radient animation clip target ", TargetIndex, " is not referenced by any channel");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }
    for (Uint32 SamplerIndex = 0; SamplerIndex < Desc.SamplerCount; ++SamplerIndex)
    {
        if (ReferencedSamplers[SamplerIndex] == 0)
        {
            LOG_ERROR_MESSAGE("Radient animation clip sampler ", SamplerIndex, " is not referenced by any channel");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    std::sort(ChannelRanges.begin(), ChannelRanges.end());
    for (size_t Index = 1; Index < ChannelRanges.size(); ++Index)
    {
        const AnimationChannelRange& Previous = ChannelRanges[Index - 1];
        const AnimationChannelRange& Current  = ChannelRanges[Index];
        if (!HaveSameTargetProperty(Previous, Current))
            continue;

        if (Previous.Type != Current.Type)
        {
            LOG_ERROR_MESSAGE("Radient animation clip channels ", Previous.ChannelIndex,
                              " and ", Current.ChannelIndex,
                              " use different native value types for the same target property");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Current.First < Previous.End)
        {
            LOG_ERROR_MESSAGE("Radient animation clip channels ", Previous.ChannelIndex,
                              " and ", Current.ChannelIndex,
                              " address overlapping ranges of the same target property");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

class CheckedLinearAllocationSize
{
public:
    template <typename Type>
    bool AddArray(Uint64 Count) noexcept
    {
        Uint64 Size = 0;
        return CheckedMultiply(sizeof(Type), Count, Size) && Add(Size, alignof(Type));
    }

    bool AddString(const Char* String) noexcept
    {
        if (String == nullptr)
            return true;

        const size_t Length = std::strlen(String);
        return IsSumRepresentable<size_t>(Length, 1u) && Add(Length + 1u, alignof(Char));
    }

    bool Add(Uint64 Size64, size_t Alignment) noexcept
    {
        if (Size64 == 0)
            return true;
        if (!IsAddressableSize(Size64))
            return false;

        size_t Size = static_cast<size_t>(Size64);
        if (m_CurrentAlignment == 0)
            m_CurrentAlignment = sizeof(void*);

        if (Alignment > m_CurrentAlignment)
        {
            if (!CheckedAdd(Alignment - m_CurrentAlignment))
                return false;
        }
        m_CurrentAlignment = Alignment;

        const size_t AlignmentMask = Alignment - 1u;
        if (!IsSumRepresentable<size_t>(Size, AlignmentMask))
            return false;
        Size = (Size + AlignmentMask) & ~AlignmentMask;
        return CheckedAdd(Size);
    }

    bool Finish() noexcept
    {
        const size_t AlignmentMask = sizeof(void*) - 1u;
        if (!IsSumRepresentable<size_t>(m_Size, AlignmentMask))
            return false;
        m_Size = (m_Size + AlignmentMask) & ~AlignmentMask;
        return true;
    }

private:
    bool CheckedAdd(size_t Size) noexcept
    {
        if (!IsSumRepresentable<size_t>(m_Size, Size))
            return false;
        m_Size += Size;
        return true;
    }

private:
    size_t m_Size             = 0;
    size_t m_CurrentAlignment = 0;
};

bool CanPackAnimationClip(const RadientAnimationClipDesc& Desc,
                          const std::string&              URI) noexcept
{
    CheckedLinearAllocationSize Size;
    if (!Size.AddString(URI.c_str()) ||
        !Size.AddString(Desc.Name != nullptr ? Desc.Name : "") ||
        !Size.AddArray<RadientAnimationTargetDesc>(Desc.TargetCount))
    {
        return false;
    }

    for (Uint32 TargetIndex = 0; TargetIndex < Desc.TargetCount; ++TargetIndex)
    {
        if (!Size.AddString(Desc.pTargets[TargetIndex].Name != nullptr ? Desc.pTargets[TargetIndex].Name : ""))
            return false;
    }

    if (!Size.AddArray<RadientAnimationSamplerDesc>(Desc.SamplerCount))
        return false;
    for (Uint32 SamplerIndex = 0; SamplerIndex < Desc.SamplerCount; ++SamplerIndex)
    {
        const RadientAnimationSamplerDesc& Sampler  = Desc.pSamplers[SamplerIndex];
        const AnimationValueTypeInfo       TypeInfo = GetAnimationValueTypeInfo(Sampler.Value.Type);
        if (!Size.AddArray<Float32>(Sampler.KeyframeCount) ||
            !Size.Add(Sampler.ValueDataSize, TypeInfo.NativeAlignment))
        {
            return false;
        }
    }

    return Size.AddArray<RadientAnimationChannelDesc>(Desc.ChannelCount) && Size.Finish();
}

using PackedMemory = std::unique_ptr<void, STDDeleterRawMem<void>>;

class PackedAnimationClipData final
{
public:
    PackedAnimationClipData(const RadientAnimationClipDesc& Desc,
                            const std::string&              URI)
    {
        const Char* const Name = Desc.Name != nullptr ? Desc.Name : "";

        FixedLinearAllocator Allocator{GetRawAllocator()};
        Allocator.AddSpaceForString(URI.c_str());
        Allocator.AddSpaceForString(Name);
        Allocator.AddSpace<RadientAnimationTargetDesc>(Desc.TargetCount);
        for (Uint32 TargetIndex = 0; TargetIndex < Desc.TargetCount; ++TargetIndex)
            Allocator.AddSpaceForString(Desc.pTargets[TargetIndex].Name != nullptr ? Desc.pTargets[TargetIndex].Name : "");

        Allocator.AddSpace<RadientAnimationSamplerDesc>(Desc.SamplerCount);
        for (Uint32 SamplerIndex = 0; SamplerIndex < Desc.SamplerCount; ++SamplerIndex)
        {
            const RadientAnimationSamplerDesc& Sampler  = Desc.pSamplers[SamplerIndex];
            const AnimationValueTypeInfo       TypeInfo = GetAnimationValueTypeInfo(Sampler.Value.Type);
            Allocator.AddSpace<Float32>(Sampler.KeyframeCount);
            Allocator.AddSpace(static_cast<size_t>(Sampler.ValueDataSize), TypeInfo.NativeAlignment);
        }
        Allocator.AddSpace<RadientAnimationChannelDesc>(Desc.ChannelCount);

        Allocator.Reserve();
        const size_t MemorySize = Allocator.GetReservedSize();
        m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

        FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
        m_Reference.URI = Writer.CopyString(URI);
        m_Desc.Name     = Writer.CopyString(Name);

        RadientAnimationTargetDesc* const pTargets = Writer.ConstructArray<RadientAnimationTargetDesc>(Desc.TargetCount);
        for (Uint32 TargetIndex = 0; TargetIndex < Desc.TargetCount; ++TargetIndex)
        {
            const RadientAnimationTargetDesc& SrcTarget = Desc.pTargets[TargetIndex];
            RadientAnimationTargetDesc&       DstTarget = pTargets[TargetIndex];
            DstTarget                                   = SrcTarget;
            DstTarget.Name                              = Writer.CopyString(SrcTarget.Name != nullptr ? SrcTarget.Name : "");
        }

        RadientAnimationSamplerDesc* const pSamplers = Writer.ConstructArray<RadientAnimationSamplerDesc>(Desc.SamplerCount);
        for (Uint32 SamplerIndex = 0; SamplerIndex < Desc.SamplerCount; ++SamplerIndex)
        {
            const RadientAnimationSamplerDesc& SrcSampler = Desc.pSamplers[SamplerIndex];
            RadientAnimationSamplerDesc&       DstSampler = pSamplers[SamplerIndex];
            const AnimationValueTypeInfo       TypeInfo   = GetAnimationValueTypeInfo(SrcSampler.Value.Type);

            DstSampler         = SrcSampler;
            DstSampler.pTimes  = Writer.CopyArray(SrcSampler.pTimes, SrcSampler.KeyframeCount);
            DstSampler.pValues = Writer.Copy(SrcSampler.pValues,
                                             static_cast<size_t>(SrcSampler.ValueDataSize),
                                             TypeInfo.NativeAlignment);
        }

        m_Reference.Version = 1;
        m_Desc.Duration     = Desc.Duration;
        m_Desc.pTargets     = pTargets;
        m_Desc.TargetCount  = Desc.TargetCount;
        m_Desc.pSamplers    = pSamplers;
        m_Desc.SamplerCount = Desc.SamplerCount;
        m_Desc.pChannels    = Writer.CopyArray(Desc.pChannels, Desc.ChannelCount);
        m_Desc.ChannelCount = Desc.ChannelCount;
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
    }

    const RadientAssetReference& GetReference() const noexcept
    {
        return m_Reference;
    }

    const RadientAnimationClipDesc& GetDesc() const noexcept
    {
        return m_Desc;
    }

private:
    PackedMemory             m_Memory;
    RadientAssetReference    m_Reference;
    RadientAnimationClipDesc m_Desc;
};

class RadientAnimationClipAssetImpl final : public ObjectBase<IRadientAnimationClipAsset>
{
public:
    using TBase = ObjectBase<IRadientAnimationClipAsset>;

    RadientAnimationClipAssetImpl(IReferenceCounters*             pRefCounters,
                                  const RadientAnimationClipDesc& Desc,
                                  const std::string&              URI) :
        TBase{pRefCounters},
        m_Data{Desc, URI}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientAnimationClipAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientAnimationClipAsset*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Data.GetReference();
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_ANIMATION_CLIP;
    }

    virtual const RadientAnimationClipDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.GetDesc();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateBinding(const RadientAnimationBindingDesc& BindingDesc,
                                                            IRadientAnimationBinding**         ppBinding) override final
    {
        if (ppBinding == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        DEV_CHECK_ERR(*ppBinding == nullptr, "Output animation binding pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
        if (*ppBinding != nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        try
        {
            return CreateRadientAnimationBinding(this, BindingDesc, ppBinding);
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to create a Radient animation binding: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
        catch (...)
        {
            LOG_ERROR_MESSAGE("Failed to create a Radient animation binding: unknown exception");
            return RADIENT_STATUS_FAILED;
        }
    }

private:
    PackedAnimationClipData m_Data;
};

} // namespace

RADIENT_STATUS RadientAssetManagerImpl::CreateAnimationClip(const RadientAnimationClipDesc& ClipDesc,
                                                            IRadientAnimationClipAsset**    ppClip)
{
    if (ppClip == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppClip == nullptr, "Output animation clip pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppClip = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateAnimationClipDesc(ClipDesc);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        const std::string URI = MakeRadientAssetURI("animation-clip");
        if (!CanPackAnimationClip(ClipDesc, URI))
        {
            LOG_ERROR_MESSAGE("Radient animation clip requires more packed storage than the platform can address");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        RefCntAutoPtr<RadientAnimationClipAssetImpl> pClip{
            MakeNewRCObj<RadientAnimationClipAssetImpl>()(ClipDesc, URI)};
        *ppClip = pClip.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient animation clip: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient animation clip: unknown exception");
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
