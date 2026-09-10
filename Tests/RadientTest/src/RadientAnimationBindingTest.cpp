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

#include "Assets/RadientAssetManagerImpl.hpp"

#include "RadientAnimation.h"
#include "RadientSkinning.h"

#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr RadientAnimationSchemaID TestAnimationSchemaID =
    {0x542837a1, 0xc634, 0x4cc9, {0x90, 0xb2, 0x4c, 0x11, 0xd7, 0x3d, 0xb4, 0x68}};

static constexpr RadientAnimationPropertyID TestPropertyA          = 11;
static constexpr RadientAnimationPropertyID TestPropertyB          = 12;
static constexpr RadientAnimationPropertyID TestPropertyC          = 13;
static constexpr RadientAnimationPropertyID TestQuaternionProperty = 14;

enum class AnimationSamplingComponentType
{
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
};

struct AnimationSamplingValueTypeCase
{
    RADIENT_ANIMATION_VALUE_TYPE   Type;
    AnimationSamplingComponentType ComponentType;
    Uint32                         ComponentCount;
    const char*                    Name;
};

static constexpr AnimationSamplingValueTypeCase AnimationSamplingValueTypeCases[] =
    {
        {RADIENT_ANIMATION_VALUE_TYPE_BOOL, AnimationSamplingComponentType::Boolean, 1, "Bool"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT, AnimationSamplingComponentType::SignedInteger, 1, "Int"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT2, AnimationSamplingComponentType::SignedInteger, 2, "Int2"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT3, AnimationSamplingComponentType::SignedInteger, 3, "Int3"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT4, AnimationSamplingComponentType::SignedInteger, 4, "Int4"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT, AnimationSamplingComponentType::UnsignedInteger, 1, "Uint"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT2, AnimationSamplingComponentType::UnsignedInteger, 2, "Uint2"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT3, AnimationSamplingComponentType::UnsignedInteger, 3, "Uint3"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT4, AnimationSamplingComponentType::UnsignedInteger, 4, "Uint4"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, AnimationSamplingComponentType::FloatingPoint, 1, "Float"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, AnimationSamplingComponentType::FloatingPoint, 2, "Float2"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, AnimationSamplingComponentType::FloatingPoint, 3, "Float3"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, AnimationSamplingComponentType::FloatingPoint, 4, "Float4"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2, AnimationSamplingComponentType::FloatingPoint, 4, "Float2x2"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3, AnimationSamplingComponentType::FloatingPoint, 9, "Float3x3"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4, AnimationSamplingComponentType::FloatingPoint, 16, "Float4x4"},
};
static_assert(sizeof(AnimationSamplingValueTypeCases) / sizeof(AnimationSamplingValueTypeCases[0]) ==
                  static_cast<size_t>(RADIENT_ANIMATION_VALUE_TYPE_COUNT - 1),
              "Every native animation value type must have sampling coverage");

struct AnimationSamplingCase
{
    AnimationSamplingValueTypeCase  ValueType;
    RADIENT_ANIMATION_INTERPOLATION Interpolation;
};

const char* GetAnimationInterpolationName(RADIENT_ANIMATION_INTERPOLATION Interpolation)
{
    switch (Interpolation)
    {
        case RADIENT_ANIMATION_INTERPOLATION_STEP:
            return "Step";

        case RADIENT_ANIMATION_INTERPOLATION_LINEAR:
            return "Linear";

        case RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE:
            return "CubicSpline";

        default:
            return "Unknown";
    }
}

std::vector<AnimationSamplingCase> GetAnimationSamplingCases()
{
    std::vector<AnimationSamplingCase> Cases;
    for (const AnimationSamplingValueTypeCase& ValueType : AnimationSamplingValueTypeCases)
    {
        Cases.push_back({ValueType, RADIENT_ANIMATION_INTERPOLATION_STEP});
        if (ValueType.ComponentType == AnimationSamplingComponentType::FloatingPoint)
        {
            Cases.push_back({ValueType, RADIENT_ANIMATION_INTERPOLATION_LINEAR});
            Cases.push_back({ValueType, RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE});
        }
    }
    return Cases;
}

std::string GetAnimationSamplingCaseName(const testing::TestParamInfo<AnimationSamplingCase>& Info)
{
    return std::string{Info.param.ValueType.Name} + "_" +
        GetAnimationInterpolationName(Info.param.Interpolation);
}

class ErrorAllowanceScope
{
public:
    explicit ErrorAllowanceScope(Int32 Count = 16)
    {
        TestingEnvironment::SetErrorAllowance(Count);
    }

    ~ErrorAllowanceScope()
    {
        TestingEnvironment::SetErrorAllowance(0);
    }
};

size_t GetAnimationValueSize(RADIENT_ANIMATION_VALUE_TYPE Type)
{
    switch (Type)
    {
        case RADIENT_ANIMATION_VALUE_TYPE_BOOL:
            return sizeof(Uint8);

        case RADIENT_ANIMATION_VALUE_TYPE_INT:
            return sizeof(Int32);

        case RADIENT_ANIMATION_VALUE_TYPE_INT2:
            return sizeof(Int32) * 2;

        case RADIENT_ANIMATION_VALUE_TYPE_INT3:
            return sizeof(Int32) * 3;

        case RADIENT_ANIMATION_VALUE_TYPE_INT4:
            return sizeof(Int32) * 4;

        case RADIENT_ANIMATION_VALUE_TYPE_UINT:
            return sizeof(Uint32);

        case RADIENT_ANIMATION_VALUE_TYPE_UINT2:
            return sizeof(Uint32) * 2;

        case RADIENT_ANIMATION_VALUE_TYPE_UINT3:
            return sizeof(Uint32) * 3;

        case RADIENT_ANIMATION_VALUE_TYPE_UINT4:
            return sizeof(Uint32) * 4;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT:
            return sizeof(Float32);

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT2:
            return sizeof(Float32) * 2;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3:
            return sizeof(Float32) * 3;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4:
            return sizeof(Float32) * 4;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2:
            return sizeof(Float32) * 4;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3:
            return sizeof(Float32) * 9;

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4:
            return sizeof(Float32) * 16;

        default:
            return 0;
    }
}

struct TestAnimationSamplerData
{
    RadientAnimationValueDesc       Value;
    RADIENT_ANIMATION_INTERPOLATION Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    std::vector<Float32>            Times;
    std::vector<std::max_align_t>   ValueStorage;
    Uint64                          ValueDataSize = 0;
};

class TestAnimationClipBuilder
{
public:
    Uint32 AddTarget(RadientAnimationObjectID Object,
                     RadientAnimationSchemaID Schema = TestAnimationSchemaID)
    {
        RadientAnimationTargetDesc Target;
        Target.Schema = Schema;
        Target.Object = Object;
        m_Targets.push_back(Target);
        return static_cast<Uint32>(m_Targets.size() - 1);
    }

    template <typename ValueType>
    Uint32 AddSampler(RADIENT_ANIMATION_VALUE_TYPE    Type,
                      RADIENT_ANIMATION_INTERPOLATION Interpolation,
                      std::vector<Float32>            Times,
                      const std::vector<ValueType>&   Values,
                      Uint32                          ArraySize = 1)
    {
        TestAnimationSamplerData Sampler;
        Sampler.Value.Type      = Type;
        Sampler.Value.ArraySize = ArraySize;
        Sampler.Interpolation   = Interpolation;
        Sampler.Times           = std::move(Times);
        Sampler.ValueDataSize   = static_cast<Uint64>(Values.size() * sizeof(ValueType));
        Sampler.ValueStorage.resize(
            (static_cast<size_t>(Sampler.ValueDataSize) + sizeof(std::max_align_t) - 1) /
            sizeof(std::max_align_t));
        if (!Values.empty())
            std::memcpy(Sampler.ValueStorage.data(), Values.data(), static_cast<size_t>(Sampler.ValueDataSize));

        m_Samplers.push_back(std::move(Sampler));
        return static_cast<Uint32>(m_Samplers.size() - 1);
    }

    void AddChannel(Uint32                     TargetIndex,
                    RadientAnimationPropertyID Property,
                    Uint32                     SamplerIndex,
                    Uint32                     FirstArrayElement = 0)
    {
        RadientAnimationChannelDesc Channel;
        Channel.TargetIndex       = TargetIndex;
        Channel.Property          = Property;
        Channel.FirstArrayElement = FirstArrayElement;
        Channel.SamplerIndex      = SamplerIndex;
        m_Channels.push_back(Channel);
    }

