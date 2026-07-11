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

#include "Assets/RadientTextureAssetManager.hpp"

#include "RadientTestAssetHelpers.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

constexpr size_t DefaultWorkerCount = 1;

static constexpr std::array<Uint8, 16> TexturePixels{
    255, 0, 0, 255,
    0, 255, 0, 255,
    0, 0, 255, 255,
    255, 255, 255, 255};

RadientTextureData MakeTextureData(const void* pData)
{
    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pData  = pData;
    TextureData.Stride = 8;
    return TextureData;
}

RadientTextureLoadInfo MakeTextureDataLoadInfo(const RadientTextureData& TextureData,
                                               Bool                      IsSRGB = True)
{
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pTextureData = &TextureData;
    LoadInfo.IsSRGB       = IsSRGB;
    return LoadInfo;
}

RefCntAutoPtr<IThreadPool> CreateTestThreadPool(size_t WorkerCount = DefaultWorkerCount)
{
    return CreateThreadPool(ThreadPoolCreateInfo{WorkerCount});
}

void WaitForAllTasksAndStop(IThreadPool& ThreadPool)
{
    ThreadPool.WaitForAllTasks();
    ThreadPool.StopThreads();
}

RefCntAutoPtr<IAsyncTask> BlockWorkerThread(IThreadPool&       ThreadPool,
                                            Threading::Signal& ReleaseWorker)
{
    RefCntAutoPtr<IAsyncTask> pTask =
        EnqueueAsyncWork(
            &ThreadPool,
            [&ReleaseWorker](Uint32) //
            {
                ReleaseWorker.Wait();
                return ASYNC_TASK_STATUS_COMPLETE;
            });
    pTask->WaitUntilRunning();
    return pTask;
}

RadientTextureAssetManager::CreateInfo MakeTextureManagerCI()
{
    RadientTextureAssetManager::CreateInfo CI;
    return CI;
}

RadientTextureAssetManagerSharedPtr CreateTextureManager()
{
    return RadientTextureAssetManager::Create(MakeTextureManagerCI());
}

void ExpectStatusOkOrPending(RADIENT_STATUS Status)
{
    EXPECT_TRUE(Status == RADIENT_STATUS_PENDING || Status == RADIENT_STATUS_OK)
        << "Unexpected status: " << static_cast<int>(Status);
}

TEST(RadientTextureAssetManagerTest, LoadTextureCreatesLightHandleBeforeWorkerRuns)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    const RadientTextureData     TextureData = MakeTextureData(TexturePixels.data());
    const RadientTextureLoadInfo LoadInfo    = MakeTextureDataLoadInfo(TextureData);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    EXPECT_NE(pTexture, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);

    ReleaseWorker.Trigger();
    WaitForAllTasksAndStop(*pThreadPool);

    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
}

TEST(RadientTextureAssetManagerTest, DeduplicatesIdenticalMemoryTextures)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    std::array<Uint8, TexturePixels.size()> TexturePixels0 = TexturePixels;
    std::array<Uint8, TexturePixels.size()> TexturePixels1 = TexturePixels;
    const RadientTextureData                TextureData0   = MakeTextureData(TexturePixels0.data());
    const RadientTextureData                TextureData1   = MakeTextureData(TexturePixels1.data());

    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData0), &pTexture0));
    ASSERT_NE(pTexture0, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture1;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData1), &pTexture1));
    ASSERT_NE(pTexture1, nullptr);
    EXPECT_NE(pTexture1.RawPtr(), pTexture0.RawPtr());

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pPayload0 = RadientTextureAssetManager::GetTexturePayload(pTexture0);
    ASSERT_NE(pPayload0, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture1), pPayload0);
}

TEST(RadientTextureAssetManagerTest, CanonicalURIAliasesSharePayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<TestRadientAssetResolver> pResolver{MakeNewRCObj<TestRadientAssetResolver>()()};
    const std::vector<Uint8>                TextureData{TransparentPng.begin(), TransparentPng.end()};
    pResolver->AddAsset("textures/albedo.png", "memory://assets/albedo.png", TextureData);
    pResolver->AddAsset("textures/../textures/albedo.png", "memory://assets/albedo.png", TextureData);

    RadientTextureAssetManager::CreateInfo ManagerCI;
    ManagerCI.pAssetResolver                     = pResolver;
    RadientTextureAssetManagerSharedPtr pManager = RadientTextureAssetManager::Create(ManagerCI);
    ASSERT_NE(pManager, nullptr);

    RadientTextureLoadInfo LoadInfo0;
    LoadInfo0.URI = "textures/albedo.png";
    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo0, &pTexture0));
    ASSERT_NE(pTexture0, nullptr);

    RadientTextureLoadInfo LoadInfo1;
    LoadInfo1.URI = "textures/../textures/albedo.png";
    RefCntAutoPtr<IRadientTextureAsset> pTexture1;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo1, &pTexture1));
    ASSERT_NE(pTexture1, nullptr);

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pPayload0 = RadientTextureAssetManager::GetTexturePayload(pTexture0);
    ASSERT_NE(pPayload0, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture1), pPayload0);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture0), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture1), RADIENT_STATUS_OK);

    // Both requests resolve their canonical identity, but only the cache creator
    // opens the shared texture bytes.
    EXPECT_EQ(pResolver->GetStats().ResolveLocationCount, 2u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
}

