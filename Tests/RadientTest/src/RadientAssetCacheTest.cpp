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

#define DILIGENT_WEAK_OBJECT_CACHE_TEST_HOOKS 1
#include "Assets/RadientAssetCache.hpp"
#undef DILIGENT_WEAK_OBJECT_CACHE_TEST_HOOKS

#include "ThreadSignal.hpp"
#include "gtest/gtest.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;

namespace
{

struct TestTextureStorage
{
    Uint32 Value = 0;
};

class TestTexturePayloadImpl final : public RadientAssetPayloadImpl<TestTextureStorage, TestTexturePayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<TestTextureStorage, TestTexturePayloadImpl>;
    using TBase::TBase;
};

RefCntAutoPtr<TestTexturePayloadImpl> CreateTestTexturePayload(Uint32 Value)
{
    return TestTexturePayloadImpl::Create(Value);
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
            m_StartSignal.Trigger(true, m_ThreadCount);

        m_StartSignal.Wait(true, m_ThreadCount);
    }

private:
    const int         m_ThreadCount = 0;
    std::atomic<int>  m_ReadyCount{0};
    Threading::Signal m_StartSignal;
};

} // namespace

TEST(RadientAssetCacheTest, ExistingLiveHitDoesNotCallFactory)
{
    RadientAssetCache<TestTexturePayloadImpl> Cache{1};
    Uint32                                    CreateCount = 0;

    auto [pAsset0, Created0] =
        Cache.GetOrCreate(
            "texture-key",
            [&]() {
                ++CreateCount;
                return CreateTestTexturePayload(1);
            });

    auto [pAsset1, Created1] =
        Cache.GetOrCreate(
            "texture-key",
            [&]() {
                ADD_FAILURE() << "Factory must not be called for a live cache hit";
                ++CreateCount;
                return CreateTestTexturePayload(2);
            });

    EXPECT_TRUE(Created0);
    EXPECT_FALSE(Created1);
    ASSERT_NE(pAsset0, nullptr);
    ASSERT_NE(pAsset1, nullptr);
    EXPECT_EQ(pAsset0.RawPtr(), pAsset1.RawPtr());
    EXPECT_EQ(CreateCount, 1u);
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, RemovesEntryWhenAssetReleased)
{
    RadientAssetCache<TestTexturePayloadImpl> Cache{1};
    Uint32                                    CreateCount = 0;

    {
        auto [pAsset, Created] =
            Cache.GetOrCreate(
                "texture-key",
                [&]() {
                    ++CreateCount;
                    return CreateTestTexturePayload(1);
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
                return CreateTestTexturePayload(2);
            });

    EXPECT_TRUE(Created);
    ASSERT_NE(pAsset, nullptr);
    EXPECT_EQ(CreateCount, 2u);
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, FactoryMayMutateCacheKeyStorage)
{
    RadientAssetCache<TestTexturePayloadImpl> Cache{1};
    std::string                               Key{"texture-key"};

    auto [pAsset, Created] =
        Cache.GetOrCreate(
            Key.c_str(),
            [&]() {
                Key = "different-key";
                return CreateTestTexturePayload(1);
            });

    EXPECT_TRUE(Created);
    ASSERT_NE(pAsset, nullptr);
    EXPECT_EQ(Key, "different-key");
    EXPECT_EQ(Cache.Size(), size_t{1});

    pAsset.Release();
    EXPECT_EQ(Cache.Size(), size_t{0});

    auto [pNewAsset, NewCreated] =
        Cache.GetOrCreate(
            "texture-key",
            []() {
                return CreateTestTexturePayload(2);
            });

    EXPECT_TRUE(NewCreated);
    ASSERT_NE(pNewAsset, nullptr);
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, KeepsEntryWhileAssetIsLive)
{
    RadientAssetCache<TestTexturePayloadImpl> Cache{1};

    auto [pAsset, Created] =
        Cache.GetOrCreate(
            "texture-key",
            []() {
                return CreateTestTexturePayload(1);
            });

    EXPECT_TRUE(Created);
    ASSERT_NE(pAsset, nullptr);
    EXPECT_EQ(Cache.Size(), size_t{1});
    EXPECT_FALSE(Cache.EraseIfExpired("texture-key"));
    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, AssetMayOutliveCache)
{
    RefCntAutoPtr<TestTexturePayloadImpl> pAsset;

    {
        RadientAssetCache<TestTexturePayloadImpl> Cache{1};
        auto [pCachedAsset, Created] =
            Cache.GetOrCreate(
                "texture-key",
                []() {
                    return CreateTestTexturePayload(1);
                });

        EXPECT_TRUE(Created);
        ASSERT_NE(pCachedAsset, nullptr);
        EXPECT_EQ(Cache.Size(), size_t{1});
        pAsset = std::move(pCachedAsset);
    }

    pAsset.Release();
}

TEST(RadientAssetCacheTest, ConcurrentSameKeyCreationRunsOneFactory)
{
    static constexpr Uint32 ThreadCount = 8;

    RadientAssetCache<TestTexturePayloadImpl>          Cache{1};
    ThreadStartGate                                    StartGate{ThreadCount};
    Threading::Signal                                  FactoryEntered;
    Threading::Signal                                  FinishFactory;
    std::atomic<Uint32>                                WaitersWaiting{0};
    std::atomic<Uint32>                                CreateCount{0};
    std::vector<std::thread>                           Threads;
    std::vector<RefCntAutoPtr<TestTexturePayloadImpl>> Assets(ThreadCount);
    std::vector<Uint8>                                 Created(ThreadCount, 0);

    Cache.SetWaitCreateCallback(
        [](const Char*, void* pUserData) {
            auto& Waiting = *static_cast<std::atomic<Uint32>*>(pUserData);
            Waiting.fetch_add(1, std::memory_order_acq_rel);
        },
        &WaitersWaiting);

    Threads.reserve(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex]() {
            StartGate.Wait();

            auto [pAsset, WasCreated] =
                Cache.GetOrCreate(
                    "texture-key",
                    [&]() {
                        const Uint32 Count = CreateCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                        EXPECT_EQ(Count, 1u);
                        if (Count == 1)
                        {
                            FactoryEntered.Trigger(true);
                            FinishFactory.Wait(true, 1);
                        }
                        return CreateTestTexturePayload(Count);
                    });

            Assets[ThreadIndex]  = std::move(pAsset);
            Created[ThreadIndex] = WasCreated ? 1 : 0;
        });
    }

    FactoryEntered.Wait(true, 1);

    while (WaitersWaiting.load(std::memory_order_acquire) != ThreadCount - 1)
        std::this_thread::yield();

    FinishFactory.Trigger(true);

    for (std::thread& Thread : Threads)
        Thread.join();

    ASSERT_NE(Assets[0], nullptr);
    EXPECT_EQ(CreateCount.load(std::memory_order_acquire), 1u);

    Uint32 CreatedCount = 0;
    for (const Uint8 WasCreated : Created)
        CreatedCount += WasCreated;
    EXPECT_EQ(CreatedCount, 1u);

    for (const RefCntAutoPtr<TestTexturePayloadImpl>& pAsset : Assets)
    {
        ASSERT_NE(pAsset, nullptr);
        EXPECT_EQ(pAsset.RawPtr(), Assets[0].RawPtr());
    }

    EXPECT_EQ(Cache.Size(), size_t{1});
}