    RefCntAutoPtr<IRadientAnimationClipAsset> Create(RadientAssetManagerImpl& AssetManager) const
    {
        std::vector<RadientAnimationSamplerDesc> Samplers(m_Samplers.size());
        for (size_t SamplerIndex = 0; SamplerIndex < m_Samplers.size(); ++SamplerIndex)
        {
            const TestAnimationSamplerData& Source = m_Samplers[SamplerIndex];
            RadientAnimationSamplerDesc&    Dest   = Samplers[SamplerIndex];
            Dest.Value                             = Source.Value;
            Dest.Interpolation                     = Source.Interpolation;
            Dest.pTimes                            = Source.Times.data();
            Dest.pValues                           = Source.ValueStorage.data();
            Dest.ValueDataSize                     = Source.ValueDataSize;
            Dest.KeyframeCount                     = static_cast<Uint32>(Source.Times.size());
        }

        RadientAnimationClipDesc Desc;
        Desc.Name         = "Generic binding test clip";
        Desc.Duration     = Duration;
        Desc.pTargets     = m_Targets.empty() ? nullptr : m_Targets.data();
        Desc.TargetCount  = static_cast<Uint32>(m_Targets.size());
        Desc.pSamplers    = Samplers.empty() ? nullptr : Samplers.data();
        Desc.SamplerCount = static_cast<Uint32>(Samplers.size());
        Desc.pChannels    = m_Channels.empty() ? nullptr : m_Channels.data();
        Desc.ChannelCount = static_cast<Uint32>(m_Channels.size());

        RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
        EXPECT_EQ(AssetManager.CreateAnimationClip(Desc, pClip.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pClip;
    }

public:
    Float32 Duration = 1.f;

private:
    std::vector<RadientAnimationTargetDesc>  m_Targets;
    std::vector<TestAnimationSamplerData>    m_Samplers;
    std::vector<RadientAnimationChannelDesc> m_Channels;
};

struct CapturedAnimationUpdate
{
    Bool                            UpdateDerivedState = False;
    std::vector<std::vector<Uint8>> Values;
    std::vector<std::uintptr_t>     OutputAddresses;
    std::vector<size_t>             ValueDataSizes;
};

struct TestAnimationDestinationState
{
    Uint32 DestinationID = 0;

    RADIENT_STATUS CreateStatus               = RADIENT_STATUS_OK;
    bool           ReturnNullChild            = false;
    bool           ReturnChildOnCreateFailure = false;

    std::vector<std::pair<RadientAnimationPropertyID, RADIENT_ANIMATION_VALUE_SEMANTIC>> Semantics;
    std::vector<RADIENT_ANIMATION_VALUE_SEMANTIC>                                        SemanticsByRequest;
    std::vector<RADIENT_STATUS>                                                          BeginStatuses;
    std::vector<RADIENT_STATUS>                                                          EndStatuses;

    std::vector<std::vector<RadientAnimationPropertyBindingDesc>> CreateRequests;
    Uint32                                                        BeginCallCount = 0;
    std::vector<CapturedAnimationUpdate>                          EndCalls;
    std::shared_ptr<std::vector<Uint32>>                          GlobalEndOrder;

    Uint32 LiveChildBindings      = 0;
    Uint32 DestroyedChildBindings = 0;
    bool   DestinationDestroyed   = false;

    RADIENT_ANIMATION_VALUE_SEMANTIC GetSemantic(const RadientAnimationPropertyBindingDesc& Property,
                                                 Uint32                                     RequestIndex) const
    {
        if (RequestIndex < SemanticsByRequest.size())
            return SemanticsByRequest[RequestIndex];

        for (const auto& Semantic : Semantics)
        {
            if (Semantic.first == Property.Property)
                return Semantic.second;
        }
        return RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
    }
};

class TestAnimationDestinationBinding final : public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    TestAnimationDestinationBinding(IReferenceCounters*                              pRefCounters,
                                    IRadientAnimationDestination*                    pDestination,
                                    std::shared_ptr<TestAnimationDestinationState>   State,
                                    std::vector<RadientAnimationPropertyBindingDesc> Properties) :
        TBase{pRefCounters},
        m_pDestination{pDestination},
        m_State{std::move(State)},
        m_Properties{std::move(Properties)},
        m_OutputStorage(m_Properties.size()),
        m_OutputPointers(m_Properties.size()),
        m_OutputSizes(m_Properties.size())
    {
        for (size_t PropertyIndex = 0; PropertyIndex < m_Properties.size(); ++PropertyIndex)
        {
            const RadientAnimationValueDesc& Value = m_Properties[PropertyIndex].Value;
            const size_t                     Size  = GetAnimationValueSize(Value.Type) * Value.ArraySize;
            m_OutputSizes[PropertyIndex]           = Size;
            m_OutputStorage[PropertyIndex].resize(
                (Size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
            m_OutputPointers[PropertyIndex] = m_OutputStorage[PropertyIndex].data();
        }

        ++m_State->LiveChildBindings;
    }

    ~TestAnimationDestinationBinding()
    {
        --m_State->LiveChildBindings;
        ++m_State->DestroyedChildBindings;
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE BeginUpdate(void* const** ppOutputs) override final
    {
        if (ppOutputs == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppOutputs = nullptr;

        const size_t         CallIndex = m_State->BeginCallCount++;
        const RADIENT_STATUS Status    = CallIndex < m_State->BeginStatuses.size() ?
            m_State->BeginStatuses[CallIndex] :
            RADIENT_STATUS_OK;
        if (Status != RADIENT_STATUS_OK)
            return Status;

        *ppOutputs = m_OutputPointers.data();
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE EndUpdate(Bool UpdateDerivedState) override final
    {
        CapturedAnimationUpdate Call;
        Call.UpdateDerivedState = UpdateDerivedState;
        Call.Values.resize(m_Properties.size());
        Call.OutputAddresses.resize(m_Properties.size());
        Call.ValueDataSizes.resize(m_Properties.size());
        for (size_t PropertyIndex = 0; PropertyIndex < m_Properties.size(); ++PropertyIndex)
        {
            Call.OutputAddresses[PropertyIndex] =
                reinterpret_cast<std::uintptr_t>(m_OutputPointers[PropertyIndex]);
            Call.ValueDataSizes[PropertyIndex] = m_OutputSizes[PropertyIndex];
            Call.Values[PropertyIndex].resize(m_OutputSizes[PropertyIndex]);
            std::memcpy(Call.Values[PropertyIndex].data(),
                        m_OutputPointers[PropertyIndex],
                        m_OutputSizes[PropertyIndex]);
        }

        if (m_State->GlobalEndOrder)
            m_State->GlobalEndOrder->push_back(m_State->DestinationID);

        const size_t CallIndex = m_State->EndCalls.size();
        m_State->EndCalls.push_back(std::move(Call));
        return CallIndex < m_State->EndStatuses.size() ?
            m_State->EndStatuses[CallIndex] :
            RADIENT_STATUS_OK;
    }

private:
    RefCntAutoPtr<IRadientAnimationDestination>      m_pDestination;
    std::shared_ptr<TestAnimationDestinationState>   m_State;
    std::vector<RadientAnimationPropertyBindingDesc> m_Properties;
    std::vector<std::vector<std::max_align_t>>       m_OutputStorage;
    std::vector<void*>                               m_OutputPointers;
    std::vector<size_t>                              m_OutputSizes;
};

bool PropertyRangesOverlap(const RadientAnimationPropertyBindingDesc& Lhs,
                           const RadientAnimationPropertyBindingDesc& Rhs)
{
    if (Lhs.Schema != Rhs.Schema ||
        Lhs.DestinationElement != Rhs.DestinationElement ||
        Lhs.Property != Rhs.Property)
    {
        return false;
    }

    const Uint64 LhsEnd = static_cast<Uint64>(Lhs.FirstArrayElement) + Lhs.Value.ArraySize;
    const Uint64 RhsEnd = static_cast<Uint64>(Rhs.FirstArrayElement) + Rhs.Value.ArraySize;
    return Lhs.FirstArrayElement < RhsEnd && Rhs.FirstArrayElement < LhsEnd;
}

class TestAnimationDestination final : public ObjectBase<IRadientAnimationDestination>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestination>;

    TestAnimationDestination(IReferenceCounters*                            pRefCounters,
                             std::shared_ptr<TestAnimationDestinationState> State) :
        TBase{pRefCounters},
        m_State{std::move(State)}
    {}

    ~TestAnimationDestination()
    {
        m_State->DestinationDestroyed = true;
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestination, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateBinding(
        const RadientAnimationPropertyBindingDesc* pProperties,
        Uint32                                     PropertyCount,
        RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
        IRadientAnimationDestinationBinding**      ppBinding) override final
    {
        if (ppBinding == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppBinding = nullptr;

        if (m_State->CreateStatus != RADIENT_STATUS_OK && !m_State->ReturnChildOnCreateFailure)
            return m_State->CreateStatus;

        if (PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        m_State->CreateRequests.emplace_back(pProperties, pProperties + PropertyCount);

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            if (pProperties[PropertyIndex].DestinationElement == InvalidRadientAnimationDestinationElement)
                return RADIENT_STATUS_NOT_FOUND;

            for (Uint32 PreviousIndex = 0; PreviousIndex < PropertyIndex; ++PreviousIndex)
            {
                if (PropertyRangesOverlap(pProperties[PreviousIndex], pProperties[PropertyIndex]))
                    return RADIENT_STATUS_INVALID_ARGUMENT;
            }

            pResolvedProperties[PropertyIndex].Semantic =
                m_State->GetSemantic(pProperties[PropertyIndex], PropertyIndex);
        }

        if (m_State->ReturnNullChild)
            return RADIENT_STATUS_OK;

        std::vector<RadientAnimationPropertyBindingDesc> Properties{
            pProperties,
            pProperties + PropertyCount};
        RefCntAutoPtr<TestAnimationDestinationBinding> pBinding{
            MakeNewRCObj<TestAnimationDestinationBinding>()(
                static_cast<IRadientAnimationDestination*>(this),
                m_State,
                std::move(Properties))};
        *ppBinding = pBinding.Detach();
        return m_State->CreateStatus;
    }

private:
    std::shared_ptr<TestAnimationDestinationState> m_State;
};

RefCntAutoPtr<TestAnimationDestination> CreateTestDestination(
    const std::shared_ptr<TestAnimationDestinationState>& State)
{
    return RefCntAutoPtr<TestAnimationDestination>{
        MakeNewRCObj<TestAnimationDestination>()(State)};
}

template <typename ValueType>
ValueType ReadCapturedValue(const CapturedAnimationUpdate& Update, Uint32 PropertyIndex)
{
    ValueType Value{};
    if (PropertyIndex >= Update.Values.size())
        return Value;

    EXPECT_EQ(Update.Values[PropertyIndex].size(), sizeof(ValueType));
    if (Update.Values[PropertyIndex].size() == sizeof(ValueType))
        std::memcpy(&Value, Update.Values[PropertyIndex].data(), sizeof(ValueType));
    return Value;
}

template <typename ComponentType>
ComponentType GetAnimationSamplingValue(Uint32 KeyIndex, Uint32 ComponentIndex)
{
    static_assert(std::is_same<ComponentType, Uint8>::value ||
                      std::is_same<ComponentType, Int32>::value ||
                      std::is_same<ComponentType, Uint32>::value ||
                      std::is_same<ComponentType, Float32>::value,
                  "Unexpected animation test component type");

    if constexpr (std::is_same<ComponentType, Uint8>::value)
    {
        return static_cast<Uint8>((KeyIndex + ComponentIndex) & 1u);
    }
    else if constexpr (std::is_same<ComponentType, Int32>::value)
    {
        constexpr Int32 Bases[] = {-100, 37, -401};
        return Bases[KeyIndex] + static_cast<Int32>(ComponentIndex) * 13;
    }
    else if constexpr (std::is_same<ComponentType, Uint32>::value)
    {
        constexpr Uint32 Bases[] = {7u, 1003u, 0x80000000u};
        return Bases[KeyIndex] + ComponentIndex * 11u;
    }
    else
    {
        constexpr Float32 Bases[] = {1.25f, 13.5f, -5.75f};
        return Bases[KeyIndex] +
            static_cast<Float32>(ComponentIndex) * (0.25f * static_cast<Float32>(KeyIndex + 1));
    }
}

Float32 GetAnimationSamplingIncomingTangent(Uint32 KeyIndex, Uint32 ComponentIndex)
{
    // The first incoming tangent is unused and deliberately conspicuous.
    constexpr Float32 Bases[] = {1000.f, -1.5f, 2.75f};
    return Bases[KeyIndex] + static_cast<Float32>(ComponentIndex) * 0.125f;
}

Float32 GetAnimationSamplingOutgoingTangent(Uint32 KeyIndex, Uint32 ComponentIndex)
{
    // The last outgoing tangent is unused and deliberately conspicuous.
    constexpr Float32 Bases[] = {1.25f, -2.5f, -1000.f};
    return Bases[KeyIndex] - static_cast<Float32>(ComponentIndex) * 0.0625f;
}

template <typename ComponentType>
std::vector<ComponentType> MakeAnimationSamplingData(const AnimationSamplingCase& Case,
                                                     Uint32                       ArraySize)
{
    const Uint32 ComponentCount = Case.ValueType.ComponentCount * ArraySize;
    const Uint32 CubicElementCount =
        Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ? 3u : 1u;

    std::vector<ComponentType> Values;
    Values.reserve(3u * CubicElementCount * ComponentCount);
    for (Uint32 KeyIndex = 0; KeyIndex < 3; ++KeyIndex)
    {
        if (Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE)
        {
            for (Uint32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
            {
                Values.push_back(static_cast<ComponentType>(
                    GetAnimationSamplingIncomingTangent(KeyIndex, ComponentIndex)));
            }
        }

        for (Uint32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
            Values.push_back(GetAnimationSamplingValue<ComponentType>(KeyIndex, ComponentIndex));

        if (Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE)
        {
            for (Uint32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
            {
                Values.push_back(static_cast<ComponentType>(
                    GetAnimationSamplingOutgoingTangent(KeyIndex, ComponentIndex)));
            }
        }
    }
    return Values;
}

template <typename ComponentType>
std::vector<ComponentType> GetExpectedAnimationSample(const AnimationSamplingCase& Case,
                                                      Uint32                       ArraySize,
                                                      Float32                      Time)
{
    static constexpr Float32 KeyTimes[]     = {1.f, 3.f, 7.f};
    const Uint32             ComponentCount = Case.ValueType.ComponentCount * ArraySize;

    Uint32 StartKey = 0;
    Uint32 EndKey   = 0;
    if (Time >= KeyTimes[2])
    {
        StartKey = 2;
        EndKey   = 2;
    }
    else if (Time > KeyTimes[0])
    {
        StartKey = Time < KeyTimes[1] ? 0u : 1u;
        EndKey   = StartKey + 1u;
    }

    if (Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP)
        EndKey = StartKey;

    std::vector<ComponentType> Expected;
    Expected.reserve(ComponentCount);
    for (Uint32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
    {
        const ComponentType Start =
            GetAnimationSamplingValue<ComponentType>(StartKey, ComponentIndex);
        if (StartKey == EndKey)
        {
            Expected.push_back(Start);
            continue;
        }

        const ComponentType End =
            GetAnimationSamplingValue<ComponentType>(EndKey, ComponentIndex);
        const Float32 Duration = KeyTimes[EndKey] - KeyTimes[StartKey];
        const Float32 Factor   = (Time - KeyTimes[StartKey]) / Duration;
        if (Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR)
        {
            Expected.push_back(static_cast<ComponentType>(
                Start + (End - Start) * Factor));
            continue;
        }

        const Float32 Factor2            = Factor * Factor;
        const Float32 Factor3            = Factor2 * Factor;
        const Float32 StartValueWeight   = 2.f * Factor3 - 3.f * Factor2 + 1.f;
        const Float32 StartTangentWeight = (Factor3 - 2.f * Factor2 + Factor) * Duration;
        const Float32 EndValueWeight     = -2.f * Factor3 + 3.f * Factor2;
        const Float32 EndTangentWeight   = (Factor3 - Factor2) * Duration;
        const Float32 StartTangent =
            GetAnimationSamplingOutgoingTangent(StartKey, ComponentIndex);
        const Float32 EndTangent =
            GetAnimationSamplingIncomingTangent(EndKey, ComponentIndex);
        Expected.push_back(static_cast<ComponentType>(
            Start * StartValueWeight +
            StartTangent * StartTangentWeight +
            End * EndValueWeight +
            EndTangent * EndTangentWeight));
    }
    return Expected;
}

template <typename ComponentType>
void ExpectCapturedAnimationComponents(const CapturedAnimationUpdate&    Update,
                                       Uint32                            PropertyIndex,
                                       const std::vector<ComponentType>& Expected,
                                       bool                              Exact)
{
    ASSERT_LT(PropertyIndex, Update.Values.size());
    const std::vector<Uint8>& ActualBytes = Update.Values[PropertyIndex];
    ASSERT_EQ(ActualBytes.size(), Expected.size() * sizeof(ComponentType));

    for (size_t ComponentIndex = 0; ComponentIndex < Expected.size(); ++ComponentIndex)
    {
        SCOPED_TRACE(ComponentIndex);
        ComponentType Actual{};
        std::memcpy(&Actual,
                    ActualBytes.data() + ComponentIndex * sizeof(ComponentType),
                    sizeof(ComponentType));
        if constexpr (std::is_same<ComponentType, Float32>::value)
        {
            if (Exact)
                EXPECT_EQ(Actual, Expected[ComponentIndex]);
            else
                EXPECT_NEAR(Actual, Expected[ComponentIndex], 1e-5f);
        }
        else if constexpr (std::is_same<ComponentType, Uint8>::value)
        {
            EXPECT_EQ(static_cast<Uint32>(Actual),
                      static_cast<Uint32>(Expected[ComponentIndex]));
        }
        else
        {
            EXPECT_EQ(Actual, Expected[ComponentIndex]);
        }
    }
}

void ExpectFloat3Near(const RadientFloat3& Value,
                      const RadientFloat3& Expected,
                      Float32              Tolerance = 1e-5f)
{
    EXPECT_NEAR(Value.x, Expected.x, Tolerance);
    EXPECT_NEAR(Value.y, Expected.y, Tolerance);
    EXPECT_NEAR(Value.z, Expected.z, Tolerance);
}

void ExpectQuaternionNear(const RadientQuaternion& Value,
                          const RadientQuaternion& Expected,
                          Float32                  Tolerance = 1e-5f)
{
    EXPECT_NEAR(Value.x, Expected.x, Tolerance);
    EXPECT_NEAR(Value.y, Expected.y, Tolerance);
    EXPECT_NEAR(Value.z, Expected.z, Tolerance);
    EXPECT_NEAR(Value.w, Expected.w, Tolerance);
}

void ExpectProperty(const RadientAnimationPropertyBindingDesc& Property,
                    RadientAnimationDestinationElement         Element,
                    RadientAnimationPropertyID                 PropertyID,
                    RADIENT_ANIMATION_VALUE_TYPE               Type,
                    Uint32                                     FirstArrayElement = 0,
                    Uint32                                     ArraySize         = 1)
{
    EXPECT_TRUE(Property.Schema == TestAnimationSchemaID);
    EXPECT_EQ(Property.DestinationElement, Element);
    EXPECT_EQ(Property.Property, PropertyID);
    EXPECT_EQ(Property.FirstArrayElement, FirstArrayElement);
    EXPECT_EQ(Property.Value.Type, Type);
    EXPECT_EQ(Property.Value.ArraySize, ArraySize);
}

class RadientAnimationBindingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pAssetManager = RadientAssetManagerImpl::Create({});
        ASSERT_NE(pAssetManager, nullptr);
    }

    RefCntAutoPtr<IRadientAnimationBinding> Bind(
        IRadientAnimationClipAsset*                         pClip,
        const std::vector<RadientAnimationDestinationDesc>& Destinations)
    {
        RadientAnimationBindingDesc Desc;
        Desc.pDestinations    = Destinations.empty() ? nullptr : Destinations.data();
        Desc.DestinationCount = static_cast<Uint32>(Destinations.size());

        RefCntAutoPtr<IRadientAnimationBinding> pBinding;
        EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pBinding;
    }

    RefCntAutoPtr<IRadientAnimationBinding> BindSingle(
        IRadientAnimationClipAsset*                                pClip,
        IRadientAnimationDestination*                              pDestination,
        const std::vector<RadientAnimationDestinationMappingDesc>& Mappings)
    {
        RadientAnimationDestinationDesc Destination;
        Destination.pDestination = pDestination;
        Destination.pMappings    = Mappings.data();
        Destination.MappingCount = static_cast<Uint32>(Mappings.size());
        return Bind(pClip, {Destination});
    }

protected:
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager;
};

class RadientAnimationValueSamplingTest :
    public RadientAnimationBindingTest,
    public testing::WithParamInterface<AnimationSamplingCase>
{
protected:
    template <typename ComponentType>
    void RunSamplingCase(const AnimationSamplingCase& Case)
    {
        TestAnimationClipBuilder Builder;
        Builder.Duration = 8.f;

        std::vector<RadientAnimationDestinationMappingDesc> Mappings;
        for (const Uint32 ArraySize : {1u, 2u})
        {
            const Uint32 Target  = Builder.AddTarget(ArraySize);
            const Uint32 Sampler = Builder.AddSampler<ComponentType>(
                Case.ValueType.Type,
                Case.Interpolation,
                {1.f, 3.f, 7.f},
                MakeAnimationSamplingData<ComponentType>(Case, ArraySize),
                ArraySize);
            Builder.AddChannel(Target, TestPropertyA, Sampler);

            RadientAnimationDestinationMappingDesc Mapping;
            Mapping.ClipTargetIndex    = Target;
            Mapping.DestinationElement = ArraySize;
            Mappings.push_back(Mapping);
        }

        RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
        ASSERT_NE(pClip, nullptr);

        auto                                    State        = std::make_shared<TestAnimationDestinationState>();
        RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
        RefCntAutoPtr<IRadientAnimationBinding> pBinding =
            BindSingle(pClip, pDestination, Mappings);
        ASSERT_NE(pBinding, nullptr);

        ASSERT_EQ(State->CreateRequests.size(), 1u);
        ASSERT_EQ(State->CreateRequests[0].size(), 2u);
        ExpectProperty(State->CreateRequests[0][0],
                       1,
                       TestPropertyA,
                       Case.ValueType.Type,
                       0,
                       1);
        ExpectProperty(State->CreateRequests[0][1],
                       2,
                       TestPropertyA,
                       Case.ValueType.Type,
                       0,
                       2);

        static constexpr std::array<Float32, 7> EvaluationTimes = {0.f, 1.f, 2.f, 3.f, 4.f, 7.f, 8.f};
        for (const Float32 Time : EvaluationTimes)
        {
            RadientAnimationEvaluateInfo Info;
            Info.Time = Time;
            ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK) << "time " << Time;
        }

        ASSERT_EQ(State->EndCalls.size(), EvaluationTimes.size());
        for (size_t EvaluationIndex = 0;
             EvaluationIndex < EvaluationTimes.size();
             ++EvaluationIndex)
        {
            const Float32 Time = EvaluationTimes[EvaluationIndex];
            SCOPED_TRACE(Time);
            const CapturedAnimationUpdate& Update = State->EndCalls[EvaluationIndex];
            ASSERT_EQ(Update.Values.size(), 2u);
            for (Uint32 ArraySize = 1; ArraySize <= 2; ++ArraySize)
            {
                SCOPED_TRACE(ArraySize);
                ExpectCapturedAnimationComponents(
                    Update,
                    ArraySize - 1,
                    GetExpectedAnimationSample<ComponentType>(Case, ArraySize, Time),
                    Case.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP);
            }
        }
    }
};

TEST_P(RadientAnimationValueSamplingTest, SamplesSingleValuesAndArrays)
{
    const AnimationSamplingCase& Case = GetParam();
    switch (Case.ValueType.ComponentType)
    {
        case AnimationSamplingComponentType::Boolean:
            RunSamplingCase<Uint8>(Case);
            break;

        case AnimationSamplingComponentType::SignedInteger:
            RunSamplingCase<Int32>(Case);
            break;

        case AnimationSamplingComponentType::UnsignedInteger:
            RunSamplingCase<Uint32>(Case);
            break;

        case AnimationSamplingComponentType::FloatingPoint:
            RunSamplingCase<Float32>(Case);
            break;
    }
}

INSTANTIATE_TEST_SUITE_P(
    NativeValueTypes,
    RadientAnimationValueSamplingTest,
    testing::ValuesIn(GetAnimationSamplingCases()),
    GetAnimationSamplingCaseName);

TEST_F(RadientAnimationBindingTest, CompilesPropertiesAndBatchesInDescriptorOrder)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target0  = Builder.AddTarget(100);
    const Uint32             Target1  = Builder.AddTarget(200);
    const Uint32             SamplerA = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 10.f});
    const Uint32 SamplerB = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {20.f, 40.f});
    const Uint32 SamplerC = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 4.f});

    // Channel order is deliberately interleaved between targets. Within a
    // mapping, the compiler preserves this clip-channel order.
    Builder.AddChannel(Target0, TestPropertyA, SamplerA);
    Builder.AddChannel(Target1, TestPropertyC, SamplerC);
    Builder.AddChannel(Target0, TestPropertyB, SamplerB);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto EndOrder          = std::make_shared<std::vector<Uint32>>();
    auto State0            = std::make_shared<TestAnimationDestinationState>();
    auto State1            = std::make_shared<TestAnimationDestinationState>();
    State0->DestinationID  = 7;
    State1->DestinationID  = 9;
    State0->GlobalEndOrder = EndOrder;
    State1->GlobalEndOrder = EndOrder;

    RefCntAutoPtr<TestAnimationDestination> pDestination0 = CreateTestDestination(State0);
    RefCntAutoPtr<TestAnimationDestination> pDestination1 = CreateTestDestination(State1);

    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings0{};
    Mappings0[0].ClipTargetIndex    = Target1;
    Mappings0[0].DestinationElement = 21;
    Mappings0[1].ClipTargetIndex    = Target0;
    Mappings0[1].DestinationElement = 10;

    RadientAnimationDestinationMappingDesc Mapping1;
    Mapping1.ClipTargetIndex    = Target0;
    Mapping1.DestinationElement = 30;

    std::array<RadientAnimationDestinationDesc, 2> Destinations{};
    Destinations[0].pDestination = pDestination0;
    Destinations[0].pMappings    = Mappings0.data();
    Destinations[0].MappingCount = static_cast<Uint32>(Mappings0.size());
    Destinations[1].pDestination = pDestination1;
    Destinations[1].pMappings    = &Mapping1;
    Destinations[1].MappingCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(
        pClip,
        std::vector<RadientAnimationDestinationDesc>{Destinations.begin(), Destinations.end()});
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(pBinding->GetClip(), pClip.RawPtr());

    ASSERT_EQ(State0->CreateRequests.size(), 1u);
    ASSERT_EQ(State0->CreateRequests[0].size(), 3u);
    ExpectProperty(State0->CreateRequests[0][0], 21, TestPropertyC, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    ExpectProperty(State0->CreateRequests[0][1], 10, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    ExpectProperty(State0->CreateRequests[0][2], 10, TestPropertyB, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);

    ASSERT_EQ(State1->CreateRequests.size(), 1u);
    ASSERT_EQ(State1->CreateRequests[0].size(), 2u);
    ExpectProperty(State1->CreateRequests[0][0], 30, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    ExpectProperty(State1->CreateRequests[0][1], 30, TestPropertyB, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);

    RadientAnimationEvaluateInfo Info;
    Info.Time               = 0.5f;
    Info.UpdateDerivedState = False;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    EXPECT_EQ(*EndOrder, (std::vector<Uint32>{7, 9}));
    ASSERT_EQ(State0->EndCalls.size(), 1u);
    ASSERT_EQ(State1->EndCalls.size(), 1u);
    EXPECT_EQ(State0->EndCalls[0].UpdateDerivedState, False);
    EXPECT_EQ(State1->EndCalls[0].UpdateDerivedState, False);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State0->EndCalls[0], 0), 3.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State0->EndCalls[0], 1), 5.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State0->EndCalls[0], 2), 30.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State1->EndCalls[0], 0), 5.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State1->EndCalls[0], 1), 30.f);
}

