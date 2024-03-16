/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "USD_Renderer.hpp"

#include <array>
#include <unordered_set>
#include <functional>

#include "RenderStateCache.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.h"

namespace Diligent
{

USD_Renderer::CreateInfo::PSMainSourceInfo USD_Renderer::GetUsdPbrPSMainSource(USD_Renderer::PSO_FLAGS PSOFlags) const
{
    USD_Renderer::CreateInfo::PSMainSourceInfo PSMainInfo;

    if (PSOFlags & USD_PSO_FLAG_ENABLE_ALL_OUTPUTS)
    {
        std::stringstream ss;

        ss << "struct PSOutput" << std::endl
           << '{' << std::endl;
        if (PSOFlags & USD_PSO_FLAG_ENABLE_COLOR_OUTPUT)
            ss << "    float4 Color      : SV_Target" << m_ColorTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT)
            ss << "    float4 MeshID     : SV_Target" << m_MeshIdTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_MOTION_VECTORS_OUTPUT)
            ss << "    float4 MotionVec  : SV_Target" << m_MotionVectorTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_NORMAL_OUTPUT)
            ss << "    float4 Normal     : SV_Target" << m_NormalTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_BASE_COLOR_OUTPUT)
            ss << "    float4 BaseColor  : SV_Target" << m_BaseColorTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_MATERIAL_DATA_OUTPUT)
            ss << "    float4 Material   : SV_Target" << m_MaterialDataTargetIndex << ';' << std::endl;

        if (PSOFlags & USD_PSO_FLAG_ENABLE_IBL_OUTPUT)
            ss << "    float4 IBL        : SV_Target" << m_IBLTargetIndex << ';' << std::endl;

        ss << "};" << std::endl;

        PSMainInfo.OutputStruct = ss.str();
    }
    else
    {
        PSMainInfo.OutputStruct = "#define PSOutput void\n";
    }

    std::stringstream ss;

    if (PSOFlags & USD_PSO_FLAG_ENABLE_ALL_OUTPUTS)
        ss << "    PSOutput PSOut;";

    ss << R"(
    float  MeshId       = 0.0;
    float3 Normal       = float3(0.0, 0.0, 0.0);
    float2 MaterialData = float2(0.0, 0.0);
    float3 IBL          = float3(0.0, 0.0, 0.0);

#if UNSHADED
    float4 OutColor     = g_Frame.Renderer.UnshadedColor + g_Frame.Renderer.HighlightColor;
    float4 BaseColor    = float4(0.0, 0.0, 0.0, 0.0);
    float2 MotionVector = float2(0.0, 0.0);
#else
    MeshId       = PRIMITIVE.CustomData.x;
    Normal       = Shading.BaseLayer.Normal.xyz;
    MaterialData = float2(Shading.BaseLayer.Srf.PerceptualRoughness, Shading.BaseLayer.Metallic);
    IBL          = GetBaseLayerSpecularIBL(Shading, SrfLighting);

#   if ENABLE_CLEAR_COAT
    {
        // We clearly can't do SSR for both base layer and clear coat, so we
        // blend the base layer properties with the clearcoat using the clearcoat factor.
        // This way when the factor is 0.0, we get the base layer, when it is 1.0,
        // we get the clear coat, and something in between otherwise.

        Normal        = normalize(lerp(Normal, Shading.Clearcoat.Normal, Shading.Clearcoat.Factor));
        MaterialData  = lerp(MaterialData, float2(Shading.Clearcoat.Srf.PerceptualRoughness, 0.0), Shading.Clearcoat.Factor);
        BaseColor.rgb = lerp(BaseColor.rgb, float3(1.0, 1.0, 1.0), Shading.Clearcoat.Factor);

        // Note that the base layer IBL is weighted by (1.0 - Shading.Clearcoat.Factor * ClearcoatFresnel).
        // Here we are weighting it by (1.0 - Shading.Clearcoat.Factor), which is always smaller,
        // so when we subtract the IBL, it can never be negative.
        IBL = lerp(IBL, GetClearcoatIBL(Shading, SrfLighting), Shading.Clearcoat.Factor);
    }
#   endif
#endif
    
)";

    if (PSOFlags & USD_PSO_FLAG_ENABLE_ALL_OUTPUTS)
    {
        if (PSOFlags & USD_PSO_FLAG_ENABLE_COLOR_OUTPUT)
        {
            ss << "    PSOut.Color = OutColor;" << std::endl;
        }

        // It is important to set alpha to 1.0 as all targets are rendered with the same blend mode
        if (PSOFlags & USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT)
        {
            ss << "    PSOut.MeshID = float4(MeshId, 0.0, 0.0, 1.0);" << std::endl;
        }

        if (PSOFlags & USD_PSO_FLAG_ENABLE_MOTION_VECTORS_OUTPUT)
        {
            // Do not blend motion vectors as it does not make sense
            ss << "    PSOut.MotionVec = float4(MotionVector, 0.0, 1.0);" << std::endl;
        }

        if (PSOFlags & USD_PSO_FLAG_ENABLE_NORMAL_OUTPUT)
        {
            // Do not blend normal - we want normal of the top layer
            ss << "    PSOut.Normal = float4(Normal, 1.0);" << std::endl;
        }

        // Blend base color, material data and IBL with background

        if (PSOFlags & USD_PSO_FLAG_ENABLE_BASE_COLOR_OUTPUT)
        {
            ss << "    PSOut.BaseColor = float4(BaseColor.rgb * BaseColor.a, BaseColor.a);" << std::endl;
        }

        if (PSOFlags & USD_PSO_FLAG_ENABLE_MATERIAL_DATA_OUTPUT)
        {
            ss << "    PSOut.Material = float4(MaterialData * BaseColor.a, 0.0, BaseColor.a);" << std::endl;
        }

        if (PSOFlags & USD_PSO_FLAG_ENABLE_IBL_OUTPUT)
        {
            ss << "    PSOut.IBL = float4(IBL * BaseColor.a, BaseColor.a);" << std::endl;
        }
    }

    if (PSOFlags & USD_PSO_FLAG_ENABLE_ALL_OUTPUTS)
        ss << "    return PSOut;" << std::endl;

    PSMainInfo.Footer = ss.str();

    return PSMainInfo;
}

