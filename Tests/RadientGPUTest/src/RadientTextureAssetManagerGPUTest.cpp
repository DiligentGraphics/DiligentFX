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

#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "GPUTestingEnvironment.hpp"
#include "TestingSwapChainBase.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace std::chrono_literals;

namespace
{

static constexpr Uint32 TestTextureWidth     = 64;
static constexpr Uint32 TestTextureHeight    = 64;
static constexpr Uint32 TestTextureStride    = TestTextureWidth * 4;
static constexpr Uint32 TestTexturePixelSize = 4;

std::array<Uint8, TestTextureStride * TestTextureHeight> MakeTexturePixels(Uint32 Seed = 0)
{
    std::array<Uint8, TestTextureStride * TestTextureHeight> Pixels{};

    for (Uint32 y = 0; y < TestTextureHeight; ++y)
    {
        for (Uint32 x = 0; x < TestTextureWidth; ++x)
        {
            const Uint32 Offset = y * TestTextureStride + x * TestTexturePixelSize;
            Pixels[Offset + 0]  = static_cast<Uint8>((x * 3 + y * 5 + Seed * 29) & 0xFF);
            Pixels[Offset + 1]  = static_cast<Uint8>((x * 11 + y * 7 + (x ^ y) + Seed * 31) & 0xFF);
            Pixels[Offset + 2]  = static_cast<Uint8>((x * y + x * 13 + y * 17 + Seed * 37) & 0xFF);
            Pixels[Offset + 3]  = static_cast<Uint8>(127 + ((x + y + Seed * 3) & 0x7F));
        }
    }

    return Pixels;
}

static const std::array<Uint8, (TestTextureStride * TestTextureHeight)> TexturePixels = MakeTexturePixels();

static const RadientTextureData TestTextureData{
    TestTextureWidth,
    TestTextureHeight,
    RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,
    TexturePixels.data(),
    TestTextureStride,
};

RadientTextureLoadInfo MakeTextureDataLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pTextureData = &TextureData;
    LoadInfo.IsSRGB       = False;
    return LoadInfo;
}