TEST_F(RadientAnimationBindingTest, CreatesAndEvaluatesEmptyBinding)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration                                = 0.f;
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(pClip, {});
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(pBinding->GetClip(), pClip.RawPtr());

    RadientAnimationEvaluateInfo Info;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_NO_CHANGE);
}

TEST_F(RadientAnimationBindingTest, RejectsNonFiniteEvaluationTime)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration = 0.f;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(pClip, {});
    ASSERT_NE(pBinding, nullptr);

    const Float32 NonFiniteTimes[] = {
        std::numeric_limits<Float32>::quiet_NaN(),
        std::numeric_limits<Float32>::infinity(),
        -std::numeric_limits<Float32>::infinity(),
    };

    ErrorAllowanceScope Scope;
    for (const Float32 Time : NonFiniteTimes)
    {
        RadientAnimationEvaluateInfo Info;
        Info.Time = Time;
        EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST_F(RadientAnimationBindingTest, RejectsNonNullBindingOutputWithoutOverwritingIt)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration                                = 0.f;
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RefCntAutoPtr<IRadientAnimationBinding> pExistingBinding = Bind(pClip, {});
    ASSERT_NE(pExistingBinding, nullptr);

    RadientAnimationBindingDesc Desc;
    IRadientAnimationBinding*   pOutput         = pExistingBinding;
    IRadientAnimationBinding*   pOriginalOutput = pOutput;
    ErrorAllowanceScope         Scope;
    EXPECT_EQ(pClip->CreateBinding(Desc, &pOutput), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pOutput, pOriginalOutput);

    RadientAnimationEvaluateInfo Info;
    EXPECT_EQ(pExistingBinding->Evaluate(Info), RADIENT_STATUS_NO_CHANGE);
}

