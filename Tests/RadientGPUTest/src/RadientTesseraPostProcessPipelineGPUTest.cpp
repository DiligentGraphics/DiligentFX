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

#include "RadientEngine.h"
#include "Render/RadientFrameRenderTargets.hpp"
#include "Render/RadientRendererImpl.hpp"

#include "GPUTestingEnvironment.hpp"

#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;

namespace Diligent
{

namespace
{

struct TestRenderTarget
{
    RefCntAutoPtr<ITexture>             pColor;
    RefCntAutoPtr<IRadientRenderTarget> pTarget;
};

TestRenderTarget CreateTestRenderTarget(IRenderDevice* pDevice, Uint32 Width, Uint32 Height)
{
    TextureDesc ColorDesc;
    ColorDesc.Name      = "Radient PostFX test output";
    ColorDesc.Type      = RESOURCE_DIM_TEX_2D;
    ColorDesc.Width     = Width;
    ColorDesc.Height    = Height;
    ColorDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
    ColorDesc.BindFlags = BIND_RENDER_TARGET;

    TestRenderTarget Result;
    pDevice->CreateTexture(ColorDesc, nullptr, Result.pColor.GetAddressOfEmpty());
    if (Result.pColor == nullptr)
        return Result;

    RadientRenderTargetDesc TargetDesc;
    TargetDesc.Name      = "Radient PostFX test target";
    TargetDesc.Size      = {Width, Height};
    TargetDesc.pColorRTV = Result.pColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    Result.pTarget       = RadientRenderTargetImpl::Create(TargetDesc);
    return Result;
}

TEST(RadientRendererGPUTest, RejectsShaderResourceRenderTargetViews)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment* const       pEnvironment = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const               pDevice      = pEnvironment->GetDevice();
    ISwapChain* const                  pSwapChain   = pEnvironment->GetSwapChain();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pSwapChain, nullptr);

    RadientEngineCreateInfo EngineCI;
    EngineCI.WorkerThreadCount = 1;
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientRenderer> pRenderer;
    ASSERT_EQ(pEngine->CreateRenderer({}, &pRenderer), RADIENT_STATUS_OK);

    TextureDesc Desc;
    Desc.Name      = "Radient invalid render target view test";
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = 32;
    Desc.Height    = 16;
    Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    Desc.BindFlags = BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> pTexture;
    pDevice->CreateTexture(Desc, nullptr, &pTexture);
    ASSERT_NE(pTexture, nullptr);
    ITextureView* const pSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ASSERT_NE(pSRV, nullptr);

    for (bool UseSwapChain : {false, true})
    {
        SCOPED_TRACE(UseSwapChain);
        RadientRenderTargetDesc TargetDesc;
        TargetDesc.Size       = {Desc.Width, Desc.Height};
        TargetDesc.pSwapChain = UseSwapChain ? pSwapChain : nullptr;
        TargetDesc.pColorRTV  = pSRV;
        {
            TestingEnvironment::ErrorScope      ExpectedError{"pColorRTV"};
            RefCntAutoPtr<IRadientRenderTarget> pTarget;
            EXPECT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_EQ(pTarget, nullptr);
        }

        TargetDesc.pColorRTV = nullptr;
        TargetDesc.pDepthDSV = pSRV;
        {
            TestingEnvironment::ErrorScope      ExpectedError{"pDepthDSV"};
            RefCntAutoPtr<IRadientRenderTarget> pTarget;
            EXPECT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_EQ(pTarget, nullptr);
        }
    }
}

