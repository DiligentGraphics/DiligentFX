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
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialStorage.hpp"
#include "RadientMaterialTestHelpers.hpp"

#include "RadientAnimation.h"
#include "RadientSkinning.h"
#include "RadientStandardMaterialParameters.h"

#include "ObjectBase.hpp"
#include "RadientMathTestHelpers.hpp"
#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
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

static constexpr Char TestPropertyA[]          = "Custom.PropertyA";
static constexpr Char TestPropertyB[]          = "Custom.PropertyB";
static constexpr Char TestPropertyC[]          = "Custom.PropertyC";
static constexpr Char TestQuaternionProperty[] = "Custom.Quaternion";

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

    void AddChannel(Uint32      TargetIndex,
                    const Char* Property,
                    Uint32      SamplerIndex,
                    Uint32      FirstArrayElement = 0)
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

    RADIENT_STATUS CreateStatus                = RADIENT_STATUS_OK;
    bool           ReturnNullChild             = false;
    bool           ReturnChildOnCreateFailure  = false;
    bool           ReturnStatusAfterResolution = false;
    bool           ReturnBindingWhenUnbound    = false;

    std::vector<std::pair<std::string, RADIENT_ANIMATION_VALUE_SEMANTIC>> Semantics;
    std::vector<RADIENT_ANIMATION_VALUE_SEMANTIC>                         SemanticsByRequest;
    std::vector<RADIENT_STATUS>                                           BeginStatuses;
    std::vector<RADIENT_STATUS>                                           EndStatuses;

    std::vector<std::vector<RadientAnimationPropertyBindingDesc>> CreateRequests;
    std::deque<std::string>                                       CreatePropertyNames;
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

        for (const std::pair<std::string, RADIENT_ANIMATION_VALUE_SEMANTIC>& Semantic : Semantics)
        {
            if (Semantic.first == Property.Property)
            {
                return Semantic.second;
            }
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
        m_OutputStorage(Properties.size()),
        m_OutputPointers(Properties.size()),
        m_OutputSizes(Properties.size())
    {
        for (size_t PropertyIndex = 0; PropertyIndex < Properties.size(); ++PropertyIndex)
        {
            const RadientAnimationValueDesc& Value = Properties[PropertyIndex].Value;
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
        Call.Values.resize(m_OutputPointers.size());
        Call.OutputAddresses.resize(m_OutputPointers.size());
        Call.ValueDataSizes.resize(m_OutputPointers.size());
        for (size_t PropertyIndex = 0; PropertyIndex < m_OutputPointers.size(); ++PropertyIndex)
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
    RefCntAutoPtr<IRadientAnimationDestination>    m_pDestination;
    std::shared_ptr<TestAnimationDestinationState> m_State;
    std::vector<std::vector<std::max_align_t>>     m_OutputStorage;
    std::vector<void*>                             m_OutputPointers;
    std::vector<size_t>                            m_OutputSizes;
};

bool PropertyRangesOverlap(const RadientAnimationPropertyBindingDesc& Lhs,
                           const RadientAnimationPropertyBindingDesc& Rhs)
{
    if (Lhs.Schema != Rhs.Schema ||
        Lhs.DestinationElement != Rhs.DestinationElement ||
        std::strcmp(Lhs.Property, Rhs.Property) != 0)
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

        if (m_State->CreateStatus != RADIENT_STATUS_OK &&
            !m_State->ReturnChildOnCreateFailure &&
            !m_State->ReturnStatusAfterResolution)
            return m_State->CreateStatus;

        if (PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
            pResolvedProperties[PropertyIndex] = {};

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            if (pProperties[PropertyIndex].Property == nullptr ||
                pProperties[PropertyIndex].Property[0] == '\0')
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        m_State->CreateRequests.emplace_back(pProperties, pProperties + PropertyCount);
        for (RadientAnimationPropertyBindingDesc& Property : m_State->CreateRequests.back())
        {
            m_State->CreatePropertyNames.emplace_back(Property.Property);
            Property.Property = m_State->CreatePropertyNames.back().c_str();
        }

        Uint32 AcceptedPropertyCount = 0;
        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            if (pProperties[PropertyIndex].DestinationElement == InvalidRadientAnimationDestinationElement)
                return RADIENT_STATUS_INVALID_ARGUMENT;

            pResolvedProperties[PropertyIndex].Semantic =
                m_State->GetSemantic(pProperties[PropertyIndex], PropertyIndex);
            if (pResolvedProperties[PropertyIndex].Semantic != RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN)
            {
                for (Uint32 PreviousIndex = 0; PreviousIndex < PropertyIndex; ++PreviousIndex)
                {
                    if (pResolvedProperties[PreviousIndex].Semantic != RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN &&
                        PropertyRangesOverlap(pProperties[PreviousIndex], pProperties[PropertyIndex]))
                    {
                        return RADIENT_STATUS_INVALID_ARGUMENT;
                    }
                }

                ++AcceptedPropertyCount;
            }
        }

        if (m_State->CreateStatus != RADIENT_STATUS_OK && m_State->ReturnStatusAfterResolution)
            return m_State->CreateStatus;

        if (AcceptedPropertyCount == 0 &&
            m_State->CreateStatus == RADIENT_STATUS_OK &&
            !m_State->ReturnBindingWhenUnbound)
            return RADIENT_STATUS_UNSUPPORTED;

        if (m_State->ReturnNullChild)
            return RADIENT_STATUS_OK;

        std::vector<RadientAnimationPropertyBindingDesc> Properties;
        Properties.reserve(AcceptedPropertyCount);
        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            if (pResolvedProperties[PropertyIndex].Semantic != RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN)
                Properties.push_back(pProperties[PropertyIndex]);
        }
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

void ExpectProperty(const RadientAnimationPropertyBindingDesc& Property,
                    RadientAnimationDestinationElement         Element,
                    const Char*                                PropertyName,
                    RADIENT_ANIMATION_VALUE_TYPE               Type,
                    Uint32                                     FirstArrayElement = 0,
                    Uint32                                     ArraySize         = 1)
{
    EXPECT_TRUE(Property.Schema == TestAnimationSchemaID);
    EXPECT_EQ(Property.DestinationElement, Element);
    EXPECT_STREQ(Property.Property, PropertyName);
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

TEST_F(RadientAnimationBindingTest, SkipsUnboundPropertiesAndCompactsDestinationOutputs)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target   = Builder.AddTarget(100);
    const Uint32             SamplerA = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 6.f});
    const Uint32 SamplerB = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {100.f, 200.f});
    const Uint32 SamplerC = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {10.f, 30.f});
    Builder.AddChannel(Target, TestPropertyA, SamplerA);
    Builder.AddChannel(Target, TestPropertyB, SamplerB);
    Builder.AddChannel(Target, TestPropertyC, SamplerC);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State                = std::make_shared<TestAnimationDestinationState>();
    State->SemanticsByRequest = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
        RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
    };
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 7;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    ASSERT_EQ(State->CreateRequests.size(), 1u);
    ASSERT_EQ(State->CreateRequests[0].size(), 3u);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    EXPECT_EQ(State->BeginCallCount, 1u);
    ASSERT_EQ(State->EndCalls.size(), 1u);
    ASSERT_EQ(State->EndCalls[0].Values.size(), 2u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 4.f);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 1), 20.f);
}

