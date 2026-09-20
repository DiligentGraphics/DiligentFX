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

#include "DataBlobImpl.hpp"
#include "GPUUploadManager.h"
#include "GPUTestingEnvironment.hpp"
#include "GraphicsAccessories.hpp"
#include "MemoryFileStream.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "RadientTypesX.hpp"
#include "TestingSwapChainBase.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "TextureLoader.h"

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

RefCntAutoPtr<ITextureLoader> MakeCompressedTexture(Uint32 Width, Uint32 Height, Uint32 MipLevels, Uint32 Seed = 0, TEXTURE_FORMAT SourceFormat = TEX_FORMAT_RGBA8_UNORM)
{
    const Uint32        PixelSize = GetTextureFormatAttribs(SourceFormat).NumComponents;
    std::vector<Uint32> Pixels((Width * Height * PixelSize + 3) / 4);
    auto*               pBytes = reinterpret_cast<Uint8*>(Pixels.data());
    for (Uint32 Row = 0; Row < Height; ++Row)
        for (Uint32 Byte = 0; Byte < Width * PixelSize; ++Byte)
            pBytes[Row * Width * PixelSize + Byte] = static_cast<Uint8>(Seed * 31 + Row * 7 + Byte * 3);

    TextureDesc Desc;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = Width;
    Desc.Height    = Height;
    Desc.MipLevels = 1;
    Desc.Format    = SourceFormat;
    TextureSubResData Subres{Pixels.data(), Width * PixelSize};
    TextureData       Data{&Subres, 1};
    TextureLoadInfo   LoadInfo;
    LoadInfo.MipLevels    = MipLevels;
    LoadInfo.GenerateMips = MipLevels > 1;
    LoadInfo.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_BC;
    RefCntAutoPtr<ITextureLoader> pLoader;
    CreateTextureLoaderFromTextureData(Desc, Data, false, &LoadInfo, &pLoader);
    return pLoader;
}

void VerifyTextureSubresourceReadback(IDeviceContext& Context, ITexture& Readback, MAP_FLAGS MapFlags, const TextureSubResData& Expected, Uint32 Mip = 0, Uint32 Slice = 0)
{
    MappedTextureSubresource Mapped;
    Context.MapTextureSubresource(&Readback, Mip, Slice, MAP_READ, MapFlags, nullptr, Mapped);
    ASSERT_NE(Mapped.pData, nullptr);
    const auto  MipProps   = GetMipLevelProperties(Readback.GetDesc(), Mip);
    const auto& FmtAttribs = GetTextureFormatAttribs(Readback.GetDesc().Format);
    for (Uint32 Row = 0; Row < MipProps.StorageHeight / FmtAttribs.BlockHeight; ++Row)
    {
        EXPECT_EQ(std::memcmp(static_cast<const Uint8*>(Mapped.pData) + Row * Mapped.Stride,
                              static_cast<const Uint8*>(Expected.pData) + Row * Expected.Stride,
                              static_cast<size_t>(MipProps.RowSize)),
                  0);
    }
    Context.UnmapTextureSubresource(&Readback, Mip, Slice);
}

void ExpectTextureDescription(const IRadientTextureAsset& Texture,
                              const RadientTextureData&   ExpectedData)
{
    const RadientTextureAssetDesc& Desc = Texture.GetDesc();
    EXPECT_EQ(Desc.Width, ExpectedData.Width);
    EXPECT_EQ(Desc.Height, ExpectedData.Height);
    EXPECT_EQ(Desc.Format, ExpectedData.Format);
    EXPECT_EQ(Desc.MipLevels, ExpectedData.GenerateMips ? ComputeMipLevelsCount(ExpectedData.Width, ExpectedData.Height) : ExpectedData.MipLevelCount);
}

