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

#include "Assets/RadientMorphTargetSource.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace Diligent;

namespace
{

struct MorphTargetSourceData
{
    static constexpr Uint32 VertexCount = 3;

    std::array<Float32, 9> PositionDeltas{
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        -0.1f, -0.2f, -0.3f};
    std::array<Float32, 6> CustomDeltas{
        1.f, 2.f,
        3.f, 4.f,
        5.f, 6.f};
    std::array<RadientMorphTargetAttributeDesc, 2>       Attributes{};
    std::array<RadientMorphTargetAttributeCreateInfo, 2> AttributeData{};
    std::array<RadientMorphTargetCreateInfo, 2>          Targets{};

    MorphTargetSourceData()
    {
        Reset();
    }

    void Reset()
    {
        PositionDeltas = {
            0.f, 0.f, 0.f,
            0.1f, 0.2f, 0.3f,
            -0.1f, -0.2f, -0.3f};
        CustomDeltas = {
            1.f, 2.f,
            3.f, 4.f,
            5.f, 6.f};
        Attributes    = {};
        AttributeData = {};
        Targets       = {};

        Attributes[0].Semantic       = RadientMorphTargetPositionSemantic;
        Attributes[0].ComponentCount = 3;
        Attributes[1].Semantic       = "CUSTOM";
        Attributes[1].ComponentCount = 2;
        AttributeData[0].pDeltas     = PositionDeltas.data();
        AttributeData[1].pDeltas     = CustomDeltas.data();

        Targets[0].Desc.Name           = "Smile";
        Targets[0].Desc.pAttributes    = Attributes.data();
        Targets[0].Desc.AttributeCount = static_cast<Uint32>(Attributes.size());
        Targets[0].Desc.DefaultWeight  = 0.25f;
        Targets[0].pAttributeData      = AttributeData.data();
        Targets[1].Desc.DefaultWeight  = -0.5f;
    }
};

} // namespace

