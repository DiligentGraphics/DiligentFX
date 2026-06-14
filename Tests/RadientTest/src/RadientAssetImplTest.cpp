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

#include "Assets/RadientAssetImpl.hpp"

#include "TestingEnvironment.hpp"
#include "ThreadSignal.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr INTERFACE_ID IID_TestTextureAssetImpl = {0xe9f6ef96, 0x3eda, 0x4f09, {0x9e, 0x3b, 0x9c, 0xc8, 0x90, 0x34, 0x64, 0x28}};

struct TestTextureStorage
{
    TestTextureStorage() = default;
    explicit TestTextureStorage(Uint32 _Value) :
        Value{_Value}
    {
    }

    TestTextureStorage(TestTextureStorage&& Rhs) noexcept :
        LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
        Value{Rhs.Value}
    {
    }

    // clang-format off
    TestTextureStorage& operator=(TestTextureStorage&&)      = delete;
    TestTextureStorage(const TestTextureStorage&)            = delete;
    TestTextureStorage& operator=(const TestTextureStorage&) = delete;
    // clang-format on

    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    Uint32                      Value = 0;
};

using TestTextureAssetImpl =
    RadientAssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TestTextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TestTextureStorage>;
using TestTextureAssetPtr = RefCntAutoPtr<TestTextureAssetImpl>;

TestTextureAssetPtr CreateTestTextureAsset(const char* URI, Uint32 Value)
{
    return TestTextureAssetPtr{
        MakeNewRCObj<TestTextureAssetImpl>()(std::string{URI},
                                             "Test texture",
                                             TestTextureStorage{Value})};
}

class ThreadStartGate
{
public:
    explicit ThreadStartGate(Uint32 ThreadCount) :
        m_ThreadCount{static_cast<int>(ThreadCount)}
    {
    }

    void Wait()
    {
        if (m_ReadyCount.fetch_add(1, std::memory_order_acq_rel) + 1 == m_ThreadCount)
            m_StartSignal.Trigger(true);

        m_StartSignal.Wait(true, m_ThreadCount);
    }

private:
    const int         m_ThreadCount = 0;
    std::atomic<int>  m_ReadyCount{0};
    Threading::Signal m_StartSignal;
};

} // namespace

TEST(RadientAssetCacheTest, CreatesAndReusesCachedAsset)
{
    RadientAssetCache<IRadientTextureAsset> Cache;

    Uint32 CreateCount = 0;
    auto [FirstAsset, FirstCreated] =
        Cache.GetOrCreate<TestTextureAssetImpl>(
            "texture-key",
            IID_TestTextureAssetImpl,
            [&]() {
                ++CreateCount;
                return CreateTestTextureAsset("texture://first", 17);
            });

    ASSERT_NE(FirstAsset, nullptr);
    EXPECT_TRUE(FirstCreated);
    EXPECT_EQ(CreateCount, 1u);
    EXPECT_STREQ(FirstAsset->GetReference().URI, "texture://first");
    EXPECT_EQ(FirstAsset->GetStorage().Value, 17u);

    auto [SecondAsset, SecondCreated] =
        Cache.GetOrCreate<TestTextureAssetImpl>(
            "texture-key",
            IID_TestTextureAssetImpl,
            [&]() {
                ADD_FAILURE() << "Factory must not be called when a live cache entry exists";
                ++CreateCount;
                return CreateTestTextureAsset("texture://unexpected", 99);
            });

    ASSERT_NE(SecondAsset, nullptr);
    EXPECT_FALSE(SecondCreated);
    EXPECT_EQ(CreateCount, 1u);
    EXPECT_EQ(SecondAsset.RawPtr(), FirstAsset.RawPtr());
    EXPECT_EQ(SecondAsset->GetStorage().Value, 17u);
}

