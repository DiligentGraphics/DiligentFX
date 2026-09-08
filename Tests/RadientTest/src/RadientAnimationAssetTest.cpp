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

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr RadientAnimationSchemaID TestTargetSchemaID =
    {0x5f50a0d8, 0x7984, 0x42f5, {0xb0, 0x2d, 0x76, 0x8f, 0x91, 0x53, 0xd7, 0xa4}};
static constexpr RadientAnimationPropertyID TestProperty = 17;

struct AnimationValueTypeCase
{
    RADIENT_ANIMATION_VALUE_TYPE Type;
    size_t                       NativeSize;
    bool                         IsDiscrete;
    const char*                  Name;
};

static constexpr std::array<AnimationValueTypeCase, RADIENT_ANIMATION_VALUE_TYPE_COUNT - 1> AnimationValueTypeCases =
    {{
        {RADIENT_ANIMATION_VALUE_TYPE_BOOL, sizeof(Uint8), true, "Bool"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT, sizeof(Int32), true, "Int"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT2, sizeof(Int32[2]), true, "Int2"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT3, sizeof(Int32[3]), true, "Int3"},
        {RADIENT_ANIMATION_VALUE_TYPE_INT4, sizeof(Int32[4]), true, "Int4"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT, sizeof(Uint32), true, "Uint"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT2, sizeof(Uint32[2]), true, "Uint2"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT3, sizeof(Uint32[3]), true, "Uint3"},
        {RADIENT_ANIMATION_VALUE_TYPE_UINT4, sizeof(Uint32[4]), true, "Uint4"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT, sizeof(Float32), false, "Float"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, sizeof(Float32[2]), false, "Float2"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, sizeof(Float32[3]), false, "Float3"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, sizeof(Float32[4]), false, "Float4"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2, sizeof(Float32[2][2]), false, "Float2x2"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3, sizeof(Float32[3][3]), false, "Float3x3"},
        {RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4, sizeof(Float32[4][4]), false, "Float4x4"},
    }};

std::string GetAnimationValueTypeCaseName(const testing::TestParamInfo<AnimationValueTypeCase>& Info)
{
    return Info.param.Name;
}

std::vector<AnimationValueTypeCase> GetDiscreteAnimationValueTypeCases()
{
    std::vector<AnimationValueTypeCase> Cases;
    for (const AnimationValueTypeCase& Case : AnimationValueTypeCases)
    {
        if (Case.IsDiscrete)
            Cases.push_back(Case);
    }
    return Cases;
}

RefCntAutoPtr<RadientAssetManagerImpl> CreateAssetManager()
{
    return RadientAssetManagerImpl::Create({});
}

class RadientAnimationAssetValidationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pAssetManager = CreateAssetManager();
        ASSERT_NE(pAssetManager, nullptr);

        Target.Schema = TestTargetSchemaID;
        Target.Object = 42;
        Target.Name   = TargetName.data();

        Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
        Sampler.Value.ArraySize = 2;
        Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
        Sampler.pTimes          = Times.data();
        Sampler.pValues         = Values.data();
        Sampler.ValueDataSize   = sizeof(Values);
        Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

        Channel.TargetIndex       = 0;
        Channel.Property          = TestProperty;
        Channel.FirstArrayElement = 0;
        Channel.SamplerIndex      = 0;

        Desc.Name         = ClipName.data();
        Desc.Duration     = 1.f;
        Desc.pTargets     = &Target;
        Desc.TargetCount  = 1;
        Desc.pSamplers    = &Sampler;
        Desc.SamplerCount = 1;
        Desc.pChannels    = &Channel;
        Desc.ChannelCount = 1;
    }

    RefCntAutoPtr<IRadientAnimationClipAsset> CreateClip(const RadientAnimationClipDesc& ClipDesc)
    {
        RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
        EXPECT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pClip;
    }

    void ExpectInvalid(const RadientAnimationClipDesc& ClipDesc,
                       const char*                     ExpectedError)
    {
        RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
        TestingEnvironment::ErrorScope            ExpectedErrors{ExpectedError};
        EXPECT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_FALSE(pClip);
    }

    void ExpectInvalidSampler(const RadientAnimationSamplerDesc& InvalidSampler,
                              const char*                        ExpectedError)
    {
        RadientAnimationClipDesc InvalidDesc = Desc;
        InvalidDesc.pSamplers                = &InvalidSampler;
        ExpectInvalid(InvalidDesc, ExpectedError);
    }

    void ExpectInvalidTarget(const RadientAnimationTargetDesc& InvalidTarget,
                             const char*                       ExpectedError)
    {
        RadientAnimationClipDesc InvalidDesc = Desc;
        InvalidDesc.pTargets                 = &InvalidTarget;
        ExpectInvalid(InvalidDesc, ExpectedError);
    }

    void ExpectInvalidChannel(const RadientAnimationChannelDesc& InvalidChannel,
                              const char*                        ExpectedError)
    {
        RadientAnimationClipDesc InvalidDesc = Desc;
        InvalidDesc.pChannels                = &InvalidChannel;
        ExpectInvalid(InvalidDesc, ExpectedError);
    }