TEST(RadientRendererGPUTest, RequiresWritableRenderTargetViews)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    IRenderDevice* const               pDevice = GPUTestingEnvironment::GetInstance()->GetDevice();
    ASSERT_NE(pDevice, nullptr);

    RadientEngineCreateInfo EngineCI;
    EngineCI.WorkerThreadCount = 1;
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientRenderer> pRenderer;
    ASSERT_EQ(pEngine->CreateRenderer({}, &pRenderer), RADIENT_STATUS_OK);

    TextureDesc ColorDesc;
    ColorDesc.Name      = "Radient valid color target view test";
    ColorDesc.Type      = RESOURCE_DIM_TEX_2D;
    ColorDesc.Width     = 32;
    ColorDesc.Height    = 16;
    ColorDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
    ColorDesc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> pColor;
    pDevice->CreateTexture(ColorDesc, nullptr, &pColor);
    ASSERT_NE(pColor, nullptr);
    ITextureView* const pRTV = pColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    ASSERT_NE(pRTV, nullptr);

    TextureDesc DepthDesc = ColorDesc;
    DepthDesc.Name        = "Radient valid depth target view test";
    DepthDesc.Format      = TEX_FORMAT_D32_FLOAT;
    DepthDesc.BindFlags   = BIND_DEPTH_STENCIL;
    RefCntAutoPtr<ITexture> pDepth;
    pDevice->CreateTexture(DepthDesc, nullptr, &pDepth);
    ASSERT_NE(pDepth, nullptr);
    ITextureView* const pDSV = pDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    ASSERT_NE(pDSV, nullptr);

    TextureViewDesc ReadOnlyDepthDesc;
    ReadOnlyDepthDesc.ViewType = TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
    RefCntAutoPtr<ITextureView> pReadOnlyDSV;
    pDepth->CreateView(ReadOnlyDepthDesc, &pReadOnlyDSV);
    ASSERT_NE(pReadOnlyDSV, nullptr);

    ITextureView* const ColorViews[] = {nullptr, pRTV};
    ITextureView* const DepthViews[] = {nullptr, pDSV};
    for (ITextureView* pColorView : ColorViews)
    {
        for (ITextureView* pDepthView : DepthViews)
        {
            RadientRenderTargetDesc TargetDesc;
            TargetDesc.Size      = {ColorDesc.Width, ColorDesc.Height};
            TargetDesc.pColorRTV = pColorView;
            TargetDesc.pDepthDSV = pDepthView;
            RefCntAutoPtr<IRadientRenderTarget> pTarget;
            ASSERT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_OK);
            ASSERT_NE(pTarget, nullptr);
            EXPECT_EQ(pTarget->GetColorRTV(), pColorView);
            EXPECT_EQ(pTarget->GetDepthDSV(), pDepthView);
        }
    }

    // A render target needs a writable depth view, even though read-only DSVs
    // may be bound for depth testing in other rendering passes.
    RadientRenderTargetDesc TargetDesc;
    TargetDesc.Size      = {ColorDesc.Width, ColorDesc.Height};
    TargetDesc.pColorRTV = pRTV;
    TargetDesc.pDepthDSV = pReadOnlyDSV;
    RefCntAutoPtr<IRadientRenderTarget> pTarget;
    TestingEnvironment::ErrorScope      ExpectedError{"pDepthDSV"};
    EXPECT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pTarget, nullptr);
}

TEST(RadientFrameRenderTargetsGPUTest, PreparesSwapChainOnlyTarget)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment* const       pEnvironment = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const               pDevice      = pEnvironment->GetDevice();
    ISwapChain* const                  pSwapChain   = pEnvironment->GetSwapChain();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pSwapChain, nullptr);

    // Presentation targets resolve their output view from the swap chain.
    // Supplying the captured RTV here would conceal a missing fallback.
    RadientRenderTargetDesc Desc;
    Desc.Size                                   = {pSwapChain->GetDesc().Width, pSwapChain->GetDesc().Height};
    Desc.pSwapChain                             = pSwapChain;
    RefCntAutoPtr<IRadientRenderTarget> pTarget = RadientRenderTargetImpl::Create(Desc);
    ASSERT_NE(pTarget, nullptr);
    ASSERT_EQ(pTarget->GetDesc().pColorRTV, nullptr);
    ASSERT_NE(pSwapChain->GetCurrentBackBufferRTV(), nullptr);

    RadientFrameRenderTargets Targets;
    ASSERT_EQ(Targets.Prepare(pDevice, *pTarget), RADIENT_STATUS_OK);
    EXPECT_EQ(Targets.GetOutputColorRTV(), pSwapChain->GetCurrentBackBufferRTV());
    EXPECT_EQ(pTarget->GetDepthDSV(), pSwapChain->GetDepthBufferDSV());
}

