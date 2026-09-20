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
#include "Core/RadientDataBlobReadAccess.hpp"

#include "RadientTestAssetHelpers.hpp"
#include "RadientTypesX.hpp"
#include "DataBlobImpl.hpp"
#include "GraphicsAccessories.hpp"
#include "MemoryFileStream.hpp"
#include "TextureLoader.h"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "TestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

TEST(RadientTextureBindingIdentityTest, HashMatchesEquality)
{
    const RadientTextureBindingIdentity First{1, TEX_FORMAT_RGBA8_UNORM};
    const RadientTextureBindingIdentity Equal{1, TEX_FORMAT_RGBA8_UNORM};
    const RadientTextureBindingIdentity DifferentResource{2, TEX_FORMAT_RGBA8_UNORM};
    const RadientTextureBindingIdentity DifferentFormat{1, TEX_FORMAT_RGBA8_UNORM_SRGB};
    const RadientTextureBindingIdentity Atlas{0, TEX_FORMAT_RGBA8_UNORM};

    const RadientTextureBindingIdentity::Hasher Hasher;
    EXPECT_EQ(First, Equal);
    EXPECT_EQ(Hasher(First), Hasher(Equal));
    EXPECT_NE(First, DifferentResource);
    EXPECT_NE(First, DifferentFormat);
    EXPECT_TRUE(Atlas);
    EXPECT_NE(First, Atlas);
}

constexpr size_t DefaultWorkerCount = 1;

static constexpr std::array<Uint8, 16> TexturePixels{
    255, 0, 0, 255,
    0, 255, 0, 255,
    0, 0, 255, 255,
    255, 255, 255, 255};

RadientTextureDataX MakeTextureData(IRadientDataBlob* pDataBlob)
{
    RadientTextureDataX TextureData{2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    TextureData.AddMip(pDataBlob, 0, 8);
    return TextureData;
}

RadientTextureLoadInfoX MakeTextureDataLoadInfo(const RadientTextureData& TextureData,
                                                Bool                      IsSRGB = True)
{
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetTextureData(TextureData).SetSRGB(IsSRGB);
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

void ExpectTextureDesc(IRadientTextureAsset*  pTexture,
                       Uint32                 Width     = 0,
                       Uint32                 Height    = 0,
                       RADIENT_TEXTURE_FORMAT Format    = RADIENT_TEXTURE_FORMAT_UNKNOWN,
                       Uint32                 MipLevels = 0)
{
    ASSERT_NE(pTexture, nullptr);
    const RadientTextureAssetDesc& Desc = pTexture->GetDesc();
    EXPECT_EQ(Desc.Width, Width);
    EXPECT_EQ(Desc.Height, Height);
    EXPECT_EQ(Desc.Format, Format);
    EXPECT_EQ(Desc.MipLevels, MipLevels);
}

RefCntAutoPtr<IRadientDataBlob> MakeDDSTestBlob(const TextureDesc& Desc)
{
    const Uint32                    SubresourceCount = Desc.MipLevels * Desc.GetArraySize();
    std::vector<std::vector<Uint8>> Pixels(SubresourceCount);
    std::vector<TextureSubResData>  Subresources(SubresourceCount);
    for (Uint32 Slice = 0; Slice < Desc.GetArraySize(); ++Slice)
    {
        for (Uint32 Mip = 0; Mip < Desc.MipLevels; ++Mip)
        {
            const auto   Properties = GetMipLevelProperties(Desc, Mip);
            const Uint32 Index      = Slice * Desc.MipLevels + Mip;
            Pixels[Index].resize(static_cast<size_t>(Properties.MipSize));
            Subresources[Index].pData       = Pixels[Index].data();
            Subresources[Index].Stride      = Properties.RowSize;
            Subresources[Index].DepthStride = Properties.DepthSliceSize;
        }
    }
    const auto pDDS    = DataBlobImpl::Create();
    const auto pStream = MemoryFileStream::Create(pDDS);
    if (!WriteDDSToStream(pStream, Desc, TextureData{Subresources.data(), SubresourceCount}))
    {
        ADD_FAILURE() << "Failed to create in-memory DDS fixture";
        return {};
    }
    return MakeTestDataBlob(pDDS->GetConstDataPtr(), pDDS->GetSize());
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

    auto                          pBlob       = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
    const RadientTextureDataX     TextureData = MakeTextureData(pBlob);
    const RadientTextureLoadInfoX LoadInfo    = MakeTextureDataLoadInfo(TextureData);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    EXPECT_NE(pTexture, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    ExpectTextureDesc(pTexture);
    const RadientTextureAssetDesc& PendingDesc = pTexture->GetDesc();

    ReleaseWorker.Trigger();
    WaitForAllTasksAndStop(*pThreadPool);

    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture), nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    ExpectTextureDesc(pTexture, 2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 2);
    // A reference obtained while pending remains an immutable empty description.
    EXPECT_EQ(PendingDesc.Width, 0u);
    EXPECT_EQ(PendingDesc.Height, 0u);
    EXPECT_EQ(PendingDesc.Format, RADIENT_TEXTURE_FORMAT_UNKNOWN);
    EXPECT_EQ(PendingDesc.MipLevels, 0u);
}

TEST(RadientTextureAssetManagerTest, LoadTextureFailsWhenThreadPoolIsStopped)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool(0);
    ASSERT_NE(pThreadPool, nullptr);
    pThreadPool->StopThreads();

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    Uint32                    ReadReleases = 0;
    auto                      pBlob        = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size(), CountBlobReadReleases, &ReadReleases);
    const RadientTextureDataX TextureData  = MakeTextureData(pBlob);
    RadientTextureLoadInfoX   LoadInfo     = MakeTextureDataLoadInfo(TextureData);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture),
                  RADIENT_STATUS_INVALID_OPERATION);
    }

    ASSERT_NE(pTexture, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_INVALID_OPERATION);
    ExpectTextureDesc(pTexture);
    EXPECT_EQ(ReadReleases, 1u);

    const RadientTextureAssetManagerStats Stats = pManager->GetStats();
    EXPECT_EQ(Stats.PendingTextureLoads, 0u);
    EXPECT_EQ(Stats.PendingTextureSourceLoads, 0u);
}