TEST(RadientAssetCacheTest, CreatesIndependentAssetsForDifferentKeys)
{
    RadientAssetCache<IRadientTextureAsset> Cache;

    Uint32 CreateCount = 0;
    auto   CreateAsset = [&](const char* URI, Uint32 Value) {
        return [&, URI, Value]() {
            ++CreateCount;
            return CreateTestTextureAsset(URI, Value);
        };
    };

    auto [FirstAsset, FirstCreated] =
        Cache.GetOrCreate<TestTextureAssetImpl>("texture-key-0", IID_TestTextureAssetImpl, CreateAsset("texture://first", 1));
    auto [SecondAsset, SecondCreated] =
        Cache.GetOrCreate<TestTextureAssetImpl>("texture-key-1", IID_TestTextureAssetImpl, CreateAsset("texture://second", 2));

    ASSERT_NE(FirstAsset, nullptr);
    ASSERT_NE(SecondAsset, nullptr);
    EXPECT_TRUE(FirstCreated);
    EXPECT_TRUE(SecondCreated);
    EXPECT_EQ(CreateCount, 2u);
    EXPECT_NE(SecondAsset.RawPtr(), FirstAsset.RawPtr());
    EXPECT_EQ(FirstAsset->GetStorage().Value, 1u);
    EXPECT_EQ(SecondAsset->GetStorage().Value, 2u);
}

TEST(RadientAssetCacheTest, ReplacesExpiredWeakEntry)
{
    RadientAssetCache<IRadientTextureAsset> Cache;

    Uint32 CreateCount = 0;
    {
        auto [Asset, Created] =
            Cache.GetOrCreate<TestTextureAssetImpl>(
                "texture-key",
                IID_TestTextureAssetImpl,
                [&]() {
                    ++CreateCount;
                    return CreateTestTextureAsset("texture://expired", 3);
                });

        ASSERT_NE(Asset, nullptr);
        EXPECT_TRUE(Created);
        EXPECT_EQ(Asset->GetStorage().Value, 3u);
    }

    auto [Asset, Created] =
        Cache.GetOrCreate<TestTextureAssetImpl>(
            "texture-key",
            IID_TestTextureAssetImpl,
            [&]() {
                ++CreateCount;
                return CreateTestTextureAsset("texture://replacement", 4);
            });

    ASSERT_NE(Asset, nullptr);
    EXPECT_TRUE(Created);
    EXPECT_EQ(CreateCount, 2u);
    EXPECT_STREQ(Asset->GetReference().URI, "texture://replacement");
    EXPECT_EQ(Asset->GetStorage().Value, 4u);
}

TEST(RadientAssetCacheTest, ReturnsEmptyWhenFactoryFails)
{
    RadientAssetCache<IRadientTextureAsset> Cache;

    Uint32 CreateCount = 0;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Failed to create asset for cache key 'texture-key'"};
        auto [Asset, Created] =
            Cache.GetOrCreate<TestTextureAssetImpl>(
                "texture-key",
                IID_TestTextureAssetImpl,
                [&]() -> RefCntAutoPtr<TestTextureAssetImpl> {
                    ++CreateCount;
                    return {};
                });

        EXPECT_EQ(Asset, nullptr);
        EXPECT_FALSE(Created);
    }

    auto [Asset, Created] =
        Cache.GetOrCreate<TestTextureAssetImpl>(
            "texture-key",
            IID_TestTextureAssetImpl,
            [&]() {
                ++CreateCount;
                return CreateTestTextureAsset("texture://created-after-failure", 5);
            });

    ASSERT_NE(Asset, nullptr);
    EXPECT_TRUE(Created);
    EXPECT_EQ(CreateCount, 2u);
    EXPECT_EQ(Asset->GetStorage().Value, 5u);
}

