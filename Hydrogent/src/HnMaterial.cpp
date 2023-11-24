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
#include "HnTypeConversions.hpp"
#include "DynamicTextureAtlas.h"
#include "GLTFResourceManager.hpp"
#include "DataBlobImpl.hpp"

#include "pxr/imaging/hd/sceneDelegate.h"

#include "USD_Renderer.hpp"
#include "DebugUtilities.hpp"
#include "Image.h"

namespace Diligent
{

namespace USD
{

HnMaterial* HnMaterial::Create(const pxr::SdfPath& id)
{
    return new HnMaterial{id};
}

HnMaterial* HnMaterial::CreateFallback(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer)
{
    return new HnMaterial{TexRegistry, UsdRenderer};
}

HnMaterial::HnMaterial(const pxr::SdfPath& id) :
    pxr::HdMaterial{id}
{
    m_BasicShaderAttribs.BaseColorFactor = float4{1, 1, 1, 1};
    m_BasicShaderAttribs.SpecularFactor  = float4{1, 1, 1, 1};
    m_BasicShaderAttribs.MetallicFactor  = 1;
    m_BasicShaderAttribs.RoughnessFactor = 1;
    m_BasicShaderAttribs.OcclusionFactor = 1;

    m_BasicShaderAttribs.Workflow = PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH;
}

HnMaterial::HnMaterial(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer) :
    HnMaterial{pxr::SdfPath{}}
{
    m_NumShaderTextureAttribs = UsdRenderer.GetNumShaderTextureAttribs();
    m_ShaderTextureAttribs    = std::make_unique<HLSL::PBRMaterialTextureAttribs[]>(m_NumShaderTextureAttribs);

    InitTextureAttribs(TexRegistry, UsdRenderer, {});
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

    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate());
    HnTextureRegistry&  TexRegistry    = RenderDelegate->GetTextureRegistry();
    const USD_Renderer& UsdRenderer    = *RenderDelegate->GetUSDRenderer();

    m_NumShaderTextureAttribs = UsdRenderer.GetNumShaderTextureAttribs();
    m_ShaderTextureAttribs    = std::make_unique<HLSL::PBRMaterialTextureAttribs[]>(m_NumShaderTextureAttribs);

    TexNameToCoordSetMapType TexNameToCoordSetMap;

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

        AllocateTextures(TexRegistry, TexNameToCoordSetMap);

        m_BasicShaderAttribs.EmissiveFactor = float4{1, 1, 1, 1};

        const auto& MaterialParams = m_Network.GetParameters();

        auto SetFallbackValue = [&](const pxr::TfToken& Name, auto SetValue) {
            if (m_Textures.find(Name) != m_Textures.end())
                return;

            for (const auto& Param : MaterialParams)
            {
                if (Param.Type == HnMaterialParameter::ParamType::Fallback && Param.Name == Name)
                {
                    SetValue(Param.FallbackValue);
                    break;
                }
            }
        };

        SetFallbackValue(HnTokens->diffuseColor, [this](const pxr::VtValue& Val) {
            m_BasicShaderAttribs.BaseColorFactor = float4{float3::MakeVector(Val.Get<pxr::GfVec3f>().data()), 1};
        });
        SetFallbackValue(HnTokens->metallic, [this](const pxr::VtValue& Val) {
            m_BasicShaderAttribs.MetallicFactor = Val.Get<float>();
        });
        SetFallbackValue(HnTokens->roughness, [this](const pxr::VtValue& Val) {
            m_BasicShaderAttribs.RoughnessFactor = Val.Get<float>();
        });
        SetFallbackValue(HnTokens->occlusion, [this](const pxr::VtValue& Val) {
            m_BasicShaderAttribs.OcclusionFactor = Val.Get<float>();
        });

        m_BasicShaderAttribs.AlphaMode = MaterialTagToPbrAlphaMode(m_Network.GetTag());

        m_BasicShaderAttribs.AlphaMaskCutoff   = m_Network.GetOpacityThreshold();
        m_BasicShaderAttribs.BaseColorFactor.a = m_Network.GetOpacity();
    }

    InitTextureAttribs(TexRegistry, UsdRenderer, TexNameToCoordSetMap);

    *DirtyBits = HdMaterial::Clean;
}

