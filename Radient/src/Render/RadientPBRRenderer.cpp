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

#include "GraphicsUtilities.h"

#include <algorithm>
#include <unordered_set>

namespace Diligent
{

RadientPBRRenderer::RadientPBRRenderer(IRenderDevice*     pDevice,
                                       IRenderStateCache* pStateCache,
                                       IDeviceContext*    pContext,
                                       const CreateInfo&  CI) :
    PBR_Renderer{pDevice, pStateCache, pContext, CI, false}
{
    CreateSignature();

    CreateUniformBuffer(pDevice,
                        GetPRBFrameAttribsSize(),
                        "Radient PBR frame attribs buffer",
                        &m_pFrameAttribsCB);
}

void RadientPBRRenderer::InitMaterialSRBVars(IShaderResourceBinding* pSRB) const
{
    if (pSRB == nullptr)
    {
        UNEXPECTED("Material SRB must not be null");
        return;
    }

    constexpr bool BindPrimitiveAttribsBuffer = false;
    constexpr bool BindMaterialAttribsBuffer  = true;
    InitCommonSRBVars(pSRB,
                      nullptr,
                      BindPrimitiveAttribsBuffer,
                      BindMaterialAttribsBuffer);

    IShaderResourceVariable* const pPrimitiveAttribs = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
    if (pPrimitiveAttribs == nullptr || GetPBRPrimitiveAttribsCB() == nullptr)
    {
        UNEXPECTED("PBR primitive attributes buffer variable is not initialized");
        return;
    }

    if (pPrimitiveAttribs->Get() == nullptr)
    {
        const Uint32 PrimitiveArraySize = std::max(GetSettings().PrimitiveArraySize, 1u);
        pPrimitiveAttribs->SetBufferRange(GetPBRPrimitiveAttribsCB(),
                                          0,
                                          GetPBRPrimitiveAttribsSize(PSO_FLAG_ALL) * PrimitiveArraySize);
    }
}

RefCntAutoPtr<IShaderResourceBinding> RadientPBRRenderer::GetOrCreateFrameSRB(RadientIBLResources* pResources)
{
    if (pResources == nullptr || m_pFrameAttribsCB == nullptr)
        return {};

    if (RefCntAutoPtr<IShaderResourceBinding> pCachedSRB = m_FrameSRBCache.Get(pResources))
        return pCachedSRB;

    RefCntAutoPtr<IShaderResourceBinding> pFrameSRB;
    CreateResourceBinding(pFrameSRB.GetAddressOfEmpty(), 0);
    if (pFrameSRB == nullptr)
        return {};

    constexpr bool BindPrimitiveAttribsBuffer = false;
    constexpr bool BindMaterialAttribsBuffer  = false;
    InitCommonSRBVars(pFrameSRB,
                      m_pFrameAttribsCB,
                      BindPrimitiveAttribsBuffer,
                      BindMaterialAttribsBuffer);
    SetIBLResourceViews(pFrameSRB,
                        pResources->GetIrradianceCubeSRV(),
                        pResources->GetPrefilteredEnvMapSRV());

    m_FrameSRBCache.Add(pResources, pFrameSRB);
    return pFrameSRB;
}

void RadientPBRRenderer::CreateCustomSignature(PipelineResourceSignatureDescX&& SignatureDesc)
{
    PipelineResourceSignatureDescX FrameSignatureDesc;

    std::unordered_set<HashMapStringKey> FrameResources;
    FrameResources.emplace("cbFrameAttribs");
    FrameResources.emplace("g_PreintegratedGGX");
    FrameResources.emplace("g_IrradianceMap");
    FrameResources.emplace("g_PrefilteredEnvMap");
    FrameResources.emplace("g_PreintegratedCharlie");
    FrameResources.emplace("g_SheenAlbedoScalingLUT");
    FrameResources.emplace("g_ShadowMap");
    FrameResources.emplace("g_ShadowMap_sampler");
    // Only move separate samplers to the frame signature. Combined GL samplers
    // must remain in the same signature as their material textures.
    FrameResources.emplace("g_LinearClampSampler");
    FrameResources.emplace("g_BaseColorMap_sampler");
    FrameResources.emplace("g_NormalMap_sampler");
    FrameResources.emplace("g_MetallicMap_sampler");
    FrameResources.emplace("g_RoughnessMap_sampler");
    FrameResources.emplace("g_PhysicalDescriptorMap_sampler");
    FrameResources.emplace("g_OcclusionMap_sampler");
    FrameResources.emplace("g_EmissiveMap_sampler");
    FrameResources.emplace("g_ClearCoat_sampler");
    FrameResources.emplace("g_Sheen_sampler");
    FrameResources.emplace("g_AnisotropyMap_sampler");
    FrameResources.emplace("g_Iridescence_sampler");
    FrameResources.emplace("g_TransmissionMap_sampler");
    static_assert(TEXTURE_ATTRIB_ID_COUNT == 17, "Did you add a new texture? Don't forget to update the sampler list above");

    FrameResources.emplace("g_OITLayers");
    FrameResources.emplace("g_OITTail");

    for (Uint32 ResourceIndex = 0; ResourceIndex < SignatureDesc.NumResources;)
    {
        const PipelineResourceDesc& Resource = SignatureDesc.GetResource(ResourceIndex);
        if (FrameResources.find(Resource.Name) != FrameResources.end())
        {
            FrameSignatureDesc.AddResource(Resource);
            SignatureDesc.RemoveResource(ResourceIndex);
        }
        else
        {
            ++ResourceIndex;
        }
    }

    for (Uint32 SamplerIndex = 0; SamplerIndex < SignatureDesc.NumImmutableSamplers;)
    {
        const ImmutableSamplerDesc& Sampler = SignatureDesc.GetImmutableSampler(SamplerIndex);
        if (FrameResources.find(Sampler.SamplerOrTextureName) != FrameResources.end())
        {
            FrameSignatureDesc.AddImmutableSampler(Sampler);
            SignatureDesc.RemoveImmutableSampler(SamplerIndex);
        }
        else
        {
            ++SamplerIndex;
        }
    }

    SignatureDesc.SetBindingIndex(1);

    RefCntAutoPtr<IPipelineResourceSignature> pFrameSignature = m_Device.CreatePipelineResourceSignature(FrameSignatureDesc);
    VERIFY_EXPR(pFrameSignature);

    RefCntAutoPtr<IPipelineResourceSignature> pMaterialSignature = m_Device.CreatePipelineResourceSignature(SignatureDesc);
    VERIFY_EXPR(pMaterialSignature);

    if (m_Settings.EnableIBL)
    {
        pFrameSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX")->Set(m_pPreintegratedGGX_SRV);
        if (m_Settings.EnableSheen)
            pFrameSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedCharlie")->Set(m_pPreintegratedCharlie_SRV);
    }

    if (m_Settings.EnableSheen)
        pFrameSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SheenAlbedoScalingLUT")->Set(m_pSheenAlbedoScaling_LUT_SRV);

    m_ResourceSignatures = {std::move(pFrameSignature), std::move(pMaterialSignature)};
}

} // namespace Diligent