TEST(RadientAssetCacheTest, ConcurrentRequestsCreateSingleAssetForSameKey)
{
    static constexpr Uint32 ThreadCount = 16;

    RadientAssetCache<IRadientTextureAsset> Cache;
    ThreadStartGate                         StartGate{ThreadCount};
    std::atomic<Uint32>                     CreateCount{0};
    std::vector<std::thread>                Threads;
    std::vector<TestTextureAssetPtr>        Assets(ThreadCount);
    std::vector<Uint8>                      Created(ThreadCount, 0);

    Threads.reserve(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex]() {
            StartGate.Wait();

            auto [Asset, WasCreated] =
                Cache.GetOrCreate<TestTextureAssetImpl>(
                    "texture-key",
                    IID_TestTextureAssetImpl,
                    [&]() {
                        CreateCount.fetch_add(1, std::memory_order_acq_rel);
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                        return CreateTestTextureAsset("texture://threaded", 42);
                    });

            Assets[ThreadIndex]  = std::move(Asset);
            Created[ThreadIndex] = WasCreated ? 1 : 0;
        });
    }

    for (std::thread& Thread : Threads)
        Thread.join();

    ASSERT_NE(Assets[0], nullptr);
    EXPECT_EQ(CreateCount.load(std::memory_order_acquire), 1u);
    EXPECT_EQ(std::count(Created.begin(), Created.end(), Uint8{1}), 1);

    for (const TestTextureAssetPtr& Asset : Assets)
    {
        ASSERT_NE(Asset, nullptr);
        EXPECT_EQ(Asset.RawPtr(), Assets[0].RawPtr());
        EXPECT_EQ(Asset->GetStorage().Value, 42u);
    }
}

TEST(RadientAssetCacheTest, ConcurrentRequestsReplaceExpiredEntryOnce)
{
    static constexpr Uint32 ThreadCount = 16;

    RadientAssetCache<IRadientTextureAsset> Cache;
    std::atomic<Uint32>                     InitialCreateCount{0};
    {
        auto [InitialAsset, InitialCreated] =
            Cache.GetOrCreate<TestTextureAssetImpl>(
                "texture-key",
                IID_TestTextureAssetImpl,
                [&]() {
                    InitialCreateCount.fetch_add(1, std::memory_order_acq_rel);
                    return CreateTestTextureAsset("texture://expired", 31);
                });

        ASSERT_NE(InitialAsset, nullptr);
        EXPECT_TRUE(InitialCreated);
        EXPECT_EQ(InitialAsset->GetStorage().Value, 31u);
    }

    EXPECT_EQ(InitialCreateCount.load(std::memory_order_acquire), 1u);

    ThreadStartGate                  StartGate{ThreadCount};
    std::atomic<Uint32>              ReplacementCreateCount{0};
    std::vector<std::thread>         Threads;
    std::vector<TestTextureAssetPtr> Assets(ThreadCount);
    std::vector<Uint8>               Created(ThreadCount, 0);

    Threads.reserve(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex]() {
            StartGate.Wait();

            auto [Asset, WasCreated] =
                Cache.GetOrCreate<TestTextureAssetImpl>(
                    "texture-key",
                    IID_TestTextureAssetImpl,
                    [&]() {
                        ReplacementCreateCount.fetch_add(1, std::memory_order_acq_rel);
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                        return CreateTestTextureAsset("texture://replacement", 32);
                    });

            Assets[ThreadIndex]  = std::move(Asset);
            Created[ThreadIndex] = WasCreated ? 1 : 0;
        });
    }

    for (std::thread& Thread : Threads)
        Thread.join();

    ASSERT_NE(Assets[0], nullptr);
    EXPECT_EQ(ReplacementCreateCount.load(std::memory_order_acquire), 1u);
    EXPECT_EQ(std::count(Created.begin(), Created.end(), Uint8{1}), 1);

    for (const TestTextureAssetPtr& Asset : Assets)
    {
        ASSERT_NE(Asset, nullptr);
        EXPECT_EQ(Asset.RawPtr(), Assets[0].RawPtr());
        EXPECT_STREQ(Asset->GetReference().URI, "texture://replacement");
        EXPECT_EQ(Asset->GetStorage().Value, 32u);
    }
}