bool IsThreadPoolIdle(IThreadPool& ThreadPool)
{
    return ThreadPool.GetQueueSize() == 0 &&
        ThreadPool.GetRunningTaskCount() == 0;
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

GLTF::ResourceManager::CreateInfo MakeResourceManagerCI()
{
    static constexpr Uint64 IndexBufferSize      = 1024 * 1024;
    static constexpr Uint32 TextureAtlasSize     = 1024;
    static constexpr Uint32 TextureAtlasMaxSlice = 16;

    GLTF::ResourceManager::CreateInfo CreateInfo;

    CreateInfo.IndexAllocatorCI.Desc.Name      = "Radient texture test index pool";
    CreateInfo.IndexAllocatorCI.Desc.Size      = IndexBufferSize;
    CreateInfo.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    CreateInfo.IndexAllocatorCI.ExpansionSize  = static_cast<Uint32>(IndexBufferSize);
    CreateInfo.IndexAllocatorCI.MaxSize        = IndexBufferSize;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient texture test atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = 1;
    CreateInfo.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    CreateInfo.DefaultAtlasDesc.MaxSliceCount  = TextureAtlasMaxSlice;
    CreateInfo.DefaultAtlasDesc.MinAlignment   = 1;

    return CreateInfo;
}

RefCntAutoPtr<GLTF::ResourceManager> CreateTestResourceManager(IRenderDevice* pDevice)
{
    return GLTF::ResourceManager::Create(pDevice, MakeResourceManagerCI());
}

RefCntAutoPtr<IGPUUploadManager> CreateTestUploadManager(IRenderDevice*  pDevice,
                                                         IDeviceContext* pContext)
{
    RefCntAutoPtr<IGPUUploadManager> pUploadManager;
    GPUUploadManagerCreateInfo       CreateInfo;
    CreateInfo.pDevice  = pDevice;
    CreateInfo.pContext = pContext;
    CreateGPUUploadManager(CreateInfo, &pUploadManager);
    return pUploadManager;
}

RadientTextureAssetManager::CreateInfo MakeTextureManagerCI(IThreadPool*           pThreadPool,
                                                            IRenderDevice*         pDevice,
                                                            GLTF::ResourceManager* pResourceManager,
                                                            IGPUUploadManager*     pUploadManager)
{
    RadientTextureAssetManager::CreateInfo CI;
    CI.pThreadPool      = pThreadPool;
    CI.pDevice          = pDevice;
    CI.pResourceManager = pResourceManager;
    CI.pUploadManager   = pUploadManager;
    return CI;
}

void PumpUploadManager(IGPUUploadManager& UploadManager,
                       IDeviceContext&    Context)
{
    UploadManager.RenderThreadUpdate(&Context);
    Context.Flush();
    Context.FinishFrame();
}

void ProcessUploads(IGPUUploadManager&    UploadManager,
                    IDeviceContext&       Context,
                    IRadientTextureAsset& Texture)
{
    for (Uint32 i = 0; i < 32 && RadientTextureAssetManager::GetTextureSRV(&Texture) == nullptr; ++i)
        PumpUploadManager(UploadManager, Context);
}

bool PumpWorkerUntilUploadScheduled(IThreadPool&       ThreadPool,
                                    IGPUUploadManager& UploadManager,
                                    IDeviceContext&    Context)
{
    for (Uint32 i = 0; i < 128; ++i)
    {
        if (IsThreadPoolIdle(ThreadPool))
            return true;

        PumpUploadManager(UploadManager, Context);

        for (Uint32 Spin = 0; Spin < 128; ++Spin)
        {
            if (IsThreadPoolIdle(ThreadPool))
                return true;

            std::this_thread::yield();
        }

        std::this_thread::sleep_for(1ms);
    }

    return IsThreadPoolIdle(ThreadPool);
}

bool IsPendingOrOK(RADIENT_STATUS Status)
{
    return Status == RADIENT_STATUS_PENDING ||
        Status == RADIENT_STATUS_OK;
}

void VerifyUploadedTextureData(IDeviceContext&           Context,
                               ISwapChain&               SwapChain,
                               IRadientTextureAsset&     Texture,
                               const RadientTextureData& ExpectedData)
{
    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{&SwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    ITextureView* pTextureSRV = RadientTextureAssetManager::GetTextureSRV(&Texture);
    ASSERT_NE(pTextureSRV, nullptr);

    ITexture* pUploadedTexture = pTextureSRV->GetTexture();
    ASSERT_NE(pUploadedTexture, nullptr);

    GLTF::Material::TextureShaderAttribs TextureAttribs;
    ASSERT_TRUE(RadientTextureAssetManager::ApplyTextureAtlasAttribs(&Texture, TextureAttribs));

    ITextureView* pBackBufferRTV = SwapChain.GetCurrentBackBufferRTV();
    ASSERT_NE(pBackBufferRTV, nullptr);

    ITexture* pBackBuffer = pBackBufferRTV->GetTexture();
    ASSERT_NE(pBackBuffer, nullptr);

    const SwapChainDesc& SCDesc = SwapChain.GetDesc();
    ASSERT_GE(SCDesc.Width, ExpectedData.Width);
    ASSERT_GE(SCDesc.Height, ExpectedData.Height);

    const size_t       ReferenceStride = static_cast<size_t>(SCDesc.Width) * TestTexturePixelSize;
    std::vector<Uint8> ReferenceData(ReferenceStride * SCDesc.Height, 0);
    const Uint8* const pSrcPixels = static_cast<const Uint8*>(ExpectedData.pData);
    const size_t       SrcStride  = static_cast<size_t>(ExpectedData.Stride);
    for (Uint32 Row = 0; Row < ExpectedData.Height; ++Row)
    {
        std::memcpy(ReferenceData.data() + Row * ReferenceStride,
                    pSrcPixels + Row * SrcStride,
                    ExpectedData.Width * TestTexturePixelSize);
    }
    pTestingSwapChain->SetReferenceData(ReferenceData.data(), ReferenceStride);

    ITextureView* ppRTVs[]      = {pBackBufferRTV};
    const float   ClearColor[4] = {};
    Context.SetRenderTargets(1, ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.ClearRenderTarget(pBackBufferRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    const TextureDesc& UploadedDesc = pUploadedTexture->GetDesc();
    const Uint32       SrcX         = static_cast<Uint32>(TextureAttribs.AtlasUVScaleAndBias.z * static_cast<float>(UploadedDesc.Width) + 0.5f);
    const Uint32       SrcY         = static_cast<Uint32>(TextureAttribs.AtlasUVScaleAndBias.w * static_cast<float>(UploadedDesc.Height) + 0.5f);
    const Uint32       SrcSlice     = static_cast<Uint32>(TextureAttribs.TextureSlice + 0.5f);

    ASSERT_GE(UploadedDesc.Width, SrcX + ExpectedData.Width);
    ASSERT_GE(UploadedDesc.Height, SrcY + ExpectedData.Height);
    ASSERT_GT(UploadedDesc.GetArraySize(), SrcSlice);

    const Box SrcBox{SrcX, SrcX + ExpectedData.Width, SrcY, SrcY + ExpectedData.Height};

    CopyTextureAttribs CopyInfo;
    CopyInfo.pSrcTexture              = pUploadedTexture;
    CopyInfo.SrcMipLevel              = 0;
    CopyInfo.SrcSlice                 = SrcSlice;
    CopyInfo.pSrcBox                  = &SrcBox;
    CopyInfo.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    CopyInfo.pDstTexture              = pBackBuffer;
    CopyInfo.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Context.CopyTexture(CopyInfo);

    SwapChain.Present();
}

TEST(RadientTextureAssetManagerGPUTest, UploadsTextureAndReturnsSRV)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManager Manager{
        MakeTextureManagerCI(pThreadPool, pDevice, pResourceManager, pUploadManager)};

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(Manager.LoadTexture(MakeTextureDataLoadInfo(TestTextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(PumpWorkerUntilUploadScheduled(*pThreadPool, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);

    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TestTextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, DeduplicatedTexturesShareUploadedPayload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManager Manager{
        MakeTextureManagerCI(pThreadPool, pDevice, pResourceManager, pUploadManager)};

    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    EXPECT_TRUE(IsPendingOrOK(Manager.LoadTexture(MakeTextureDataLoadInfo(TestTextureData), &pTexture0)));
    ASSERT_NE(pTexture0, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture1;
    EXPECT_TRUE(IsPendingOrOK(Manager.LoadTexture(MakeTextureDataLoadInfo(TestTextureData), &pTexture1)));
    ASSERT_NE(pTexture1, nullptr);

    ASSERT_TRUE(PumpWorkerUntilUploadScheduled(*pThreadPool, *pUploadManager, *pContext));

    const TexturePayloadImpl* pPayload = RadientTextureAssetManager::GetTexturePayload(pTexture0);
    ASSERT_NE(pPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture1), pPayload);

    ProcessUploads(*pUploadManager, *pContext, *pTexture0);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture0), nullptr);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture1), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture0, TestTextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, ParallelTextureUploads)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    static constexpr size_t NumTextures = 4;

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{NumTextures});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManager Manager{
        MakeTextureManagerCI(pThreadPool, pDevice, pResourceManager, pUploadManager)};

    std::array<std::array<Uint8, TestTextureStride * TestTextureHeight>, NumTextures> TexturePixelData;
    std::array<RadientTextureData, NumTextures>                                       TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures>                      Textures;
    std::array<RADIENT_STATUS, NumTextures>                                           LoadStatuses{};

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TexturePixelData[i] = MakeTexturePixels(static_cast<Uint32>(i + 1));
        TextureData[i]      = RadientTextureData{
            TestTextureWidth,
            TestTextureHeight,
            RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,
            TexturePixelData[i].data(),
            TestTextureStride,
        };
    }

    Threading::Signal        StartSignal;
    std::vector<std::thread> Threads;
    Threads.reserve(NumTextures);
    for (size_t i = 0; i < NumTextures; ++i)
    {
        Threads.emplace_back(
            [&Manager, &TextureData, &Textures, &LoadStatuses, &StartSignal, i]() {
                StartSignal.Wait();

                RadientTextureLoadInfo LoadInfo;
                LoadInfo.pTextureData = &TextureData[i];
                LoadInfo.IsSRGB       = False;
                LoadStatuses[i]       = Manager.LoadTexture(LoadInfo, &Textures[i]);
            });
    }

    StartSignal.Trigger();

    for (std::thread& Thread : Threads)
        Thread.join();

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_TRUE(IsPendingOrOK(LoadStatuses[i])) << i;
        ASSERT_NE(Textures[i], nullptr) << i;
    }

    ASSERT_TRUE(PumpWorkerUntilUploadScheduled(*pThreadPool, *pUploadManager, *pContext));

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_OK) << i;
        ProcessUploads(*pUploadManager, *pContext, *Textures[i]);
    }

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(Textures[i]), nullptr) << i;
        VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *Textures[i], TextureData[i]);
    }

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, ManagerMayDieWhileUploadIsPending)
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

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RadientTextureAssetManager Manager{
            MakeTextureManagerCI(pThreadPool, pDevice, pResourceManager, pUploadManager)};

        EXPECT_TRUE(IsPendingOrOK(Manager.LoadTexture(MakeTextureDataLoadInfo(TestTextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);
        ASSERT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    }

    ASSERT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);

    ReleaseWorker.Trigger();
    ASSERT_TRUE(PumpWorkerUntilUploadScheduled(*pThreadPool, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pTexture);

    const RADIENT_STATUS LoadStatus = RadientTextureAssetManager::GetLoadStatus(pTexture);
    EXPECT_NE(LoadStatus, RADIENT_STATUS_PENDING);

    if (RadientTextureAssetManager::GetTextureSRV(pTexture) != nullptr)
        VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TestTextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, TextureHandleMayOutliveManagerAfterUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RadientTextureAssetManager Manager{
            MakeTextureManagerCI(pThreadPool, pDevice, pResourceManager, pUploadManager)};

        EXPECT_TRUE(IsPendingOrOK(Manager.LoadTexture(MakeTextureDataLoadInfo(TestTextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);

        ASSERT_TRUE(PumpWorkerUntilUploadScheduled(*pThreadPool, *pUploadManager, *pContext));
        ProcessUploads(*pUploadManager, *pContext, *pTexture);
        ASSERT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    }

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TestTextureData);

    pThreadPool->StopThreads();
}

} // namespace