TEST_F(RadientAnimationBindingTest, HoldsSamplerEndpointValues)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration     = 3.f;
    const Uint32 Target  = Builder.AddTarget(1);
    const Uint32 Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {1.f, 2.f},
        {10.f, 20.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 3;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = -5.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    Info.Time = 8.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 2u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 10.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[1], 0), 20.f);
}

TEST_F(RadientAnimationBindingTest, UsesStepInterpolation)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Uint32>(
        RADIENT_ANIMATION_VALUE_TYPE_UINT,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f},
        {7u, 19u});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 4;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.75f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);
    EXPECT_EQ(ReadCapturedValue<Uint32>(State->EndCalls[0], 0), 7u);
}

TEST_F(RadientAnimationBindingTest, StepQuaternionNormalizesTheSelectedKey)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration     = 2.f;
    const Uint32 Target  = Builder.AddTarget(1);
    const Uint32 Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f, 2.f},
        {
            {0.f, 0.f, 0.f, 2.f},
            {0.f, 0.f, 3.f, 0.f},
            {0.f, 4.f, 0.f, 0.f},
        });
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 4;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    for (const Float32 Time : {-1.f, 0.5f, 1.f, 1.5f, 2.f})
    {
        Info.Time = Time;
        ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    }

    ASSERT_EQ(State->EndCalls.size(), 5u);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 0),
                         {0.f, 0.f, 0.f, 1.f});
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[1], 0),
                         {0.f, 0.f, 0.f, 1.f});
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[2], 0),
                         {0.f, 0.f, 1.f, 0.f});
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[3], 0),
                         {0.f, 0.f, 1.f, 0.f});
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[4], 0),
                         {0.f, 1.f, 0.f, 0.f});
}

