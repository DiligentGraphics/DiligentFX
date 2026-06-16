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

#include "Assets/RadientAssetCache.hpp"

#include "gtest/gtest.h"

#include <string>

using namespace Diligent;

namespace
{

static constexpr INTERFACE_ID IID_TestTextureAssetImpl = {0x115f62b7, 0xba9d, 0x4e35, {0x9a, 0xd5, 0x86, 0xc4, 0xf9, 0xb7, 0x8c, 0x92}};

struct TestTextureStorage
{
    Uint32 Value = 0;
};

using TestTextureAssetImpl =
    RadientAssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TestTextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TestTextureStorage>;

RefCntAutoPtr<TestTextureAssetImpl> CreateTestTextureAsset(const char* URI, Uint32 Value)
{
    return RefCntAutoPtr<TestTextureAssetImpl>{
        MakeNewRCObj<TestTextureAssetImpl>()(std::string{URI}, URI, TestTextureStorage{Value})};
}

} // namespace

TEST(RadientAssetCacheTest, RemovesEntryWhenAssetReleased)
{
    RadientAssetCache<IRadientTextureAsset> Cache{1};
    Uint32                                  CreateCount = 0;

    {
        auto [pAsset, Created] =
            Cache.GetOrCreate(
                "texture-key",
                [&]() {
                    ++CreateCount;
                    return CreateTestTextureAsset("texture-uri", 1);
                });

        EXPECT_TRUE(Created);
        ASSERT_NE(pAsset, nullptr);
        EXPECT_EQ(CreateCount, 1u);
        EXPECT_EQ(Cache.Size(), size_t{1});
    }

    EXPECT_EQ(Cache.Size(), size_t{0});

    auto [pAsset, Created] =
        Cache.GetOrCreate(
            "texture-key",
            [&]() {
                ++CreateCount;
                return CreateTestTextureAsset("texture-uri", 2);
            });

    EXPECT_TRUE(Created);
    ASSERT_NE(pAsset, nullptr);
    EXPECT_EQ(CreateCount, 2u);
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, KeepsEntryWhileAssetIsLive)
{
    RadientAssetCache<IRadientTextureAsset> Cache{1};

    auto [pAsset, Created] =
        Cache.GetOrCreate(
            "texture-key",
            []() {
                return CreateTestTextureAsset("texture-uri", 1);
            });

    EXPECT_TRUE(Created);
    ASSERT_NE(pAsset, nullptr);
    EXPECT_EQ(Cache.Size(), size_t{1});
    EXPECT_FALSE(Cache.EraseIfExpired("texture-key"));
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, AssetMayOutliveCache)
{
    RefCntAutoPtr<IRadientTextureAsset> pAsset;

    {
        RadientAssetCache<IRadientTextureAsset> Cache{1};
        auto [pCachedAsset, Created] =
            Cache.GetOrCreate(
                "texture-key",
                []() {
                    return CreateTestTextureAsset("texture-uri", 1);
                });

        EXPECT_TRUE(Created);
        ASSERT_NE(pCachedAsset, nullptr);
        EXPECT_EQ(Cache.Size(), size_t{1});
        pAsset = std::move(pCachedAsset);
    }

    pAsset.Release();
}
