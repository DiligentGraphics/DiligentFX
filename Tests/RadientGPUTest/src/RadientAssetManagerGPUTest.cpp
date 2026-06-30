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
#include "Assets/RadientTextureAssetManager.hpp"

#include "GPUTestingEnvironment.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace std::chrono_literals;

namespace
{

std::vector<Uint8> MakeTexturePixels(Uint32 Width,
                                     Uint32 Height,
                                     Uint32 Stride,
                                     Uint32 Seed)
{
    static constexpr Uint32 PixelSize = 4;

    std::vector<Uint8> Pixels(static_cast<size_t>(Stride) * Height);

    for (Uint32 y = 0; y < Height; ++y)
    {
        for (Uint32 x = 0; x < Width; ++x)
        {
            const Uint32 Offset = y * Stride + x * PixelSize;
            Pixels[Offset + 0]  = static_cast<Uint8>((x * 3 + y * 5 + Seed * 29) & 0xFF);
            Pixels[Offset + 1]  = static_cast<Uint8>((x * 11 + y * 7 + Seed * 31) & 0xFF);
            Pixels[Offset + 2]  = static_cast<Uint8>((x * y + x * 13 + y * 17 + Seed * 37) & 0xFF);
            Pixels[Offset + 3]  = static_cast<Uint8>(127 + ((x + y + Seed * 3) & 0x7F));
        }
    }

    return Pixels;
}

RadientTextureData MakeTextureData(Uint32      Width,
                                   Uint32      Height,
                                   Uint32      Stride,
                                   const void* pData)
{
    RadientTextureData TextureData{};
    TextureData.Width  = Width;
    TextureData.Height = Height;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pData  = pData;
    TextureData.Stride = Stride;
    return TextureData;
}

RadientTextureLoadInfo MakeTextureLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    LoadInfo.IsSRGB       = False;
    return LoadInfo;
}

bool IsPendingOrOK(RADIENT_STATUS Status)
{
    return Status == RADIENT_STATUS_PENDING ||
        Status == RADIENT_STATUS_OK;
}

bool WaitForTextureUploadScheduling(RadientAssetManagerImpl& AssetManager)
{
    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientTextureAssetManagerStats Stats = AssetManager.GetTextureManagerStats();
        if (Stats.PendingUploadScheduling != 0)
            return true;

        std::this_thread::sleep_for(1ms);
    }

    return AssetManager.GetTextureManagerStats().PendingUploadScheduling != 0;
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

TEST(RadientAssetManagerGPUTest, ManagerMayDieWhileTextureLoadsArePending)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    const Uint32 TextureWidth  = 64;
    const Uint32 TextureHeight = 64;
    const Uint32 TextureStride = TextureWidth * 4;

    static constexpr size_t NumTextures = 4;

    std::array<std::vector<Uint8>, NumTextures>                  TexturePixels;
    std::array<RadientTextureData, NumTextures>                  TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures> Textures;

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TexturePixels[i] = MakeTexturePixels(TextureWidth, TextureHeight, TextureStride, static_cast<Uint32>(i + 1));
        TextureData[i]   = MakeTextureData(TextureWidth, TextureHeight, TextureStride, TexturePixels[i].data());
    }

    {
        RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
        AssetManagerCI.pThreadPool = pThreadPool;
        AssetManagerCI.pDevice     = pDevice;

        RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
        ASSERT_NE(pAssetManager, nullptr);

        for (size_t i = 0; i < NumTextures; ++i)
        {
            EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadTexture(MakeTextureLoadInfo(TextureData[i]), &Textures[i]))) << i;
            ASSERT_NE(Textures[i], nullptr) << i;
            EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_PENDING) << i;
            EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(Textures[i]), nullptr) << i;
        }

        EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    }

    // After Stop() and manager release, texture load tasks observe expired GPU
    // upload dependencies and exit without completing the texture load.
    ReleaseWorker.Trigger();
    pThreadPool->StopThreads();

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_NE(Textures[i], nullptr) << i;
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_INVALID_OPERATION) << i;
        EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(Textures[i]), nullptr) << i;
    }
}

TEST(RadientAssetManagerGPUTest, StopShutsDownUploadManagerForBlockedTextureUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const Uint32 TextureWidth  = 64;
    const Uint32 TextureHeight = 64;
    const Uint32 TextureStride = TextureWidth * 4;

    std::vector<Uint8> TexturePixels = MakeTexturePixels(TextureWidth, TextureHeight, TextureStride, 1);
    RadientTextureData TextureData   = MakeTextureData(TextureWidth, TextureHeight, TextureStride, TexturePixels.data());

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    bool                                EnteredUploadScheduling = false;

    {
        RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
        AssetManagerCI.pThreadPool = pThreadPool;
        AssetManagerCI.pDevice     = pDevice;

        RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
        ASSERT_NE(pAssetManager, nullptr);

        EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadTexture(MakeTextureLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);

        EnteredUploadScheduling = WaitForTextureUploadScheduling(*pAssetManager);
        ASSERT_TRUE(EnteredUploadScheduling);

        // The worker has queued upload callbacks, but they have not reported
        // success or failure yet, so the texture load remains pending.
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_PENDING);
        EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    }

    pThreadPool->StopThreads();

    // Stop() shuts down the upload manager and drains the blocked callbacks.
    // No texture copy was enqueued, so the load reaches a terminal failure.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(pTexture), nullptr);
}

} // namespace