TEST_F(RadientAnimationBindingTest, StepsAndLinearlyInterpolatesQuaternionArrays)
{
    TestAnimationClipBuilder Builder;
    const Uint32             StepTarget   = Builder.AddTarget(1);
    const Uint32             LinearTarget = Builder.AddTarget(2);
    const Uint32             StepSampler  = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f},
        {
            {0.f, 0.f, 0.f, 2.f},
            {3.f, 0.f, 0.f, 0.f},
            {0.f, 0.f, 4.f, 0.f},
            {0.f, 5.f, 0.f, 0.f},
        },
        2);
    const Uint32 LinearSampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {
            {0.f, 0.f, 0.f, 2.f},
            {2.f, 0.f, 0.f, 0.f},
            {0.f, 0.f, 2.f, 0.f},
            {0.f, 2.f, 0.f, 0.f},
        },
        2);
    Builder.AddChannel(StepTarget, TestQuaternionProperty, StepSampler);
    Builder.AddChannel(LinearTarget, TestQuaternionProperty, LinearSampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex                      = StepTarget;
    Mappings[0].DestinationElement                   = 10;
    Mappings[1].ClipTargetIndex                      = LinearTarget;
    Mappings[1].DestinationElement                   = 20;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip,
        pDestination,
        std::vector<RadientAnimationDestinationMappingDesc>{Mappings.begin(), Mappings.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    ASSERT_EQ(State->EndCalls[0].Values.size(), 2u);
    const auto StepValues =
        ReadCapturedValue<std::array<RadientQuaternion, 2>>(State->EndCalls[0], 0);
    ExpectQuaternionNear(StepValues[0], {0.f, 0.f, 0.f, 1.f});
    ExpectQuaternionNear(StepValues[1], {1.f, 0.f, 0.f, 0.f});

    const auto LinearValues =
        ReadCapturedValue<std::array<RadientQuaternion, 2>>(State->EndCalls[0], 1);
    const Float32 HalfSqrt = std::sqrt(0.5f);
    ExpectQuaternionNear(LinearValues[0], {0.f, 0.f, HalfSqrt, HalfSqrt});
    ExpectQuaternionNear(LinearValues[1], {HalfSqrt, HalfSqrt, 0.f, 0.f});
}


TEST_F(RadientAnimationBindingTest, SamplesOneKeyForEveryInterpolationMode)
{
    TestAnimationClipBuilder Builder;
    const Uint32             StepTarget   = Builder.AddTarget(1);
    const Uint32             LinearTarget = Builder.AddTarget(2);
    const Uint32             CubicTarget  = Builder.AddTarget(3);
    const Uint32             StepSampler  = Builder.AddSampler<Uint32>(
        RADIENT_ANIMATION_VALUE_TYPE_UINT,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.5f},
        {17u});
    const Uint32 LinearSampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.5f},
        {3.5f});
    const Uint32 CubicSampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.5f},
        {100.f, 7.f, -100.f});
    Builder.AddChannel(StepTarget, TestPropertyA, StepSampler);
    Builder.AddChannel(LinearTarget, TestPropertyB, LinearSampler);
    Builder.AddChannel(CubicTarget, TestPropertyC, CubicSampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                                  State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 3> Mappings{};
    Mappings[0].ClipTargetIndex                      = StepTarget;
    Mappings[0].DestinationElement                   = 10;
    Mappings[1].ClipTargetIndex                      = LinearTarget;
    Mappings[1].DestinationElement                   = 20;
    Mappings[2].ClipTargetIndex                      = CubicTarget;
    Mappings[2].DestinationElement                   = 30;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip,
        pDestination,
        std::vector<RadientAnimationDestinationMappingDesc>{Mappings.begin(), Mappings.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    for (const Float32 Time : {0.f, 1.f})
    {
        Info.Time = Time;
        ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    }

    ASSERT_EQ(State->EndCalls.size(), 2u);
    for (const CapturedAnimationUpdate& Update : State->EndCalls)
    {
        ASSERT_EQ(Update.Values.size(), 3u);
        EXPECT_EQ(ReadCapturedValue<Uint32>(Update, 0), 17u);
        EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(Update, 1), 3.5f);
        EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(Update, 2), 7.f);
    }
}

TEST_F(RadientAnimationBindingTest, InterpolatesComponentsIndependently)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 2.f, 4.f}, {10.f, 6.f, -4.f}});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 5;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.25f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);
    ExpectFloat3Near(ReadCapturedValue<RadientFloat3>(State->EndCalls[0], 0),
                     {2.5f, 3.f, 2.f});
}

TEST_F(RadientAnimationBindingTest, KeepsExtremeLinearCancellationFinite)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Float32            Max     = std::numeric_limits<Float32>::max();
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {-Max, Max});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 5;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);

    const Float32 Value = ReadCapturedValue<Float32>(State->EndCalls[0], 0);
    EXPECT_TRUE(std::isfinite(Value));
    EXPECT_FLOAT_EQ(Value, 0.f);
}

TEST_F(RadientAnimationBindingTest, ScalesCubicSplineTangentsByKeyInterval)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration     = 3.f;
    const Uint32 Target  = Builder.AddTarget(1);
    const Uint32 Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {1.f, 3.f},
        {
            0.f,
            2.f,
            3.f, // Key 0: incoming tangent, value, outgoing tangent.
            -1.f,
            10.f,
            0.f,
        });
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 5;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 2.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);

    // At u=0.5, Hermite interpolation uses tangents 3*2 and -1*2 because
    // the key interval is two seconds. Omitting that scale would produce 6.5.
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 7.f);
}

TEST_F(RadientAnimationBindingTest, SamplesFloat3ArraysWithExactLayoutAndAlignment)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration     = 2.f;
    const Uint32 Target  = Builder.AddTarget(1);
    const Uint32 Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 2.f},
        {
            {0.f, 1.f, 2.f},
            {10.f, 20.f, 30.f},
            {2.f, 3.f, 4.f},
            {20.f, 40.f, 60.f},
        },
        2);
    Builder.AddChannel(Target, TestPropertyA, Sampler, 4);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 8;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    ASSERT_EQ(State->CreateRequests.size(), 1u);
    ASSERT_EQ(State->CreateRequests[0].size(), 1u);
    ExpectProperty(State->CreateRequests[0][0],
                   8,
                   TestPropertyA,
                   RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                   4,
                   2);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);

    const CapturedAnimationUpdate& Update = State->EndCalls[0];
    ASSERT_EQ(Update.Values.size(), 1u);
    ASSERT_EQ(Update.OutputAddresses.size(), 1u);
    ASSERT_EQ(Update.ValueDataSizes.size(), 1u);
    EXPECT_EQ(Update.ValueDataSizes[0], sizeof(RadientFloat3) * 2);
    ASSERT_EQ(Update.Values[0].size(), sizeof(RadientFloat3) * 2);
    EXPECT_EQ(Update.OutputAddresses[0] % alignof(RadientFloat3), 0u);

    std::array<RadientFloat3, 2> Values{};
    std::memcpy(Values.data(), Update.Values[0].data(), sizeof(Values));
    ExpectFloat3Near(Values[0], {1.f, 2.f, 3.f});
    ExpectFloat3Near(Values[1], {15.f, 30.f, 45.f});
}

TEST_F(RadientAnimationBindingTest, SlerpsAndNormalizesQuaternionValues)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f, 2.f}, {0.f, 0.f, 2.f, 0.f}});
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 6;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = -1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 2u);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 0),
                         {0.f, 0.f, 0.f, 1.f});
    const Float32 HalfSqrt = std::sqrt(0.5f);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[1], 0),
                         {0.f, 0.f, HalfSqrt, HalfSqrt});
}

TEST_F(RadientAnimationBindingTest, CubicInterpolatesAndNormalizesQuaternionValues)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration     = 2.f;
    const Uint32 Target  = Builder.AddTarget(1);
    const Uint32 Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.f, 2.f},
        {
            {0.f, 0.f, 0.f, 0.f},
            {0.f, 0.f, 0.f, 1.f},
            {0.f, 0.f, 0.f, 0.f},
            {0.f, 0.f, 0.f, 0.f},
            {0.f, 0.f, 1.f, 0.f},
            {0.f, 0.f, 0.f, 0.f},
        });
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 6;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    ASSERT_EQ(State->EndCalls.size(), 1u);

    const Float32 HalfSqrt = std::sqrt(0.5f);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 0),
                         {0.f, 0.f, HalfSqrt, HalfSqrt});
}

TEST_F(RadientAnimationBindingTest, CubicInterpolationHoldsAndNormalizesEndpointValues)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration              = 3.f;
    const Uint32 ComponentTarget  = Builder.AddTarget(1);
    const Uint32 QuaternionTarget = Builder.AddTarget(2);
    const Uint32 ComponentSampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {1.f, 3.f},
        {
            100.f,
            2.f,
            50.f,
            -50.f,
            10.f,
            -100.f,
        });
    const Uint32 QuaternionSampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {1.f, 3.f},
        {
            {1.f, 2.f, 3.f, 4.f},
            {0.f, 0.f, 0.f, 2.f},
            {4.f, 3.f, 2.f, 1.f},
            {-1.f, -2.f, -3.f, -4.f},
            {0.f, 0.f, 3.f, 0.f},
            {-4.f, -3.f, -2.f, -1.f},
        });
    Builder.AddChannel(ComponentTarget, TestPropertyA, ComponentSampler);
    Builder.AddChannel(QuaternionTarget, TestQuaternionProperty, QuaternionSampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex                      = ComponentTarget;
    Mappings[0].DestinationElement                   = 10;
    Mappings[1].ClipTargetIndex                      = QuaternionTarget;
    Mappings[1].DestinationElement                   = 20;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip,
        pDestination,
        std::vector<RadientAnimationDestinationMappingDesc>{Mappings.begin(), Mappings.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = -2.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    Info.Time = 9.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 2u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 2.f);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 1),
                         {0.f, 0.f, 0.f, 1.f});
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[1], 0), 10.f);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[1], 1),
                         {0.f, 0.f, 1.f, 0.f});
}

TEST_F(RadientAnimationBindingTest, OneKeyCubicNormalizesEveryQuaternionArrayElement)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.5f},
        {
            // Incoming tangents.
            {1.f, 2.f, 3.f, 4.f},
            {4.f, 3.f, 2.f, 1.f},
            // Central values.
            {0.f, 0.f, 0.f, 2.f},
            {0.f, 0.f, 3.f, 0.f},
            // Outgoing tangents.
            {-1.f, -2.f, -3.f, -4.f},
            {-4.f, -3.f, -2.f, -1.f},
        },
        2);
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 6;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.75f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    const auto Values = ReadCapturedValue<std::array<RadientQuaternion, 2>>(State->EndCalls[0], 0);
    ExpectQuaternionNear(Values[0], {0.f, 0.f, 0.f, 1.f});
    ExpectQuaternionNear(Values[1], {0.f, 0.f, 1.f, 0.f});
}