void VerifyUploadedTextureData(IDeviceContext&           Context,
                               ISwapChain&               SwapChain,
                               IRadientTextureAsset&     Texture,
                               const RadientTextureData& ExpectedData)
{
    ExpectTextureDescription(Texture, ExpectedData);

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{&SwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    ITextureView* pTextureSRV = RadientTextureAssetManager::GetTextureSRV(&Texture);
    ASSERT_NE(pTextureSRV, nullptr);

    ITexture* pUploadedTexture = pTextureSRV->GetTexture();
    ASSERT_NE(pUploadedTexture, nullptr);

    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(&Texture, SamplingInfo));
    EXPECT_EQ(SamplingInfo.Width, ExpectedData.Width);
    EXPECT_EQ(SamplingInfo.Height, ExpectedData.Height);

    ITextureView* pBackBufferRTV = SwapChain.GetCurrentBackBufferRTV();
    ASSERT_NE(pBackBufferRTV, nullptr);

    ITexture* pBackBuffer = pBackBufferRTV->GetTexture();
    ASSERT_NE(pBackBuffer, nullptr);

    const SwapChainDesc& SCDesc = SwapChain.GetDesc();
    ASSERT_GE(SCDesc.Width, ExpectedData.Width);
    ASSERT_GE(SCDesc.Height, ExpectedData.Height);

    const size_t       ReferenceStride = static_cast<size_t>(SCDesc.Width) * TestTexturePixelSize;
    std::vector<Uint8> ReferenceData(ReferenceStride * SCDesc.Height, 0);
    const void*        pData = nullptr;
    ASSERT_EQ(ExpectedData.pMipLevels[0].pDataBlob->BeginRead(&pData), RADIENT_STATUS_OK);
    const Uint8* const pSrcPixels = static_cast<const Uint8*>(pData) + ExpectedData.pMipLevels[0].ByteOffset;
    const size_t       SrcStride  = static_cast<size_t>(ExpectedData.pMipLevels[0].Stride);
    for (Uint32 Row = 0; Row < ExpectedData.Height; ++Row)
    {
        std::memcpy(ReferenceData.data() + Row * ReferenceStride,
                    pSrcPixels + Row * SrcStride,
                    ExpectedData.Width * TestTexturePixelSize);
    }
    EXPECT_EQ(ExpectedData.pMipLevels[0].pDataBlob->EndRead(), RADIENT_STATUS_OK);
    pTestingSwapChain->SetReferenceData(ReferenceData.data(), ReferenceStride);

    ITextureView* ppRTVs[]      = {pBackBufferRTV};
    const float   ClearColor[4] = {};
    Context.SetRenderTargets(1, ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.ClearRenderTarget(pBackBufferRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    const TextureDesc& UploadedDesc = pUploadedTexture->GetDesc();
    EXPECT_EQ(SamplingInfo.MipLevels,
              std::min(UploadedDesc.MipLevels,
                       ComputeMipLevelsCount(ExpectedData.Width, ExpectedData.Height)));
    const Uint32 SrcX     = static_cast<Uint32>(SamplingInfo.UVScaleBias.z * static_cast<float>(UploadedDesc.Width) + 0.5f);
    const Uint32 SrcY     = static_cast<Uint32>(SamplingInfo.UVScaleBias.w * static_cast<float>(UploadedDesc.Height) + 0.5f);
    const Uint32 SrcSlice = static_cast<Uint32>(SamplingInfo.TextureSlice + 0.5f);

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
    ExpectTextureDescription(Texture, ExpectedData);

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{&SwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    ITextureView* pTextureSRV = RadientTextureAssetManager::GetTextureSRV(&Texture);
    ASSERT_NE(pTextureSRV, nullptr);

    ITexture* pUploadedTexture = pTextureSRV->GetTexture();
    ASSERT_NE(pUploadedTexture, nullptr);

    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(&Texture, SamplingInfo));
    EXPECT_FLOAT_EQ(SamplingInfo.UVScaleBias.x, 1.f);
    EXPECT_FLOAT_EQ(SamplingInfo.UVScaleBias.y, 1.f);
    EXPECT_FLOAT_EQ(SamplingInfo.UVScaleBias.z, 0.f);
    EXPECT_FLOAT_EQ(SamplingInfo.UVScaleBias.w, 0.f);
    EXPECT_FLOAT_EQ(SamplingInfo.TextureSlice, 0.f);
    EXPECT_EQ(SamplingInfo.Width, ExpectedData.Width);
    EXPECT_EQ(SamplingInfo.Height, ExpectedData.Height);

    const TextureDesc& UploadedDesc = pUploadedTexture->GetDesc();
    ASSERT_EQ(UploadedDesc.Type, RESOURCE_DIM_TEX_2D_ARRAY);
    ASSERT_EQ(UploadedDesc.Width, ExpectedData.Width);
    ASSERT_EQ(UploadedDesc.Height, ExpectedData.Height);
    ASSERT_EQ(UploadedDesc.GetArraySize(), 1u);
    EXPECT_EQ(SamplingInfo.MipLevels, UploadedDesc.MipLevels);

    ITextureView* pBackBufferRTV = SwapChain.GetCurrentBackBufferRTV();
    ASSERT_NE(pBackBufferRTV, nullptr);

    ITexture* pBackBuffer = pBackBufferRTV->GetTexture();
    ASSERT_NE(pBackBuffer, nullptr);

    const SwapChainDesc& SCDesc = SwapChain.GetDesc();
    ASSERT_GE(SCDesc.Width, ExpectedData.Width);
    ASSERT_GE(SCDesc.Height, ExpectedData.Height);

    const size_t       ReferenceStride = static_cast<size_t>(SCDesc.Width) * TestTexturePixelSize;
    std::vector<Uint8> ReferenceData(ReferenceStride * SCDesc.Height, 0);
    const void*        pData = nullptr;
    ASSERT_EQ(ExpectedData.pMipLevels[0].pDataBlob->BeginRead(&pData), RADIENT_STATUS_OK);
    const Uint8* const pSrcPixels = static_cast<const Uint8*>(pData) + ExpectedData.pMipLevels[0].ByteOffset;
    const size_t       SrcStride  = static_cast<size_t>(ExpectedData.pMipLevels[0].Stride);
    for (Uint32 Row = 0; Row < ExpectedData.Height; ++Row)
    {
        std::memcpy(ReferenceData.data() + Row * ReferenceStride,
                    pSrcPixels + Row * SrcStride,
                    ExpectedData.Width * TestTexturePixelSize);
    }
    EXPECT_EQ(ExpectedData.pMipLevels[0].pDataBlob->EndRead(), RADIENT_STATUS_OK);
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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(WaitForPendingCopyCommandEnqueueCallbacks(pManager));
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    ExpectTextureDescription(*pTexture, TextureData);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);

    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, DescriptionKeepsLogicalMipChainWhenAtlasHasFewerLevels)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreads{pThreadPool};

    constexpr Uint32                  AtlasSize         = 128;
    constexpr Uint32                  AtlasMipLevels    = 2;
    GLTF::ResourceManager::CreateInfo ResourceManagerCI = MakeResourceManagerCI(AtlasSize);
    ResourceManagerCI.DefaultAtlasDesc.Desc.MipLevels   = AtlasMipLevels;
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager =
        GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
    ASSERT_NE(pResourceManager, nullptr);
    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    auto pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const TestTextureParams Params{32, 16};
    const auto              pTextureDataBlob = MakeTextureDataBlob(0, Params);
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX           TextureData = MakeTextureData(pTextureDataBlob, Params);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(WaitForPendingCopyCommandEnqueueCallbacks(pManager));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);
    ExpectTextureDescription(*pTexture, TextureData);
    EXPECT_EQ(pTexture->GetDesc().MipLevels, 6u);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    ITextureView* pSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
    ASSERT_NE(pSRV, nullptr);
    EXPECT_EQ(pSRV->GetTexture()->GetDesc().Width, AtlasSize);
    EXPECT_EQ(pSRV->GetTexture()->GetDesc().Height, AtlasSize);
    EXPECT_EQ(pSRV->GetTexture()->GetDesc().MipLevels, AtlasMipLevels);
    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));
    EXPECT_EQ(SamplingInfo.MipLevels, AtlasMipLevels);
    ExpectTextureDescription(*pTexture, TextureData);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);
}