protected:
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager;
    std::array<Char, 5>                    ClipName   = {'C', 'l', 'i', 'p', '\0'};
    std::array<Char, 7>                    TargetName = {'T', 'a', 'r', 'g', 'e', 't', '\0'};
    std::array<Float32, 2>                 Times      = {0.f, 1.f};
    std::array<Float32, 4>                 Values     = {1.f, 2.f, 3.f, 4.f};
    RadientAnimationTargetDesc             Target;
    RadientAnimationSamplerDesc            Sampler;
    RadientAnimationChannelDesc            Channel;
    RadientAnimationClipDesc               Desc;
};

class RadientAnimationNativeValueTypeValidationTest :
    public RadientAnimationAssetValidationTest,
    public testing::WithParamInterface<AnimationValueTypeCase>
{};

class RadientAnimationDiscreteValueTypeValidationTest :
    public RadientAnimationAssetValidationTest,
    public testing::WithParamInterface<AnimationValueTypeCase>
{};

TEST(RadientAnimationAssetTest, CreatesEmptyClip)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    const RadientAnimationClipDesc            EmptyDesc{};
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(EmptyDesc, pClip.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pClip, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pClip), RADIENT_STATUS_OK);
    EXPECT_EQ(pClip->GetType(), RADIENT_ASSET_TYPE_ANIMATION_CLIP);

    const RadientAnimationClipDesc& StoredDesc = pClip->GetDesc();
    ASSERT_NE(StoredDesc.Name, nullptr);
    EXPECT_STREQ(StoredDesc.Name, "");
    EXPECT_FLOAT_EQ(StoredDesc.Duration, 0.f);
    EXPECT_EQ(StoredDesc.pTargets, nullptr);
    EXPECT_EQ(StoredDesc.TargetCount, 0u);
    EXPECT_EQ(StoredDesc.pSamplers, nullptr);
    EXPECT_EQ(StoredDesc.SamplerCount, 0u);
    EXPECT_EQ(StoredDesc.pChannels, nullptr);
    EXPECT_EQ(StoredDesc.ChannelCount, 0u);
}

