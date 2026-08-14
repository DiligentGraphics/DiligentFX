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

#include "Render/RadientPBRRenderer.hpp"
#include "Core/RadientViewImpl.hpp"

#include "Cast.hpp"
#include "GPUTestingEnvironment.hpp"

#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

bool HasResource(const PipelineResourceSignatureDesc& Desc, const char* Name)
{
    for (Uint32 ResourceIndex = 0; ResourceIndex < Desc.NumResources; ++ResourceIndex)
    {
        if (SafeStrEqual(Desc.Resources[ResourceIndex].Name, Name))
            return true;
    }
    return false;
}

TEST(RadientPBRRendererGPUTest, SeparatesFrameAndMaterialResources)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    PBR_Renderer::CreateInfo RendererCI{};
    RendererCI.EnableIBL             = false;
    RendererCI.EnableSheen           = true;
    RendererCI.EnableShadows         = true;
    RendererCI.CreateDefaultTextures = false;
    RendererCI.NumBRDFSamples        = 16;

    RadientPBRRenderer Renderer{pDevice, nullptr, pContext, RendererCI};

    ITextureView* const pPreintegratedSheen = Renderer.GetPreintegratedSheen_SRV();
    ASSERT_NE(pPreintegratedSheen, nullptr);
    EXPECT_EQ(pPreintegratedSheen->GetDesc().Format, TEX_FORMAT_RG16_FLOAT);

    RefCntAutoPtr<IShaderResourceBinding> pFrameSRB;
    RefCntAutoPtr<IShaderResourceBinding> pMaterialSRB;
    Renderer.CreateResourceBinding(pFrameSRB.GetAddressOfEmpty(), 0);
    Renderer.CreateResourceBinding(pMaterialSRB.GetAddressOfEmpty(), 1);
    ASSERT_NE(pFrameSRB, nullptr);
    ASSERT_NE(pMaterialSRB, nullptr);

    const PipelineResourceSignatureDesc& FrameDesc = pFrameSRB->GetPipelineResourceSignature()->GetDesc();
    EXPECT_EQ(FrameDesc.BindingIndex, 0);
    EXPECT_TRUE(HasResource(FrameDesc, "cbFrameAttribs"));
    EXPECT_TRUE(HasResource(FrameDesc, "g_ShadowMap"));
    EXPECT_TRUE(HasResource(FrameDesc, "g_PreintegratedSheen"));
    EXPECT_FALSE(HasResource(FrameDesc, "cbPrimitiveAttribs"));
    EXPECT_FALSE(HasResource(FrameDesc, "cbMaterialAttribs"));
    EXPECT_FALSE(HasResource(FrameDesc, "g_BaseColorMap"));

    const PipelineResourceSignatureDesc& MaterialDesc = pMaterialSRB->GetPipelineResourceSignature()->GetDesc();
    EXPECT_EQ(MaterialDesc.BindingIndex, 1);
    EXPECT_FALSE(HasResource(MaterialDesc, "cbFrameAttribs"));
    EXPECT_FALSE(HasResource(MaterialDesc, "g_ShadowMap"));
    EXPECT_FALSE(HasResource(MaterialDesc, "g_PreintegratedSheen"));
    EXPECT_TRUE(HasResource(MaterialDesc, "cbPrimitiveAttribs"));
    EXPECT_TRUE(HasResource(MaterialDesc, "cbMaterialAttribs"));
    EXPECT_TRUE(HasResource(MaterialDesc, "g_BaseColorMap"));
}