TEST_F(RadientAnimationBindingTest, CubicInterpolatesFloat3AndQuaternionArrays)
{
    TestAnimationClipBuilder Builder;
    Builder.Duration              = 2.f;
    const Uint32 Float3Target     = Builder.AddTarget(1);
    const Uint32 QuaternionTarget = Builder.AddTarget(2);
    const Uint32 Float3Sampler    = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.f, 2.f},
        {
            // Key 0 incoming tangents, central values, and outgoing tangents.
            {0.f, 0.f, 0.f},
            {0.f, 0.f, 0.f},
            {0.f, 2.f, 4.f},
            {10.f, 20.f, 30.f},
            {2.f, 0.f, -2.f},
            {-4.f, 2.f, 6.f},
            // Key 1 incoming tangents, central values, and outgoing tangents.
            {-2.f, 2.f, 0.f},
            {8.f, -2.f, 4.f},
            {4.f, 6.f, 8.f},
            {14.f, 18.f, 22.f},
            {0.f, 0.f, 0.f},
            {0.f, 0.f, 0.f},
        },
        2);
    const RadientQuaternion Zero              = {0.f, 0.f, 0.f, 0.f};
    const Uint32            QuaternionSampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.f, 2.f},
        {
            Zero,
            Zero,
            {0.f, 0.f, 0.f, 2.f},
            {2.f, 0.f, 0.f, 0.f},
            Zero,
            Zero,
            Zero,
            Zero,
            {0.f, 0.f, 2.f, 0.f},
            {0.f, 2.f, 0.f, 0.f},
            Zero,
            Zero,
        },
        2);
    Builder.AddChannel(Float3Target, TestPropertyA, Float3Sampler);
    Builder.AddChannel(QuaternionTarget, TestQuaternionProperty, QuaternionSampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex                      = Float3Target;
    Mappings[0].DestinationElement                   = 10;
    Mappings[1].ClipTargetIndex                      = QuaternionTarget;
    Mappings[1].DestinationElement                   = 20;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip,
        pDestination,
        std::vector<RadientAnimationDestinationMappingDesc>{Mappings.begin(), Mappings.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    const auto Float3Values =
        ReadCapturedValue<std::array<RadientFloat3, 2>>(State->EndCalls[0], 0);
    ExpectFloat3Near(Float3Values[0], {3.f, 3.5f, 5.5f});
    ExpectFloat3Near(Float3Values[1], {9.f, 20.f, 26.5f});
    const auto Quaternions =
        ReadCapturedValue<std::array<RadientQuaternion, 2>>(State->EndCalls[0], 1);
    const Float32 HalfSqrt = std::sqrt(0.5f);
    ExpectQuaternionNear(Quaternions[0], {0.f, 0.f, HalfSqrt, HalfSqrt});
    ExpectQuaternionNear(Quaternions[1], {HalfSqrt, HalfSqrt, 0.f, 0.f});
}

TEST_F(RadientAnimationBindingTest, SamplesSharedSamplerSeparatelyForDifferentSemantics)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 0.f}});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    Builder.AddChannel(Target, TestPropertyB, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 7;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    ASSERT_EQ(State->EndCalls[0].Values.size(), 3u);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 0),
                         {0.f, 0.f, 0.5f, 0.5f});
    const Float32 HalfSqrt = std::sqrt(0.5f);
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 1),
                         {0.f, 0.f, HalfSqrt, HalfSqrt});
    ExpectQuaternionNear(ReadCapturedValue<RadientQuaternion>(State->EndCalls[0], 2),
                         {0.f, 0.f, 0.5f, 0.5f});
}

TEST_F(RadientAnimationBindingTest, FansOutSharedSampleToMultipleOutputsInOneDestination)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target0 = Builder.AddTarget(1);
    const Uint32             Target1 = Builder.AddTarget(2);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 8.f});
    Builder.AddChannel(Target0, TestPropertyA, Sampler);
    Builder.AddChannel(Target1, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                                  State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex                      = Target0;
    Mappings[0].DestinationElement                   = 7;
    Mappings[1].ClipTargetIndex                      = Target1;
    Mappings[1].DestinationElement                   = 9;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip, pDestination, {Mappings[0], Mappings[1]});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.25f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    ASSERT_EQ(State->EndCalls[0].Values.size(), 2u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 3.5f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 1), 3.5f);
    EXPECT_NE(State->EndCalls[0].OutputAddresses[0], State->EndCalls[0].OutputAddresses[1]);
}

TEST_F(RadientAnimationBindingTest, RejectsMalformedBindingDescriptors)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 1;
    RadientAnimationDestinationDesc Destination;
    Destination.pDestination = pDestination;
    Destination.pMappings    = &Mapping;
    Destination.MappingCount = 1;

    {
        ErrorAllowanceScope         Scope;
        RadientAnimationBindingDesc Desc;
        EXPECT_EQ(pClip->CreateBinding(Desc, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    }

    auto ExpectInvalid = [&](const RadientAnimationBindingDesc& Desc) {
        ErrorAllowanceScope                     Scope;
        RefCntAutoPtr<IRadientAnimationBinding> pBinding;
        EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_FALSE(pBinding);
    };

    RadientAnimationBindingDesc Desc;
    Desc.pDestinations    = &Destination;
    Desc.DestinationCount = 0;
    ExpectInvalid(Desc);

    Desc.pDestinations    = nullptr;
    Desc.DestinationCount = 1;
    ExpectInvalid(Desc);

    RadientAnimationDestinationDesc InvalidDestination = Destination;
    InvalidDestination.pDestination                    = nullptr;
    Desc.pDestinations                                 = &InvalidDestination;
    ExpectInvalid(Desc);

    InvalidDestination              = Destination;
    InvalidDestination.pMappings    = nullptr;
    InvalidDestination.MappingCount = 1;
    Desc.pDestinations              = &InvalidDestination;
    ExpectInvalid(Desc);

    InvalidDestination              = Destination;
    InvalidDestination.pMappings    = &Mapping;
    InvalidDestination.MappingCount = 0;
    Desc.pDestinations              = &InvalidDestination;
    ExpectInvalid(Desc);

    RadientAnimationDestinationMappingDesc InvalidMapping = Mapping;
    InvalidMapping.ClipTargetIndex                        = pClip->GetDesc().TargetCount;
    InvalidDestination                                    = Destination;
    InvalidDestination.pMappings                          = &InvalidMapping;
    Desc.pDestinations                                    = &InvalidDestination;
    ExpectInvalid(Desc);

    std::array<RadientAnimationDestinationMappingDesc, 2> DuplicateMappings = {Mapping, Mapping};
    InvalidDestination                                                      = Destination;
    InvalidDestination.pMappings                                            = DuplicateMappings.data();
    InvalidDestination.MappingCount                                         = static_cast<Uint32>(DuplicateMappings.size());
    Desc.pDestinations                                                      = &InvalidDestination;
    ExpectInvalid(Desc);

    std::array<RadientAnimationDestinationDesc, 2> DuplicateDestinations = {Destination, Destination};
    Desc.pDestinations                                                   = DuplicateDestinations.data();
    Desc.DestinationCount                                                = static_cast<Uint32>(DuplicateDestinations.size());
    ExpectInvalid(Desc);
}

TEST_F(RadientAnimationBindingTest, RejectsOverlappingDestinationWrites)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target0  = Builder.AddTarget(1);
    const Uint32             Target1  = Builder.AddTarget(2);
    const Uint32             Sampler0 = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    const Uint32 Sampler1 = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 3.f});
    Builder.AddChannel(Target0, TestPropertyA, Sampler0);
    Builder.AddChannel(Target1, TestPropertyA, Sampler1);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                                  State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex    = Target0;
    Mappings[0].DestinationElement = 4;
    Mappings[1].ClipTargetIndex    = Target1;
    Mappings[1].DestinationElement = 4;

    RadientAnimationDestinationDesc Destination;
    Destination.pDestination = pDestination;
    Destination.pMappings    = Mappings.data();
    Destination.MappingCount = static_cast<Uint32>(Mappings.size());
    RadientAnimationBindingDesc Desc;
    Desc.pDestinations    = &Destination;
    Desc.DestinationCount = 1;

    ErrorAllowanceScope                     Scope;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientAnimationBindingTest, PropagatesDestinationCreationFailureAndReleasesEarlierChildren)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State0                                           = std::make_shared<TestAnimationDestinationState>();
    auto State1                                           = std::make_shared<TestAnimationDestinationState>();
    State1->CreateStatus                                  = RADIENT_STATUS_NOT_FOUND;
    RefCntAutoPtr<TestAnimationDestination> pDestination0 = CreateTestDestination(State0);
    RefCntAutoPtr<TestAnimationDestination> pDestination1 = CreateTestDestination(State1);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 1;
    std::array<RadientAnimationDestinationDesc, 2> Destinations{};
    Destinations[0].pDestination = pDestination0;
    Destinations[0].pMappings    = &Mapping;
    Destinations[0].MappingCount = 1;
    Destinations[1].pDestination = pDestination1;
    Destinations[1].pMappings    = &Mapping;
    Destinations[1].MappingCount = 1;
    RadientAnimationBindingDesc Desc;
    Desc.pDestinations    = Destinations.data();
    Desc.DestinationCount = static_cast<Uint32>(Destinations.size());

    ErrorAllowanceScope                     Scope;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_FALSE(pBinding);
    EXPECT_EQ(State0->DestroyedChildBindings, 1u);
    EXPECT_EQ(State0->LiveChildBindings, 0u);
    EXPECT_EQ(State0->CreateRequests.size(), 1u);
}

TEST_F(RadientAnimationBindingTest, RejectsDestinationContractViolations)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 0.f}});
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 1;

    auto ExpectInvalidOperation = [&](const std::shared_ptr<TestAnimationDestinationState>& State) {
        RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
        RadientAnimationDestinationDesc         Destination;
        Destination.pDestination = pDestination;
        Destination.pMappings    = &Mapping;
        Destination.MappingCount = 1;
        RadientAnimationBindingDesc Desc;
        Desc.pDestinations    = &Destination;
        Desc.DestinationCount = 1;

        ErrorAllowanceScope                     Scope;
        RefCntAutoPtr<IRadientAnimationBinding> pBinding;
        EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_FALSE(pBinding);
    };

    auto NullChildState             = std::make_shared<TestAnimationDestinationState>();
    NullChildState->ReturnNullChild = true;
    ExpectInvalidOperation(NullChildState);

    auto PositiveStatusState          = std::make_shared<TestAnimationDestinationState>();
    PositiveStatusState->CreateStatus = RADIENT_STATUS_NO_CHANGE;
    ExpectInvalidOperation(PositiveStatusState);

    auto FailureWithChildState                        = std::make_shared<TestAnimationDestinationState>();
    FailureWithChildState->CreateStatus               = RADIENT_STATUS_NOT_FOUND;
    FailureWithChildState->ReturnChildOnCreateFailure = true;
    ExpectInvalidOperation(FailureWithChildState);
    EXPECT_EQ(FailureWithChildState->LiveChildBindings, 0u);
    EXPECT_EQ(FailureWithChildState->DestroyedChildBindings, 1u);

    auto UnknownSemanticState = std::make_shared<TestAnimationDestinationState>();
    UnknownSemanticState->Semantics.emplace_back(TestQuaternionProperty,
                                                 RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    ExpectInvalidOperation(UnknownSemanticState);
}