TEST_F(RadientAnimationBindingTest, IgnoresOverlappingRangesForUnboundProperties)
{
    TestAnimationClipBuilder Builder;
    const Uint32             AcceptedTarget = Builder.AddTarget(100);
    const Uint32             IgnoredTarget0 = Builder.AddTarget(200);
    const Uint32             IgnoredTarget1 = Builder.AddTarget(300);
    const Uint32             Sampler        = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 8.f});
    Builder.AddChannel(AcceptedTarget, TestPropertyA, Sampler);
    Builder.AddChannel(IgnoredTarget0, TestPropertyB, Sampler);
    Builder.AddChannel(IgnoredTarget1, TestPropertyB, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State                = std::make_shared<TestAnimationDestinationState>();
    State->SemanticsByRequest = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
    };
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);

    std::array<RadientAnimationDestinationMappingDesc, 3> Mappings{};
    Mappings[0].ClipTargetIndex                      = AcceptedTarget;
    Mappings[0].DestinationElement                   = 7;
    Mappings[1].ClipTargetIndex                      = IgnoredTarget0;
    Mappings[1].DestinationElement                   = 9;
    Mappings[2].ClipTargetIndex                      = IgnoredTarget1;
    Mappings[2].DestinationElement                   = 9;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(
        pClip, pDestination, {Mappings[0], Mappings[1], Mappings[2]});
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(State->CreateRequests.size(), 1u);
    ASSERT_EQ(State->CreateRequests[0].size(), 3u);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    ASSERT_EQ(State->EndCalls.size(), 1u);
    ASSERT_EQ(State->EndCalls[0].Values.size(), 1u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(State->EndCalls[0], 0), 5.f);
}

TEST_F(RadientAnimationBindingTest, OmitsEntirelyUnsupportedDestination)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(100);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {2.f, 8.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto UnsupportedState                = std::make_shared<TestAnimationDestinationState>();
    UnsupportedState->SemanticsByRequest = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
    };
    auto                                    SupportedState = std::make_shared<TestAnimationDestinationState>();
    RefCntAutoPtr<TestAnimationDestination> pUnsupportedDestination =
        CreateTestDestination(UnsupportedState);
    RefCntAutoPtr<TestAnimationDestination> pSupportedDestination =
        CreateTestDestination(SupportedState);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 7;
    std::array<RadientAnimationDestinationDesc, 2> Destinations{};
    Destinations[0].pDestination = pUnsupportedDestination;
    Destinations[0].pMappings    = &Mapping;
    Destinations[0].MappingCount = 1;
    Destinations[1].pDestination = pSupportedDestination;
    Destinations[1].pMappings    = &Mapping;
    Destinations[1].MappingCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding = Bind(
        pClip,
        std::vector<RadientAnimationDestinationDesc>{Destinations.begin(), Destinations.end()});
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(UnsupportedState->CreateRequests.size(), 1u);
    EXPECT_EQ(UnsupportedState->LiveChildBindings, 0u);

    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);

    EXPECT_EQ(UnsupportedState->BeginCallCount, 0u);
    EXPECT_TRUE(UnsupportedState->EndCalls.empty());
    ASSERT_EQ(SupportedState->EndCalls.size(), 1u);
    EXPECT_FLOAT_EQ(ReadCapturedValue<Float32>(SupportedState->EndCalls[0], 0), 5.f);
}

TEST_F(RadientAnimationBindingTest, CreatesEmptyBindingWhenEveryDestinationIsUnsupported)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target  = Builder.AddTarget(100);
    const Uint32             Sampler = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f},
        {7.f});
    Builder.AddChannel(Target, TestPropertyA, Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    auto State                = std::make_shared<TestAnimationDestinationState>();
    State->SemanticsByRequest = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
    };
    RefCntAutoPtr<TestAnimationDestination> pDestination = CreateTestDestination(State);
    RadientAnimationDestinationMappingDesc  Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 7;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(State->CreateRequests.size(), 1u);
    EXPECT_EQ(State->LiveChildBindings, 0u);

    RadientAnimationEvaluateInfo Info;
    EXPECT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State->BeginCallCount, 0u);
    EXPECT_TRUE(State->EndCalls.empty());
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
    InvalidMapping.DestinationElement                     = InvalidRadientAnimationDestinationElement;
    InvalidDestination                                    = Destination;
    InvalidDestination.pMappings                          = &InvalidMapping;
    Desc.pDestinations                                    = &InvalidDestination;
    ExpectInvalid(Desc);
    EXPECT_TRUE(State->CreateRequests.empty());

    InvalidMapping                 = Mapping;
    InvalidMapping.ClipTargetIndex = pClip->GetDesc().TargetCount;
    InvalidDestination             = Destination;
    InvalidDestination.pMappings   = &InvalidMapping;
    Desc.pDestinations             = &InvalidDestination;
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
    const std::string        PropertyName{TestPropertyA};
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
    Builder.AddChannel(Target1, PropertyName.c_str(), Sampler1);
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
    Builder.AddChannel(Target, TestPropertyA, Sampler);
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

    auto UnboundSuccessState                = std::make_shared<TestAnimationDestinationState>();
    UnboundSuccessState->SemanticsByRequest = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
        RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN,
    };
    UnboundSuccessState->ReturnBindingWhenUnbound = true;
    ExpectInvalidOperation(UnboundSuccessState);

    auto PositiveStatusState          = std::make_shared<TestAnimationDestinationState>();
    PositiveStatusState->CreateStatus = RADIENT_STATUS_NO_CHANGE;
    ExpectInvalidOperation(PositiveStatusState);

    auto FailureWithChildState                        = std::make_shared<TestAnimationDestinationState>();
    FailureWithChildState->CreateStatus               = RADIENT_STATUS_NOT_FOUND;
    FailureWithChildState->ReturnChildOnCreateFailure = true;
    ExpectInvalidOperation(FailureWithChildState);
    EXPECT_EQ(FailureWithChildState->LiveChildBindings, 0u);
    EXPECT_EQ(FailureWithChildState->DestroyedChildBindings, 1u);

    auto UnsupportedWithResolvedState                         = std::make_shared<TestAnimationDestinationState>();
    UnsupportedWithResolvedState->CreateStatus                = RADIENT_STATUS_UNSUPPORTED;
    UnsupportedWithResolvedState->ReturnStatusAfterResolution = true;
    ExpectInvalidOperation(UnsupportedWithResolvedState);

    auto InvalidSemanticState = std::make_shared<TestAnimationDestinationState>();
    InvalidSemanticState->Semantics.emplace_back(TestQuaternionProperty,
                                                 RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT);
    ExpectInvalidOperation(InvalidSemanticState);
}