TEST(RadientTextureAssetManagerTest, DeduplicatesIdenticalMemoryTextures)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    std::array<Uint8, TexturePixels.size()> TexturePixels0 = TexturePixels;
    std::array<Uint8, TexturePixels.size()> TexturePixels1 = TexturePixels;
    auto                                    pBlob0         = MakeTestDataBlob(TexturePixels0.data(), TexturePixels0.size());
    auto                                    pBlob1         = MakeTestDataBlob(TexturePixels1.data(), TexturePixels1.size());
    const RadientTextureDataX               TextureData0   = MakeTextureData(pBlob0);
    const RadientTextureDataX               TextureData1   = MakeTextureData(pBlob1);

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
    ExpectTextureDesc(pTexture0, 2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 2);
    ExpectTextureDesc(pTexture1, 2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 2);
    const RadientTextureAssetDesc& Desc = pTexture0->GetDesc();
    pTexture1.Release();
    pManager.reset();
    pBlob0.Release();
    pBlob1.Release();
    ExpectTextureDesc(pTexture0, 2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 2);
    EXPECT_EQ(&pTexture0->GetDesc(), &Desc);
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

    RadientTextureLoadInfoX LoadInfo0;
    LoadInfo0.SetURI("textures/albedo.png");
    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo0, &pTexture0));
    ASSERT_NE(pTexture0, nullptr);

    RadientTextureLoadInfoX LoadInfo1;
    LoadInfo1.SetURI("textures/../textures/albedo.png");
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

    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetURI("textures/missing.png");

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ExpectStatusOkOrPending(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture));
    ASSERT_NE(pTexture, nullptr);

    WaitForAllTasksAndStop(*pThreadPool);

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NOT_FOUND);
    ExpectTextureDesc(pTexture);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
}

TEST(RadientTextureAssetManagerTest, DifferentTextureOptionsUseDifferentPayloads)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateTestThreadPool();
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager();
    ASSERT_NE(pManager, nullptr);

    auto                      pBlob       = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
    const RadientTextureDataX TextureData = MakeTextureData(pBlob);

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