TEST(RadientTextureAssetManagerTest, PreservesAssetOpenFailureStatus)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<TestRadientAssetResolver> pResolver{MakeNewRCObj<TestRadientAssetResolver>()()};
    pResolver->AddAsset("textures/missing.png",
                        "memory://assets/missing.png",
                        std::vector<Uint8>{TransparentPng.begin(), TransparentPng.end()});
    pResolver->SetOpenAssetStatus(RADIENT_STATUS_NOT_FOUND);

    RadientTextureAssetManager::CreateInfo ManagerCI;
    ManagerCI.pAssetResolver                     = pResolver;
    RadientTextureAssetManagerSharedPtr pManager = RadientTextureAssetManager::Create(ManagerCI);
    ASSERT_NE(pManager, nullptr);

    RadientTextureLoadInfo LoadInfo;
    LoadInfo.URI = "textures/missing.png";

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture));
    ASSERT_NE(pTexture, nullptr);

    WaitForAllTasksAndStop(*pThreadPool);

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
}

TEST(RadientTextureAssetManagerTest, DifferentTextureOptionsUseDifferentPayloads)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    const RadientTextureData TextureData = MakeTextureData(TexturePixels.data());

    RefCntAutoPtr<IRadientTextureAsset> pSRGBTexture;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData, True), &pSRGBTexture));
    ASSERT_NE(pSRGBTexture, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pLinearTexture;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData, False), &pLinearTexture));
    ASSERT_NE(pLinearTexture, nullptr);

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pSRGBPayload = RadientTextureAssetManager::GetTexturePayload(pSRGBTexture);
    ASSERT_NE(pSRGBPayload, nullptr);
    const TexturePayloadImpl* pLinearPayload = RadientTextureAssetManager::GetTexturePayload(pLinearTexture);
    ASSERT_NE(pLinearPayload, nullptr);
    EXPECT_NE(pLinearPayload, pSRGBPayload);
}

TEST(RadientTextureAssetManagerTest, ConcurrentSameTextureLoadsSharePayload)
{
    constexpr Uint32 ThreadCount = 8;

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool(ThreadCount);
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures(ThreadCount);
    std::vector<std::thread>                         Threads;
    Threads.reserve(ThreadCount);

    Threading::Signal   StartSignal;
    std::atomic<Uint32> ReadyCount{0};

    for (Uint32 i = 0; i < ThreadCount; ++i)
    {
        Threads.emplace_back(
            [pManager, pThreadPool, &Textures, &StartSignal, &ReadyCount, i]() //
            {
                ReadyCount.fetch_add(1, std::memory_order_release);
                StartSignal.Wait(true, ThreadCount);

                const RadientTextureData     TextureData = MakeTextureData(TexturePixels.data());
                const RadientTextureLoadInfo LoadInfo    = MakeTextureDataLoadInfo(TextureData);

                RefCntAutoPtr<IRadientTextureAsset> pTexture;
                ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture));
                EXPECT_NE(pTexture, nullptr);
                Textures[i] = std::move(pTexture);
            });
    }

    while (ReadyCount.load(std::memory_order_acquire) != ThreadCount)
        std::this_thread::yield();

    StartSignal.Trigger(true);

    for (std::thread& Thread : Threads)
        Thread.join();

    WaitForAllTasksAndStop(*pThreadPool);

    ASSERT_NE(Textures[0], nullptr);
    const TexturePayloadImpl* pPayload = RadientTextureAssetManager::GetTexturePayload(Textures[0]);
    ASSERT_NE(pPayload, nullptr);

    for (const RefCntAutoPtr<IRadientTextureAsset>& pTexture : Textures)
    {
        ASSERT_NE(pTexture, nullptr);
        EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture), pPayload);
    }
}

TEST(RadientTextureAssetManagerTest, TextureHandleMayOutliveManager)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
        ASSERT_NE(pManager, nullptr);

        const RadientTextureData     TextureData = MakeTextureData(TexturePixels.data());
        const RadientTextureLoadInfo LoadInfo    = MakeTextureDataLoadInfo(TextureData);
        ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture));
        ASSERT_NE(pTexture, nullptr);

        WaitForAllTasksAndStop(*pThreadPool);
        ASSERT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    }

    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
}

TEST(RadientTextureAssetManagerTest, ManagerMayDieBeforeWorkerRuns)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
        ASSERT_NE(pManager, nullptr);

        const RadientTextureData     TextureData = MakeTextureData(TexturePixels.data());
        const RadientTextureLoadInfo LoadInfo    = MakeTextureDataLoadInfo(TextureData);
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
        EXPECT_NE(pTexture, nullptr);
        if (pTexture != nullptr)
            EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    }

    ReleaseWorker.Trigger();
    WaitForAllTasksAndStop(*pThreadPool);

    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
}

} // namespace
