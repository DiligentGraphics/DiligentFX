/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "HnMaterial.hpp"
#include "HnRenderDelegate.hpp"
#include "HnTokens.hpp"

#include "pxr/imaging/hd/sceneDelegate.h"

#include "PBR_Renderer.hpp"
#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

std::shared_ptr<HnMaterial> HnMaterial::Create(const pxr::SdfPath& id)
{
    return std::shared_ptr<HnMaterial>(new HnMaterial{id});
}

HnMaterial::HnMaterial(const pxr::SdfPath& id) :
    pxr::HdMaterial{id}
{
}

HnMaterial::~HnMaterial()
{
}

void HnMaterial::Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits == pxr::HdMaterial::Clean)
        return;

    pxr::VtValue vtMat = SceneDelegate->GetMaterialResource(GetId());
    if (vtMat.IsHolding<pxr::HdMaterialNetworkMap>())
    {
        const pxr::HdMaterialNetworkMap& hdNetworkMap = vtMat.UncheckedGet<pxr::HdMaterialNetworkMap>();
        if (!hdNetworkMap.terminals.empty() && !hdNetworkMap.map.empty())
        {
            try
            {
                m_Network = HnMaterialNetwork{GetId(), hdNetworkMap};
            }
            catch (const std::runtime_error& err)
            {
                LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": ", err.what());
                m_Network = {};
            }
            catch (...)
            {
                LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": unknown error");
                m_Network = {};
            }
        }

        HnTextureRegistry& TexRegistry = static_cast<HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate())->GetTextureRegistry();
        AllocateTextures(TexRegistry);

        m_ShaderAttribs.BaseColorFactor = float4{1, 1, 1, 1};
        m_ShaderAttribs.EmissiveFactor  = float4{0, 0, 0, 0};
        m_ShaderAttribs.SpecularFactor  = float4{1, 1, 1, 1};
        m_ShaderAttribs.MetallicFactor  = 1;
        m_ShaderAttribs.RoughnessFactor = 1;
        m_ShaderAttribs.OcclusionFactor = 1;

        const auto& MaterialParams = m_Network.GetParameters();

        auto SetFallbackValue = [&](const pxr::TfToken& Name, auto SetValue) {
            if (m_Textures.find(Name) != m_Textures.end())
                return;

            for (const auto Param : MaterialParams)
            {
                if (Param.Type == HnMaterialParameter::ParamType::Fallback && Param.Name == Name)
                {
                    SetValue(Param.FallbackValue);
                    break;
                }
            }
        };

        SetFallbackValue(HnTokens->diffuseColor, [this](const pxr::VtValue& Val) {
            m_ShaderAttribs.BaseColorFactor = float4{float3::MakeVector(Val.Get<pxr::GfVec3f>().data()), 1};
        });
        SetFallbackValue(HnTokens->metallic, [this](const pxr::VtValue& Val) {
            m_ShaderAttribs.MetallicFactor = Val.Get<float>();
        });
        SetFallbackValue(HnTokens->roughness, [this](const pxr::VtValue& Val) {
            m_ShaderAttribs.RoughnessFactor = Val.Get<float>();
        });
        SetFallbackValue(HnTokens->occlusion, [this](const pxr::VtValue& Val) {
            m_ShaderAttribs.OcclusionFactor = Val.Get<float>();
        });

        m_ShaderAttribs.Workflow      = PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH;
        m_ShaderAttribs.UVSelector0   = 0;
        m_ShaderAttribs.UVSelector1   = 0;
        m_ShaderAttribs.UVSelector2   = 0;
        m_ShaderAttribs.UVSelector3   = 0;
        m_ShaderAttribs.UVSelector4   = 0;
        m_ShaderAttribs.TextureSlice0 = 0;
        m_ShaderAttribs.TextureSlice1 = 0;
        m_ShaderAttribs.TextureSlice2 = 0;
        m_ShaderAttribs.TextureSlice3 = 0;
        m_ShaderAttribs.TextureSlice4 = 0;

        const auto& Tag = m_Network.GetTag();
        if (Tag == HnMaterialTagTokens->translucent)
            m_ShaderAttribs.AlphaMode = PBR_Renderer::ALPHA_MODE_BLEND;
        else if (Tag == HnMaterialTagTokens->masked)
            PBR_Renderer::ALPHA_MODE_MASK;
        else
            PBR_Renderer::ALPHA_MODE_OPAQUE;

        m_ShaderAttribs.AlphaMaskCutoff   = m_Network.GetOpacityThreshold();
        m_ShaderAttribs.BaseColorFactor.a = m_Network.GetOpacity();
    }

    *DirtyBits = HdMaterial::Clean;
}

