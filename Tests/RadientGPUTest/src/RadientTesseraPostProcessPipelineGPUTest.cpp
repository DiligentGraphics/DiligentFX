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

#include "Render/RadientFrameRenderTargets.hpp"
#include "Render/RadientRendererImpl.hpp"
#include "Render/Tessera/Passes/RadientTesseraPostProcessPipeline.hpp"

#include "GPUTestingEnvironment.hpp"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"

#include "gtest/gtest.h"

#include <initializer_list>

using namespace Diligent;
using namespace Diligent::Testing;

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL

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

void ExecutePostProcessVariants(std::initializer_list<RADIENT_TONE_MAPPING_MODE> ToneMappingModes)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const         pDevice      = pEnvironment->GetDevice();
    IDeviceContext* const        pContext     = pEnvironment->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    TestRenderTarget Target = CreateTestRenderTarget(pDevice, 32, 16);
    ASSERT_NE(Target.pTarget, nullptr);

    RadientFrameRenderTargets Targets;
    ASSERT_EQ(Targets.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);
    Targets.ClearGBuffer(pContext);

    RefCntAutoPtr<IBuffer> pFrameAttribsCB;
    CreateUniformBuffer(pDevice,
                        sizeof(HLSL::PBRFrameAttribs),
                        "Radient PostFX test frame attribs",
                        pFrameAttribsCB.GetAddressOfEmpty());
    ASSERT_NE(pFrameAttribsCB, nullptr);
    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{pContext,
                                                      pFrameAttribsCB,
                                                      MAP_WRITE,
                                                      MAP_FLAG_DISCARD};
        ASSERT_TRUE(FrameAttribs);
        *FrameAttribs = {};
    }

    RadientTesseraPostProcessPipeline Pipeline;
    for (RADIENT_TONE_MAPPING_MODE ToneMappingMode : ToneMappingModes)
    {
        RadientToneMappingDesc ToneMapping;
        ToneMapping.Mode = ToneMappingMode;
        ASSERT_EQ(Pipeline.Prepare(pDevice, pContext, Targets, ToneMapping, {}, {}, {}, 0, pFrameAttribsCB, nullptr),
                  RADIENT_STATUS_OK);
        EXPECT_EQ(Pipeline.Execute(pDevice, pContext, Targets, true), RADIENT_STATUS_OK);
    }

    pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    pContext->Flush();
}

TEST(RadientTesseraPostProcessPipelineGPUTest, ExecutesToneMappingVariant)
{
    ExecutePostProcessVariants({RADIENT_TONE_MAPPING_MODE_UNCHARTED2});
}

TEST(RadientTesseraPostProcessPipelineGPUTest, ExecutesCopyVariant)
{
    ExecutePostProcessVariants({RADIENT_TONE_MAPPING_MODE_NONE});
}

TEST(RadientTesseraPostProcessPipelineGPUTest, ReconfiguresToneMappingMode)
{
    ExecutePostProcessVariants({RADIENT_TONE_MAPPING_MODE_UNCHARTED2,
                                RADIENT_TONE_MAPPING_MODE_NONE,
                                RADIENT_TONE_MAPPING_MODE_AGX});
}

TEST(RadientTesseraPostProcessPipelineGPUTest, ExecutesSSAOComposition)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const         pDevice      = pEnvironment->GetDevice();
    IDeviceContext* const        pContext     = pEnvironment->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    TestRenderTarget Target = CreateTestRenderTarget(pDevice, 32, 16);
    ASSERT_NE(Target.pTarget, nullptr);

    RadientFrameRenderTargets Targets;
    ASSERT_EQ(Targets.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);

    RefCntAutoPtr<IBuffer> pFrameAttribsCB;
    CreateUniformBuffer(pDevice,
                        sizeof(HLSL::PBRFrameAttribs),
                        "Radient SSAO test frame attribs",
                        pFrameAttribsCB.GetAddressOfEmpty());
    ASSERT_NE(pFrameAttribsCB, nullptr);
    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{pContext,
                                                      pFrameAttribsCB,
                                                      MAP_WRITE,
                                                      MAP_FLAG_DISCARD};
        ASSERT_TRUE(FrameAttribs);
        *FrameAttribs                     = {};
        FrameAttribs->Camera.mViewProj    = float4x4::Identity();
        FrameAttribs->Camera.mViewProjInv = float4x4::Identity();
        FrameAttribs->PrevCamera          = FrameAttribs->Camera;
    }

    RadientSSAODesc SSAO;
    SSAO.Enabled = True;

    RadientTesseraPostProcessPipeline Pipeline;
    for (Uint32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
    {
        Targets.ClearGBuffer(pContext);
        ASSERT_EQ(Pipeline.Prepare(pDevice,
                                   pContext,
                                   Targets,
                                   {},
                                   SSAO,
                                   {},
                                   {},
                                   FrameIndex,
                                   pFrameAttribsCB,
                                   nullptr),
                  RADIENT_STATUS_OK);
        EXPECT_EQ(Pipeline.Execute(pDevice, pContext, Targets, FrameIndex == 0), RADIENT_STATUS_OK);
        Targets.CommitFrame();
    }

    pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    pContext->Flush();
}