TEST(RadientTextureAssetManagerTest, ReflectsEncodedPNGColorSpaceWithoutGPUResources)
{
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    auto pBlob       = MakeTestDataBlob(TransparentPng.data(), TransparentPng.size());
    ASSERT_NE(pBlob, nullptr);
    std::array<RefCntAutoPtr<IRadientTextureAsset>, 2> Textures;
    for (Uint32 i = 0; i < Textures.size(); ++i)
    {
        RadientTextureLoadInfoX LoadInfo;
        LoadInfo.SetDataBlob(pBlob).SetSRGB(i != 0 ? True : False);
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &Textures[i]), RADIENT_STATUS_PENDING);
        ExpectTextureDesc(Textures[i]);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[i]), RADIENT_STATUS_NO_GPU_DATA);
        ExpectTextureDesc(Textures[i], 1, 1,
                          i != 0 ? RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB : RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 1);
    }
    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(Textures[0]),
              RadientTextureAssetManager::GetTexturePayload(Textures[1]));
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReflectsDecodedDimensionsFormatAndGeneratedMipCount)
{
    const std::array<Uint8, 8 * 4> Pixels{};
    auto                           pThreadPool = CreateTestThreadPool(0);
    auto                           pManager    = CreateTextureManager();
    auto                           pBlob       = MakeTestDataBlob(Pixels.data(), Pixels.size());
    RadientTextureMipData          Mip;
    RadientTextureData             Data;
    Data.pMipLevels    = &Mip;
    Data.MipLevelCount = 1;
    EXPECT_EQ(Data.GenerateMips, True);
    Data.Width    = 8;
    Data.Height   = 4;
    Data.Format   = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    Mip.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pTextureData = &Data;
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    // The load request copies the input descriptor.
    Data.Width  = 1;
    Data.Height = 1;
    Data.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    ExpectTextureDesc(pTexture, 8, 4, RADIENT_TEXTURE_FORMAT_R8_UNORM, 4);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReflectsRawCompressedTextureDataWithoutGeneratingMips)
{
    const std::pair<RADIENT_TEXTURE_FORMAT, Uint32> Formats[] = {
        {RADIENT_TEXTURE_FORMAT_BC1_UNORM, 8},
        {RADIENT_TEXTURE_FORMAT_BC3_UNORM_SRGB, 16},
        {RADIENT_TEXTURE_FORMAT_BC6H_UF16, 16},
    };
    const std::array<Uint8, 67> Blocks{};
    auto                        pThreadPool = CreateTestThreadPool(0);
    auto                        pManager    = CreateTextureManager();
    for (const auto& [Format, BlockSize] : Formats)
    {
        SCOPED_TRACE(static_cast<Uint32>(Format));
        const Uint32        Stride = 2 * BlockSize + 3;
        auto                pBlob  = MakeTestDataBlob(Blocks.data(), Stride + 2 * BlockSize);
        RadientTextureDataX Data{8, 8, Format};
        Data.SetGenerateMips(False);
        Data.AddMip(pBlob, 0, Stride);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(Data, False), &pTexture), RADIENT_STATUS_PENDING);
        ExpectTextureDesc(pTexture);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
        ExpectTextureDesc(pTexture, Data.Get().Width, Data.Get().Height, Format, 1);
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReflectsSuppliedCompressedMipChains)
{
    const std::array<Uint8, 56> Blocks{};
    auto                        pBlob       = MakeTestDataBlob(Blocks.data(), Blocks.size());
    const RadientTextureMipData Mips[]      = {{pBlob}, {pBlob, 32}, {pBlob, 40}, {pBlob, 48}};
    auto                        pThreadPool = CreateTestThreadPool(0);
    auto                        pManager    = CreateTextureManager();
    for (Uint32 MipCount : {2u, 4u})
    {
        SCOPED_TRACE(MipCount);
        RadientTextureDataX Data{8, 8, RADIENT_TEXTURE_FORMAT_BC1_UNORM};
        Data.SetGenerateMips(False);
        for (Uint32 Mip = 0; Mip < MipCount; ++Mip)
        {
            Data.AddMip(Mips[Mip]);
        }
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(Data, False), &pTexture), RADIENT_STATUS_PENDING);
        ExpectTextureDesc(pTexture);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        ExpectTextureDesc(pTexture, 8, 8, RADIENT_TEXTURE_FORMAT_BC1_UNORM, MipCount);
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, RetainsQueuedMipDescriptorsAndReflectsOptionalGeneration)
{
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    for (Bool Generate : {False, True})
    {
        SCOPED_TRACE(Generate);
        Uint32                          BaseReleases = 0;
        Uint32                          TailReleases = 0;
        auto                            pBase        = MakeTestMutableDataBlob(nullptr, 64, CountBlobReadReleases, &BaseReleases);
        auto                            pTail        = MakeTestMutableDataBlob(nullptr, 16, CountBlobReadReleases, &TailReleases);
        RefCntWeakPtr<IRadientDataBlob> WeakBase{pBase.RawPtr()};
        RefCntWeakPtr<IRadientDataBlob> WeakTail{pTail.RawPtr()};
        RadientTextureMipData           Mips[] = {{pBase}, {pTail}};
        RadientTextureData              Data;
        Data.Width = Data.Height = 8;
        Data.Format              = RADIENT_TEXTURE_FORMAT_R8_UNORM;
        Data.pMipLevels          = Mips;
        Data.MipLevelCount       = 2;
        Data.GenerateMips        = Generate;
        RadientTextureLoadInfo LoadInfo;
        LoadInfo.pTextureData = &Data;
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
        void* pWrite = nullptr;
        EXPECT_EQ(pBase->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(pTail->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
        pBase.Release();
        pTail.Release();
        Mips[0] = Mips[1] = {};
        Data              = {};
        EXPECT_NE(WeakBase.Lock(), nullptr);
        EXPECT_NE(WeakTail.Lock(), nullptr);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        ExpectTextureDesc(pTexture, 8, 8, RADIENT_TEXTURE_FORMAT_R8_UNORM, Generate ? 4 : 2);
        EXPECT_EQ(BaseReleases, 1u);
        EXPECT_EQ(TailReleases, 1u);
        EXPECT_EQ(WeakBase.Lock(), nullptr);
        EXPECT_EQ(WeakTail.Lock(), nullptr);
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReflectsCompressedDDSFormatsAndStoredMipCount)
{
    struct TestCase
    {
        TEXTURE_FORMAT         SourceFormat;
        RADIENT_TEXTURE_FORMAT ExpectedFormat;
        Bool                   IsSRGB;
    };
    const TestCase Cases[] = {
        {TEX_FORMAT_BC1_UNORM, RADIENT_TEXTURE_FORMAT_BC1_UNORM, False},
        {TEX_FORMAT_BC1_UNORM, RADIENT_TEXTURE_FORMAT_BC1_UNORM_SRGB, True},
        {TEX_FORMAT_BC6H_UF16, RADIENT_TEXTURE_FORMAT_BC6H_UF16, False},
    };
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    for (const auto& Case : Cases)
    {
        SCOPED_TRACE(static_cast<Uint32>(Case.ExpectedFormat));
        TextureDesc Desc;
        Desc.Type   = RESOURCE_DIM_TEX_2D;
        Desc.Width  = 8;
        Desc.Height = 8;
        Desc.Format = Case.SourceFormat;
        // Lower levels include 2x2 and 1x1 images; only mip 0 must cover whole blocks.
        Desc.MipLevels = 4;
        auto pBlob     = MakeDDSTestBlob(Desc);
        ASSERT_NE(pBlob, nullptr);
        RadientTextureLoadInfoX LoadInfo;
        LoadInfo.SetDataBlob(pBlob).SetSRGB(Case.IsSRGB);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
        ExpectTextureDesc(pTexture, 8, 8, Case.ExpectedFormat, 4);
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, RejectsCompressedDDSWithPartialBlockMipZeroDimensions)
{
    const Uint32 Dimensions[][2] = {{5, 8}, {8, 5}, {5, 7}, {2, 2}};
    auto         pThreadPool     = CreateTestThreadPool(0);
    auto         pManager        = CreateTextureManager();
    for (const auto Format : {TEX_FORMAT_BC1_UNORM, TEX_FORMAT_BC7_UNORM})
    {
        for (const auto& Dimension : Dimensions)
        {
            SCOPED_TRACE(static_cast<Uint32>(Format));
            SCOPED_TRACE(Dimension[0]);
            SCOPED_TRACE(Dimension[1]);
            TextureDesc Desc;
            Desc.Type      = RESOURCE_DIM_TEX_2D;
            Desc.Width     = Dimension[0];
            Desc.Height    = Dimension[1];
            Desc.Format    = Format;
            Desc.MipLevels = 1;
            auto pBlob     = MakeDDSTestBlob(Desc);
            ASSERT_NE(pBlob, nullptr);
            RadientTextureLoadInfoX LoadInfo;
            LoadInfo.SetDataBlob(pBlob);
            RefCntAutoPtr<IRadientTextureAsset> pTexture;
            ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
            {
                TestingEnvironment::ErrorScope ExpectedErrors{"must be multiples of the 4 x 4 block dimensions"};
                ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
            }
            EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_UNSUPPORTED);
            EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_UNSUPPORTED);
            ExpectTextureDesc(pTexture);
        }
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, PreservesDDSLoadingForUnreflectedFormats)
{
    const TEXTURE_FORMAT Formats[] = {
        TEX_FORMAT_RGBA8_TYPELESS,
        TEX_FORMAT_BC1_TYPELESS,
        TEX_FORMAT_D32_FLOAT_S8X24_UINT,
        TEX_FORMAT_D32_FLOAT,
        TEX_FORMAT_D24_UNORM_S8_UINT,
        TEX_FORMAT_D16_UNORM,
        TEX_FORMAT_R16_FLOAT,
        TEX_FORMAT_RG16_FLOAT,
        TEX_FORMAT_RGBA16_FLOAT,
        TEX_FORMAT_RGB32_FLOAT,
        TEX_FORMAT_RGB10A2_UNORM,
        TEX_FORMAT_BGRA8_UNORM,
    };
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    for (const auto Format : Formats)
    {
        SCOPED_TRACE(static_cast<Uint32>(Format));
        TextureDesc Desc;
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 4;
        Desc.Height    = 4;
        Desc.Format    = Format;
        Desc.MipLevels = 2;
        auto pBlob     = MakeDDSTestBlob(Desc);
        ASSERT_NE(pBlob, nullptr);
        RadientTextureLoadInfoX LoadInfo;
        LoadInfo.SetDataBlob(pBlob);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
        ExpectTextureDesc(pTexture);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
        ExpectTextureDesc(pTexture, Desc.Width, Desc.Height, RADIENT_TEXTURE_FORMAT_UNKNOWN, Desc.MipLevels);
    }
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReflectsEncodedTextureDimensionsWithoutRestrictingShape)
{
    const std::pair<RESOURCE_DIMENSION, Uint32> Cases[] = {
        {RESOURCE_DIM_TEX_1D, 1},
        {RESOURCE_DIM_TEX_2D_ARRAY, 3},
        {RESOURCE_DIM_TEX_CUBE, 6},
        {RESOURCE_DIM_TEX_CUBE_ARRAY, 12},
    };
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    for (const auto& [Dimension, ArraySize] : Cases)
    {
        SCOPED_TRACE(static_cast<Uint32>(Dimension));
        TextureDesc Desc;
        Desc.Type      = Dimension;
        Desc.Width     = 8;
        Desc.Height    = Desc.Is1D() ? 1 : (Desc.IsCube() ? 8 : 4);
        Desc.ArraySize = ArraySize;
        Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
        Desc.MipLevels = 3;
        auto pBlob     = MakeDDSTestBlob(Desc);
        ASSERT_NE(pBlob, nullptr);
        {
            // Verify that cube fixtures actually decode as cubes, rather than arrays.
            RadientDataBlobReadAccess ReadAccess{pBlob};
            ASSERT_TRUE(ReadAccess);
            RefCntAutoPtr<ITextureLoader> pLoader;
            CreateTextureLoaderFromMemory(ReadAccess.GetData(), static_cast<size_t>(ReadAccess.GetSize()),
                                          false, TextureLoadInfo{}, &pLoader);
            ASSERT_NE(pLoader, nullptr);
            EXPECT_EQ(pLoader->GetTextureDesc().Type, Dimension);
            EXPECT_EQ(pLoader->GetTextureDesc().GetArraySize(), ArraySize);
        }
        RadientTextureLoadInfoX LoadInfo;
        LoadInfo.SetDataBlob(pBlob);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
        ExpectTextureDesc(pTexture);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
        ExpectTextureDesc(pTexture, Desc.Width, Desc.Height, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, Desc.MipLevels);
    }
    pThreadPool->StopThreads();
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

                auto                          pBlob       = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
                const RadientTextureDataX     TextureData = MakeTextureData(pBlob);
                const RadientTextureLoadInfoX LoadInfo    = MakeTextureDataLoadInfo(TextureData);

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

        auto                          pBlob       = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
        const RadientTextureDataX     TextureData = MakeTextureData(pBlob);
        const RadientTextureLoadInfoX LoadInfo    = MakeTextureDataLoadInfo(TextureData);
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

        auto                          pBlob       = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
        const RadientTextureDataX     TextureData = MakeTextureData(pBlob);
        const RadientTextureLoadInfoX LoadInfo    = MakeTextureDataLoadInfo(TextureData);
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

TEST(RadientTextureAssetManagerTest, RetainsQueuedBlobUntilWorkerFinishes)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RefCntWeakPtr<IRadientDataBlob> WeakBlob{pBlob.RawPtr()};
    RadientTextureLoadInfoX         LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(TransparentPng.size() + 1), RADIENT_STATUS_INVALID_OPERATION);
    pBlob.Release();
    LoadInfo.Clear();
    EXPECT_NE(WeakBlob.Lock(), nullptr);
    EXPECT_EQ(ReadReleases, 0u);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
    ExpectTextureDesc(pTexture, 1, 1, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, 1);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, SharedBlobReadAccessEndsAfterCacheHit)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pFirst, pSecond;
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pFirst), RADIENT_STATUS_PENDING);
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pSecond), RADIENT_STATUS_PENDING);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(ReadReleases, 0u);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pFirst), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pSecond), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pFirst),
              RadientTextureAssetManager::GetTexturePayload(pSecond));
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, RejectsActiveBlobWriterBeforeQueuingLoad)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    void* pWriteData = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Unable to acquire read access to encoded texture data blob."};
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(pTexture, nullptr);
    EXPECT_EQ(ReadReleases, 0u);
    EXPECT_EQ(pManager->GetStats().PendingTextureLoads, 0u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    // The same blob can be submitted after the writer finishes.
    EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(ReadReleases, 1u);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReleasesBlobReadAccessOnDecodeFailure)
{
    auto                       pThreadPool  = CreateTestThreadPool(0);
    auto                       pManager     = CreateTextureManager();
    Uint32                     ReadReleases = 0;
    const std::array<Uint8, 4> InvalidImage{1, 2, 3, 4};
    auto                       pBlob = MakeTestMutableDataBlob(InvalidImage.data(), InvalidImage.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Failed to create texture loader from memory", "Unable to derive image format"};
        EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
    }
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_FAILED);
    ExpectTextureDesc(pTexture);
    EXPECT_EQ(ReadReleases, 1u);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, ReleasesBlobReadAccessWhenEnqueueFails)
{
    auto pThreadPool = CreateTestThreadPool(0);
    pThreadPool->StopThreads();
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pManager->GetStats().PendingTextureLoads, 0u);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientTextureAssetManagerTest, ReleasesReadAccessWhenBlobIsEmpty)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestDataBlob(nullptr, 0, CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    LoadInfo.SetURI("textures/albedo.png");
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Encoded texture data blob must not be empty."};
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture), RADIENT_STATUS_INVALID_ARGUMENT);
    }
    EXPECT_EQ(pTexture, nullptr);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    pThreadPool->StopThreads();
}