TEST(RadientTextureAssetManagerGPUTest, EncodedDDSCubePreservesFacesMipmapsAndReflection)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv     = GPUTestingEnvironment::GetInstance();
    auto*                              pDevice  = pEnv->GetDevice();
    auto*                              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    if (pDevice->GetDeviceInfo().Features.TextureCompressionBC != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "BC-compressed DDS textures are not supported by this device.";
    const auto& FormatInfo = pDevice->GetTextureFormatInfoExt(TEX_FORMAT_BC3_UNORM);
    if ((FormatInfo.Dimensions & RESOURCE_DIMENSION_SUPPORT_TEX_CUBE) == 0 ||
        (FormatInfo.BindFlags & BIND_SHADER_RESOURCE) == 0)
        GTEST_SKIP() << "BC3 cube textures are not supported by this device.";

    constexpr Uint32                                     FaceSize  = 8;
    constexpr Uint32                                     FaceCount = 6;
    constexpr Uint32                                     MipLevels = 3;
    std::array<RefCntAutoPtr<ITextureLoader>, FaceCount> FaceLoaders;
    std::array<TextureSubResData, FaceCount * MipLevels> Subresources;
    for (Uint32 Face = 0; Face < FaceCount; ++Face)
    {
        FaceLoaders[Face] = MakeCompressedTexture(FaceSize, FaceSize, MipLevels, Face);
        ASSERT_NE(FaceLoaders[Face], nullptr);
        ASSERT_EQ(FaceLoaders[Face]->GetTextureDesc().Format, TEX_FORMAT_BC3_UNORM);
        ASSERT_EQ(FaceLoaders[Face]->GetTextureDesc().MipLevels, MipLevels);
        for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
            Subresources[Face * MipLevels + Mip] = FaceLoaders[Face]->GetSubresourceData(Mip);
    }
    TextureDesc CubeDesc = FaceLoaders[0]->GetTextureDesc();
    CubeDesc.Type        = RESOURCE_DIM_TEX_CUBE;
    CubeDesc.ArraySize   = FaceCount;
    auto pDDSData        = DataBlobImpl::Create();
    auto pDDSStream      = MemoryFileStream::Create(pDDSData);
    ASSERT_TRUE(WriteDDSToStream(pDDSStream, CubeDesc,
                                 TextureData{Subresources.data(), static_cast<Uint32>(Subresources.size())}));
    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = pDDSData->GetSize();
    BlobCI.pData = pDDSData->GetConstDataPtr();
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);

    auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreads{pThreadPool};
    auto                pResourceManager = CreateTestResourceManager(pDevice, 128);
    ASSERT_NE(pResourceManager, nullptr);
    auto pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    auto pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetDataBlob(pBlob);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, LoadInfo, &pTexture)));
    ASSERT_NE(pTexture, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
    const auto& Desc = pTexture->GetDesc();
    EXPECT_EQ(Desc.Width, FaceSize);
    EXPECT_EQ(Desc.Height, FaceSize);
    EXPECT_EQ(Desc.Format, RADIENT_TEXTURE_FORMAT_BC3_UNORM);
    EXPECT_EQ(Desc.MipLevels, MipLevels);

    auto* pSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
    ASSERT_NE(pSRV, nullptr);
    EXPECT_EQ(pSRV->GetDesc().TextureDim, RESOURCE_DIM_TEX_CUBE);
    auto* pUploaded = pSRV->GetTexture();
    ASSERT_NE(pUploaded, nullptr);
    EXPECT_EQ(pUploaded->GetDesc().Type, RESOURCE_DIM_TEX_CUBE);
    EXPECT_EQ(pUploaded->GetDesc().GetArraySize(), FaceCount);
    EXPECT_EQ(pUploaded->GetDesc().MipLevels, MipLevels);

    if (pDevice->GetDeviceInfo().IsGLDevice())
        GTEST_SKIP() << "Cube upload and metadata checks completed; the OpenGL backend does not support compressed texture readback.";

    TextureDesc ReadbackDesc    = CubeDesc;
    ReadbackDesc.Name           = "DDS cube face readback";
    ReadbackDesc.Type           = RESOURCE_DIM_TEX_2D_ARRAY;
    ReadbackDesc.Usage          = USAGE_STAGING;
    ReadbackDesc.BindFlags      = BIND_NONE;
    ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> pReadback;
    pDevice->CreateTexture(ReadbackDesc, nullptr, &pReadback);
    ASSERT_NE(pReadback, nullptr);
    for (Uint32 Face = 0; Face < FaceCount; ++Face)
    {
        for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
        {
            CopyTextureAttribs Copy;
            Copy.pSrcTexture              = pUploaded;
            Copy.SrcSlice                 = Face;
            Copy.SrcMipLevel              = Mip;
            Copy.pDstTexture              = pReadback;
            Copy.DstSlice                 = Face;
            Copy.DstMipLevel              = Mip;
            Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            pContext->CopyTexture(Copy);
        }
    }
    pContext->WaitForIdle();

    const auto      DeviceType = pDevice->GetDeviceInfo().Type;
    const MAP_FLAGS MapFlags   = DeviceType == RENDER_DEVICE_TYPE_D3D11 || DeviceType == RENDER_DEVICE_TYPE_GL ?
        MAP_FLAG_NONE :
        MAP_FLAG_DO_NOT_WAIT;
    for (Uint32 Face = 0; Face < FaceCount; ++Face)
    {
        for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
        {
            SCOPED_TRACE(Face);
            SCOPED_TRACE(Mip);
            VerifyTextureSubresourceReadback(*pContext, *pReadback, MapFlags,
                                             FaceLoaders[Face]->GetSubresourceData(Mip), Mip, Face);
        }
    }
}