TEST_F(RadientAnimationBindingTest, RejectsInconsistentSemanticsForEqualPropertyNames)
{
    const std::string        PropertyName{TestQuaternionProperty};
    TestAnimationClipBuilder Builder;
    const Uint32             Target0 = Builder.AddTarget(1);
    const Uint32             Target1 = Builder.AddTarget(2);
    const Uint32             Sampler = Builder.AddSampler<RadientQuaternion>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 0.f}});
    Builder.AddChannel(Target0, TestQuaternionProperty, Sampler);
    Builder.AddChannel(Target1, PropertyName.c_str(), Sampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    std::shared_ptr<TestAnimationDestinationState> State = std::make_shared<TestAnimationDestinationState>();
    State->SemanticsByRequest                            = {
        RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
        RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION,
    };
    RefCntAutoPtr<TestAnimationDestination>               pDestination = CreateTestDestination(State);
    std::array<RadientAnimationDestinationMappingDesc, 2> Mappings{};
    Mappings[0].ClipTargetIndex    = Target0;
    Mappings[0].DestinationElement = 1;
    Mappings[1].ClipTargetIndex    = Target1;
    Mappings[1].DestinationElement = 2;

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
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_FALSE(pBinding);
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
    ASSERT_EQ(State->CreateRequests.size(), 1u);
    ASSERT_EQ(State->CreateRequests[0].size(), 1u);
    EXPECT_STREQ(State->CreateRequests[0][0].Property, TestPropertyA);
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
        const Char*                        Property,
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
        MakeNodeProperty(2, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(0, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(1, RadientNodeScalePropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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
        MakeNodeProperty(1, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RadientNodeScalePropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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

TEST_F(RadientSkeletonPoseAnimationDestinationTest, ResolvesSupportedSubsetWithCompactOutputs)
{
    const std::array Properties = {
        MakeNodeProperty(0, RadientNodeVisibilityPropertyName, RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeNodeProperty(1, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(1,
                         RadientNodeTranslationPropertyName,
                         RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                         0,
                         1,
                         TestAnimationSchemaID),
        MakeNodeProperty(2, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(0, RadientNodeScalePropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 5> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Result : Resolved)
        Result.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[4].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    const RadientQuaternion Rotation    = {0.f, 0.f, 1.f, 0.f};
    const RadientFloat3     Translation = {10.f, 20.f, 30.f};
    const RadientFloat3     Scale       = {4.f, 5.f, 6.f};
    std::memcpy(pOutputs[0], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[1], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[2], &Scale, sizeof(Scale));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    const std::array<RadientTransform, 3> Transforms = GetLocalTransforms();
    EXPECT_EQ(Transforms[0].Scale, Scale);
    EXPECT_EQ(Transforms[1].Rotation, Rotation);
    EXPECT_EQ(Transforms[2].Position, Translation);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, IgnoresVisibilityWhileAnimatingJointTransform)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target             = Builder.AddTarget(12, RadientNodeAnimationSchemaID);
    const Uint32             TranslationSampler = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f},
        {{10.f, 20.f, 30.f}, {20.f, 40.f, 60.f}});
    const Uint32 VisibilitySampler = Builder.AddSampler<Uint8>(
        RADIENT_ANIMATION_VALUE_TYPE_BOOL,
        RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f},
        {0, 1});
    Builder.AddChannel(Target, RadientNodeTranslationPropertyName, TranslationSampler);
    Builder.AddChannel(Target, RadientNodeVisibilityPropertyName, VisibilitySampler);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 1;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);

    const Uint64                 InitialVersion = m_pPose->GetVersion();
    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pPose->GetVersion(), InitialVersion + 1);

    const std::array<RadientTransform, 3> Transforms = GetLocalTransforms();
    EXPECT_EQ(Transforms[0], m_Joints[0].LocalRestTransform);
    ExpectFloat3Near(Transforms[1].Position, {15.f, 30.f, 45.f});
    EXPECT_EQ(Transforms[1].Rotation, m_Joints[1].LocalRestTransform.Rotation);
    EXPECT_EQ(Transforms[1].Scale, m_Joints[1].LocalRestTransform.Scale);
    EXPECT_EQ(Transforms[2].Position, m_Joints[2].LocalRestTransform.Position);
    ExpectQuaternionNear(Transforms[2].Rotation, m_Joints[2].LocalRestTransform.Rotation);
    EXPECT_EQ(Transforms[2].Scale, m_Joints[2].LocalRestTransform.Scale);
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
    Builder.AddChannel(TransformTarget, RadientNodeTranslationPropertyName, TranslationSampler);
    Builder.AddChannel(RotationTarget, RadientNodeRotationPropertyName, RotationSampler);
    Builder.AddChannel(TransformTarget, RadientNodeScalePropertyName, ScaleSampler);

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
    Builder.AddChannel(Target, RadientNodeTranslationPropertyName, Sampler);
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
    Builder.AddChannel(Target, RadientNodeTranslationPropertyName, Sampler);
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
    Builder.AddChannel(Target, RadientNodeTranslationPropertyName, Sampler);
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
    Builder.AddChannel(Target, RadientNodeTranslationPropertyName, Sampler);
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

TEST_F(RadientSkeletonPoseAnimationDestinationTest, DestinationBindingRetainsPoseAndCompiledProperties)
{
    Char              TranslationName[] = "Translation";
    Char              RotationName[]    = "Rotation";
    Char              ScaleName[]       = "Scale";
    const std::vector Properties        = {
        MakeNodeProperty(0, TranslationName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(1, RotationName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(2, ScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(Properties);
    ASSERT_NE(pBinding, nullptr);

    TranslationName[0] = 'X';
    RotationName[0]    = 'X';
    ScaleName[0]       = 'X';

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
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, nullptr, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullDestinationBindingOutput)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc Resolved;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNonNullDestinationBindingOutputWithoutOverwritingIt)
{
    const std::vector Properties = {
        MakeNodeProperty(0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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
        RadientNodeTranslationPropertyName,
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

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullPropertyName)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, nullptr, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsEmptyPropertyName)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, "", RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsInvalidDestinationElementBeforeUnsupportedProperty)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        InvalidRadientAnimationDestinationElement,
        TestPropertyA,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        TestAnimationSchemaID);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsInvalidAnimationValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsEmptyAnimationValueArray)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 0);
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
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
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
        RadientNodeTranslationPropertyName,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        TestAnimationSchemaID);
    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsUnsupportedNodeProperty)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RequiresExactPropertyName)
{
    for (const Char* PropertyName : {"translation", "Translation ", "Translation.Custom"})
    {
        SCOPED_TRACE(PropertyName);
        const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
            0, PropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
        RadientAnimationResolvedPropertyDesc               Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_UNSUPPORTED);
        EXPECT_FALSE(pBinding);
        EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    }
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, DoesNotExposeNodeVisibility)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeVisibilityPropertyName, RADIENT_ANIMATION_VALUE_TYPE_BOOL);
    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, TreatsVisibilityAsUnsupportedBeforeLayoutAndElementResolution)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        static_cast<Uint64>(m_Joints.size()) + 1,
        RadientNodeVisibilityPropertyName,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        1,
        2);
    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsOutOfRangeJointIndex)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        static_cast<Uint32>(m_Joints.size()),
        RadientNodeTranslationPropertyName,
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
        RadientNodeTranslationPropertyName,
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
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsWrongRotationValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsWrongScaleValueType)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeScalePropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNodePropertyArrayOffset)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNodePropertyArraySize)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 2);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsDuplicatePhysicalWrites)
{
    struct DuplicateCase
    {
        const Char*                  Property;
        RADIENT_ANIMATION_VALUE_TYPE Type;
    };

    const DuplicateCase Cases[] = {
        {RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3},
        {RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4},
        {RadientNodeScalePropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3},
    };
    for (const DuplicateCase& Case : Cases)
    {
        SCOPED_TRACE(Case.Property);
        const std::string EqualPropertyName{Case.Property};
        const std::array  Properties = {
            MakeNodeProperty(0, Case.Property, Case.Type),
            MakeNodeProperty(0, EqualPropertyName.c_str(), Case.Type),
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

TEST_F(RadientSkeletonPoseAnimationDestinationTest, UnsupportedPropertyDoesNotMaskDuplicateWrites)
{
    const std::array Properties = {
        MakeNodeProperty(0, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, RadientNodeRotationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(0, TestPropertyA, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 3> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientSkeletonPoseAnimationDestinationTest, RejectsNullBeginUpdateOutput)
{
    const std::vector Properties = {
        MakeNodeProperty(0, RadientNodeTranslationPropertyName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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


class RadientMaterialAnimationDestinationTest : public RadientAnimationBindingTest
{
protected:
    void SetUp() override
    {
        RadientAnimationBindingTest::SetUp();
        RadientStandardMaterialDefinitionCreateInfo Definition;
        ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(Definition, m_pDefinition.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        ASSERT_EQ(pAssetManager->CreateMaterial(m_pDefinition, m_pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pMaterial, nullptr);
        m_pMaterial->QueryInterface(IID_RadientAnimationDestination, m_pDestination.GetAddressOfEmpty());
        ASSERT_NE(m_pDestination, nullptr);
    }

    static RadientAnimationPropertyBindingDesc MakeMaterialProperty(
        const Char*                  PropertyName,
        RADIENT_ANIMATION_VALUE_TYPE Type,
        Uint32                       FirstArrayElement = 0,
        Uint32                       ArraySize         = 1)
    {
        RadientAnimationPropertyBindingDesc Property;
        Property.Schema             = RadientMaterialAnimationSchemaID;
        Property.DestinationElement = 0;
        Property.Property           = PropertyName;
        Property.FirstArrayElement  = FirstArrayElement;
        Property.Value.Type         = Type;
        Property.Value.ArraySize    = ArraySize;
        return Property;
    }

    static RadientAnimationPropertyBindingDesc MakeSurfaceProperty(
        RADIENT_ANIMATION_VALUE_TYPE Type,
        Uint32                       FirstArrayElement = 0,
        Uint32                       ArraySize         = 1)
    {
        RadientAnimationPropertyBindingDesc Property = MakeMaterialProperty(
            RadientSurfaceMaterialAlphaCutoffPropertyName, Type, FirstArrayElement, ArraySize);
        Property.Schema = RadientSurfaceMaterialAnimationSchemaID;
        return Property;
    }

    void CreateCustomMaterial(const RadientMaterialParameterDesc* pParameters, Uint32 ParameterCount)
    {
        RadientSurfaceMaterialDefinitionDesc Definition;
        Definition.pParameters    = pParameters;
        Definition.ParameterCount = ParameterCount;
        m_pDestination.Release();
        m_pMaterial.Release();
        m_pDefinition.Release();
        ASSERT_EQ(RadientMaterialAssetManager::CreateDefinition(Definition, m_pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_EQ(pAssetManager->CreateMaterial(m_pDefinition, m_pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        m_pMaterial->QueryInterface(IID_RadientAnimationDestination, m_pDestination.GetAddressOfEmpty());
        ASSERT_NE(m_pDestination, nullptr);
    }

    template <typename ValueType>
    void VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE ParameterType,
                                RADIENT_ANIMATION_VALUE_TYPE    AnimationType,
                                const ValueType&                Value)
    {
        SCOPED_TRACE(ParameterType);
        std::array<RadientMaterialParameterDesc, 3> Parameters;
        Parameters[0].Name = "User.Prefix";
        Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_BOOL;
        Parameters[1].Name = "User.Value";
        Parameters[1].Type = ParameterType;
        Parameters[2].Name = "User.Suffix";
        Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_BOOL;
        CreateCustomMaterial(Parameters.data(), static_cast<Uint32>(Parameters.size()));
        ASSERT_NE(m_pDestination, nullptr);
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(
            {MakeMaterialProperty(Parameters[0].Name, RADIENT_ANIMATION_VALUE_TYPE_BOOL),
             MakeMaterialProperty(Parameters[1].Name, AnimationType),
             MakeMaterialProperty(Parameters[2].Name, RADIENT_ANIMATION_VALUE_TYPE_BOOL),
             MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT)});
        ASSERT_NE(pBinding, nullptr);
        void* const* pOutputs = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        for (Uint32 Index = 0; Index < 4; ++Index)
            ASSERT_NE(pOutputs[Index], nullptr);

        // One-byte values precede both the numeric parameter and alpha cutoff,
        // so packed output storage must preserve each value's native alignment.
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pOutputs[1]) % alignof(ValueType), 0u);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pOutputs[3]) % alignof(Float32), 0u);
        const Bool    Flag   = True;
        const Float32 Cutoff = 0.25f;
        std::memcpy(pOutputs[0], &Flag, sizeof(Flag));
        std::memcpy(pOutputs[1], &Value, sizeof(Value));
        std::memcpy(pOutputs[2], &Flag, sizeof(Flag));
        std::memcpy(pOutputs[3], &Cutoff, sizeof(Cutoff));
        ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
        EXPECT_EQ(GetMaterialParameter<Bool>(*m_pMaterial, Parameters[0].Name), Flag);
        EXPECT_EQ(GetMaterialParameter<ValueType>(*m_pMaterial, Parameters[1].Name), Value);
        EXPECT_EQ(GetMaterialParameter<Bool>(*m_pMaterial, Parameters[2].Name), Flag);
        RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{m_pMaterial, IID_RadientSurfaceMaterialAsset};
        ASSERT_NE(pSurface, nullptr);
        EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), Cutoff);
    }

    RefCntAutoPtr<IRadientAnimationDestinationBinding> CreateDestinationBinding(
        const std::vector<RadientAnimationPropertyBindingDesc>& Properties)
    {
        std::vector<RadientAnimationResolvedPropertyDesc>  Resolved(Properties.size());
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(Properties.data(), static_cast<Uint32>(Properties.size()),
                                                Resolved.data(), pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pBinding;
    }

    template <typename ValueType>
    void SetParameter(const char* Name, const ValueType& Value)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(m_pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(m_pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    }

protected:
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> m_pDefinition;
    RefCntAutoPtr<IRadientMaterialAsset>           m_pMaterial;
    RefCntAutoPtr<IRadientAnimationDestination>    m_pDestination;
};

TEST_F(RadientMaterialAnimationDestinationTest, DestinationSharesMaterialIdentity)
{
    RefCntAutoPtr<IObject> pMaterialIdentity;
    RefCntAutoPtr<IObject> pDestinationIdentity;
    m_pMaterial->QueryInterface(IID_Unknown, pMaterialIdentity.GetAddressOfEmpty());
    m_pDestination->QueryInterface(IID_Unknown, pDestinationIdentity.GetAddressOfEmpty());
    EXPECT_EQ(pMaterialIdentity, pDestinationIdentity);
    RefCntAutoPtr<IRadientMaterialAsset> pRoundTripMaterial{m_pDestination, IID_RadientMaterialAsset};
    EXPECT_EQ(pRoundTripMaterial, m_pMaterial);
}

TEST_F(RadientMaterialAnimationDestinationTest, ResolvesReflectedAndSurfaceMaterialProperties)
{
    const std::vector<RadientAnimationPropertyBindingDesc> Properties = {
        MakeMaterialProperty(RadientStandardMaterialBaseColorFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeMaterialProperty(RadientStandardMaterialNormalScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureUVBiasName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT2),
        MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureUVScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT2),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureUVRotationName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureUVSelectorName, RADIENT_ANIMATION_VALUE_TYPE_INT),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureWrapUName, RADIENT_ANIMATION_VALUE_TYPE_UINT),
        MakeMaterialProperty(RadientStandardMaterialBaseColorTextureWrapVName, RADIENT_ANIMATION_VALUE_TYPE_UINT),
        MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved(Properties.size());
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(Properties.data(), static_cast<Uint32>(Properties.size()),
                                            Resolved.data(), pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);
    for (const RadientAnimationResolvedPropertyDesc& Property : Resolved)
        EXPECT_EQ(Property.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
}

TEST_F(RadientMaterialAnimationDestinationTest, EvaluatesNativeClipAfterMaterialInitialization)
{
    ASSERT_EQ(RadientMaterialAssetManager::GetLoadStatus(m_pMaterial), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientMaterialAssetManager::GetMaterialView(m_pMaterial).pMaterial, m_pMaterial.RawPtr());
    TestAnimationClipBuilder Builder;
    const Uint32             Target        = Builder.AddTarget(7, RadientMaterialAnimationSchemaID);
    const Uint32             SurfaceTarget = Builder.AddTarget(7, RadientSurfaceMaterialAnimationSchemaID);
    const Uint32             Color         = Builder.AddSampler<RadientFloat4>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {{0.f, 0.2f, 0.4f, 0.6f}, {0.2f, 0.4f, 0.6f, 0.8f}});
    const Uint32 NormalScale = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE,
        {0.f, 1.f}, {0.f, 2.f, 4.f, 0.f, 6.f, 0.f});
    const Uint32 UVBias = Builder.AddSampler<RadientFloat2>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {{0.f, 0.2f}, {0.4f, 0.6f}});
    const Uint32 Emission = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {{0.f, 2.f, 4.f}, {2.f, 4.f, 6.f}});
    const Uint32 UVSelector = Builder.AddSampler<Int32>(
        RADIENT_ANIMATION_VALUE_TYPE_INT, RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f}, {0, 1});
    const Uint32 Wrap = Builder.AddSampler<Uint32>(
        RADIENT_ANIMATION_VALUE_TYPE_UINT, RADIENT_ANIMATION_INTERPOLATION_STEP,
        {0.f, 1.f}, {RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP, RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP});
    const Uint32 UVScale = Builder.AddSampler<RadientFloat2>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {{1.f, 2.f}, {3.f, 4.f}});
    // Rotation is an unwrapped scalar: halfway through one complete turn is pi,
    // rather than zero as shortest-arc interpolation would produce.
    const Uint32 UVRotation = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {0.f, 2.f * PI_F});
    const Uint32 AlphaCutoff = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {0.2f, 0.6f});
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorFactorName, Color);
    Builder.AddChannel(Target, RadientStandardMaterialNormalScaleName, NormalScale);
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorTextureUVBiasName, UVBias);
    Builder.AddChannel(Target, RadientStandardMaterialEmissiveFactorName, Emission);
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorTextureUVSelectorName, UVSelector);
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorTextureWrapUName, Wrap);
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorTextureUVScaleName, UVScale);
    Builder.AddChannel(Target, RadientStandardMaterialBaseColorTextureUVRotationName, UVRotation);
    Builder.AddChannel(SurfaceTarget, RadientSurfaceMaterialAlphaCutoffPropertyName, AlphaCutoff);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);
    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex    = Target;
    Mapping.DestinationElement = 0;
    RadientAnimationDestinationMappingDesc SurfaceMapping;
    SurfaceMapping.ClipTargetIndex                   = SurfaceTarget;
    SurfaceMapping.DestinationElement                = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping, SurfaceMapping});
    ASSERT_NE(pBinding, nullptr);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{m_pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);

    RadientAnimationEvaluateInfo Info;
    Info.Time                   = 0.5f;
    Info.UpdateDerivedState     = False;
    const Uint64 InitialVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), InitialVersion + 1);
    const RadientFloat4 ColorValue = GetMaterialParameter<RadientFloat4>(*m_pMaterial, "BaseColorFactor");
    EXPECT_FLOAT_EQ(ColorValue.x, 0.1f);
    EXPECT_FLOAT_EQ(ColorValue.y, 0.3f);
    EXPECT_FLOAT_EQ(ColorValue.z, 0.5f);
    EXPECT_FLOAT_EQ(ColorValue.w, 0.7f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, "NormalScale"), 4.5f);
    const RadientFloat2 BiasValue = GetMaterialParameter<RadientFloat2>(*m_pMaterial, "BaseColorTextureUVBias");
    EXPECT_FLOAT_EQ(BiasValue.x, 0.2f);
    EXPECT_FLOAT_EQ(BiasValue.y, 0.4f);
    EXPECT_EQ(GetMaterialParameter<RadientFloat3>(*m_pMaterial, "EmissiveFactor"), (RadientFloat3{1.f, 3.f, 5.f}));
    EXPECT_EQ(GetMaterialParameter<Int32>(*m_pMaterial, "BaseColorTextureUVSelector"), 0);
    EXPECT_EQ(GetMaterialParameter<Uint32>(*m_pMaterial, "BaseColorTextureWrapU"), RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP);
    EXPECT_EQ(GetMaterialParameter<RadientFloat2>(*m_pMaterial, "BaseColorTextureUVScale"), (RadientFloat2{2.f, 3.f}));
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, "BaseColorTextureUVRotation"), PI_F);
    EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), 0.4f);
    EXPECT_EQ(pSurface->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_FALSE(pSurface->IsDoubleSided());

    Info.UpdateDerivedState = True;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), InitialVersion + 1);
    Info.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), InitialVersion + 2);
    EXPECT_EQ(GetMaterialParameter<Int32>(*m_pMaterial, "BaseColorTextureUVSelector"), 1);
    EXPECT_EQ(GetMaterialParameter<Uint32>(*m_pMaterial, "BaseColorTextureWrapU"), RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP);
    EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), 0.6f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, "BaseColorTextureUVRotation"), 2.f * PI_F);
}

