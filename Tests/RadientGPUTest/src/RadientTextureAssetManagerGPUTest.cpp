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

#include "GPUUploadManager.h"
#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "TestingSwapChainBase.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

static constexpr Uint32 TestTexturePixelSize = TestTextureParams{}.PixelSize;

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

void VerifyUploadedStandaloneTextureData(IDeviceContext&           Context,
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
    EXPECT_TRUE(RadientTextureAssetManager::ApplyTextureAtlasAttribs(&Texture, TextureAttribs));
    EXPECT_FLOAT_EQ(TextureAttribs.AtlasUVScaleAndBias.x, 1.f);
    EXPECT_FLOAT_EQ(TextureAttribs.AtlasUVScaleAndBias.y, 1.f);
    EXPECT_FLOAT_EQ(TextureAttribs.AtlasUVScaleAndBias.z, 0.f);
    EXPECT_FLOAT_EQ(TextureAttribs.AtlasUVScaleAndBias.w, 0.f);
    EXPECT_FLOAT_EQ(TextureAttribs.TextureSlice, 0.f);

    const TextureDesc& UploadedDesc = pUploadedTexture->GetDesc();
    ASSERT_EQ(UploadedDesc.Width, ExpectedData.Width);
    ASSERT_EQ(UploadedDesc.Height, ExpectedData.Height);
    ASSERT_EQ(UploadedDesc.GetArraySize(), 1u);

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

    const Box SrcBox{0, ExpectedData.Width, 0, ExpectedData.Height};

    CopyTextureAttribs CopyInfo;
    CopyInfo.pSrcTexture              = pUploadedTexture;
    CopyInfo.SrcMipLevel              = 0;
    CopyInfo.SrcSlice                 = 0;
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

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);

    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, UploadsOversizedTextureAsStandaloneTexture)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice, TextureData.Width / 2);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);

    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedStandaloneTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);

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

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture0)));
    ASSERT_NE(pTexture0, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture1;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture1)));
    ASSERT_NE(pTexture1, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));

    const TexturePayloadImpl* pPayload = RadientTextureAssetManager::GetTexturePayload(pTexture0);
    ASSERT_NE(pPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture1), pPayload);

    ProcessUploads(*pUploadManager, *pContext, *pTexture0);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture0), nullptr);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture1), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture0, TextureData);

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

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    std::array<std::vector<Uint8>, NumTextures>                  TexturePixelData;
    std::array<RadientTextureData, NumTextures>                  TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures> Textures;
    std::array<RADIENT_STATUS, NumTextures>                      LoadStatuses{};

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TexturePixelData[i] = MakeTexturePixels(static_cast<Uint32>(i + 1));
        TextureData[i]      = MakeTextureData(TexturePixelData[i]);
    }

    Threading::Signal        StartSignal;
    std::vector<std::thread> Threads;
    Threads.reserve(NumTextures);
    for (size_t i = 0; i < NumTextures; ++i)
    {
        Threads.emplace_back(
            [pThreadPool, pManager, &TextureData, &Textures, &LoadStatuses, &StartSignal, i]() {
                StartSignal.Wait();

                RadientTextureLoadInfo LoadInfo;
                LoadInfo.pTextureData = &TextureData[i];
                LoadInfo.IsSRGB       = False;
                LoadStatuses[i]       = pManager->LoadTexture(*pThreadPool, LoadInfo, &Textures[i]);
            });
    }

    StartSignal.Trigger(true);

    for (std::thread& Thread : Threads)
        Thread.join();

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_TRUE(IsPendingOrOK(LoadStatuses[i])) << i;
        ASSERT_NE(Textures[i], nullptr) << i;
    }

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));

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

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
        ASSERT_NE(pResourceManager, nullptr);

        RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
        ASSERT_NE(pUploadManager, nullptr);

        RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
        ASSERT_NE(pManager, nullptr);

        EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);
        ASSERT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    }

    // Do not pump GPU uploads or wait for manager stats here. The worker
    // must observe expired weak upload dependencies and exit safely.
    ReleaseWorker.Trigger();

    pThreadPool->StopThreads();

    ASSERT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
}

TEST(RadientTextureAssetManagerGPUTest, UploadManagerStopUnblocksTextureUpload)
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

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, nullptr);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    const bool PendingCopyCommandEnqueueCallbacks = WaitForPendingCopyCommandEnqueueCallbacks(pManager);
    ASSERT_TRUE(PendingCopyCommandEnqueueCallbacks);

    // The source has loaded, but upload callbacks have not reported whether
    // they could enqueue copy commands, so GPU resource status remains pending.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);

    pUploadManager->Stop(pContext);
    pThreadPool->StopThreads();

    EXPECT_TRUE(IsTextureManagerIdle(pManager->GetStats()));

    // Stop() drains the pending callbacks with no upload context; since no
    // copy command was enqueued, GPU resource creation must fail.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
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

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
        ASSERT_NE(pManager, nullptr);

        EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);

        ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
        ProcessUploads(*pUploadManager, *pContext, *pTexture);
        ASSERT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    }

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);

    pThreadPool->StopThreads();
}

} // namespace