TEST(RadientTesseraPostProcessPipelineGPUTest, ExecutesSSRAndDOFColorChain)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const         pDevice      = pEnvironment->GetDevice();
    IDeviceContext* const        pContext     = pEnvironment->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    TestRenderTarget Target = CreateTestRenderTarget(pDevice, 32, 16);
    ASSERT_NE(Target.pTarget, nullptr);

    RadientFrameRenderTargets Targets;
    ASSERT_EQ(Targets.Prepare(pDevice, *Target.pTarget), RADIENT_STATUS_OK);

    RefCntAutoPtr<IBuffer> pFrameAttribsCB;
    CreateUniformBuffer(pDevice,
                        sizeof(HLSL::PBRFrameAttribs),
                        "Radient SSR test frame attribs",
                        pFrameAttribsCB.GetAddressOfEmpty());
    ASSERT_NE(pFrameAttribsCB, nullptr);
    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{pContext,
                                                      pFrameAttribsCB,
                                                      MAP_WRITE,
                                                      MAP_FLAG_DISCARD};
        ASSERT_TRUE(FrameAttribs);
        *FrameAttribs                     = {};
        FrameAttribs->Camera.mViewProj    = float4x4::Identity();
        FrameAttribs->Camera.mViewProjInv = float4x4::Identity();
        FrameAttribs->PrevCamera          = FrameAttribs->Camera;
    }

    TextureDesc BRDFDesc;
    BRDFDesc.Name      = "Radient SSR test preintegrated GGX";
    BRDFDesc.Type      = RESOURCE_DIM_TEX_2D;
    BRDFDesc.Width     = 16;
    BRDFDesc.Height    = 16;
    BRDFDesc.Format    = TEX_FORMAT_RG16_FLOAT;
    BRDFDesc.BindFlags = BIND_SHADER_RESOURCE;

    RefCntAutoPtr<ITexture> pPreintegratedGGX;
    pDevice->CreateTexture(BRDFDesc, nullptr, pPreintegratedGGX.GetAddressOfEmpty());
    ASSERT_NE(pPreintegratedGGX, nullptr);
    ITextureView* const pPreintegratedGGXSRV =
        pPreintegratedGGX->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ASSERT_NE(pPreintegratedGGXSRV, nullptr);

    RadientSSRDesc SSR;
    SSR.Enabled = True;

    RadientDepthOfFieldDesc DepthOfField;
    DepthOfField.Enabled = True;

    RadientTesseraPostProcessPipeline Pipeline;
    for (Uint32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
    {
        Targets.ClearGBuffer(pContext);
        ASSERT_EQ(Pipeline.Prepare(pDevice,
                                   pContext,
                                   Targets,
                                   {},
                                   {},
                                   SSR,
                                   DepthOfField,
                                   FrameIndex,
                                   pFrameAttribsCB,
                                   pPreintegratedGGXSRV),
                  RADIENT_STATUS_OK);
        EXPECT_EQ(Pipeline.Execute(pDevice, pContext, Targets, FrameIndex == 0), RADIENT_STATUS_OK);
        Targets.CommitFrame();
    }

    pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    pContext->Flush();
}

} // namespace

} // namespace Diligent