TEST_F(RadientMaterialAnimationDestinationTest, PreservesExternalChangesToUnboundProperties)
{
    TestAnimationClipBuilder Builder;
    const Uint32             Target   = Builder.AddTarget(3, RadientMaterialAnimationSchemaID);
    const Uint32             Emission = Builder.AddSampler<RadientFloat3>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {{2.f, 3.f, 4.f}, {4.f, 5.f, 6.f}});
    Builder.AddChannel(Target, RadientStandardMaterialEmissiveFactorName, Emission);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);
    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);
    RadientAnimationEvaluateInfo Info;
    Info.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(GetMaterialParameter<RadientFloat3>(*m_pMaterial, "EmissiveFactor"), (RadientFloat3{3.f, 4.f, 5.f}));

    SetParameter("EmissiveFactor", RadientFloat3{7.f, 8.f, 9.f});
    SetParameter("RoughnessFactor", 0.75f);
    const RadientFloat4 Color = {0.25f, 0.5f, 0.75f, 1.f};
    SetParameter("BaseColorFactor", Color);
    const Uint64 ExternalVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), ExternalVersion + 1);
    EXPECT_EQ(GetMaterialParameter<RadientFloat3>(*m_pMaterial, "EmissiveFactor"), (RadientFloat3{3.f, 4.f, 5.f}));
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, "RoughnessFactor"), 0.75f);
    EXPECT_EQ(GetMaterialParameter<RadientFloat4>(*m_pMaterial, "BaseColorFactor"), Color);
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), ExternalVersion + 1);
}