TEST(RadientFrameRenderTargetsGPUTest, CreatesIndependentGBuffers)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    IRenderDevice* const pDevice = GPUTestingEnvironment::GetInstance()->GetDevice();
    ASSERT_NE(pDevice, nullptr);

    TestRenderTarget Target = CreateTestRenderTarget(pDevice, 32, 16);
    ASSERT_NE(Target.pTarget, nullptr);

    RadientFrameRenderTargets First;
    ASSERT_EQ(First.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);
    EXPECT_EQ(First.GetUseReverseDepth(), pDevice->GetDeviceInfo().NDC.MinZ == 0.f);
    std::array<ITexture*, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT> FirstGBuffer{};
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        const auto TargetId = static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex);
        ASSERT_NE(First.GetGBufferRTV(TargetId), nullptr);
        ASSERT_NE(First.GetGBufferSRV(TargetId), nullptr);
        EXPECT_EQ(First.GetGBufferRTV(TargetId)->GetTexture(),
                  First.GetGBufferSRV(TargetId)->GetTexture());
        EXPECT_EQ(First.GetGBufferRTV(TargetId)->GetTexture()->GetDesc().Format,
                  RadientFrameRenderTargets::GetGBufferFormat(TargetId));
        FirstGBuffer[TargetIndex] = First.GetGBufferRTV(TargetId)->GetTexture();
    }
    EXPECT_EQ(First.GetOutputColorRTV(), Target.pColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    ASSERT_NE(First.GetDepthDSV(), nullptr);
    ASSERT_NE(First.GetDepthSRV(), nullptr);
    EXPECT_EQ(First.GetDepthDSV()->GetTexture(), First.GetDepthSRV()->GetTexture());
    EXPECT_EQ(First.GetPreviousDepthSRV(), First.GetDepthSRV());

    const Uint32 InitialVersion = First.GetVersion();
    EXPECT_EQ(First.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);
    EXPECT_EQ(First.GetVersion(), InitialVersion);
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        const auto TargetId = static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex);
        EXPECT_EQ(First.GetGBufferRTV(TargetId)->GetTexture(), FirstGBuffer[TargetIndex]);
    }

    ITexture* const pFirstDepth = First.GetDepthSRV()->GetTexture();
    First.CommitFrame();
    EXPECT_NE(First.GetDepthSRV()->GetTexture(), pFirstDepth);
    EXPECT_EQ(First.GetPreviousDepthSRV()->GetTexture(), pFirstDepth);

    RadientFrameRenderTargets Second;
    ASSERT_EQ(Second.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        const auto TargetId = static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex);
        EXPECT_NE(Second.GetGBufferRTV(TargetId)->GetTexture(), FirstGBuffer[TargetIndex]);
    }

    TestRenderTarget ResizedTarget = CreateTestRenderTarget(pDevice, 64, 32);
    ASSERT_NE(ResizedTarget.pTarget, nullptr);
    ASSERT_EQ(First.Prepare(pDevice, *ResizedTarget.pTarget), RADIENT_STATUS_OK);
    EXPECT_GT(First.GetVersion(), InitialVersion);
    EXPECT_EQ(First.GetSize(), (RadientExtent2D{64, 32}));
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        const auto TargetId = static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex);
        EXPECT_NE(First.GetGBufferRTV(TargetId)->GetTexture(), FirstGBuffer[TargetIndex]);
    }
    EXPECT_EQ(First.GetPreviousDepthSRV(), First.GetDepthSRV());
    EXPECT_NE(First.GetDepthSRV()->GetTexture(), pFirstDepth);
}

} // namespace

} // namespace Diligent
