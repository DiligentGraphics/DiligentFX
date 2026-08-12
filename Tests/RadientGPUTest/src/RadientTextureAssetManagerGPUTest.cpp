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
#include "GraphicsAccessories.hpp"
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

    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(&Texture, SamplingInfo));
    EXPECT_EQ(SamplingInfo.UVScaleBias, TextureAttribs.AtlasUVScaleAndBias);
    EXPECT_EQ(SamplingInfo.TextureSlice, TextureAttribs.TextureSlice);
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
    EXPECT_EQ(SamplingInfo.MipLevels,
              std::min(UploadedDesc.MipLevels,
                       ComputeMipLevelsCount(ExpectedData.Width, ExpectedData.Height)));
    const Uint32 SrcX     = static_cast<Uint32>(TextureAttribs.AtlasUVScaleAndBias.z * static_cast<float>(UploadedDesc.Width) + 0.5f);
    const Uint32 SrcY     = static_cast<Uint32>(TextureAttribs.AtlasUVScaleAndBias.w * static_cast<float>(UploadedDesc.Height) + 0.5f);
    const Uint32 SrcSlice = static_cast<Uint32>(TextureAttribs.TextureSlice + 0.5f);

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

    RadientTextureSamplingInfo SamplingInfo;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(&Texture, SamplingInfo));
    EXPECT_EQ(SamplingInfo.UVScaleBias, TextureAttribs.AtlasUVScaleAndBias);
    EXPECT_EQ(SamplingInfo.TextureSlice, TextureAttribs.TextureSlice);
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

    const std::vector<Uint8> LinearPixels = MakeTexturePixels(0);
    const std::vector<Uint8> SRGBPixels   = MakeTexturePixels(1);
    RadientTextureData       LinearData   = MakeTextureData(LinearPixels);
    RadientTextureData       SRGBData     = MakeTextureData(SRGBPixels);
    SRGBData.Format                       = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB;

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

    const std::vector<float> TexturePixels(Width * Height * ComponentCount, 1.f);
    const RadientTextureData TextureData{
        Width,
        Height,
        RADIENT_TEXTURE_FORMAT_RGBA32_FLOAT,
        TexturePixels.data(),
        Width * ComponentCount * static_cast<Uint32>(sizeof(float))};

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

    const std::vector<Uint8> FirstPixels = MakeTexturePixels(0, Params);
    const RadientTextureData FirstData   = MakeTextureData(FirstPixels, Params);

    RefCntAutoPtr<IRadientTextureAsset> pFirstTexture;
    EXPECT_TRUE(IsPendingOrOK(pManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(FirstData), &pFirstTexture)));
    ASSERT_NE(pFirstTexture, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(pManager, *pUploadManager, *pContext));
    ProcessUploads(*pUploadManager, *pContext, *pFirstTexture);

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
    const std::vector<Uint8> SecondPixels = MakeTexturePixels(1, Params);
    const RadientTextureData SecondData   = MakeTextureData(SecondPixels, Params);

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

    const std::vector<Uint8> TexturePixels0 = MakeTexturePixels(0);
    const std::vector<Uint8> TexturePixels1 = MakeTexturePixels(1);
    const RadientTextureData TextureData0   = MakeTextureData(TexturePixels0);
    const RadientTextureData TextureData1   = MakeTextureData(TexturePixels1);

    RadientTextureLoadInfo LoadInfo0 = MakeTextureDataLoadInfo(TextureData0);
    RadientTextureLoadInfo LoadInfo1 = MakeTextureDataLoadInfo(TextureData1);
    // The handle URI is intentionally the same, but the source data and payload keys differ.
    LoadInfo0.URI = "textures/shared-name.png";
    LoadInfo1.URI = "textures/shared-name.png";

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

    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_CANCELLED);
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
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_FAILED);
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