TEST_F(RadientMaterialAnimationDestinationTest, SamePropertyWorksAcrossCompatibleDefinitions)
{
    for (RADIENT_SURFACE_SHADING_MODEL ShadingModel :
         {RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS, RADIENT_SURFACE_SHADING_MODEL_UNLIT})
    {
        SCOPED_TRACE(ShadingModel);
        RadientStandardMaterialDefinitionCreateInfo DefinitionCI;
        DefinitionCI.ShadingModel = ShadingModel;
        if (ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
            DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;
        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, DefinitionCI, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAnimationDestination> pDestination{pMaterial, IID_RadientAnimationDestination};
        ASSERT_NE(pDestination, nullptr);
        const RadientAnimationPropertyBindingDesc Property =
            MakeMaterialProperty(RadientStandardMaterialBaseColorFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
        RadientAnimationResolvedPropertyDesc               Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        ASSERT_EQ(pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
        void* const* pOutputs = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        const RadientFloat4 Color = {0.1f, 0.2f, 0.3f, 0.4f};
        std::memcpy(pOutputs[0], &Color, sizeof(Color));
        ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
        EXPECT_EQ(GetMaterialParameter<RadientFloat4>(*pMaterial, "BaseColorFactor"), Color);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, SamePropertyDoesNotDependOnParameterOrder)
{
    for (Uint32 ColorIndex : {0u, 1u})
    {
        SCOPED_TRACE(ColorIndex);
        std::array<RadientMaterialParameterDesc, 2> Parameters;
        Parameters[ColorIndex].Name     = "BaseColorFactor";
        Parameters[ColorIndex].Type     = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
        Parameters[1 - ColorIndex].Name = "CustomValue";
        Parameters[1 - ColorIndex].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
        RadientSurfaceMaterialDefinitionDesc Definition;
        Definition.pParameters    = Parameters.data();
        Definition.ParameterCount = static_cast<Uint32>(Parameters.size());
        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
        ASSERT_EQ(RadientMaterialAssetManager::CreateDefinition(Definition, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        ASSERT_EQ(pAssetManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RadientMaterialParameterHandle ColorHandle;
        ASSERT_EQ(pDefinition->FindParameter("BaseColorFactor", &ColorHandle), RADIENT_STATUS_OK);
        EXPECT_EQ(ColorHandle.Index, ColorIndex);

        RefCntAutoPtr<IRadientAnimationDestination> pDestination{pMaterial, IID_RadientAnimationDestination};
        ASSERT_NE(pDestination, nullptr);
        const RadientAnimationPropertyBindingDesc Property =
            MakeMaterialProperty(RadientStandardMaterialBaseColorFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
        RadientAnimationResolvedPropertyDesc               Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        ASSERT_EQ(pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        void* const* pOutputs = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        const RadientFloat4 Color = {0.1f, 0.2f, 0.3f, 0.4f};
        std::memcpy(pOutputs[0], &Color, sizeof(Color));
        ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
        EXPECT_EQ(GetParameter<RadientFloat4>(*pMaterial, ColorHandle), Color);
        EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "CustomValue"), 0.f);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, WritesEveryReflectedNumericType)
{
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_BOOL, RADIENT_ANIMATION_VALUE_TYPE_BOOL, True);
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_INT, RADIENT_ANIMATION_VALUE_TYPE_INT, Int32{-1});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_INT2, RADIENT_ANIMATION_VALUE_TYPE_INT2, std::array<Int32, 2>{{-1, 2}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_INT3, RADIENT_ANIMATION_VALUE_TYPE_INT3, std::array<Int32, 3>{{-1, 2, -3}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_INT4, RADIENT_ANIMATION_VALUE_TYPE_INT4, std::array<Int32, 4>{{-1, 2, -3, 4}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_UINT, RADIENT_ANIMATION_VALUE_TYPE_UINT, Uint32{3});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_UINT2, RADIENT_ANIMATION_VALUE_TYPE_UINT2, std::array<Uint32, 2>{{1, 2}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_UINT3, RADIENT_ANIMATION_VALUE_TYPE_UINT3, std::array<Uint32, 3>{{1, 2, 3}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_UINT4, RADIENT_ANIMATION_VALUE_TYPE_UINT4, std::array<Uint32, 4>{{1, 2, 3, 4}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 0.25f);
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2, RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, std::array<Float32, 2>{{0.1f, 0.2f}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, std::array<Float32, 3>{{0.1f, 0.2f, 0.3f}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, std::array<Float32, 4>{{0.1f, 0.2f, 0.3f, 0.4f}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2, RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2, std::array<Float32, 4>{{1, 2, 3, 4}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3, std::array<Float32, 9>{{1, 2, 3, 4, 5, 6, 7, 8, 9}});
    VerifyNumericParameter(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4, std::array<Float32, 16>{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}});
}

TEST_F(RadientMaterialAnimationDestinationTest, UsesReflectedTypeInsteadOfStandardNameContract)
{
    // A custom definition may give this name a different type than the standard definition.
    RadientMaterialParameterDesc Parameter;
    Parameter.Name = RadientStandardMaterialBaseColorFactorName;
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(
        {MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3)});
    ASSERT_NE(pBinding, nullptr);
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    const RadientFloat3 Value{0.2f, 0.4f, 0.6f};
    std::memcpy(pOutputs[0], &Value, sizeof(Value));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(GetMaterialParameter<RadientFloat3>(*m_pMaterial, Parameter.Name), Value);
}

TEST_F(RadientMaterialAnimationDestinationTest, SkipsTexturesAndCaseMismatchedNames)
{
    RadientMaterialParameterDesc Parameter;
    Parameter.Name = "User.Texture";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    const Char* Names[] = {"User.Texture", "user.texture", "Missing"};
    for (const Char* Name : Names)
    {
        SCOPED_TRACE(Name);
        const RadientAnimationPropertyBindingDesc          Property = MakeMaterialProperty(Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
        RadientAnimationResolvedPropertyDesc               Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_UNSUPPORTED);
        EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
        EXPECT_EQ(pBinding, nullptr);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, ResolvesNamesByContentAndDoesNotRetainThem)
{
    const RadientAnimationPropertyBindingDesc          WrongCase = MakeMaterialProperty("normalscale", RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&WrongCase, 1, &Resolved, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(pBinding, nullptr);
    std::string Name{RadientStandardMaterialNormalScaleName};
    pBinding = CreateDestinationBinding({MakeMaterialProperty(Name.c_str(), RADIENT_ANIMATION_VALUE_TYPE_FLOAT)});
    ASSERT_NE(pBinding, nullptr);
    Name.assign(Name.size(), 'x');
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    const Float32 Value = 0.75f;
    std::memcpy(pOutputs[0], &Value, sizeof(Value));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, RadientStandardMaterialNormalScaleName), Value);
}

TEST_F(RadientMaterialAnimationDestinationTest, KeepsCustomAlphaCutoffSeparateFromSurfaceState)
{
    RadientMaterialParameterDesc Parameter;
    Parameter.Name = "AlphaCutoff";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding({
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    });
    ASSERT_NE(pBinding, nullptr);
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    const Float32 ParameterValue = 0.25f;
    const Float32 SurfaceValue   = 0.75f;
    std::memcpy(pOutputs[0], &ParameterValue, sizeof(ParameterValue));
    std::memcpy(pOutputs[1], &SurfaceValue, sizeof(SurfaceValue));
    const Uint64 BeforeVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), BeforeVersion + 1);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, Parameter.Name), ParameterValue);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{m_pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);
    EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), SurfaceValue);
}

TEST_F(RadientMaterialAnimationDestinationTest, PartialArrayChannelsPreserveExternalChangesToUnboundElements)
{
    RadientMaterialParameterDesc Parameter;
    Parameter.Name      = "User.Weights";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.ArraySize = 6;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    SetParameter(Parameter.Name, std::array<Float32, 6>{{1, 2, 3, 4, 5, 6}});

    TestAnimationClipBuilder Builder;
    const Uint32             Target = Builder.AddTarget(0, RadientMaterialAnimationSchemaID);
    const Uint32             Tail   = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {40.f, 50.f, 60.f, 70.f}, 2);
    const Uint32 Middle = Builder.AddSampler<Float32>(
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RADIENT_ANIMATION_INTERPOLATION_LINEAR,
        {0.f, 1.f}, {10.f, 20.f, 30.f, 40.f}, 2);
    // Disjoint ranges intentionally arrive in reverse array order.
    Builder.AddChannel(Target, Parameter.Name, Tail, 4);
    Builder.AddChannel(Target, Parameter.Name, Middle, 1);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = Builder.Create(*pAssetManager);
    ASSERT_NE(pClip, nullptr);
    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex                          = Target;
    Mapping.DestinationElement                       = 0;
    RefCntAutoPtr<IRadientAnimationBinding> pBinding = BindSingle(pClip, m_pDestination, {Mapping});
    ASSERT_NE(pBinding, nullptr);
    RadientAnimationEvaluateInfo Info;
    Info.Time                  = 0.5f;
    const Uint64 BeforeVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), BeforeVersion + 1);
    EXPECT_EQ((GetMaterialParameter<std::array<Float32, 6>>(*m_pMaterial, Parameter.Name)),
              (std::array<Float32, 6>{{1, 20, 30, 4, 50, 60}}));

    SetParameter(Parameter.Name, std::array<Float32, 6>{{11, 12, 13, 14, 15, 16}});
    const Uint64 ExternalVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), ExternalVersion + 1);
    EXPECT_EQ((GetMaterialParameter<std::array<Float32, 6>>(*m_pMaterial, Parameter.Name)),
              (std::array<Float32, 6>{{11, 20, 30, 14, 50, 60}}));
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), ExternalVersion + 1);
}