TEST_F(RadientAnimationAssetValidationTest, CopiesDescriptionAndReferencedData)
{
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = CreateClip(Desc);
    ASSERT_NE(pClip, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pClip), RADIENT_STATUS_OK);

    ClipName[0]       = 'X';
    TargetName[0]     = 'X';
    Target.Schema     = InvalidRadientAnimationSchemaID;
    Target.Object     = InvalidRadientAnimationObject;
    Times[1]          = 0.f;
    Values[0]         = 99.f;
    Sampler.Value     = {};
    Channel.Property  = InvalidRadientAnimationPropertyID;
    Desc.Duration     = 99.f;
    Desc.TargetCount  = 0;
    Desc.SamplerCount = 0;
    Desc.ChannelCount = 0;

    const RadientAnimationClipDesc& StoredDesc = pClip->GetDesc();
    EXPECT_EQ(pClip->GetType(), RADIENT_ASSET_TYPE_ANIMATION_CLIP);
    EXPECT_STREQ(StoredDesc.Name, "Clip");
    EXPECT_FLOAT_EQ(StoredDesc.Duration, 1.f);

    ASSERT_EQ(StoredDesc.TargetCount, 1u);
    ASSERT_NE(StoredDesc.pTargets, nullptr);
    EXPECT_NE(StoredDesc.pTargets, &Target);
    EXPECT_TRUE(StoredDesc.pTargets[0].Schema == TestTargetSchemaID);
    EXPECT_EQ(StoredDesc.pTargets[0].Object, 42u);
    EXPECT_STREQ(StoredDesc.pTargets[0].Name, "Target");
    EXPECT_NE(StoredDesc.pTargets[0].Name, TargetName.data());

    ASSERT_EQ(StoredDesc.SamplerCount, 1u);
    ASSERT_NE(StoredDesc.pSamplers, nullptr);
    EXPECT_NE(StoredDesc.pSamplers, &Sampler);
    const RadientAnimationSamplerDesc& StoredSampler = StoredDesc.pSamplers[0];
    EXPECT_EQ(StoredSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    EXPECT_EQ(StoredSampler.Value.ArraySize, 2u);
    EXPECT_EQ(StoredSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_LINEAR);
    EXPECT_EQ(StoredSampler.KeyframeCount, 2u);
    EXPECT_EQ(StoredSampler.ValueDataSize, sizeof(Values));
    ASSERT_NE(StoredSampler.pTimes, nullptr);
    EXPECT_NE(StoredSampler.pTimes, Times.data());
    EXPECT_FLOAT_EQ(StoredSampler.pTimes[1], 1.f);
    ASSERT_NE(StoredSampler.pValues, nullptr);
    EXPECT_NE(StoredSampler.pValues, Values.data());
    const auto* const pStoredValues = static_cast<const Float32*>(StoredSampler.pValues);
    EXPECT_FLOAT_EQ(pStoredValues[0], 1.f);
    EXPECT_FLOAT_EQ(pStoredValues[3], 4.f);

    ASSERT_EQ(StoredDesc.ChannelCount, 1u);
    ASSERT_NE(StoredDesc.pChannels, nullptr);
    EXPECT_NE(StoredDesc.pChannels, &Channel);
    EXPECT_EQ(StoredDesc.pChannels[0].TargetIndex, 0u);
    EXPECT_EQ(StoredDesc.pChannels[0].Property, TestProperty);
    EXPECT_EQ(StoredDesc.pChannels[0].FirstArrayElement, 0u);
    EXPECT_EQ(StoredDesc.pChannels[0].SamplerIndex, 0u);
}

TEST_F(RadientAnimationAssetValidationTest, ExposesStableIdentityInterfacesAndAlignedStorage)
{
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = CreateClip(Desc);
    ASSERT_NE(pClip, nullptr);

    const RadientAssetReference& Reference = pClip->GetReference();
    ASSERT_NE(Reference.URI, nullptr);
    const std::string URI{Reference.URI};
    EXPECT_EQ(URI.find("radient://session/animation-clip/"), 0u);
    EXPECT_GT(URI.size(), std::string{"radient://session/animation-clip/"}.size());
    EXPECT_EQ(Reference.Version, 1u);

    RefCntAutoPtr<IRadientAnimationClipAsset> pClipInterface{pClip, IID_RadientAnimationClipAsset};
    EXPECT_EQ(pClipInterface.RawPtr(), pClip.RawPtr());
    RefCntAutoPtr<IRadientAsset> pAssetInterface{pClip, IID_RadientAsset};
    EXPECT_EQ(pAssetInterface.RawPtr(), static_cast<IRadientAsset*>(pClip.RawPtr()));

    const RadientAnimationClipDesc* const pStoredDesc = &pClip->GetDesc();
    EXPECT_EQ(&pClip->GetDesc(), pStoredDesc);
    ASSERT_NE(pStoredDesc->pSamplers, nullptr);
    ASSERT_NE(pStoredDesc->pSamplers[0].pTimes, nullptr);
    ASSERT_NE(pStoredDesc->pSamplers[0].pValues, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pStoredDesc->pSamplers[0].pTimes) % alignof(Float32), 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pStoredDesc->pSamplers[0].pValues) % alignof(Float32), 0u);
}