struct USD_Renderer::USDRendererCreateInfoWrapper
{
    USDRendererCreateInfoWrapper(const USD_Renderer::CreateInfo& _CI, const USD_Renderer& Renderer) :
        CI{_CI}
    {
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = 0;
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = 1;
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC]   = 2;
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS]  = 3;
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]  = 4;
        CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = 5;

        if (CI.GetPSMainSource == nullptr)
        {
            CI.GetPSMainSource = std::bind(&USD_Renderer::GetUsdPbrPSMainSource, &Renderer, std::placeholders::_1);
        }
    }

    operator const PBR_Renderer::CreateInfo &() const
    {
        return CI;
    }

    USD_Renderer::CreateInfo CI;
};

USD_Renderer::USD_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    PBR_Renderer{
        pDevice,
        pStateCache,
        pCtx,
        USDRendererCreateInfoWrapper{CI, *this},
        false, // InitSignature
    },
    m_ColorTargetIndex{CI.ColorTargetIndex},
    m_MeshIdTargetIndex{CI.MeshIdTargetIndex},
    m_MotionVectorTargetIndex{CI.MotionVectorTargetIndex},
    m_NormalTargetIndex{CI.NormalTargetIndex},
    m_BaseColorTargetIndex{CI.BaseColorTargetIndex},
    m_MaterialDataTargetIndex{CI.MaterialDataTargetIndex},
    m_IBLTargetIndex{CI.IBLTargetIndex}
{
#ifdef DILIGENT_DEVELOPMENT
    {
        std::unordered_set<Uint32> TargetIndices;
        for (Uint32 Idx : {m_ColorTargetIndex,
                           m_MeshIdTargetIndex,
                           m_MotionVectorTargetIndex,
                           m_NormalTargetIndex,
                           m_BaseColorTargetIndex,
                           m_MaterialDataTargetIndex,
                           m_IBLTargetIndex})
        {
            if (Idx != ~0u)
            {
                DEV_CHECK_ERR(TargetIndices.insert(Idx).second, "Target index ", Idx, " is specified more than once");
            }
        }
    }
#endif

    CreateSignature();
}

void USD_Renderer::CreateCustomSignature(PipelineResourceSignatureDescX&& SignatureDesc)
{
    PipelineResourceSignatureDescX FrameAttribsSignDesc;

    std::unordered_set<HashMapStringKey> FrameResources;
    FrameResources.emplace("cbFrameAttribs");
    FrameResources.emplace("g_PreintegratedGGX");
    FrameResources.emplace("g_IrradianceMap");
    FrameResources.emplace("g_PrefilteredEnvMap");
    FrameResources.emplace("g_PreintegratedCharlie");
    FrameResources.emplace("g_SheenAlbedoScalingLUT");
    FrameResources.emplace("g_ShadowMap");
    FrameResources.emplace("g_ShadowMap_sampler");
    // Only move separate samplers to the frame signature.
    // Combined GL samplers should stay in the resource signature.
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
    static_assert(TEXTURE_ATTRIB_ID_COUNT == 17, "Did you add a new texture? Don't forget to update the list above");

    for (Uint32 ResIdx = 0; ResIdx < SignatureDesc.NumResources;)
    {
        const PipelineResourceDesc& Res = SignatureDesc.GetResource(ResIdx);
        if (FrameResources.find(Res.Name) != FrameResources.end())
        {
            FrameAttribsSignDesc.AddResource(Res);
            SignatureDesc.RemoveResource(ResIdx);
        }
        else
        {
            ++ResIdx;
        }
    }
    for (Uint32 SamIdx = 0; SamIdx < SignatureDesc.NumImmutableSamplers;)
    {
        const ImmutableSamplerDesc& Sam = SignatureDesc.GetImmutableSampler(SamIdx);
        if (FrameResources.find(Sam.SamplerOrTextureName) != FrameResources.end())
        {
            FrameAttribsSignDesc.AddImmutableSampler(Sam);
            SignatureDesc.RemoveImmutableSampler(SamIdx);
        }
        else
        {
            ++SamIdx;
        }
    }
    SignatureDesc.SetBindingIndex(1);

    RefCntAutoPtr<IPipelineResourceSignature> FrameAttribsSignature = m_Device.CreatePipelineResourceSignature(FrameAttribsSignDesc);
    VERIFY_EXPR(FrameAttribsSignature);

    RefCntAutoPtr<IPipelineResourceSignature> ResourceSignature = m_Device.CreatePipelineResourceSignature(SignatureDesc);
    VERIFY_EXPR(ResourceSignature);

    if (m_Settings.EnableIBL)
    {
        FrameAttribsSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedGGX")->Set(m_pPreintegratedGGX_SRV);
        if (m_Settings.EnableSheen)
        {
            FrameAttribsSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_PreintegratedCharlie")->Set(m_pPreintegratedCharlie_SRV);
        }
    }

    if (m_Settings.EnableSheen)
    {
        FrameAttribsSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SheenAlbedoScalingLUT")->Set(m_pSheenAlbedoScaling_LUT_SRV);
    }
    m_ResourceSignatures = {std::move(FrameAttribsSignature), std::move(ResourceSignature)};
}

} // namespace Diligent