TEST_F(RadientMaterialAnimationDestinationTest, DisjointRangesCanCoverAnEntireArray)
{
    RadientMaterialParameterDesc Parameter;
    Parameter.Name      = "User.Vectors";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3;
    Parameter.ArraySize = 3;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding({
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 2, 1),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 2),
    });
    ASSERT_NE(pBinding, nullptr);
    const std::array<RadientFloat3, 3> Values   = {{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}};
    void* const*                       pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    std::memcpy(pOutputs[0], &Values[2], sizeof(Values[2]));
    std::memcpy(pOutputs[1], Values.data(), 2 * sizeof(Values[0]));
    const Uint64 BeforeVersion = m_pMaterial->GetVersion();
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pMaterial->GetVersion(), BeforeVersion + 1);
    EXPECT_EQ((GetMaterialParameter<std::array<RadientFloat3, 3>>(*m_pMaterial, Parameter.Name)), Values);
}

TEST_F(RadientMaterialAnimationDestinationTest, ReusesStagingAndPublishesOnlyChangedRanges)
{
    const std::array<RadientMaterialParameterDesc, 2> Parameters = {{
        {"User.Weights", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, 6},
        {"User.Unbound", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, 1},
    }};
    CreateCustomMaterial(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(m_pDestination, nullptr);
    SetParameter("User.Weights", std::array<Float32, 6>{{1, 2, 3, 4, 5, 6}});
    SetParameter("User.Unbound", 13.f);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{m_pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding({
        MakeMaterialProperty("User.Weights", RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 4, 2),
        MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeMaterialProperty("User.Weights", RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1, 2),
    });
    ASSERT_NE(pBinding, nullptr);
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(m_pMaterial);
    ASSERT_NE(pStorage, nullptr);
    const RadientMaterialDetail::MaterialChangeVersions InitialVersions = pStorage->GetChangeVersions();
    const Uint64                                        InitialRevision = pStorage->GetIdentity().pChangeTracker->GetRevision();
    Uint64                                              UnboundVersion;
    {
        const RadientMaterialDetail::MaterialStorage::ReadAccess Access = pStorage->AcquireReadAccess();
        UnboundVersion                                                  = Access.GetPackedData().GetValueVersion(1);
    }

    struct Sample
    {
        std::array<Float32, 4> Weights;
        Float32                AlphaCutoff;
        Uint64                 Publication;
        Uint64                 WeightsPublication;
        Uint64                 SurfacePublication;
    };
    // Repeating a sample publishes nothing. Changing one kind of property must
    // leave the other kind's shader-data version untouched.
    const Sample Samples[] = {
        {{{20, 30, 50, 60}}, 0.25f, 1, 1, 1},
        {{{20, 30, 50, 60}}, 0.25f, 1, 1, 1},
        {{{21, 31, 50, 60}}, 0.25f, 2, 2, 1},
        {{{21, 31, 50, 60}}, 0.75f, 3, 2, 3},
        {{{22, 32, 52, 62}}, 0.50f, 4, 4, 4},
        {{{22, 32, 52, 62}}, 0.50f, 4, 4, 4},
    };
    std::array<std::uintptr_t, 3> OutputAddresses{};
    std::uintptr_t                OutputArrayAddress = 0;
    for (const Sample& Current : Samples)
    {
        SCOPED_TRACE(Current.Publication);
        const std::array<Float32, 6> BeforeWeights =
            GetMaterialParameter<std::array<Float32, 6>>(*m_pMaterial, "User.Weights");
        const Float32 BeforeAlpha   = pSurface->GetAlphaCutoff();
        const Uint64  BeforeVersion = m_pMaterial->GetVersion();
        void* const*  pOutputs      = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        if (OutputArrayAddress == 0)
        {
            OutputArrayAddress = reinterpret_cast<std::uintptr_t>(pOutputs);
            for (size_t Index = 0; Index < OutputAddresses.size(); ++Index)
            {
                ASSERT_NE(pOutputs[Index], nullptr);
                OutputAddresses[Index] = reinterpret_cast<std::uintptr_t>(pOutputs[Index]);
            }
        }
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pOutputs), OutputArrayAddress);
        for (size_t Index = 0; Index < OutputAddresses.size(); ++Index)
        {
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pOutputs[Index]), OutputAddresses[Index]);
        }
        std::memcpy(pOutputs[0], Current.Weights.data() + 2, 2 * sizeof(Float32));
        std::memcpy(pOutputs[1], &Current.AlphaCutoff, sizeof(Current.AlphaCutoff));
        std::memcpy(pOutputs[2], Current.Weights.data(), 2 * sizeof(Float32));

        // Sampling into staging must not expose a partially updated material.
        EXPECT_EQ((GetMaterialParameter<std::array<Float32, 6>>(*m_pMaterial, "User.Weights")), BeforeWeights);
        EXPECT_EQ(pSurface->GetAlphaCutoff(), BeforeAlpha);
        EXPECT_EQ(m_pMaterial->GetVersion(), BeforeVersion);
        ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);
        EXPECT_EQ((GetMaterialParameter<std::array<Float32, 6>>(*m_pMaterial, "User.Weights")),
                  (std::array<Float32, 6>{{1, Current.Weights[0], Current.Weights[1], 4, Current.Weights[2], Current.Weights[3]}}));
        EXPECT_EQ(pSurface->GetAlphaCutoff(), Current.AlphaCutoff);
        EXPECT_EQ(GetMaterialParameter<Float32>(*m_pMaterial, "User.Unbound"), 13.f);
        EXPECT_EQ(m_pMaterial->GetVersion(), InitialVersions.Version + Current.Publication);
        EXPECT_EQ(pStorage->GetIdentity().pChangeTracker->GetRevision(), InitialRevision + Current.Publication);
        const RadientMaterialDetail::MaterialChangeVersions& Versions = pStorage->GetChangeVersions();
        EXPECT_EQ(Versions.ShaderDataVersion, InitialVersions.ShaderDataVersion + Current.Publication);
        EXPECT_EQ(Versions.RenderStateVersion, InitialVersions.RenderStateVersion);
        EXPECT_EQ(Versions.TextureBindingsVersion, InitialVersions.TextureBindingsVersion);
        const RadientMaterialDetail::MaterialStorage::ReadAccess Access = pStorage->AcquireReadAccess();
        EXPECT_EQ(Access.GetPackedData().GetValueVersion(0), InitialVersions.ShaderDataVersion + Current.WeightsPublication);
        EXPECT_EQ(Access.GetPackedData().GetValueVersion(1), UnboundVersion);
        EXPECT_EQ(Access.GetSurfaceShaderDataVersion(), InitialVersions.ShaderDataVersion + Current.SurfacePublication);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, RejectsOverlappingAndOutOfBoundsArrayRanges)
{
    RadientMaterialParameterDesc Parameter;
    Parameter.Name      = "User.Weights";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.ArraySize = 6;
    CreateCustomMaterial(&Parameter, 1);
    ASSERT_NE(m_pDestination, nullptr);
    const RadientAnimationPropertyBindingDesc First   = MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1, 3);
    const RadientAnimationPropertyBindingDesc Cases[] = {
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 0, 2),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 2, 1),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 3, 2),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 0, 6),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 5, 2),
        MakeMaterialProperty(Parameter.Name, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, std::numeric_limits<Uint32>::max(), 2),
    };
    for (const RadientAnimationPropertyBindingDesc& Property : Cases)
    {
        SCOPED_TRACE(Property.FirstArrayElement);
        SCOPED_TRACE(Property.Value.ArraySize);
        const RadientAnimationPropertyBindingDesc           Properties[] = {First, Property};
        std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(Properties, 2, Resolved.data(), pBinding.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pBinding, nullptr);
    }
}