TEST(RadientMorphTargetSourceTest, RejectsInvalidDimensions)
{
    MorphTargetSourceData Data;

    RadientMorphTargetSource MissingTargets{nullptr, 1, MorphTargetSourceData::VertexCount};
    EXPECT_EQ(MissingTargets.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMorphTargetSource EmptyTargets{Data.Targets.data(), 0, MorphTargetSourceData::VertexCount};
    EXPECT_EQ(EmptyTargets.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMorphTargetSource EmptyVertices{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), 0};
    EXPECT_EQ(EmptyVertices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidTargetDescription)
{
    MorphTargetSourceData Data;

    Data.Targets[0].Desc.DefaultWeight = (std::numeric_limits<Float32>::infinity)();
    RadientMorphTargetSource NonFiniteWeight{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NonFiniteWeight.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Targets[0].Desc.pAttributes = nullptr;
    RadientMorphTargetSource NoAttributes{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoAttributes.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Targets[0].pAttributeData = nullptr;
    RadientMorphTargetSource NoAttributeData{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoAttributeData.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidAttributeDescription)
{
    MorphTargetSourceData Data;

    Data.Attributes[0].Semantic = nullptr;
    RadientMorphTargetSource MissingSemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(MissingSemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].Semantic = "";
    RadientMorphTargetSource EmptySemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(EmptySemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 0;
    RadientMorphTargetSource ZeroComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(ZeroComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 5;
    RadientMorphTargetSource TooManyComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(TooManyComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.AttributeData[0].pDeltas = nullptr;
    RadientMorphTargetSource NoDeltas{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoDeltas.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 2;
    RadientMorphTargetSource InvalidStandardComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(InvalidStandardComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[1].Semantic = RadientMorphTargetPositionSemantic;
    RadientMorphTargetSource DuplicateSemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(DuplicateSemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsDataLargerThanUint32)
{
    Float32                               Delta = 0.f;
    RadientMorphTargetAttributeDesc       Attribute{};
    RadientMorphTargetAttributeCreateInfo AttributeData{};
    RadientMorphTargetCreateInfo          Target{};
    Attribute.Semantic         = "CUSTOM";
    Attribute.ComponentCount   = 4;
    AttributeData.pDeltas      = &Delta;
    Target.Desc.pAttributes    = &Attribute;
    Target.Desc.AttributeCount = 1;
    Target.pAttributeData      = &AttributeData;

    RadientMorphTargetSource Source{&Target, 1, (std::numeric_limits<Uint32>::max)()};
    EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, CopiesMetadataAndPacksUploadData)
{
    MorphTargetSourceData Data;
    std::string           TargetName = "Expression";
    std::string           Semantic   = "CUSTOM_DATA";
    Data.Targets[0].Desc.Name        = TargetName.c_str();
    Data.Attributes[1].Semantic      = Semantic.c_str();

    const auto               ExpectedPositionDeltas = Data.PositionDeltas;
    const auto               ExpectedCustomDeltas   = Data.CustomDeltas;
    RadientMorphTargetSource Source{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};

    TargetName.assign("changed");
    Semantic.assign("changed");
    Data.PositionDeltas.fill(42.f);
    Data.CustomDeltas.fill(24.f);

    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Source.GetVertexCount(), Data.VertexCount);
    ASSERT_EQ(Source.GetTargetCount(), 2u);

    const RadientMorphTargetSource::Target& Target0 = Source.GetTarget(0);
    const RadientMorphTargetSource::Target& Target1 = Source.GetTarget(1);
    EXPECT_EQ(Target0.Name, "Expression");
    EXPECT_FLOAT_EQ(Target0.DefaultWeight, 0.25f);
    ASSERT_EQ(Target0.Attributes.size(), 2u);
    EXPECT_EQ(Target0.Attributes[0].Semantic, RadientMorphTargetPositionSemantic);
    EXPECT_EQ(Target0.Attributes[1].Semantic, "CUSTOM_DATA");
    EXPECT_EQ(Target0.Attributes[0].ComponentCount, 3u);
    EXPECT_EQ(Target0.Attributes[1].ComponentCount, 2u);
    EXPECT_EQ(Target0.Attributes[0].DataOffset, 0u);
    EXPECT_EQ(Target0.Attributes[1].DataOffset, sizeof(ExpectedPositionDeltas));
    EXPECT_EQ(Target1.Name, "");
    EXPECT_FLOAT_EQ(Target1.DefaultWeight, -0.5f);

    std::vector<Uint8> PackedData(Source.GetDataSize());
    ASSERT_EQ(Source.PackData({PackedData.data(), static_cast<Uint32>(PackedData.size())}), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(PackedData.data() + Target0.Attributes[0].DataOffset,
                          ExpectedPositionDeltas.data(), sizeof(ExpectedPositionDeltas)),
              0);
    EXPECT_EQ(std::memcmp(PackedData.data() + Target0.Attributes[1].DataOffset,
                          ExpectedCustomDeltas.data(), sizeof(ExpectedCustomDeltas)),
              0);
}

TEST(RadientMorphTargetSourceTest, SupportsTargetsWithoutAttributes)
{
    RadientMorphTargetCreateInfo Target{};
    Target.Desc.Name          = "Empty";
    Target.Desc.DefaultWeight = 0.5f;

    RadientMorphTargetSource Source{&Target, 1, 3};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Source.GetDataSize(), 0u);
    EXPECT_EQ(Source.PackData({}), RADIENT_STATUS_OK);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidPackDestination)
{
    MorphTargetSourceData    Data;
    RadientMorphTargetSource Source{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    std::vector<Uint8> SmallBuffer(Source.GetDataSize() - 1);
    EXPECT_EQ(Source.PackData({nullptr, Source.GetDataSize()}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackData({SmallBuffer.data(), static_cast<Uint32>(SmallBuffer.size())}), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, CacheKeyIncludesMetadataAndDeltas)
{
    MorphTargetSourceData DataA;
    MorphTargetSourceData DataB;
    MorphTargetSourceData DifferentDeltas;
    MorphTargetSourceData DifferentMetadata;
    DifferentDeltas.PositionDeltas[4] += 1.f;
    DifferentMetadata.Targets[0].Desc.Name = "Frown";

    const RadientMorphTargetSource SourceA{DataA.Targets.data(), static_cast<Uint32>(DataA.Targets.size()), DataA.VertexCount};
    const RadientMorphTargetSource SourceB{DataB.Targets.data(), static_cast<Uint32>(DataB.Targets.size()), DataB.VertexCount};
    const RadientMorphTargetSource SourceWithDifferentDeltas{
        DifferentDeltas.Targets.data(), static_cast<Uint32>(DifferentDeltas.Targets.size()), DifferentDeltas.VertexCount};
    const RadientMorphTargetSource SourceWithDifferentMetadata{
        DifferentMetadata.Targets.data(), static_cast<Uint32>(DifferentMetadata.Targets.size()), DifferentMetadata.VertexCount};

    ASSERT_FALSE(SourceA.MakeCacheKey().empty());
    EXPECT_EQ(SourceA.MakeCacheKey(), SourceB.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceWithDifferentDeltas.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceWithDifferentMetadata.MakeCacheKey());
}