void HnMaterial::AllocateTextures(HnTextureRegistry& TexRegistry)
{
    for (const HnMaterialNetwork::TextureDescriptor& TexDescriptor : m_Network.GetTextures())
    {
        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId, TexDescriptor.SamplerParams))
        {
            m_Textures[TexDescriptor.Name] = pTex;
        }
    }
}

const HnTextureRegistry::TextureHandle* HnMaterial::GetTexture(const pxr::TfToken& Name) const
{
    auto tex_it = m_Textures.find(Name);
    return tex_it != m_Textures.end() ? tex_it->second.get() : nullptr;
}

pxr::HdDirtyBits HnMaterial::GetInitialDirtyBitsMask() const
{
    return pxr::HdMaterial::AllDirty;
}

void HnMaterial::UpdateSRB(IRenderDevice* pDevice,
                           PBR_Renderer&  PbrRenderer,
                           IBuffer*       pCameraAttribs,
                           IBuffer*       pLightAttribs)
{
    if (m_SRB)
        return;

    PbrRenderer.CreateResourceBinding(&m_SRB);

    PbrRenderer.InitCommonSRBVars(m_SRB, pCameraAttribs, pLightAttribs);

    auto SetTexture = [&](const pxr::TfToken& Name, ITextureView* pDefaultTexSRV, const char* VarName) //
    {
        RefCntAutoPtr<ITextureView> pTexSRV;

        if (auto* pTexHandle = GetTexture(Name))
        {
            if (pTexHandle->pTexture)
            {
                const auto& TexDesc = pTexHandle->pTexture->GetDesc();
                if (TexDesc.Type == RESOURCE_DIM_TEX_2D)
                {
                    UNEXPECTED("2D textures should be loaded as single-slice 2D array textures");

                    const auto Name = std::string{"Tex2DArray view of texture '"} + TexDesc.Name + "'";

                    TextureViewDesc SRVDesc;
                    SRVDesc.Name       = Name.c_str();
                    SRVDesc.ViewType   = TEXTURE_VIEW_SHADER_RESOURCE;
                    SRVDesc.TextureDim = RESOURCE_DIM_TEX_2D_ARRAY;
                    pTexHandle->pTexture->CreateView(SRVDesc, &pTexSRV);

                    pTexSRV->SetSampler(pTexHandle->pSampler);
                }
                else
                {
                    pTexSRV = pTexHandle->pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                }
            }
        }

        if (pTexSRV == nullptr)
            pTexSRV = pDefaultTexSRV;

        if (auto* pVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
            pVar->Set(pTexSRV);
    };

    SetTexture(HnTokens->diffuseColor, PbrRenderer.GetWhiteTexSRV(), "g_ColorMap");
    SetTexture(HnTokens->metallic, PbrRenderer.GetWhiteTexSRV(), "g_MetallicMap");
    SetTexture(HnTokens->roughness, PbrRenderer.GetWhiteTexSRV(), "g_RoughnessMap");
    SetTexture(HnTokens->normal, PbrRenderer.GetDefaultNormalMapSRV(), "g_NormalMap");
    SetTexture(HnTokens->occlusion, PbrRenderer.GetWhiteTexSRV(), "g_AOMap");
    if (auto* pVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EmissiveMap"))
        pVar->Set(PbrRenderer.GetWhiteTexSRV());
}

} // namespace USD

} // namespace Diligent