TEST_F(RadientMaterialAnimationDestinationTest, BindingRetainsMaterialAndDefinition)
{
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(
        {MakeMaterialProperty(RadientStandardMaterialNormalScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT)});
    ASSERT_NE(pBinding, nullptr);
    IRadientMaterialAsset* const pRawMaterial = m_pMaterial;
    m_pDestination.Release();
    m_pMaterial.Release();
    m_pDefinition.Release();
    pAssetManager.Release();
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    const Float32 Value = 0.625f;
    std::memcpy(pOutputs[0], &Value, sizeof(Value));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pRawMaterial, "NormalScale"), Value);
}

TEST_F(RadientMaterialAnimationDestinationTest, SkipsUnsupportedPropertiesInOutputOrder)
{
    RadientAnimationPropertyBindingDesc Foreign                       = MakeMaterialProperty(RadientStandardMaterialNormalScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    Foreign.Schema                                                    = RadientNodeAnimationSchemaID;
    const std::vector<RadientAnimationPropertyBindingDesc> Properties = {
        MakeMaterialProperty(RadientStandardMaterialBaseColorFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeMaterialProperty(RadientStandardMaterialClearCoatFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        Foreign,
        MakeMaterialProperty("UnknownParameter", RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved(Properties.size());
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(Properties.data(), static_cast<Uint32>(Properties.size()),
                                            Resolved.data(), pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[4].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);
    const RadientFloat4 Color  = {0.1f, 0.2f, 0.3f, 0.4f};
    const Float32       Cutoff = 0.25f;
    std::memcpy(pOutputs[0], &Color, sizeof(Color));
    std::memcpy(pOutputs[1], &Cutoff, sizeof(Cutoff));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(GetMaterialParameter<RadientFloat4>(*m_pMaterial, "BaseColorFactor"), Color);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{m_pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);
    EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), Cutoff);
}

TEST_F(RadientMaterialAnimationDestinationTest, RejectsUnsupportedAndInvalidPropertyRequests)
{
    struct RejectionCase
    {
        RadientAnimationPropertyBindingDesc Property;
        RADIENT_STATUS                      ExpectedStatus;
    };
    const RadientAnimationPropertyBindingDesc Valid = MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    std::vector<RejectionCase>                Cases;
    Cases.push_back({MakeMaterialProperty("UnknownParameter", RADIENT_ANIMATION_VALUE_TYPE_FLOAT), RADIENT_STATUS_UNSUPPORTED});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialClearCoatFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT), RADIENT_STATUS_UNSUPPORTED});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialDiffuseFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4), RADIENT_STATUS_UNSUPPORTED});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 2), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1, std::numeric_limits<Uint32>::max()), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 0), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeMaterialProperty(RadientStandardMaterialEmissiveFactorName, RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN), RADIENT_STATUS_INVALID_ARGUMENT});
    RadientAnimationPropertyBindingDesc Invalid = Valid;
    Invalid.Schema                              = RadientNodeAnimationSchemaID;
    Cases.push_back({Invalid, RADIENT_STATUS_UNSUPPORTED});
    Invalid.Schema = InvalidRadientAnimationSchemaID;
    Cases.push_back({Invalid, RADIENT_STATUS_INVALID_ARGUMENT});
    Invalid          = Valid;
    Invalid.Property = nullptr;
    Cases.push_back({Invalid, RADIENT_STATUS_INVALID_ARGUMENT});
    Invalid.Property = "";
    Cases.push_back({Invalid, RADIENT_STATUS_INVALID_ARGUMENT});
    Invalid                    = Valid;
    Invalid.DestinationElement = 1;
    Cases.push_back({Invalid, RADIENT_STATUS_NOT_FOUND});
    Invalid.DestinationElement = InvalidRadientAnimationDestinationElement;
    Cases.push_back({Invalid, RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT4), RADIENT_STATUS_INVALID_ARGUMENT});
    Cases.push_back({MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1), RADIENT_STATUS_INVALID_ARGUMENT});
    for (size_t Index = 0; Index < Cases.size(); ++Index)
    {
        SCOPED_TRACE(Index);
        RadientAnimationResolvedPropertyDesc Resolved;
        Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(&Cases[Index].Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  Cases[Index].ExpectedStatus);
        EXPECT_EQ(pBinding, nullptr);
        if (Cases[Index].ExpectedStatus == RADIENT_STATUS_UNSUPPORTED)
            EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, RejectsDuplicatePropertyWrites)
{
    const RadientAnimationPropertyBindingDesc Cases[] = {
        MakeMaterialProperty(RadientStandardMaterialNormalScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeSurfaceProperty(RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    for (const RadientAnimationPropertyBindingDesc& Property : Cases)
    {
        SCOPED_TRACE(Property.Property);
        // Equal names in different allocations still identify the same property.
        const std::string                                  DuplicateName{Property.Property};
        std::array<RadientAnimationPropertyBindingDesc, 3> Properties = {{
            Property,
            Property,
            MakeMaterialProperty("UnknownParameter", RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        }};
        Properties[1].Property                                        = DuplicateName.c_str();
        std::array<RadientAnimationResolvedPropertyDesc, 3> Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(Properties.data(), static_cast<Uint32>(Properties.size()),
                                                Resolved.data(), pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pBinding, nullptr);
    }
}

TEST_F(RadientMaterialAnimationDestinationTest, RejectsInvalidBindingArguments)
{
    const RadientAnimationPropertyBindingDesc          Property = MakeMaterialProperty(RadientStandardMaterialNormalScaleName, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 0, nullptr, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 1, &Resolved, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, nullptr, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    pBinding = CreateDestinationBinding({Property});
    ASSERT_NE(pBinding, nullptr);
    IRadientAnimationDestinationBinding* pOutput = pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, &pOutput), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pOutput, pBinding.RawPtr());
    EXPECT_EQ(pBinding->BeginUpdate(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

} // namespace