TEST(RadientTextureAssetManagerGPUTest, RawCompressedTextureUsesOnlyUploadedAtlasMip)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv     = GPUTestingEnvironment::GetInstance();
    auto*                              pDevice  = pEnv->GetDevice();
    auto*                              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    if (pDevice->GetDeviceInfo().Features.TextureCompressionBC != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "BC textures are not supported by this device.";

    constexpr Uint32 Width          = 32;
    constexpr Uint32 Height         = 16;
    constexpr Uint32 AtlasSize      = 128;
    constexpr Uint32 AtlasMipLevels = 4;
    for (TEXTURE_FORMAT SourceFormat : {TEX_FORMAT_R8_UNORM, TEX_FORMAT_RGBA8_UNORM})
    {
        SCOPED_TRACE(SourceFormat);
        auto pLoader = MakeCompressedTexture(Width, Height, 1, 0, SourceFormat);
        ASSERT_NE(pLoader, nullptr);
        const auto& CompressedDesc = pLoader->GetTextureDesc();
        const auto& FormatInfo     = pDevice->GetTextureFormatInfoExt(CompressedDesc.Format);
        if ((FormatInfo.Dimensions & RESOURCE_DIMENSION_SUPPORT_TEX_2D_ARRAY) == 0 ||
            (FormatInfo.BindFlags & BIND_SHADER_RESOURCE) == 0)
            GTEST_SKIP() << "Compressed texture arrays are not supported by this device.";
        const auto&  Expected   = pLoader->GetSubresourceData(0);
        const auto   MipProps   = GetMipLevelProperties(CompressedDesc, 0);
        const auto&  FmtAttribs = GetTextureFormatAttribs(CompressedDesc.Format);
        const Uint32 Rows       = MipProps.StorageHeight / FmtAttribs.BlockHeight;
        const Uint32 Stride     = static_cast<Uint32>(MipProps.RowSize) + 3;
        // Deliberately unaligned pointer and padded block rows. No padding is
        // needed after the final row, and neither affects the uploaded blocks.
        std::vector<Uint8> PaddedData(static_cast<size_t>(1 + (Rows - 1) * Stride + MipProps.RowSize), 0xCD);
        for (Uint32 Row = 0; Row < Rows; ++Row)
            std::memcpy(PaddedData.data() + 1 + Row * Stride,
                        static_cast<const Uint8*>(Expected.pData) + Row * Expected.Stride,
                        static_cast<size_t>(MipProps.RowSize));
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size  = PaddedData.size() - 1;
        BlobCI.pData = PaddedData.data() + 1;
        RefCntAutoPtr<IRadientDataBlob> pBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);

        auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
        ASSERT_NE(pThreadPool, nullptr);
        ThreadPoolStopGuard StopThreads{pThreadPool};
        auto                ResourceManagerCI             = MakeResourceManagerCI(AtlasSize);
        ResourceManagerCI.DefaultAtlasDesc.Desc.MipLevels = AtlasMipLevels;
        auto pResourceManager                             = GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
        ASSERT_NE(pResourceManager, nullptr);
        auto pUploadManager = CreateTestUploadManager(pDevice, pContext);
        ASSERT_NE(pUploadManager, nullptr);
        auto pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
        ASSERT_NE(pManager, nullptr);

        const auto Format = SourceFormat == TEX_FORMAT_R8_UNORM ?
            RADIENT_TEXTURE_FORMAT_BC4_UNORM :
            RADIENT_TEXTURE_FORMAT_BC3_UNORM;

        RadientTextureDataX TextureData{Width, Height, Format};
        TextureData.AddMip(pBlob, 0, Stride);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);
        ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
        ProcessUploads(*pUploadManager, *pContext, *pTexture);
        ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture->GetDesc().Width, Width);
        EXPECT_EQ(pTexture->GetDesc().Height, Height);
        EXPECT_EQ(pTexture->GetDesc().Format, Format);
        EXPECT_EQ(pTexture->GetDesc().MipLevels, 1u);
        RadientTextureSamplingInfo SamplingInfo;
        ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));
        EXPECT_EQ(SamplingInfo.MipLevels, 1u);
        auto* pSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
        ASSERT_NE(pSRV, nullptr);
        auto* pUploaded = pSRV->GetTexture();
        ASSERT_NE(pUploaded, nullptr);
        EXPECT_EQ(pUploaded->GetDesc().Width, AtlasSize);
        EXPECT_EQ(pUploaded->GetDesc().Height, AtlasSize);
        EXPECT_EQ(pUploaded->GetDesc().MipLevels, AtlasMipLevels);

        if (pDevice->GetDeviceInfo().IsGLDevice())
            continue; // Check both BC formats before reporting unavailable readback below.

        TextureDesc ReadbackDesc    = CompressedDesc;
        ReadbackDesc.Usage          = USAGE_STAGING;
        ReadbackDesc.BindFlags      = BIND_NONE;
        ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> pReadback;
        pDevice->CreateTexture(ReadbackDesc, nullptr, &pReadback);
        ASSERT_NE(pReadback, nullptr);
        const Uint32       X = static_cast<Uint32>(SamplingInfo.UVScaleBias.z * AtlasSize + 0.5f);
        const Uint32       Y = static_cast<Uint32>(SamplingInfo.UVScaleBias.w * AtlasSize + 0.5f);
        const Box          SourceBox{X, X + Width, Y, Y + Height};
        CopyTextureAttribs Copy;
        Copy.pSrcTexture              = pUploaded;
        Copy.SrcSlice                 = static_cast<Uint32>(SamplingInfo.TextureSlice);
        Copy.pSrcBox                  = &SourceBox;
        Copy.pDstTexture              = pReadback;
        Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        pContext->CopyTexture(Copy);
        pContext->WaitForIdle();
        const auto      DeviceType = pDevice->GetDeviceInfo().Type;
        const MAP_FLAGS MapFlags   = DeviceType == RENDER_DEVICE_TYPE_D3D11 || DeviceType == RENDER_DEVICE_TYPE_GL ?
            MAP_FLAG_NONE :
            MAP_FLAG_DO_NOT_WAIT;
        VerifyTextureSubresourceReadback(*pContext, *pReadback, MapFlags, Expected);
    }
    if (pDevice->GetDeviceInfo().IsGLDevice())
        GTEST_SKIP() << "BC3 and BC4 upload and metadata checks completed; the OpenGL backend does not support compressed texture readback.";
}