TEST_F(RadientAnimationBindingTest, RejectsQuaternionSemanticForNonFloat4Storage)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}});
    Builder.AddChannel(Target, TestQuaternionProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State = std::make_shared<TestAnimationDestinationState>();
    State->Semantics.emplace_back(TestQuaternionProperty,
                                  RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 1;

    RadientAnimationDestinationDesc Destination;
    Destination.pDestination = pDestination;
    Destination.pMappings    = &Mapping;
    Destination.MappingCount = 1;
    RadientAnimationBindingDesc Desc;
    Desc.pDestinations    = &Destination;
    Desc.DestinationCount = 1;

    ErrorAllowanceScope                     Scope;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    EXPECT_EQ(pClip->CreateBinding(Desc, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientAnimationBindingTest, PropagatesBeginUpdateFailureAndStopsLaterDestinations)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                                          EndOrder = std::make_shared<std::vector<Uint32>>();
    std::array<std::shared_ptr<TestAnimationDestinationState>, 3> States   = {
        std::make_shared<TestAnimationDestinationState>(),
        std::make_shared<TestAnimationDestinationState>(),
        std::make_shared<TestAnimationDestinationState>()};
    std::array<RefCntAutoPtr<TestAnimationDestination>, 3> DestinationObjects;
    std::array<RadientAnimationDestinationMappingDesc, 3>  Mappings{};
    std::array<RadientAnimationDestinationDesc, 3>         Destinations{};
    for (Uint32 Index = 0; Index < static_cast<Uint32>(States.size()); ++Index)
    {
        States[Index]->DestinationID       = Index;
        States[Index]->GlobalEndOrder      = EndOrder;
        DestinationObjects[Index]          = CreateTestDestination(States[Index]);
        Mappings[Index].ClipTargetIndex    = Target;
        Mappings[Index].DestinationElement = Index;
        Destinations[Index].pDestination   = DestinationObjects[Index];
        Destinations[Index].pMappings      = &Mappings[Index];
        Destinations[Index].MappingCount   = 1;
    }
    States[1]->BeginStatuses.push_back(RADIENT_STATUS_FAILED);

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(
        pClip,
        std::vector<RadientAnimationDestinationDesc>{Destinations.begin(), Destinations.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    ErrorAllowanceScope          Scope;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_FAILED);
    EXPECT_EQ(*EndOrder, (std::vector<Uint32>{0}));
    EXPECT_EQ(States[0]->BeginCallCount, 1u);
    EXPECT_EQ(States[0]->EndCalls.size(), 1u);
    EXPECT_EQ(States[1]->BeginCallCount, 1u);
    EXPECT_TRUE(States[1]->EndCalls.empty());
    EXPECT_EQ(States[2]->BeginCallCount, 0u);
    EXPECT_TRUE(States[2]->EndCalls.empty());
}

TEST_F(RadientAnimationBindingTest, PropagatesEndUpdateFailureAndStopsLaterDestinations)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto                                                          EndOrder = std::make_shared<std::vector<Uint32>>();
    std::array<std::shared_ptr<TestAnimationDestinationState>, 3> States   = {
        std::make_shared<TestAnimationDestinationState>(),
        std::make_shared<TestAnimationDestinationState>(),
        std::make_shared<TestAnimationDestinationState>()};
    std::array<RefCntAutoPtr<TestAnimationDestination>, 3> DestinationObjects;
    std::array<RadientAnimationDestinationMappingDesc, 3>  Mappings{};
    std::array<RadientAnimationDestinationDesc, 3>         Destinations{};
    for (Uint32 Index = 0; Index < static_cast<Uint32>(States.size()); ++Index)
    {
        States[Index]->DestinationID       = Index;
        States[Index]->GlobalEndOrder      = EndOrder;
        DestinationObjects[Index]          = CreateTestDestination(States[Index]);
        Mappings[Index].ClipTargetIndex    = Target;
        Mappings[Index].DestinationElement = Index;
        Destinations[Index].pDestination   = DestinationObjects[Index];
        Destinations[Index].pMappings      = &Mappings[Index];
        Destinations[Index].MappingCount   = 1;
    }
    States[1]->EndStatuses.push_back(RADIENT_STATUS_FAILED);

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(
        pClip,
        std::vector<RadientAnimationDestinationDesc>{Destinations.begin(), Destinations.end()});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    ErrorAllowanceScope          Scope;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_FAILED);
    EXPECT_EQ(*EndOrder, (std::vector<Uint32>{0, 1}));
    EXPECT_EQ(States[0]->BeginCallCount, 1u);
    EXPECT_EQ(States[0]->EndCalls.size(), 1u);
    EXPECT_EQ(States[1]->BeginCallCount, 1u);
    EXPECT_EQ(States[1]->EndCalls.size(), 1u);
    EXPECT_EQ(States[2]->BeginCallCount, 0u);
    EXPECT_TRUE(States[2]->EndCalls.empty());
}

TEST_F(RadientAnimationBindingTest, RejectsUnexpectedPositiveUpdateStatuses)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State                                           = std::make_shared<TestAnimationDestinationState>();
    State->BeginStatuses                                 = {RADIENT_STATUS_NO_CHANGE, RADIENT_STATUS_OK};
    State->EndStatuses                                   = {RADIENT_STATUS_NO_CHANGE};
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 1;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    ErrorAllowanceScope          Scope;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State->BeginCallCount, 1u);
    EXPECT_TRUE(State->EndCalls.empty());

    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State->BeginCallCount, 2u);
    EXPECT_EQ(State->EndCalls.size(), 1u);
}

TEST_F(RadientAnimationBindingTest, RetainsClipDestinationAndChildBinding)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {0.f, 1.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);
    IRadientAnimationClipAsset* const pRawClip = pClip;

    auto                                    State        = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 1;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(State->LiveChildBindings, 1u);

    pClip.Release();
    pDestination.Release();
    EXPECT_FALSE(State->DestinationDestroyed);
    EXPECT_EQ(pBinding->GetClip(), pRawClip);

    RadientAnimationEvaluateInfo Info;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    pBinding.Release();
    EXPECT_EQ(State->LiveChildBindings, 0u);
    EXPECT_EQ(State->DestroyedChildBindings, 1u);
    EXPECT_TRUE(State->DestinationDestroyed);
}