TEST_F(RadientAnimationAssetValidationTest, StoresNullNamesAsEmptyStrings)
{
    Desc.Name   = nullptr;
    Target.Name = nullptr;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip = CreateClip(Desc);
    ASSERT_NE(pClip, nullptr);
    const RadientAnimationClipDesc& StoredDesc = pClip->GetDesc();
    ASSERT_NE(StoredDesc.Name, nullptr);
    EXPECT_STREQ(StoredDesc.Name, "");
    ASSERT_NE(StoredDesc.pTargets[0].Name, nullptr);
    EXPECT_STREQ(StoredDesc.pTargets[0].Name, "");
}

TEST_F(RadientAnimationAssetValidationTest, AcceptsOneKeyCubicBooleanAndMatrixSamplers)
{
    const std::array<Float32, 1> CubicTimes  = {0.5f};
    const std::array<Float32, 6> CubicValues = {10.f, 20.f, 1.f, 2.f, 30.f, 40.f};
    Sampler.Interpolation                    = RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
    Sampler.pTimes                           = CubicTimes.data();
    Sampler.pValues                          = CubicValues.data();
    Sampler.ValueDataSize                    = sizeof(CubicValues);
    Sampler.KeyframeCount                    = 1;
    EXPECT_NE(CreateClip(Desc), nullptr);

    const std::array<Uint8, 4> BoolValues = {0, 1, 1, 0};
    Sampler.Value.Type                    = RADIENT_ANIMATION_VALUE_TYPE_BOOL;
    Sampler.Interpolation                 = RADIENT_ANIMATION_INTERPOLATION_STEP;
    Sampler.pTimes                        = Times.data();
    Sampler.pValues                       = BoolValues.data();
    Sampler.ValueDataSize                 = sizeof(BoolValues);
    Sampler.KeyframeCount                 = static_cast<Uint32>(Times.size());
    EXPECT_NE(CreateClip(Desc), nullptr);

    const std::array<Float32, 8> MatrixValues = {
        1.f, 0.f, 0.f, 1.f,
        2.f, 0.f, 0.f, 2.f};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2;
    Sampler.Value.ArraySize = 1;
    Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Sampler.pValues         = MatrixValues.data();
    Sampler.ValueDataSize   = sizeof(MatrixValues);
    EXPECT_NE(CreateClip(Desc), nullptr);
}

TEST_F(RadientAnimationAssetValidationTest, SupportsEveryNativeValueTypeLayoutAndInterpolationCategory)
{
    alignas(Float32) const std::array<Uint8, sizeof(Float32[4][4])> ZeroValues = {};

    Sampler.Value.ArraySize = 1;
    Sampler.pValues         = ZeroValues.data();
    Sampler.KeyframeCount   = 1;

    for (const AnimationValueTypeCase& Case : AnimationValueTypeCases)
    {
        SCOPED_TRACE(static_cast<Uint32>(Case.Type));

        Sampler.Value.Type    = Case.Type;
        Sampler.Interpolation = Case.IsDiscrete ? RADIENT_ANIMATION_INTERPOLATION_STEP :
                                                  RADIENT_ANIMATION_INTERPOLATION_LINEAR;
        Sampler.ValueDataSize = Case.NativeSize;
        EXPECT_NE(CreateClip(Desc), nullptr);
    }
}

TEST_P(RadientAnimationNativeValueTypeValidationTest, RejectsUndersizedSamplerValueData)
{
    const AnimationValueTypeCase&                                   Case       = GetParam();
    alignas(Float32) const std::array<Uint8, sizeof(Float32[4][4])> ZeroValues = {};

    Sampler.Value.Type      = Case.Type;
    Sampler.Value.ArraySize = 1;
    Sampler.Interpolation   = Case.IsDiscrete ?
        RADIENT_ANIMATION_INTERPOLATION_STEP :
        RADIENT_ANIMATION_INTERPOLATION_LINEAR;

    Sampler.pValues       = ZeroValues.data();
    Sampler.ValueDataSize = Case.NativeSize - 1;
    Sampler.KeyframeCount = 1;
    ExpectInvalidSampler(Sampler, "bytes are required");
}

INSTANTIATE_TEST_SUITE_P(
    NativeValueTypes,
    RadientAnimationNativeValueTypeValidationTest,
    testing::ValuesIn(AnimationValueTypeCases),
    GetAnimationValueTypeCaseName);