TEST(RadientTextureAssetManagerGPUTest, RawCompressedMipChainPreservesSuppliedLevels)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv     = GPUTestingEnvironment::GetInstance();
    auto*                              pDevice  = pEnv->GetDevice();
    auto*                              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    if (pDevice->GetDeviceInfo().Features.TextureCompressionBC != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "BC textures are not supported by this device.";
    const auto& FormatInfo = pDevice->GetTextureFormatInfoExt(TEX_FORMAT_BC3_UNORM);
    if ((FormatInfo.Dimensions & RESOURCE_DIMENSION_SUPPORT_TEX_2D_ARRAY) == 0 ||
        (FormatInfo.BindFlags & BIND_SHADER_RESOURCE) == 0)
        GTEST_SKIP() << "BC3 texture arrays are not supported by this device.";

    constexpr Uint32 Width     = 32;
    constexpr Uint32 Height    = 16;
    constexpr Uint32 MipLevels = 6;
    auto             pLoader   = MakeCompressedTexture(Width, Height, MipLevels);
    ASSERT_NE(pLoader, nullptr);
    ASSERT_EQ(pLoader->GetTextureDesc().MipLevels, MipLevels);
    std::array<RefCntAutoPtr<IRadientMutableDataBlob>, MipLevels> Blobs;
    RadientTextureDataX                                           TextureData{Width, Height, RADIENT_TEXTURE_FORMAT_BC3_UNORM};
    TextureData.SetGenerateMips(False);
    for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
    {
        const auto                MipProps = GetMipLevelProperties(pLoader->GetTextureDesc(), Mip);
        const auto&               Expected = pLoader->GetSubresourceData(Mip);
        const Uint32              Rows     = MipProps.StorageHeight / 4;
        const Uint32              Stride   = static_cast<Uint32>(MipProps.RowSize) + 3;
        const Uint64              Offset   = Mip * 3 + 1;
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size = Offset + Uint64{Rows - 1} * Stride + MipProps.RowSize;
        ASSERT_EQ(CreateRadientMutableDataBlob(BlobCI, &Blobs[Mip]), RADIENT_STATUS_OK);
        void* pData = nullptr;
        ASSERT_EQ(Blobs[Mip]->BeginWrite(&pData), RADIENT_STATUS_OK);
        std::memset(pData, 0xCD, static_cast<size_t>(BlobCI.Size));
        for (Uint32 Row = 0; Row < Rows; ++Row)
            std::memcpy(static_cast<Uint8*>(pData) + Offset + Row * Stride,
                        static_cast<const Uint8*>(Expected.pData) + Row * Expected.Stride,
                        static_cast<size_t>(MipProps.RowSize));
        ASSERT_EQ(Blobs[Mip]->EndWrite(), RADIENT_STATUS_OK);
        TextureData.AddMip(Blobs[Mip], Offset, Stride);
    }

    for (bool UseAtlas : {true, false})
    {
        SCOPED_TRACE(UseAtlas);
        auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
        ASSERT_NE(pThreadPool, nullptr);
        ThreadPoolStopGuard StopThreads{pThreadPool};
        // The small atlas cannot fit mip zero, forcing standalone storage.
        const Uint32 AtlasSize                            = UseAtlas ? 128u : 16u;
        auto         ResourceManagerCI                    = MakeResourceManagerCI(AtlasSize);
        ResourceManagerCI.DefaultAtlasDesc.Desc.MipLevels = UseAtlas ? MipLevels : 1;
        auto pResourceManager                             = GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
        ASSERT_NE(pResourceManager, nullptr);
        auto pUploadManager = CreateTestUploadManager(pDevice, pContext);
        ASSERT_NE(pUploadManager, nullptr);
        auto pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
        ASSERT_NE(pManager, nullptr);
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);
        ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
        ProcessUploads(*pUploadManager, *pContext, *pTexture);
        ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture->GetDesc().Width, Width);
        EXPECT_EQ(pTexture->GetDesc().Height, Height);
        EXPECT_EQ(pTexture->GetDesc().Format, RADIENT_TEXTURE_FORMAT_BC3_UNORM);
        EXPECT_EQ(pTexture->GetDesc().MipLevels, MipLevels);
        RadientTextureSamplingInfo SamplingInfo;
        ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));
        // Mip 2 is 8x4; subsequent levels contain partial blocks that cannot
        // be copied into the interior of an atlas. Standalone storage keeps them.
        const Uint32 UploadedMipLevels = UseAtlas ? 3u : MipLevels;
        EXPECT_EQ(SamplingInfo.MipLevels, UploadedMipLevels);
        auto* pSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
        ASSERT_NE(pSRV, nullptr);
        auto* pUploaded = pSRV->GetTexture();
        ASSERT_NE(pUploaded, nullptr);
        const auto& UploadedDesc = pUploaded->GetDesc();
        EXPECT_EQ(UploadedDesc.Width, UseAtlas ? AtlasSize : Width);
        EXPECT_EQ(UploadedDesc.Height, UseAtlas ? AtlasSize : Height);
        EXPECT_EQ(UploadedDesc.MipLevels, MipLevels);
        if (pDevice->GetDeviceInfo().IsGLDevice())
            continue; // Verify both storage paths before reporting unavailable readback.

        TextureDesc ReadbackDesc    = pLoader->GetTextureDesc();
        ReadbackDesc.MipLevels      = UploadedMipLevels;
        ReadbackDesc.Usage          = USAGE_STAGING;
        ReadbackDesc.BindFlags      = BIND_NONE;
        ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> pReadback;
        pDevice->CreateTexture(ReadbackDesc, nullptr, &pReadback);
        ASSERT_NE(pReadback, nullptr);
        const Uint32 X = static_cast<Uint32>(SamplingInfo.UVScaleBias.z * UploadedDesc.Width + 0.5f);
        const Uint32 Y = static_cast<Uint32>(SamplingInfo.UVScaleBias.w * UploadedDesc.Height + 0.5f);
        for (Uint32 Mip = 0; Mip < UploadedMipLevels; ++Mip)
        {
            const auto MipProps = GetMipLevelProperties(ReadbackDesc, Mip);
            const Box  SourceBox{
                X >> Mip,
                (X >> Mip) + MipProps.LogicalWidth,
                Y >> Mip,
                (Y >> Mip) + MipProps.LogicalHeight,
            };

            CopyTextureAttribs Copy;
            Copy.pSrcTexture = pUploaded;
            Copy.SrcSlice    = static_cast<Uint32>(SamplingInfo.TextureSlice);
            Copy.SrcMipLevel = Mip;
            // A null box copies the entire standalone mip, including its partial blocks.
            Copy.pSrcBox                  = UseAtlas ? &SourceBox : nullptr;
            Copy.pDstTexture              = pReadback;
            Copy.DstMipLevel              = Mip;
            Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            pContext->CopyTexture(Copy);
        }
        pContext->WaitForIdle();
        const MAP_FLAGS MapFlags = pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11 ?
            MAP_FLAG_NONE :
            MAP_FLAG_DO_NOT_WAIT;
        for (Uint32 Mip = 0; Mip < UploadedMipLevels; ++Mip)
        {
            SCOPED_TRACE(Mip);
            VerifyTextureSubresourceReadback(*pContext, *pReadback, MapFlags,
                                             pLoader->GetSubresourceData(Mip), Mip);
        }
    }
    if (pDevice->GetDeviceInfo().IsGLDevice())
        GTEST_SKIP() << "Atlas and standalone mip-chain uploads and metadata checks completed; the OpenGL backend does not support compressed texture readback.";
}