TEST(RadientTextureAssetManagerTest, RetainsQueuedPixelBlobUntilWorkerFinishes)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TexturePixels.data(), TexturePixels.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RefCntWeakPtr<IRadientDataBlob>     WeakBlob{pBlob.RawPtr()};
    RadientTextureDataX                 TextureData = MakeTextureData(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture), RADIENT_STATUS_PENDING);

    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(TexturePixels.size() + 1), RADIENT_STATUS_INVALID_OPERATION);
    pBlob.Release();
    TextureData = {};
    EXPECT_NE(WeakBlob.Lock(), nullptr);
    EXPECT_EQ(ReadReleases, 0u);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, PaddedPixelBlobsSharePayloadWithPackedPixels)
{
    // The first blob ends at the final active pixel; the second includes trailing
    // padding. Neither row padding nor unused trailing bytes affect cache identity.
    const std::array<Uint8, 20> MinimalPixels{
        255, 0, 0, 255, 0, 255, 0, 255,
        1, 2, 3, 4,
        0, 0, 255, 255, 255, 255, 255, 255};
    const std::array<Uint8, 24> PaddedPixels{
        255, 0, 0, 255, 0, 255, 0, 255,
        5, 6, 7, 8,
        0, 0, 255, 255, 255, 255, 255, 255,
        9, 10, 11, 12};
    auto                               pThreadPool  = CreateTestThreadPool(0);
    auto                               pManager     = CreateTextureManager();
    auto                               pMinimalBlob = MakeTestDataBlob(MinimalPixels.data(), MinimalPixels.size());
    auto                               pPaddedBlob  = MakeTestDataBlob(PaddedPixels.data(), PaddedPixels.size());
    auto                               pPackedBlob  = MakeTestDataBlob(TexturePixels.data(), TexturePixels.size());
    std::array<RadientTextureDataX, 3> TextureData{
        MakeTextureData(pMinimalBlob), MakeTextureData(pPaddedBlob), MakeTextureData(pPackedBlob)};
    TextureData[0].SetMip(0, pMinimalBlob, 0, 12);
    TextureData[1].SetMip(0, pPaddedBlob, 0, 12);
    std::array<RefCntAutoPtr<IRadientTextureAsset>, 3> Textures;
    for (size_t i = 0; i < Textures.size(); ++i)
    {
        ASSERT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData[i]), &Textures[i]), RADIENT_STATUS_PENDING);
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_OK);
    }
    const auto* pPayload = RadientTextureAssetManager::GetTexturePayload(Textures[0]);
    ASSERT_NE(pPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(Textures[1]), pPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(Textures[2]), pPayload);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, RejectsActivePixelBlobWriterBeforeQueuingLoad)
{
    auto   pThreadPool  = CreateTestThreadPool(0);
    auto   pManager     = CreateTextureManager();
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TexturePixels.data(), TexturePixels.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    void* pWriteData = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    const RadientTextureDataX           TextureData = MakeTextureData(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Unable to acquire read access to texture mip 0 data blob."};
        EXPECT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(pTexture, nullptr);
    EXPECT_EQ(ReadReleases, 0u);
    EXPECT_EQ(pManager->GetStats().PendingTextureLoads, 0u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);

    ASSERT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture), RADIENT_STATUS_PENDING);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pBlob->Resize(0), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerTest, RejectsPixelBlobsShorterThanActiveSpan)
{
    auto pThreadPool = CreateTestThreadPool(0);
    auto pManager    = CreateTextureManager();
    for (Uint64 Size : {Uint64{0}, Uint64{19}})
    {
        SCOPED_TRACE(Size);
        Uint32 ReadReleases = 0;
        auto   pBlob        = MakeTestMutableDataBlob(nullptr, Size, CountBlobReadReleases, &ReadReleases);
        ASSERT_NE(pBlob, nullptr);
        RadientTextureDataX TextureData = MakeTextureData(pBlob);
        TextureData.SetMip(0, pBlob, 0, 12);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        {
            TestingEnvironment::ErrorScope ExpectedErrors{"Invalid texture mip 0: ByteOffset (0) and required data size (20) exceed data blob size"};
            EXPECT_EQ(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture), RADIENT_STATUS_INVALID_ARGUMENT);
        }
        EXPECT_EQ(pTexture, nullptr);
        EXPECT_EQ(ReadReleases, 1u);
        EXPECT_EQ(pManager->GetStats().PendingTextureLoads, 0u);
        EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
        EXPECT_EQ(pBlob->Resize(20), RADIENT_STATUS_OK);
    }
    pThreadPool->StopThreads();
}

} // namespace