class RadientSkeletonPoseAnimationDestinationTest : public RadientAnimationBindingTest
{
protected:
    void SetUp() override
    {
        RadientAnimationBindingTest::SetUp();

        m_Joints[0].LocalRestTransform.Position = {1.f, 2.f, 3.f};
        m_Joints[0].LocalRestTransform.Scale    = {1.f, 2.f, 3.f};

        m_Joints[1].ParentJointIndex            = 0;
        m_Joints[1].LocalRestTransform.Position = {4.f, 5.f, 6.f};
        m_Joints[1].LocalRestTransform.Scale    = {2.f, 3.f, 4.f};

        m_Joints[2].LocalRestTransform.Position = {7.f, 8.f, 9.f};
        m_Joints[2].LocalRestTransform.Rotation = {0.70710678f, 0.f, 0.f, 0.70710678f};
        m_Joints[2].LocalRestTransform.Scale    = {3.f, 4.f, 5.f};

        RadientSkeletonDesc SkeletonDesc;
        SkeletonDesc.Name       = "Animation destination skeleton";
        SkeletonDesc.pJoints    = m_Joints.data();
        SkeletonDesc.JointCount = static_cast<Uint32>(m_Joints.size());
        ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, m_pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        ASSERT_NE(m_pSkeleton, nullptr);
        ASSERT_EQ(m_pSkeleton->CreatePose(m_pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pPose, nullptr);

        m_pPose->QueryInterface(IID_RadientAnimationDestination, m_pDestination.GetAddressOfEmpty());
        ASSERT_NE(m_pDestination, nullptr);
    }

    static RadientAnimationPropertyBindingDesc MakeNodeProperty(
        RadientAnimationDestinationElement Element,
        RadientAnimationPropertyID         Property,
        RADIENT_ANIMATION_VALUE_TYPE       Type,
        Uint32                             FirstArrayElement = 0,
        Uint32                             ArraySize         = 1,
        RadientAnimationSchemaID           Schema            = RadientNodeAnimationSchemaID)
    {
        RadientAnimationPropertyBindingDesc Desc;
        Desc.Schema             = Schema;
        Desc.DestinationElement = Element;
        Desc.Property           = Property;
        Desc.FirstArrayElement  = FirstArrayElement;
        Desc.Value.Type         = Type;
        Desc.Value.ArraySize    = ArraySize;
        return Desc;
    }

    RefCntAutoPtr<IRadientAnimationDestinationBinding> CreateDestinationBinding(
        const std::vector<RadientAnimationPropertyBindingDesc>& Properties)
    {
        std::vector<RadientAnimationResolvedPropertyDesc>  Resolved(Properties.size());
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      Properties.data(),
                      static_cast<Uint32>(Properties.size()),
                      Resolved.data(),
                      pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pBinding;
    }

    std::array<RadientTransform, 3> GetLocalTransforms() const
    {
        std::array<RadientTransform, 3> Transforms{};
        EXPECT_EQ(m_pPose->GetJointLocalTransforms(
                      0,
                      static_cast<Uint32>(Transforms.size()),
                      Transforms.data()),
                  RADIENT_STATUS_OK);
        return Transforms;
    }

protected:
    std::array<RadientSkeletonJointDesc, 3>     m_Joints{};
    RefCntAutoPtr<IRadientSkeletonAsset>        m_pSkeleton;
    RefCntAutoPtr<IRadientSkeletonPose>         m_pPose;
    RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
};

TEST_F(RadientSkeletonPoseAnimationDestinationTest, PoseExposesAnimationDestination)
{
    RefCntAutoPtr<IObject> pPoseIdentity;
    RefCntAutoPtr<IObject> pDestinationIdentity;
    m_pPose->QueryInterface(IID_Unknown, pPoseIdentity.GetAddressOfEmpty());
    m_pDestination->QueryInterface(IID_Unknown, pDestinationIdentity.GetAddressOfEmpty());
    ASSERT_NE(pPoseIdentity, nullptr);
    ASSERT_NE(pDestinationIdentity, nullptr);
    EXPECT_EQ(pPoseIdentity, pDestinationIdentity);

    RefCntAutoPtr<IRadientSkeletonPose> pRoundTripPose;
    m_pDestination->QueryInterface(IID_RadientSkeletonPose, pRoundTripPose.GetAddressOfEmpty());
    EXPECT_EQ(pRoundTripPose, m_pPose);
    pRoundTripPose.Release();
    pPoseIdentity.Release();
    pDestinationIdentity.Release();

    IRadientSkeletonPose* const pRawPose = m_pPose;
    m_pPose.Release();
    m_pSkeleton.Release();

    RadientTransform Transform;
    EXPECT_EQ(pRawPose->GetJointLocalTransforms(0, 1, &Transform), RADIENT_STATUS_OK);
    EXPECT_EQ(Transform, m_Joints[0].LocalRestTransform);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, ResolvesNodeTransformProperties)
{
    const std::array Properties = {
        MakeNodeProperty(2, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(0, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(1, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 3> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, ResolvesDistinctTransformComponentsPerJoint)
{
    const std::array Properties = {
        MakeNodeProperty(1, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 4> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, UpdatesMappedJointPropertiesInOneBatch)
{
    TestAnimationClipBuilder Builder;
    const Uint32             TransformTarget    = Builder.AddTarget(12, RadientNodeAnimationSchemaID);
    const Uint32             RotationTarget     = Builder.AddTarget(47, RadientNodeAnimationSchemaID);
    const Uint32             TranslationSampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{10.f, 20.f, 30.f}, {20.f, 40.f, 60.f}});
    const Uint32 ScaleSampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{2.f, 4.f, 6.f}, {4.f, 6.f, 8.f}});
    const Uint32 RotationSampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 0.f}});
    Builder.AddChannel(TransformTarget, RadientNodeTranslationProperty, TranslationSampler);
    Builder.AddChannel(RotationTarget, RadientNodeRotationProperty, RotationSampler);
    Builder.AddChannel(TransformTarget, RadientNodeScaleProperty, ScaleSampler);

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex                      = TransformTarget;
    Mappings[0].DestinationElement                   = 2;
    Mappings[1].ClipTargetIndex                      = RotationTarget;
    Mappings[1].DestinationElement                   = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip, m_pDestination, {Mappings[0], Mappings[1]});
    ASSERT_NE(pBinding, nullptr);

    const Uint64                 InitialVersion = m_pPose->GetVersion();
    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion + 1);

    const std::array<RadientTransform, 3> Transforms = GetLocalTransforms();
    EXPECT_EQ(Transforms[0].Position, m_Joints[0].LocalRestTransform.Position);
    ExpectQuaternionNear(Transforms[0].Rotation, {0.f, 0.f, 0.70710678f, 0.70710678f});
    EXPECT_EQ(Transforms[0].Scale, m_Joints[0].LocalRestTransform.Scale);
    EXPECT_EQ(Transforms[1], m_Joints[1].LocalRestTransform);
    ExpectFloat3Near(Transforms[2].Position, {15.f, 30.f, 45.f});
    ExpectQuaternionNear(Transforms[2].Rotation, m_Joints[2].LocalRestTransform.Rotation);
    ExpectFloat3Near(Transforms[2].Scale, {3.f, 5.f, 7.f});

    std::array<RadientMatrix4x4, 3> GlobalMatrices{};
    EXPECT_EQ(m_pPose->GetJointGlobalMatrices(
                  0,
                  static_cast<Uint32>(GlobalMatrices.size()),
                  GlobalMatrices.data()),
              RADIENT_STATUS_OK);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, DefersDerivedStateUpdate)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1, RadientNodeAnimationSchemaID);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}});
    Builder.AddChannel(Target, RadientNodeTranslationProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 1;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    const Uint64                 InitialVersion = m_pPose->GetVersion();
    RadientAnimationEvaluateInfo Info;
    Info.Time               = 0.5f;
    Info.UpdateDerivedState = False;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion);

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(m_pPose->GetJointGlobalMatrices(1, 1, &Matrix), RADIENT_STATUS_PENDING);

    Info.UpdateDerivedState = True;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion + 1);
    EXPECT_EQ(m_pPose->GetJointGlobalMatrices(1, 1, &Matrix), RADIENT_STATUS_OK);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, UpdatesIdenticalValuesWithoutChangeDetection)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1, RadientNodeAnimationSchemaID);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f},
        {{10.f, 20.f, 30.f}});
    Builder.AddChannel(Target, RadientNodeTranslationProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 1;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    const Uint64                 InitialVersion = m_pPose->GetVersion();
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion + 1);

    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion + 2);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, PreservesExternalSparseChangesBetweenEvaluations)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1, RadientNodeAnimationSchemaID);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}});
    Builder.AddChannel(Target, RadientNodeTranslationProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(m_pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    std::array<RadientTransform, 3> ExternalTransforms = GetLocalTransforms();
    ExternalTransforms[0].Scale                        = {9.f, 8.f, 7.f};
    ExternalTransforms[2].Position                     = {100.f, 200.f, 300.f};
    ASSERT_EQ(pWriter->SetJointLocalTransforms(
                  0,
                  static_cast<Uint32>(ExternalTransforms.size()),
                  ExternalTransforms.data()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    const std::array<RadientTransform, 3> Transforms = GetLocalTransforms();
    EXPECT_EQ(Transforms[0].Position, (RadientFloat3{20.f, 0.f, 0.f}));
    EXPECT_EQ(Transforms[0].Scale, ExternalTransforms[0].Scale);
    EXPECT_EQ(Transforms[2], ExternalTransforms[2]);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, BindingRetainsPoseAndClip)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(1, RadientNodeAnimationSchemaID);
    const Uint32             Sampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}});
    Builder.AddChannel(Target, RadientNodeTranslationProperty, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    IRadientAnimationClipAsset* const pRawClip = pClip;
    IRadientSkeletonPose* const       pRawPose = m_pPose;
    pClip.Release();
    m_pDestination.Release();
    m_pPose.Release();
    m_pSkeleton.Release();

    EXPECT_EQ(pBinding->GetClip(), pRawClip);
    RadientAnimationEvaluateInfo Info;
    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    RadientTransform Transform;
    ASSERT_EQ(pRawPose->GetJointLocalTransforms(0, 1, &Transform), RADIENT_STATUS_OK);
    EXPECT_EQ(Transform.Position, (RadientFloat3{20.f, 0.f, 0.f}));
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, DestinationBindingRetainsPose)
{
    const std::vector Properties = {
        MakeNodeProperty(0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(2, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(Properties);
    ASSERT_NE(pBinding, nullptr);

    IRadientSkeletonPose* const pRawPose = m_pPose;
    m_pDestination.Release();
    m_pPose.Release();
    m_pSkeleton.Release();

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);
    ASSERT_NE(pOutputs[2], nullptr);

    const RadientFloat3     Translation = {10.f, 20.f, 30.f};
    const RadientQuaternion Rotation    = {0.f, 0.f, 1.f, 0.f};
    const RadientFloat3     Scale       = {4.f, 5.f, 6.f};
    std::memcpy(pOutputs[0], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[1], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[2], &Scale, sizeof(Scale));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    std::array<RadientTransform, 3> Transforms{};
    ASSERT_EQ(pRawPose->GetJointLocalTransforms(
                  0,
                  static_cast<Uint32>(Transforms.size()),
                  Transforms.data()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(Transforms[0].Position, Translation);
    EXPECT_EQ(Transforms[1].Rotation, Rotation);
    EXPECT_EQ(Transforms[2].Scale, Scale);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsEmptyPropertyBindingRequest)
{
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 0, nullptr, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullPropertyBindingArray)
{
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullResolvedPropertyArray)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, nullptr, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullDestinationBindingOutput)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc Resolved;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNonNullDestinationBindingOutputWithoutOverwritingIt)
{
    const std::vector Properties = {
        MakeNodeProperty(0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(Properties);
    ASSERT_NE(pBinding, nullptr);

    IRadientAnimationDestinationBinding* pOutput = pBinding;
    RadientAnimationResolvedPropertyDesc Resolved;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  &Resolved,
                  &pOutput),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pOutput, pBinding.RawPtr());
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsInvalidSchemaIdentifier)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0,
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        InvalidRadientAnimationSchemaID);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsInvalidPropertyIdentifier)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, InvalidRadientAnimationPropertyID, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsInvalidAnimationValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsEmptyAnimationValueArray)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 0);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsUnaddressablePropertyTableOnWin32)
{
    if (sizeof(size_t) != sizeof(Uint32))
        GTEST_SKIP() << "The property-count boundary is specific to 32-bit address spaces";

    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  &Property,
                  std::numeric_limits<Uint32>::max(),
                  &Resolved,
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsUnsupportedSchema)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0,
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        TestAnimationSchemaID);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsUnsupportedNodeProperty)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsOutOfRangeJointIndex)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        static_cast<Uint32>(m_Joints.size()),
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNonRepresentableJointIndex)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        static_cast<Uint64>(std::numeric_limits<Uint32>::max()) + 1,
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsWrongNodePropertyValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsWrongRotationValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsWrongScaleValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNodePropertyArrayOffset)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNodePropertyArraySize)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 2);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsDuplicatePhysicalWrites)
{
    struct DuplicateCase
    {
        RadientAnimationPropertyID   Property;
        RADIENT_ANIMATION_VALUE_TYPE Type;
    };

    const DuplicateCase Cases[] = {
        {RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3},
        {RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4},
        {RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3},
    };
    for (const DuplicateCase& Case : Cases)
    {
        SCOPED_TRACE(Case.Property);
        const std::array Properties = {
            MakeNodeProperty(0, Case.Property, Case.Type),
            MakeNodeProperty(0, Case.Property, Case.Type),
        };
        std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      Properties.data(),
                      static_cast<Uint32>(Properties.size()),
                      Resolved.data(),
                      pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_FALSE(pBinding);
    }
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, ResolvesAllPropertiesBeforeRejectingDuplicateWrites)
{
    const std::array Properties = {
        MakeNodeProperty(0, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 3> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Result : Resolved)
        Result.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    for (const RadientAnimationResolvedPropertyDesc& Result : Resolved)
        EXPECT_EQ(Result.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullBeginUpdateOutput)
{
    const std::vector Properties = {
        MakeNodeProperty(0, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(Properties);
    ASSERT_NE(pBinding, nullptr);

    const std::array<RadientTransform, 3> Before        = GetLocalTransforms();
    const Uint64                          BeforeVersion = m_pPose->GetVersion();
    EXPECT_EQ(pBinding->BeginUpdate(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(GetLocalTransforms(), Before);
    EXPECT_EQ(m_pPose->GetVersion(), BeforeVersion);

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(m_pPose->GetJointGlobalMatrices(0, 1, &Matrix), RADIENT_STATUS_OK);
}

} // namespace