TEST(RadientPBRRendererGPUTest, ViewsOwnIndependentIBLResources)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    PBR_Renderer::CreateInfo RendererCI{};
    RendererCI.EnableIBL      = true;
    RendererCI.EnableSheen    = true;
    RendererCI.NumBRDFSamples = 16;

    RadientPBRRenderer Renderer{pDevice, nullptr, pContext, RendererCI};

    ASSERT_NE(Renderer.GetFrameAttribsCB(), nullptr);

    RefCntAutoPtr<IRadientView> pFirstView  = RadientViewImpl::Create({});
    RefCntAutoPtr<IRadientView> pSecondView = RadientViewImpl::Create({});
    ASSERT_NE(pFirstView, nullptr);
    ASSERT_NE(pSecondView, nullptr);

    RadientViewImpl* pFirstViewImpl  = ClassPtrCast<RadientViewImpl>(pFirstView.RawPtr());
    RadientViewImpl* pSecondViewImpl = ClassPtrCast<RadientViewImpl>(pSecondView.RawPtr());
    ASSERT_NE(pFirstViewImpl, nullptr);
    ASSERT_NE(pSecondViewImpl, nullptr);

    ASSERT_EQ(pFirstViewImpl->Prepare(Renderer, pContext), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondViewImpl->Prepare(Renderer, pContext), RADIENT_STATUS_OK);

    RefCntAutoPtr<IShaderResourceBinding> pFirstFrameSRB =
        Renderer.GetOrCreateFrameSRB(pFirstViewImpl->GetIBLResources());
    RefCntAutoPtr<IShaderResourceBinding> pSecondFrameSRB =
        Renderer.GetOrCreateFrameSRB(pSecondViewImpl->GetIBLResources());

    ITextureView* const pFirstIrradiance       = pFirstViewImpl->GetIrradianceCubeSRV();
    ITextureView* const pFirstPrefiltered      = pFirstViewImpl->GetPrefilteredEnvMapSRV();
    ITextureView* const pFirstPrefilteredSheen = pFirstViewImpl->GetPrefilteredSheenEnvMapSRV();
    ASSERT_NE(pFirstIrradiance, nullptr);
    ASSERT_NE(pFirstPrefiltered, nullptr);
    ASSERT_NE(pFirstPrefilteredSheen, nullptr);
    if (pDevice->GetDeviceInfo().IsD3DDevice() || pDevice->GetDeviceInfo().IsVulkanDevice())
    {
        EXPECT_EQ(pFirstIrradiance->GetTexture()->GetState(), RESOURCE_STATE_SHADER_RESOURCE);
        EXPECT_EQ(pFirstPrefiltered->GetTexture()->GetState(), RESOURCE_STATE_SHADER_RESOURCE);
        EXPECT_EQ(pFirstPrefilteredSheen->GetTexture()->GetState(), RESOURCE_STATE_SHADER_RESOURCE);
    }

    EXPECT_NE(pFirstIrradiance, pSecondViewImpl->GetIrradianceCubeSRV());
    EXPECT_NE(pFirstPrefiltered, pSecondViewImpl->GetPrefilteredEnvMapSRV());
    EXPECT_NE(pFirstPrefilteredSheen, pSecondViewImpl->GetPrefilteredSheenEnvMapSRV());

    ASSERT_NE(pFirstFrameSRB, nullptr);
    ASSERT_NE(pSecondFrameSRB, nullptr);
    EXPECT_NE(pFirstFrameSRB, pSecondFrameSRB);

    IShaderResourceVariable* const pFirstIrradianceVar =
        pFirstFrameSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap");
    IShaderResourceVariable* const pFirstPrefilteredVar =
        pFirstFrameSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap");
    IShaderResourceVariable* const pFirstPrefilteredSheenVar =
        pFirstFrameSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredSheenEnvMap");
    ASSERT_NE(pFirstIrradianceVar, nullptr);
    ASSERT_NE(pFirstPrefilteredVar, nullptr);
    ASSERT_NE(pFirstPrefilteredSheenVar, nullptr);
    EXPECT_EQ(pFirstIrradianceVar->Get(), pFirstIrradiance);
    EXPECT_EQ(pFirstPrefilteredVar->Get(), pFirstPrefiltered);
    EXPECT_EQ(pFirstPrefilteredSheenVar->Get(), pFirstPrefilteredSheen);

    ASSERT_EQ(pFirstViewImpl->Prepare(Renderer, pContext), RADIENT_STATUS_OK);
    EXPECT_EQ(Renderer.GetOrCreateFrameSRB(pFirstViewImpl->GetIBLResources()).RawPtr(),
              pFirstFrameSRB.RawPtr());
    EXPECT_EQ(pFirstViewImpl->GetIrradianceCubeSRV(), pFirstIrradiance);
    EXPECT_EQ(pFirstViewImpl->GetPrefilteredEnvMapSRV(), pFirstPrefiltered);
    EXPECT_EQ(pFirstViewImpl->GetPrefilteredSheenEnvMapSRV(), pFirstPrefilteredSheen);
}

} // namespace
