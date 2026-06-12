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

#include "Assets/RadientTextureSource.hpp"

#include "gtest/gtest.h"

#include <array>
#include <utility>
#include <vector>

using namespace Diligent;

namespace
{

struct ReleaseState
{
    Uint32      Count    = 0;
    const void* pData    = nullptr;
    Uint64      DataSize = 0;
};

void ReleaseTextureData(const void* pData, Uint64 DataSize, void* pUserData)
{
    auto& State = *static_cast<ReleaseState*>(pUserData);
    ++State.Count;
    State.pData    = pData;
    State.DataSize = DataSize;
}

std::vector<Uint8> ReadSourceBytes(const RadientTextureSource& Source)
{
    const Uint8* pBytes = static_cast<const Uint8*>(Source.GetData());
    return std::vector<Uint8>{pBytes, pBytes + Source.GetDataSize()};
}

} // namespace

TEST(RadientTextureSourceTest, BuildsStableURITextureCacheKey)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI      = "memory://texture";
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());
    LoadInfo.IsSRGB   = True;

    EXPECT_EQ(RadientTextureSource::MakeCacheKey(LoadInfo),
              RadientTextureSource::MakeCacheKey(LoadInfo));

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = False;
    EXPECT_NE(RadientTextureSource::MakeCacheKey(LoadInfo),
              RadientTextureSource::MakeCacheKey(LinearLoadInfo));
}

TEST(RadientTextureSourceTest, BuildsStableMemoryTextureCacheKeys)
{
    std::array<Uint8, 4> Data0{1, 2, 3, 4};
    std::array<Uint8, 4> Data1{1, 2, 3, 4};
    std::array<Uint8, 4> Data2{1, 2, 3, 5};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data0.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data0.size());

    RadientTextureLoadInfo SameDataLoadInfo = LoadInfo;
    SameDataLoadInfo.pData                  = Data1.data();

    EXPECT_EQ(RadientTextureSource::MakeCacheKey(LoadInfo),
              RadientTextureSource::MakeCacheKey(SameDataLoadInfo));

    RadientTextureLoadInfo DifferentDataLoadInfo = LoadInfo;
    DifferentDataLoadInfo.pData                  = Data2.data();
    EXPECT_NE(RadientTextureSource::MakeCacheKey(LoadInfo),
              RadientTextureSource::MakeCacheKey(DifferentDataLoadInfo));

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = True;
    EXPECT_NE(RadientTextureSource::MakeCacheKey(LoadInfo),
              RadientTextureSource::MakeCacheKey(LinearLoadInfo));
}

TEST(RadientTextureSourceTest, BorrowsMemoryWhenCopyIsDisabled)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    EXPECT_TRUE(Source.IsMemory());
    EXPECT_FALSE(Source.OwnsMemory());
    EXPECT_EQ(Source.GetData(), Data.data());
    EXPECT_EQ(Source.GetDataSize(), Data.size());
}

TEST(RadientTextureSourceTest, CopiesMemoryWhenRequested)
{
    std::array<Uint8, 4>     Data{1, 2, 3, 4};
    const std::vector<Uint8> Expected{Data.begin(), Data.end()};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    ASSERT_TRUE(Source.IsMemory());
    EXPECT_TRUE(Source.OwnsMemory());
    EXPECT_NE(Source.GetData(), Data.data());

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Source), Expected);
}

TEST(RadientTextureSourceTest, MovePreservesCopiedMemory)
{
    std::array<Uint8, 4>     Data{1, 2, 3, 4};
    const std::vector<Uint8> Expected{Data.begin(), Data.end()};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    RadientTextureSource Moved{std::move(Source)};

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Moved), Expected);
    EXPECT_NE(Moved.GetData(), Data.data());
}

TEST(RadientTextureSourceTest, ReleasesCallbackOwnedMemory)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReleaseState         State;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData                = Data.data();
    LoadInfo.DataSize             = static_cast<Uint64>(Data.size());
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_TRUE(Source.OwnsMemory());
        EXPECT_EQ(Source.GetData(), Data.data());
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, Data.size());
}

TEST(RadientTextureSourceTest, MoveTransfersReleaseCallbackOwnership)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReleaseState         State;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData                = Data.data();
    LoadInfo.DataSize             = static_cast<Uint64>(Data.size());
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource Source{LoadInfo};
        RadientTextureSource Moved{std::move(Source)};
        EXPECT_TRUE(Moved.OwnsMemory());
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, Data.size());
}