TEST(RadientAssetCacheTest, ConcurrentLiveCacheHitsDoNotCallFactory)
{
    static constexpr Uint32 ThreadCount = 16;

    RadientAssetCache<IRadientTextureAsset> Cache;
    std::atomic<Uint32>                     CreateCount{0};

    auto [InitialAsset, InitialCreated] =
        Cache.GetOrCreate<TestTextureAssetImpl>(
            "texture-key",
            IID_TestTextureAssetImpl,
            [&]() {
                CreateCount.fetch_add(1, std::memory_order_acq_rel);
                return CreateTestTextureAsset("texture://cached", 11);
            });

    ASSERT_NE(InitialAsset, nullptr);
    EXPECT_TRUE(InitialCreated);

    ThreadStartGate                  StartGate{ThreadCount};
    std::vector<std::thread>         Threads;
    std::vector<TestTextureAssetPtr> Assets(ThreadCount);
    std::vector<Uint8>               Created(ThreadCount, 0);

    Threads.reserve(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex]() {
            StartGate.Wait();

            auto [Asset, WasCreated] =
                Cache.GetOrCreate<TestTextureAssetImpl>(
                    "texture-key",
                    IID_TestTextureAssetImpl,
                    [&]() {
                        CreateCount.fetch_add(1, std::memory_order_acq_rel);
                        return CreateTestTextureAsset("texture://unexpected", 99);
                    });

            Assets[ThreadIndex]  = std::move(Asset);
            Created[ThreadIndex] = WasCreated ? 1 : 0;
        });
    }

    for (std::thread& Thread : Threads)
        Thread.join();

    EXPECT_EQ(CreateCount.load(std::memory_order_acquire), 1u);
    EXPECT_EQ(std::count(Created.begin(), Created.end(), Uint8{1}), 0);

    for (const TestTextureAssetPtr& Asset : Assets)
    {
        ASSERT_NE(Asset, nullptr);
        EXPECT_EQ(Asset.RawPtr(), InitialAsset.RawPtr());
        EXPECT_EQ(Asset->GetStorage().Value, 11u);
    }
}

TEST(RadientAssetCacheTest, ConcurrentRequestsForDifferentKeysCreateIndependentAssets)
{
    static constexpr Uint32 ThreadCount = 16;

    RadientAssetCache<IRadientTextureAsset> Cache;
    ThreadStartGate                         StartGate{ThreadCount};
    std::atomic<Uint32>                     CreateCount{0};
    std::vector<std::thread>                Threads;
    std::vector<TestTextureAssetPtr>        Assets(ThreadCount);
    std::vector<Uint8>                      Created(ThreadCount, 0);

    Threads.reserve(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex]() {
            StartGate.Wait();

            const std::string CacheKey = "texture-key-" + std::to_string(ThreadIndex);
            const std::string URI      = "texture://threaded-" + std::to_string(ThreadIndex);
            auto [Asset, WasCreated] =
                Cache.GetOrCreate<TestTextureAssetImpl>(
                    CacheKey,
                    IID_TestTextureAssetImpl,
                    [&, URI, ThreadIndex]() {
                        CreateCount.fetch_add(1, std::memory_order_acq_rel);
                        return CreateTestTextureAsset(URI.c_str(), ThreadIndex);
                    });

            Assets[ThreadIndex]  = std::move(Asset);
            Created[ThreadIndex] = WasCreated ? 1 : 0;
        });
    }

    for (std::thread& Thread : Threads)
        Thread.join();

    EXPECT_EQ(CreateCount.load(std::memory_order_acquire), ThreadCount);
    EXPECT_EQ(std::count(Created.begin(), Created.end(), Uint8{1}), ThreadCount);

    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        ASSERT_NE(Assets[ThreadIndex], nullptr);
        EXPECT_EQ(Assets[ThreadIndex]->GetStorage().Value, ThreadIndex);
        for (Uint32 OtherIndex = ThreadIndex + 1; OtherIndex < ThreadCount; ++OtherIndex)
            EXPECT_NE(Assets[ThreadIndex].RawPtr(), Assets[OtherIndex].RawPtr());
    }
}