void HnMaterial::InitTextureAttribs(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer, const TexNameToCoordSetMapType& TexNameToCoordSetMap)
{
    auto SetTextureParams = [&](const pxr::TfToken& Name, Uint32 Idx) {
        if (Idx >= m_NumShaderTextureAttribs)
        {
            UNEXPECTED("Texture attribute index (", Idx, ") exceeds the number of texture attributes (", m_NumShaderTextureAttribs, ")");
            return;
        }

        auto coord_it                          = TexNameToCoordSetMap.find(Name);
        m_ShaderTextureAttribs[Idx].UVSelector = coord_it != TexNameToCoordSetMap.end() ?
            static_cast<float>(coord_it->second) :
            0;

        auto tex_it = m_Textures.find(Name);
        if (tex_it == m_Textures.end())
        {
            tex_it = m_Textures.emplace(Name, GetDefaultTexture(TexRegistry, Name)).first;
        }

        ITextureAtlasSuballocation* pAtlasSuballocation = tex_it->second->pAtlasSuballocation;
        if (pAtlasSuballocation != nullptr)
        {
            m_ShaderTextureAttribs[Idx].TextureSlice = static_cast<float>(pAtlasSuballocation->GetSlice());
            m_ShaderTextureAttribs[Idx].UVScaleBias  = pAtlasSuballocation->GetUVScaleBias();

            m_UsesAtlas = true;
        }
        else
        {
            m_ShaderTextureAttribs[Idx].TextureSlice = 0;
            m_ShaderTextureAttribs[Idx].UVScaleBias  = float4{1, 1, 0, 0};
        }
    };

    const auto& TexAttribIndices = UsdRenderer.GetShaderTextureAttributeIndices();
    // clang-format off
    SetTextureParams(HnTokens->diffuseColor,  TexAttribIndices.BaseColor);
    SetTextureParams(HnTokens->normal,        TexAttribIndices.Normal);
    SetTextureParams(HnTokens->metallic,      TexAttribIndices.Metallic);
    SetTextureParams(HnTokens->roughness,     TexAttribIndices.Roughness);
    SetTextureParams(HnTokens->occlusion,     TexAttribIndices.Occlusion);
    SetTextureParams(HnTokens->emissiveColor, TexAttribIndices.Emissive);
    // clang-format on
}

static RefCntAutoPtr<Image> CreateDefaultImage(const pxr::TfToken& Name)
{
    ImageDesc ImgDesc;
    ImgDesc.Width         = 64;
    ImgDesc.Height        = 64;
    ImgDesc.ComponentType = VT_UINT8;
    RefCntAutoPtr<IDataBlob> pData;

    auto InitData = [&](Uint32 NumComponents, int Value) {
        ImgDesc.NumComponents = NumComponents;
        ImgDesc.RowStride     = ImgDesc.Width * ImgDesc.NumComponents;
        pData                 = DataBlobImpl::Create(size_t{ImgDesc.RowStride} * size_t{ImgDesc.Height});
        if (Value >= 0)
        {
            memset(pData->GetDataPtr(), Value, pData->GetSize());
        }
    };
    if (Name == HnTokens->diffuseColor)
    {
        InitData(4, 255);
    }
    else if (Name == HnTokens->metallic)
    {
        InitData(1, 255);
    }
    else if (Name == HnTokens->roughness)
    {
        InitData(1, 255);
    }
    else if (Name == HnTokens->normal)
    {
        InitData(4, -1);

        Uint8* pDst = reinterpret_cast<Uint8*>(pData->GetDataPtr());
        for (size_t i = 0; i < pData->GetSize(); i += 4)
        {
            pDst[i + 0] = 128;
            pDst[i + 1] = 128;
            pDst[i + 2] = 255;
            pDst[i + 3] = 0;
        }
    }
    else if (Name == HnTokens->occlusion)
    {
        InitData(4, 255);
    }
    else if (Name == HnTokens->emissiveColor)
    {
        InitData(4, 0);
    }
    else
    {
        UNEXPECTED("Unknown texture name '", Name, "'");
        InitData(4, 0);
    }

    RefCntAutoPtr<Image> pImage;
    Image::CreateFromMemory(ImgDesc, pData, &pImage);
    VERIFY_EXPR(pImage);
    return pImage;
}

static pxr::TfToken GetDefaultTexturePath(const pxr::TfToken& Name)
{
    return pxr::TfToken{std::string{"$Default "} + Name.GetString()};
}

HnTextureRegistry::TextureHandleSharedPtr HnMaterial::GetDefaultTexture(HnTextureRegistry& TexRegistry, const pxr::TfToken& Name)
{
    const pxr::TfToken DefaultTexPath = GetDefaultTexturePath(Name);

    pxr::HdSamplerParameters SamplerParams;
    SamplerParams.wrapS     = pxr::HdWrapRepeat;
    SamplerParams.wrapT     = pxr::HdWrapRepeat;
    SamplerParams.wrapR     = pxr::HdWrapRepeat;
    SamplerParams.minFilter = pxr::HdMinFilterLinearMipmapLinear;
    SamplerParams.magFilter = pxr::HdMagFilterLinear;
    return TexRegistry.Allocate(DefaultTexPath, SamplerParams,
                                [&]() {
                                    RefCntAutoPtr<Image> pImage = CreateDefaultImage(Name);

                                    TextureLoadInfo               LoadInfo{Name.GetText()};
                                    RefCntAutoPtr<ITextureLoader> pLoader;
                                    CreateTextureLoaderFromImage(pImage, LoadInfo, &pLoader);
                                    VERIFY_EXPR(pLoader);
                                    return pLoader;
                                });
}

