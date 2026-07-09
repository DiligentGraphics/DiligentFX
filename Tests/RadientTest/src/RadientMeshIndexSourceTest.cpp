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

#include "Assets/RadientMeshIndexSource.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

using namespace Diligent;

namespace
{

template <typename IndexType>
RadientMeshIndexSource MakeIndexSource(std::array<IndexType, 3>& Indices)
{
    RadientMeshIndexSource::CreateInfo CI{};
    CI.pData      = Indices.data();
    CI.IndexCount = static_cast<Uint32>(Indices.size());

    if constexpr (sizeof(IndexType) == sizeof(Uint8))
        CI.Type = VT_UINT8;
    else if constexpr (sizeof(IndexType) == sizeof(Uint16))
        CI.Type = VT_UINT16;
    else
        CI.Type = VT_UINT32;

    return RadientMeshIndexSource{CI};
}

void ExpectPackedIndices(const RadientMeshIndexSource& Source,
                         std::initializer_list<Uint32> ExpectedIndices)
{
    std::vector<Uint32> PackedIndices(Source.GetIndexCount(), 0xCDCDCDCDu);
    ASSERT_EQ(PackedIndices.size(), ExpectedIndices.size());

    ASSERT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{
                  PackedIndices.data(),
                  static_cast<Uint32>(PackedIndices.size() * sizeof(PackedIndices[0]))}),
              RADIENT_STATUS_OK);

    EXPECT_EQ(PackedIndices, std::vector<Uint32>{ExpectedIndices});
}

} // namespace

TEST(RadientMeshIndexSourceTest, RejectsInvalidCreateInfo)
{
    std::array<Uint16, 3> Indices{0, 1, 2};

    RadientMeshIndexSource::CreateInfo CI{};
    CI.pData      = Indices.data();
    CI.Type       = VT_UINT16;
    CI.IndexCount = static_cast<Uint32>(Indices.size());

    auto ExpectInvalid = [](const RadientMeshIndexSource::CreateInfo& InvalidCI) //
    {
        RadientMeshIndexSource Source{InvalidCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshIndexSource::CreateInfo InvalidCI = CI;
    InvalidCI.pData                              = nullptr;
    ExpectInvalid(InvalidCI);

    InvalidCI      = CI;
    InvalidCI.Type = VT_UNDEFINED;
    ExpectInvalid(InvalidCI);

    InvalidCI            = CI;
    InvalidCI.IndexCount = 0;
    ExpectInvalid(InvalidCI);
}

TEST(RadientMeshIndexSourceTest, RejectsInvalidRadientCreateInfo)
{
    std::array<Uint16, 3> Indices{0, 1, 2};

    RadientMeshCreateInfo MeshCI{};
    MeshCI.pIndices   = Indices.data();
    MeshCI.IndexCount = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType  = RADIENT_INDEX_TYPE_UINT16;

    RadientMeshIndexSource ValidSource{MeshCI};
    EXPECT_EQ(ValidSource.GetStatus(), RADIENT_STATUS_OK);

    RadientMeshCreateInfo InvalidMeshCI = MeshCI;
    InvalidMeshCI.pIndices              = nullptr;
    RadientMeshIndexSource MissingIndices{InvalidMeshCI};
    EXPECT_EQ(MissingIndices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidMeshCI           = MeshCI;
    InvalidMeshCI.IndexType = RADIENT_INDEX_TYPE_NONE;
    RadientMeshIndexSource InvalidType{InvalidMeshCI};
    EXPECT_EQ(InvalidType.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshIndexSourceTest, PacksUint8Uint16AndUint32AsUint32)
{
    std::array<Uint8, 3>  Indices8{2, 1, 0};
    std::array<Uint16, 3> Indices16{3, 4, 5};
    std::array<Uint32, 3> Indices32{6, 7, 8};

    const RadientMeshIndexSource Source8  = MakeIndexSource(Indices8);
    const RadientMeshIndexSource Source16 = MakeIndexSource(Indices16);
    const RadientMeshIndexSource Source32 = MakeIndexSource(Indices32);

    ASSERT_EQ(Source8.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source16.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source32.GetStatus(), RADIENT_STATUS_OK);

    ExpectPackedIndices(Source8, {2, 1, 0});
    ExpectPackedIndices(Source16, {3, 4, 5});
    ExpectPackedIndices(Source32, {6, 7, 8});
}

TEST(RadientMeshIndexSourceTest, CopiesSourceDataByDefault)
{
    std::array<Uint16, 3>  Indices{0, 1, 2};
    RadientMeshIndexSource Source = MakeIndexSource(Indices);
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    Indices = {7, 7, 7};

    ExpectPackedIndices(Source, {0, 1, 2});
}

TEST(RadientMeshIndexSourceTest, BorrowsSourceDataAndKeepsOwnerAlive)
{
    struct SourceData
    {
        std::array<Uint16, 3> Indices;
    };

    std::weak_ptr<const void>               WeakOwner;
    std::unique_ptr<RadientMeshIndexSource> Source;

    {
        auto Data     = std::make_shared<SourceData>();
        Data->Indices = {0, 1, 2};

        std::shared_ptr<const void> Owner = Data;
        WeakOwner                         = Owner;

        RadientMeshIndexSource::CreateInfo CI{};
        CI.pData            = Data->Indices.data();
        CI.Type             = VT_UINT16;
        CI.IndexCount       = static_cast<Uint32>(Data->Indices.size());
        CI.pSourceDataOwner = Owner;
        Source.reset(new RadientMeshIndexSource{CI});
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
    }

    ASSERT_FALSE(WeakOwner.expired());
    ExpectPackedIndices(*Source, {0, 1, 2});

    Source.reset();
    EXPECT_TRUE(WeakOwner.expired());
}

TEST(RadientMeshIndexSourceTest, RejectsInvalidPackDestination)
{
    std::array<Uint16, 3>  Indices{0, 1, 2};
    RadientMeshIndexSource Source = MakeIndexSource(Indices);
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    std::array<Uint32, 2> SmallBuffer{};
    EXPECT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{nullptr, Source.GetIndexDataSize()}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{
                  SmallBuffer.data(),
                  static_cast<Uint32>(SmallBuffer.size() * sizeof(SmallBuffer[0]))}),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshIndexSourceTest, CacheKeyDependsOnIndexData)
{
    std::array<Uint16, 3> IndicesA{0, 1, 2};
    std::array<Uint16, 3> IndicesB{0, 1, 2};
    std::array<Uint16, 3> IndicesC{2, 1, 0};

    const RadientMeshIndexSource SourceA = MakeIndexSource(IndicesA);
    const RadientMeshIndexSource SourceB = MakeIndexSource(IndicesB);
    const RadientMeshIndexSource SourceC = MakeIndexSource(IndicesC);

    ASSERT_FALSE(SourceA.MakeCacheKey().empty());
    EXPECT_EQ(SourceA.MakeCacheKey(), SourceB.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceC.MakeCacheKey());
}