TEST_P(RadientAnimationDiscreteValueTypeValidationTest, RejectsNonStepInterpolation)
{
    const AnimationValueTypeCase&                              Case       = GetParam();
    alignas(Float32) const std::array<Uint8, sizeof(Int32[4])> ZeroValues = {};

    Sampler.Value.Type      = Case.Type;
    Sampler.Value.ArraySize = 1;
    Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Sampler.pValues         = ZeroValues.data();
    Sampler.ValueDataSize   = Case.NativeSize;
    Sampler.KeyframeCount   = 1;
    ExpectInvalidSampler(Sampler, "must use STEP interpolation");
}

INSTANTIATE_TEST_SUITE_P(
    DiscreteValueTypes,
    RadientAnimationDiscreteValueTypeValidationTest,
    testing::ValuesIn(GetDiscreteAnimationValueTypeCases()),
    GetAnimationValueTypeCaseName);

TEST_F(RadientAnimationAssetValidationTest, RejectsNullOutput)
{
    EXPECT_EQ(pAssetManager->CreateAnimationClip(Desc, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST_F(RadientAnimationAssetValidationTest, RejectsCreationAfterManagerStops)
{
    ASSERT_EQ(pAssetManager->Stop(nullptr), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    EXPECT_EQ(pAssetManager->CreateAnimationClip(Desc, pClip.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_FALSE(pClip);
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNullTargetTableWithNonzeroCount)
{
    Desc.pTargets = nullptr;
    ExpectInvalid(Desc, "target count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNonNullTargetTableWithZeroCount)
{
    Desc.TargetCount = 0;
    ExpectInvalid(Desc, "target count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNullSamplerTableWithNonzeroCount)
{
    Desc.pSamplers = nullptr;
    ExpectInvalid(Desc, "sampler count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNonNullSamplerTableWithZeroCount)
{
    Desc.SamplerCount = 0;
    ExpectInvalid(Desc, "sampler count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNullChannelTableWithNonzeroCount)
{
    Desc.pChannels = nullptr;
    ExpectInvalid(Desc, "channel count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNonNullChannelTableWithZeroCount)
{
    Desc.ChannelCount = 0;
    ExpectInvalid(Desc, "channel count and pointer");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsPartiallyEmptyClip)
{
    RadientAnimationClipDesc InvalidDesc = Desc;
    InvalidDesc.pChannels                = nullptr;
    InvalidDesc.ChannelCount             = 0;
    ExpectInvalid(InvalidDesc, "must contain targets, samplers, and channels");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNegativeDuration)
{
    Desc.Duration = -1.f;
    ExpectInvalid(Desc, "duration must be finite and non-negative");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInfiniteDuration)
{
    Desc.Duration = std::numeric_limits<Float32>::infinity();
    ExpectInvalid(Desc, "duration must be finite and non-negative");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNaNDuration)
{
    Desc.Duration = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalid(Desc, "duration must be finite and non-negative");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidTargetSchema)
{
    RadientAnimationTargetDesc InvalidTarget = Target;
    InvalidTarget.Schema                     = InvalidRadientAnimationSchemaID;
    ExpectInvalidTarget(InvalidTarget, "invalid schema identifier");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidTargetObject)
{
    RadientAnimationTargetDesc InvalidTarget = Target;
    InvalidTarget.Object                     = InvalidRadientAnimationObject;
    ExpectInvalidTarget(InvalidTarget, "invalid source object identifier");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsDuplicateTargetIdentity)
{
    std::array<RadientAnimationTargetDesc, 2>  DuplicateTargets = {Target, Target};
    std::array<RadientAnimationChannelDesc, 2> Channels         = {Channel, Channel};
    Channels[1].TargetIndex                                     = 1;
    RadientAnimationClipDesc InvalidDesc                        = Desc;
    InvalidDesc.pTargets                                        = DuplicateTargets.data();
    InvalidDesc.TargetCount                                     = static_cast<Uint32>(DuplicateTargets.size());
    InvalidDesc.pChannels                                       = Channels.data();
    InvalidDesc.ChannelCount                                    = static_cast<Uint32>(Channels.size());
    ExpectInvalid(InvalidDesc, "same schema and source object identifier");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsUnknownSamplerValueType)
{
    Sampler.Value.Type = RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN;
    ExpectInvalidSampler(Sampler, "invalid value type");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsOutOfRangeSamplerValueType)
{
    Sampler.Value.Type = RADIENT_ANIMATION_VALUE_TYPE_COUNT;
    ExpectInvalidSampler(Sampler, "invalid value type");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsEmptySamplerValueArray)
{
    Sampler.Value.ArraySize = 0;
    ExpectInvalidSampler(Sampler, "empty value array");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsOutOfRangeSamplerInterpolation)
{
    Sampler.Interpolation = RADIENT_ANIMATION_INTERPOLATION_COUNT;
    ExpectInvalidSampler(Sampler, "invalid interpolation mode");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsSamplerWithoutKeyframes)
{
    Sampler.KeyframeCount = 0;
    ExpectInvalidSampler(Sampler, "does not contain keyframes");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNullSamplerKeyframeTimes)
{
    Sampler.pTimes = nullptr;
    ExpectInvalidSampler(Sampler, "keyframe times must not be null");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNullSamplerValues)
{
    Sampler.pValues = nullptr;
    ExpectInvalidSampler(Sampler, "values must not be null");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsOversizedSamplerValueData)
{
    Sampler.ValueDataSize = sizeof(Values) + 1;
    ExpectInvalidSampler(Sampler, "bytes are required");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsSamplerValueDataSizeOverflow)
{
    // Keep the keyframe-time data addressable on Win32 while making the
    // value-data size exactly one byte past the Uint64 addressable range.
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4;
    Sampler.Value.ArraySize = Uint32{1} << 31;
    Sampler.KeyframeCount   = Uint32{1} << 27;
    Sampler.ValueDataSize   = 0;
    ExpectInvalidSampler(Sampler, "value-data size overflows");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNegativeKeyframeTime)
{
    Times[0] = -1.f;
    ExpectInvalidSampler(Sampler, "time outside the clip duration");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNaNKeyframeTime)
{
    Times[1] = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalidSampler(Sampler, "time outside the clip duration");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsKeyframeAfterClipDuration)
{
    Times[1] = 2.f;
    ExpectInvalidSampler(Sampler, "time outside the clip duration");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsDuplicateKeyframeTimes)
{
    Times = {0.5f, 0.5f};
    ExpectInvalidSampler(Sampler, "strictly increasing");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsNonFiniteFloatingPointSampleValue)
{
    Values[2] = std::numeric_limits<Float32>::infinity();
    ExpectInvalidSampler(Sampler, "non-finite floating-point component");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidBooleanSampleValue)
{
    const std::array<Uint8, 4> InvalidBoolValues = {0, 1, 2, 0};
    Sampler.Value.Type                           = RADIENT_ANIMATION_VALUE_TYPE_BOOL;
    Sampler.Interpolation                        = RADIENT_ANIMATION_INTERPOLATION_STEP;
    Sampler.pValues                              = InvalidBoolValues.data();
    Sampler.ValueDataSize                        = sizeof(InvalidBoolValues);
    ExpectInvalidSampler(Sampler, "Boolean value other than zero or one");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidChannelTargetIndex)
{
    Channel.TargetIndex = InvalidRadientAnimationTargetIndex;
    ExpectInvalidChannel(Channel, "references invalid target");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidChannelSamplerIndex)
{
    Channel.SamplerIndex = InvalidRadientAnimationSamplerIndex;
    ExpectInvalidChannel(Channel, "references invalid sampler");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsInvalidChannelProperty)
{
    Channel.Property = InvalidRadientAnimationPropertyID;
    ExpectInvalidChannel(Channel, "invalid property identifier");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsChannelArrayRangeOverflow)
{
    Channel.FirstArrayElement = std::numeric_limits<Uint32>::max();
    ExpectInvalidChannel(Channel, "array range overflows Uint32");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsUnreferencedTarget)
{
    std::array<RadientAnimationTargetDesc, 2> Targets = {Target, Target};
    Targets[1].Object                                 = 43;
    RadientAnimationClipDesc InvalidDesc              = Desc;
    InvalidDesc.pTargets                              = Targets.data();
    InvalidDesc.TargetCount                           = static_cast<Uint32>(Targets.size());
    ExpectInvalid(InvalidDesc, "target 1 is not referenced by any channel");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsUnreferencedSampler)
{
    std::array<RadientAnimationSamplerDesc, 2> Samplers    = {Sampler, Sampler};
    RadientAnimationClipDesc                   InvalidDesc = Desc;
    InvalidDesc.pSamplers                                  = Samplers.data();
    InvalidDesc.SamplerCount                               = static_cast<Uint32>(Samplers.size());
    ExpectInvalid(InvalidDesc, "sampler 1 is not referenced by any channel");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsIdenticalChannelRanges)
{
    std::array<RadientAnimationSamplerDesc, 2> Samplers = {Sampler, Sampler};
    std::array<RadientAnimationChannelDesc, 2> Channels = {Channel, Channel};
    Channels[1].SamplerIndex                            = 1;

    RadientAnimationClipDesc InvalidDesc = Desc;
    InvalidDesc.pSamplers                = Samplers.data();
    InvalidDesc.SamplerCount             = static_cast<Uint32>(Samplers.size());
    InvalidDesc.pChannels                = Channels.data();
    InvalidDesc.ChannelCount             = static_cast<Uint32>(Channels.size());
    ExpectInvalid(InvalidDesc, "overlapping ranges");
}

TEST_F(RadientAnimationAssetValidationTest, RejectsPartiallyOverlappingChannelRanges)
{
    std::array<RadientAnimationSamplerDesc, 2> Samplers = {Sampler, Sampler};
    std::array<RadientAnimationChannelDesc, 2> Channels = {Channel, Channel};
    Channels[1].SamplerIndex                            = 1;
    Channels[1].FirstArrayElement                       = 1;

    RadientAnimationClipDesc InvalidDesc = Desc;
    InvalidDesc.pSamplers                = Samplers.data();
    InvalidDesc.SamplerCount             = static_cast<Uint32>(Samplers.size());
    InvalidDesc.pChannels                = Channels.data();
    InvalidDesc.ChannelCount             = static_cast<Uint32>(Channels.size());
    ExpectInvalid(InvalidDesc, "overlapping ranges");
}

TEST_F(RadientAnimationAssetValidationTest, AcceptsAdjacentChannelRangesWithOneValueType)
{
    std::array<RadientAnimationSamplerDesc, 2> Samplers = {Sampler, Sampler};
    std::array<RadientAnimationChannelDesc, 2> Channels = {Channel, Channel};
    Channels[1].SamplerIndex                            = 1;
    Channels[1].FirstArrayElement                       = 2;

    RadientAnimationClipDesc AdjacentDesc = Desc;
    AdjacentDesc.pSamplers                = Samplers.data();
    AdjacentDesc.SamplerCount             = static_cast<Uint32>(Samplers.size());
    AdjacentDesc.pChannels                = Channels.data();
    AdjacentDesc.ChannelCount             = static_cast<Uint32>(Channels.size());
    EXPECT_NE(CreateClip(AdjacentDesc), nullptr);
}

TEST_F(RadientAnimationAssetValidationTest, RejectsDisjointChannelRangesWithDifferentValueTypes)
{
    std::array<RadientAnimationSamplerDesc, 2> Samplers = {Sampler, Sampler};
    Samplers[1].Value.Type                              = RADIENT_ANIMATION_VALUE_TYPE_INT;
    Samplers[1].Interpolation                           = RADIENT_ANIMATION_INTERPOLATION_STEP;
    std::array<RadientAnimationChannelDesc, 2> Channels = {Channel, Channel};
    Channels[1].SamplerIndex                            = 1;
    Channels[1].FirstArrayElement                       = 2;

    RadientAnimationClipDesc InvalidDesc = Desc;
    InvalidDesc.pSamplers                = Samplers.data();
    InvalidDesc.SamplerCount             = static_cast<Uint32>(Samplers.size());
    InvalidDesc.pChannels                = Channels.data();
    InvalidDesc.ChannelCount             = static_cast<Uint32>(Channels.size());
    ExpectInvalid(InvalidDesc, "different native value types");
}

} // namespace