void HnMaterial::AllocateTextures(HnTextureRegistry& TexRegistry, TexNameToCoordSetMapType& TexNameToCoordSetMap)
{
    std::unordered_map<pxr::TfToken, size_t, pxr::TfToken::HashFunctor> TexCoordMapping;
    for (const HnMaterialNetwork::TextureDescriptor& TexDescriptor : m_Network.GetTextures())
    {
        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId, TexDescriptor.SamplerParams))
        {
            m_Textures[TexDescriptor.Name] = pTex;
            // Find texture coordinate
            size_t TexCoordIdx = ~size_t{0};
            for (const HnMaterialParameter& Param : m_Network.GetParameters())
            {
                if (Param.Type == HnMaterialParameter::ParamType::Texture && Param.Name == TexDescriptor.Name)
                {
                    if (!Param.SamplerCoords.empty())
                    {
                        if (Param.SamplerCoords.size() > 1)
                            LOG_WARNING_MESSAGE("Texture '", TexDescriptor.Name, "' has ", Param.SamplerCoords.size(), " texture coordinates. Only the first set will be used");
                        const pxr::TfToken& TexCoordName = Param.SamplerCoords[0];

                        auto it_inserted = TexCoordMapping.emplace(TexCoordName, TexCoordMapping.size());
                        TexCoordIdx      = it_inserted.first->second;
                        if (it_inserted.second)
                        {
                            m_TexCoords.resize(TexCoordIdx + 1);
                            m_TexCoords[TexCoordIdx] = {TexCoordName};
                        }
                    }
                    else
                    {
                        LOG_ERROR_MESSAGE("Texture '", TexDescriptor.Name, "' has no texture coordinates");
                    }
                    break;
                }
            }

            if (TexCoordIdx == ~size_t{0})
            {
                LOG_ERROR_MESSAGE("Failed to find texture coordinates for texture '", TexDescriptor.Name, "'");
            }
        }
    }
}

const HnTextureRegistry::TextureHandleSharedPtr& HnMaterial::GetTexture(const pxr::TfToken& Name) const
{
    static const HnTextureRegistry::TextureHandleSharedPtr NullHandle;

    auto tex_it = m_Textures.find(Name);
    return tex_it != m_Textures.end() ? tex_it->second : NullHandle;
}

pxr::HdDirtyBits HnMaterial::GetInitialDirtyBitsMask() const
{
    return pxr::HdMaterial::AllDirty;
}

void HnMaterial::UpdateSRB(IRenderDevice* pDevice,
                           PBR_Renderer&  PbrRenderer,
                           IBuffer*       pFrameAttribs,
                           Uint32         AtlasVersion)
{
    if (m_UsesAtlas && AtlasVersion != m_AtlasVersion)
        m_SRB.Release();

    if (m_SRB)
        return;

    PbrRenderer.CreateResourceBinding(&m_SRB);
    VERIFY_EXPR(m_SRB);
    m_PrimitiveAttribsVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
    VERIFY_EXPR(m_PrimitiveAttribsVar != nullptr);

    if (IShaderResourceVariable* pVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs"))
    {
        // Primitive attribs buffer is a large buffer that fits multiple primitives.
        // In the render loop, we write multiple primitive attribs into this buffer
        // and use the SetBufferOffset function to select the attribs for the current primitive.
        pVar->SetBufferRange(PbrRenderer.GetPBRPrimitiveAttribsCB(), 0, PbrRenderer.GetPBRPrimitiveAttribsSize());
    }
    else
    {
        UNEXPECTED("Failed to find 'cbPrimitiveAttribs' variable in the shader resource binding");
    }

    PbrRenderer.InitCommonSRBVars(m_SRB, pFrameAttribs);

    auto SetTexture = [&](const pxr::TfToken& Name, const char* VarName) //
    {
        const HnTextureRegistry::TextureHandleSharedPtr& pTexHandle = GetTexture(Name);
        if (!pTexHandle)
        {
            UNEXPECTED("Texture '", Name, "' is not initialized. This is unexpected as at least the default texture must always be set.");
            return;
        }

        RefCntAutoPtr<ITextureView> pTexSRV;
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
        else if (pTexHandle->pAtlasSuballocation)
        {
            pTexSRV = pTexHandle->pAtlasSuballocation->GetAtlas()->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        else
        {
            UNEXPECTED("Texture '", Name, "' is not loaded");
        }

        if (auto* pVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
            pVar->Set(pTexSRV);
    };

    // clang-format off
    SetTexture(HnTokens->diffuseColor,  "g_ColorMap");
    SetTexture(HnTokens->metallic,      "g_MetallicMap");
    SetTexture(HnTokens->roughness,     "g_RoughnessMap");
    SetTexture(HnTokens->normal,        "g_NormalMap");
    SetTexture(HnTokens->occlusion,     "g_AOMap");
    SetTexture(HnTokens->emissiveColor, "g_EmissiveMap");
    // clang-format on

    m_AtlasVersion = AtlasVersion;
}

} // namespace USD

} // namespace Diligent