TEST(RadientTextureAssetManagerGPUTest, GeneratesMipTailFromLastSuppliedLevel)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv     = GPUTestingEnvironment::GetInstance();
    auto*                              pDevice  = pEnv->GetDevice();
    auto*                              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    constexpr Uint32                                                      Width             = 16;
    constexpr Uint32                                                      Height            = 8;
    constexpr Uint32                                                      SuppliedMipLevels = 2;
    constexpr Uint32                                                      MipLevels         = 5;
    const std::array<std::array<Uint8, 4>, SuppliedMipLevels>             Colors{{{7, 31, 63, 255}, {199, 101, 53, 255}}};
    std::array<RefCntAutoPtr<IRadientMutableDataBlob>, SuppliedMipLevels> Blobs;
    RadientTextureDataX                                                   TextureData{Width, Height, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    TextureData.SetGenerateMips(True);
    for (Uint32 Mip = 0; Mip < SuppliedMipLevels; ++Mip)
    {
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size = Uint64{Width >> Mip} * (Height >> Mip) * 4;
        ASSERT_EQ(CreateRadientMutableDataBlob(BlobCI, &Blobs[Mip]), RADIENT_STATUS_OK);
        void* pData = nullptr;
        ASSERT_EQ(Blobs[Mip]->BeginWrite(&pData), RADIENT_STATUS_OK);
        for (size_t Byte = 0; Byte < BlobCI.Size; ++Byte)
            static_cast<Uint8*>(pData)[Byte] = Colors[Mip][Byte % 4];
        ASSERT_EQ(Blobs[Mip]->EndWrite(), RADIENT_STATUS_OK);
        TextureData.AddMip(Blobs[Mip]);
    }
    auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreads{pThreadPool};
    auto                pResourceManager = CreateTestResourceManager(pDevice, 4);
    ASSERT_NE(pResourceManager, nullptr);
    auto pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    auto pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    ASSERT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pTexture);
    ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture->GetDesc().MipLevels, MipLevels);
    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));
    EXPECT_EQ(SamplingInfo.MipLevels, MipLevels);
    auto* pSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
    ASSERT_NE(pSRV, nullptr);
    auto* pUploaded = pSRV->GetTexture();
    ASSERT_NE(pUploaded, nullptr);
    EXPECT_EQ(pUploaded->GetDesc().Width, Width);
    EXPECT_EQ(pUploaded->GetDesc().Height, Height);
    EXPECT_EQ(pUploaded->GetDesc().MipLevels, MipLevels);
    TextureDesc ReadbackDesc    = pUploaded->GetDesc();
    ReadbackDesc.Format         = TEX_FORMAT_RGBA8_UNORM;
    ReadbackDesc.Usage          = USAGE_STAGING;
    ReadbackDesc.BindFlags      = BIND_NONE;
    ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> pReadback;
    pDevice->CreateTexture(ReadbackDesc, nullptr, &pReadback);
    ASSERT_NE(pReadback, nullptr);
    for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
    {
        CopyTextureAttribs Copy;
        Copy.pSrcTexture              = pUploaded;
        Copy.SrcMipLevel              = Mip;
        Copy.pDstTexture              = pReadback;
        Copy.DstMipLevel              = Mip;
        Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        pContext->CopyTexture(Copy);
    }
    pContext->WaitForIdle();
    const auto&     DeviceInfo = pDevice->GetDeviceInfo();
    const MAP_FLAGS MapFlags   = DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D11 || DeviceInfo.IsGLDevice() ?
        MAP_FLAG_NONE :
        MAP_FLAG_DO_NOT_WAIT;
    for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
    {
        SCOPED_TRACE(Mip);
        const auto         MipProps = GetMipLevelProperties(ReadbackDesc, Mip);
        std::vector<Uint8> ExpectedPixels(static_cast<size_t>(MipProps.RowSize) * MipProps.LogicalHeight);
        // The supplied levels intentionally differ. Every generated level must
        // keep mip 1's color, proving generation starts from that supplied level.
        const auto& Color = Colors[Mip == 0 ? 0 : 1];
        for (size_t Byte = 0; Byte < ExpectedPixels.size(); ++Byte)
            ExpectedPixels[Byte] = Color[Byte % 4];
        VerifyTextureSubresourceReadback(*pContext, *pReadback, MapFlags,
                                         TextureSubResData{ExpectedPixels.data(), MipProps.RowSize}, Mip);
    }
}