TEST(RadientAssetCacheTest, ReleaseMayOverlapSameKeyGetOrCreate)
{
    static constexpr Uint32 IterationCount = 64;

    RadientAssetCache<TestTexturePayloadImpl> Cache{1};
    std::atomic<Uint32>                       CreateCount{0};

    for (Uint32 Iteration = 0; Iteration < IterationCount; ++Iteration)
    {
        auto [pInitialAsset, InitialCreated] =
            Cache.GetOrCreate(
                "texture-key",
                [&]() {
                    CreateCount.fetch_add(1, std::memory_order_acq_rel);
                    return CreateTestTexturePayload(Iteration);
                });

        ASSERT_NE(pInitialAsset, nullptr);
        EXPECT_TRUE(InitialCreated);
        EXPECT_EQ(Cache.Size(), size_t{1});

        Threading::Signal                     StartWorker;
        RefCntAutoPtr<TestTexturePayloadImpl> pWorkerAsset;
        bool                                  WorkerCreated = false;

        std::thread Worker{[&]() {
            StartWorker.Wait(true, 1);

            auto [pAsset, Created] =
                Cache.GetOrCreate(
                    "texture-key",
                    [&]() {
                        CreateCount.fetch_add(1, std::memory_order_acq_rel);
                        return CreateTestTexturePayload(Iteration);
                    });

            pWorkerAsset  = std::move(pAsset);
            WorkerCreated = Created;
        }};

        StartWorker.Trigger(true);
        pInitialAsset.Release();
        Worker.join();

        ASSERT_NE(pWorkerAsset, nullptr);
        EXPECT_EQ(Cache.Size(), size_t{1});
        EXPECT_FALSE(Cache.EraseIfExpired("texture-key"));

        pWorkerAsset.Release();
        EXPECT_EQ(Cache.Size(), size_t{0});

        (void)WorkerCreated;
    }

    EXPECT_GE(CreateCount.load(std::memory_order_acquire), IterationCount);
    EXPECT_LE(CreateCount.load(std::memory_order_acquire), IterationCount * 2);
}