TEST(RadientTextureAssetManagerGPUTest, LinearAndSRGBViewsShareTypelessAtlas)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    if (pDevice->GetDeviceInfo().Features.TextureSubresourceViews != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "Typed linear and sRGB texture views are not supported by this device.";

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pLinearDataBlob = MakeTextureDataBlob(0);
    ASSERT_NE(pLinearDataBlob, nullptr);
    RadientTextureDataX                   LinearData    = MakeTextureData(pLinearDataBlob);
    const RefCntAutoPtr<IRadientDataBlob> pSRGBDataBlob = MakeTextureDataBlob(1);
    ASSERT_NE(pSRGBDataBlob, nullptr);
    RadientTextureDataX SRGBData = MakeTextureData(pSRGBDataBlob);
    SRGBData.SetFormat(RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB);

    RefCntAutoPtr<IRadientTextureAsset> pLinearTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(LinearData), &pLinearTexture)));
    ASSERT_NE(pLinearTexture, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pSRGBTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(SRGBData), &pSRGBTexture)));
    ASSERT_NE(pSRGBTexture, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pLinearTexture);
    ProcessUploads(*pUploadManager, *pContext, *pSRGBTexture);

    ITextureView* const pLinearSRV = RadientTextureAssetManager::GetTextureSRV(pLinearTexture, RadientTextureViewType::Linear);
    ITextureView* const pSRGBSRV   = RadientTextureAssetManager::GetTextureSRV(pLinearTexture, RadientTextureViewType::SRGB);
    ASSERT_NE(pLinearSRV, nullptr);
    ASSERT_NE(pSRGBSRV, nullptr);
    EXPECT_EQ(pLinearSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM);
    EXPECT_EQ(pSRGBSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM_SRGB);
    EXPECT_EQ(pLinearSRV->GetDesc().TextureDim, RESOURCE_DIM_TEX_2D_ARRAY);
    EXPECT_EQ(pSRGBSRV->GetDesc().TextureDim, RESOURCE_DIM_TEX_2D_ARRAY);
    EXPECT_EQ(pLinearSRV->GetTexture(), pSRGBSRV->GetTexture());

    ITextureView* const pOtherLinearSRV = RadientTextureAssetManager::GetTextureSRV(pSRGBTexture, RadientTextureViewType::Linear);
    ITextureView* const pOtherSRGBSRV   = RadientTextureAssetManager::GetTextureSRV(pSRGBTexture, RadientTextureViewType::SRGB);
    ASSERT_NE(pOtherLinearSRV, nullptr);
    ASSERT_NE(pOtherSRGBSRV, nullptr);
    EXPECT_EQ(pOtherLinearSRV, pLinearSRV);
    EXPECT_EQ(pOtherSRGBSRV, pSRGBSRV);
    EXPECT_EQ(pOtherLinearSRV->GetTexture(), pLinearSRV->GetTexture());
    EXPECT_EQ(pOtherSRGBSRV->GetTexture(), pLinearSRV->GetTexture());
    EXPECT_EQ(pLinearSRV->GetTexture()->GetDesc().Format, TEX_FORMAT_RGBA8_TYPELESS);
    ExpectTextureDescription(*pLinearTexture, LinearData);
    ExpectTextureDescription(*pSRGBTexture, SRGBData);

    const RadientTextureBindingIdentity LinearBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pLinearTexture, RadientTextureViewType::Linear);
    const RadientTextureBindingIdentity SRGBBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pLinearTexture, RadientTextureViewType::SRGB);
    ASSERT_TRUE(LinearBinding);
    ASSERT_TRUE(SRGBBinding);
    EXPECT_EQ(LinearBinding.StandaloneResourceId, 0);
    EXPECT_EQ(SRGBBinding.StandaloneResourceId, 0);
    EXPECT_EQ(LinearBinding.ViewFormat, TEX_FORMAT_RGBA8_UNORM);
    EXPECT_EQ(SRGBBinding.ViewFormat, TEX_FORMAT_RGBA8_UNORM_SRGB);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureBindingIdentity(pSRGBTexture, RadientTextureViewType::Linear),
              LinearBinding);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureBindingIdentity(pSRGBTexture, RadientTextureViewType::SRGB),
              SRGBBinding);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, SRGBViewRequestUsesNativeFormatWhenSRGBIsUnavailable)
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

    static constexpr Uint32 Width          = 4;
    static constexpr Uint32 Height         = 4;
    static constexpr Uint32 ComponentCount = 4;

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size = Width * Height * ComponentCount * sizeof(float);
    RefCntAutoPtr<IRadientMutableDataBlob> pTextureDataBlob;
    ASSERT_EQ(CreateRadientMutableDataBlob(BlobCI, &pTextureDataBlob), RADIENT_STATUS_OK);
    ASSERT_NE(pTextureDataBlob, nullptr);

    void* pTexturePixels = nullptr;
    ASSERT_EQ(pTextureDataBlob->BeginWrite(&pTexturePixels), RADIENT_STATUS_OK);
    static constexpr float ComponentValue = 1.f;
    for (Uint32 i = 0; i < Width * Height * ComponentCount; ++i)
        std::memcpy(static_cast<Uint8*>(pTexturePixels) + i * sizeof(float), &ComponentValue, sizeof(ComponentValue));
    ASSERT_EQ(pTextureDataBlob->EndWrite(), RADIENT_STATUS_OK);
    RadientTextureDataX TextureData{Width, Height, RADIENT_TEXTURE_FORMAT_RGBA32_FLOAT};
    TextureData.AddMip(pTextureDataBlob, 0, Width * ComponentCount * static_cast<Uint32>(sizeof(float)));

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pTexture);

    ITextureView* const pLinearSRV = RadientTextureAssetManager::GetTextureSRV(pTexture, RadientTextureViewType::Linear);
    ITextureView* const pSRGBSRV   = RadientTextureAssetManager::GetTextureSRV(pTexture, RadientTextureViewType::SRGB);
    ASSERT_NE(pLinearSRV, nullptr);
    ASSERT_NE(pSRGBSRV, nullptr);
    EXPECT_EQ(pLinearSRV, pSRGBSRV);
    EXPECT_EQ(pLinearSRV->GetDesc().Format, TEX_FORMAT_RGBA32_FLOAT);
    ExpectTextureDescription(*pTexture, TextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, TypedViewsRefreshAfterAtlasResize)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    if (pDevice->GetDeviceInfo().Features.TextureSubresourceViews != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "Typed linear and sRGB texture views are not supported by this device.";

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const TestTextureParams              Params;
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice, Params.Width);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pManager, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pFirstDataBlob = MakeTextureDataBlob(0, Params);
    ASSERT_NE(pFirstDataBlob, nullptr);
    const RadientTextureDataX FirstData = MakeTextureData(pFirstDataBlob, Params);

    RefCntAutoPtr<IRadientTextureAsset> pFirstTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(FirstData), &pFirstTexture)));
    ASSERT_NE(pFirstTexture, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pFirstTexture);
    ExpectTextureDescription(*pFirstTexture, FirstData);

    // Keep the original views alive so pointer comparison remains reliable
    // after the atlas replaces its backing texture.
    RefCntAutoPtr<ITextureView> pOldLinearSRV{RadientTextureAssetManager::GetTextureSRV(pFirstTexture, RadientTextureViewType::Linear)};
    RefCntAutoPtr<ITextureView> pOldSRGBSRV{RadientTextureAssetManager::GetTextureSRV(pFirstTexture, RadientTextureViewType::SRGB)};
    ASSERT_NE(pOldLinearSRV, nullptr);
    ASSERT_NE(pOldSRGBSRV, nullptr);
    ITexture* const                     pOldAtlasTexture = pOldLinearSRV->GetTexture();
    const RadientTextureBindingIdentity OldLinearBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pFirstTexture, RadientTextureViewType::Linear);
    const RadientTextureBindingIdentity OldSRGBBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pFirstTexture, RadientTextureViewType::SRGB);
    ASSERT_TRUE(OldLinearBinding);
    ASSERT_TRUE(OldSRGBBinding);

    // A full-atlas allocation consumes the first slice. This second texture
    // requires another slice and forces the dynamic array to grow.
    const RefCntAutoPtr<IRadientDataBlob> pSecondDataBlob = MakeTextureDataBlob(1, Params);
    ASSERT_NE(pSecondDataBlob, nullptr);
    const RadientTextureDataX SecondData = MakeTextureData(pSecondDataBlob, Params);

    RefCntAutoPtr<IRadientTextureAsset> pSecondTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(SecondData), &pSecondTexture)));
    ASSERT_NE(pSecondTexture, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pSecondTexture);

    ITextureView* const pNewLinearSRV = RadientTextureAssetManager::GetTextureSRV(pFirstTexture, RadientTextureViewType::Linear);
    ITextureView* const pNewSRGBSRV   = RadientTextureAssetManager::GetTextureSRV(pFirstTexture, RadientTextureViewType::SRGB);
    ASSERT_NE(pNewLinearSRV, nullptr);
    ASSERT_NE(pNewSRGBSRV, nullptr);
    EXPECT_NE(pNewLinearSRV->GetTexture(), pOldAtlasTexture);
    ExpectTextureDescription(*pFirstTexture, FirstData);
    ExpectTextureDescription(*pSecondTexture, SecondData);
    EXPECT_EQ(pNewLinearSRV->GetTexture(), pNewSRGBSRV->GetTexture());
    EXPECT_EQ(pNewLinearSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM);
    EXPECT_EQ(pNewSRGBSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM_SRGB);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureBindingIdentity(pFirstTexture, RadientTextureViewType::Linear),
              OldLinearBinding);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureBindingIdentity(pFirstTexture, RadientTextureViewType::SRGB),
              OldSRGBBinding);

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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice, TextureData.Get().Width / 2);
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
    ITextureView* const pLinearSRV = RadientTextureAssetManager::GetTextureSRV(pTexture, RadientTextureViewType::Linear);
    ITextureView* const pSRGBSRV   = RadientTextureAssetManager::GetTextureSRV(pTexture, RadientTextureViewType::SRGB);
    ASSERT_NE(pLinearSRV, nullptr);
    ASSERT_NE(pSRGBSRV, nullptr);
    EXPECT_EQ(pLinearSRV->GetTexture(), pSRGBSRV->GetTexture());
    EXPECT_EQ(pLinearSRV->GetTexture()->GetDesc().Format, TEX_FORMAT_RGBA8_TYPELESS);
    EXPECT_EQ(pLinearSRV->GetDesc().TextureDim, RESOURCE_DIM_TEX_2D_ARRAY);
    EXPECT_EQ(pSRGBSRV->GetDesc().TextureDim, RESOURCE_DIM_TEX_2D_ARRAY);
    EXPECT_EQ(pLinearSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM);
    EXPECT_EQ(pSRGBSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM_SRGB);
    const RadientTextureBindingIdentity LinearBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pTexture, RadientTextureViewType::Linear);
    const RadientTextureBindingIdentity SRGBBinding =
        RadientTextureAssetManager::GetTextureBindingIdentity(pTexture, RadientTextureViewType::SRGB);
    ASSERT_TRUE(LinearBinding);
    ASSERT_TRUE(SRGBBinding);
    EXPECT_EQ(LinearBinding.StandaloneResourceId, pLinearSRV->GetTexture()->GetUniqueID());
    EXPECT_EQ(SRGBBinding.StandaloneResourceId, LinearBinding.StandaloneResourceId);
    EXPECT_EQ(LinearBinding.ViewFormat, TEX_FORMAT_RGBA8_UNORM);
    EXPECT_EQ(SRGBBinding.ViewFormat, TEX_FORMAT_RGBA8_UNORM_SRGB);
    VerifyUploadedStandaloneTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture, TextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, UploadsTextureAboveAtlasMipLevel0SizeAsStandaloneTexture)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    GLTF::ResourceManager::CreateInfo ResourceManagerCI = MakeResourceManagerCI();
    ResourceManagerCI.DefaultAtlasDesc.Desc.MipLevels   = 0;
    ResourceManagerCI.DefaultAtlasMipLevel0Size         = 32u * 32u * TestTexturePixelSize;

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager =
        GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
    ASSERT_NE(pResourceManager, nullptr);

    const TextureDesc AtlasDesc = pResourceManager->GetAtlasDesc(TEX_FORMAT_RGBA8_TYPELESS);
    ASSERT_EQ(AtlasDesc.Width, 32u);
    ASSERT_EQ(AtlasDesc.Height, 32u);

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
    ITextureView* const pTextureSRV = RadientTextureAssetManager::GetTextureSRV(pTexture);
    ASSERT_NE(pTextureSRV, nullptr);

    const RadientTextureBindingIdentity BindingIdentity =
        RadientTextureAssetManager::GetTextureBindingIdentity(pTexture, RadientTextureViewType::Linear);
    ASSERT_TRUE(BindingIdentity);
    EXPECT_EQ(BindingIdentity.StandaloneResourceId, pTextureSRV->GetTexture()->GetUniqueID());

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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

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
    ExpectTextureDescription(*pTexture0, TextureData);
    ExpectTextureDescription(*pTexture1, TextureData);

    ProcessUploads(*pUploadManager, *pContext, *pTexture0);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture0), nullptr);
    EXPECT_NE(RadientTextureAssetManager::GetTextureSRV(pTexture1), nullptr);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture0, TextureData);

    pThreadPool->StopThreads();
}

TEST(RadientTextureAssetManagerGPUTest, DifferentPayloadsWithSameAssetURIUseSeparateAtlasAllocations)
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

    const RefCntAutoPtr<IRadientDataBlob> pTextureData0Blob = MakeTextureDataBlob(0);
    ASSERT_NE(pTextureData0Blob, nullptr);
    const RadientTextureDataX             TextureData0      = MakeTextureData(pTextureData0Blob);
    const RefCntAutoPtr<IRadientDataBlob> pTextureData1Blob = MakeTextureDataBlob(1);
    ASSERT_NE(pTextureData1Blob, nullptr);
    const RadientTextureDataX TextureData1 = MakeTextureData(pTextureData1Blob);

    RadientTextureLoadInfoX LoadInfo0 = MakeTextureDataLoadInfo(TextureData0);
    RadientTextureLoadInfoX LoadInfo1 = MakeTextureDataLoadInfo(TextureData1);
    // The handle URI is intentionally the same, but the source data and payload keys differ.
    LoadInfo0.SetURI("textures/shared-name.png");
    LoadInfo1.SetURI("textures/shared-name.png");

    RefCntAutoPtr<IRadientTextureAsset> pTexture0;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, LoadInfo0, &pTexture0)));
    ASSERT_NE(pTexture0, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pTexture1;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, LoadInfo1, &pTexture1)));
    ASSERT_NE(pTexture1, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pTexture0),
              RadientTextureAssetManager::GetTexturePayload(pTexture1));

    ProcessUploads(*pUploadManager, *pContext, *pTexture0);
    ProcessUploads(*pUploadManager, *pContext, *pTexture1);

    // Each asset must retain its own atlas region after both uploads complete.
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture0, TextureData0);
    VerifyUploadedTextureData(*pContext, *pEnv->GetSwapChain(), *pTexture1, TextureData1);

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

    std::array<RefCntAutoPtr<IRadientDataBlob>, NumTextures>     TextureBlobs;
    std::array<RadientTextureDataX, NumTextures>                 TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures> Textures;
    std::array<RADIENT_STATUS, NumTextures>                      LoadStatuses{};

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TextureBlobs[i] = MakeTextureDataBlob(static_cast<Uint32>(i + 1));
        ASSERT_NE(TextureBlobs[i], nullptr);
        TextureData[i] = MakeTextureData(TextureBlobs[i]);
    }

    Threading::Signal        StartSignal;
    std::vector<std::thread> Threads;
    Threads.reserve(NumTextures);
    for (size_t i = 0; i < NumTextures; ++i)
    {
        Threads.emplace_back(
            [pThreadPool, pManager, &TextureData, &Textures, &LoadStatuses, &StartSignal, i]() {
                StartSignal.Wait();

                RadientTextureLoadInfoX LoadInfo;
                LoadInfo.SetTextureData(TextureData[i]).SetSRGB(False);
                LoadStatuses[i] = pManager->LoadTexture(*pThreadPool, LoadInfo, &Textures[i]);
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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

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

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_CANCELLED);
    ASSERT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    ExpectTextureDescription(*pTexture, TextureData);
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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    const bool PendingCopyCommandEnqueueCallbacks = WaitForPendingCopyCommandEnqueueCallbacks(pManager);
    ASSERT_TRUE(PendingCopyCommandEnqueueCallbacks);

    // The source has loaded, but upload callbacks have not reported whether
    // they could enqueue copy commands, so GPU resource status remains pending.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);
    ExpectTextureDescription(*pTexture, TextureData);

    pUploadManager->Stop(pContext);
    pThreadPool->StopThreads();

    EXPECT_TRUE(IsTextureManagerIdle(pManager->GetStats()));

    // Stop() drains the pending callbacks with no upload context; since no
    // copy command was enqueued, GPU resource creation must fail.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_FAILED);
    EXPECT_EQ(RadientTextureAssetManager::GetTextureSRV(pTexture), nullptr);
    ExpectTextureDescription(*pTexture, TextureData);
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

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

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
