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
#include "GfTypeConversions.hpp"
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

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnMaterialPrivateTokens,
    (whiteRgba8)
    (blackRgba8)
    (whiteR8)
);
// clang-format on


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

// Default material
HnMaterial::HnMaterial(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer) :
    HnMaterial{pxr::SdfPath{}}
{
    // Sync() is never called for the default material, so we need to initialize texture attributes now.
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

    // A mapping from the texture name to the texture coordinate set index (e.g. "diffuseColor" -> 0)
    TexNameToCoordSetMapType TexNameToCoordSetMap;

    pxr::VtValue vtMat = SceneDelegate->GetMaterialResource(GetId());
    if (vtMat.IsHolding<pxr::HdMaterialNetworkMap>())
    {
        const pxr::HdMaterialNetworkMap& hdNetworkMap = vtMat.UncheckedGet<pxr::HdMaterialNetworkMap>();
        if (!hdNetworkMap.terminals.empty() && !hdNetworkMap.map.empty())
        {
            try
            {
                m_Network = HnMaterialNetwork{GetId(), hdNetworkMap}; // May throw

                TexNameToCoordSetMap = AllocateTextures(TexRegistry);
                ProcessMaterialNetwork();
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
    }

    // It is important to initialize texture attributes with default values even if there is no material network.
    InitTextureAttribs(TexRegistry, UsdRenderer, TexNameToCoordSetMap);

    *DirtyBits = HdMaterial::Clean;
}

void HnMaterial::ProcessMaterialNetwork()
{
    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->diffuseColor))
    {
        m_BasicShaderAttribs.BaseColorFactor = float4{ToFloat3(Param->FallbackValue.Get<pxr::GfVec3f>()), 1};
    }

    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->metallic))
    {
        m_BasicShaderAttribs.MetallicFactor = Param->FallbackValue.Get<float>();
    }

    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->roughness))
    {
        m_BasicShaderAttribs.RoughnessFactor = Param->FallbackValue.Get<float>();
    }

    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->occlusion))
    {
        m_BasicShaderAttribs.OcclusionFactor = Param->FallbackValue.Get<float>();
    }

    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->emissiveColor))
    {
        m_BasicShaderAttribs.EmissiveFactor = float4{ToFloat3(Param->FallbackValue.Get<pxr::GfVec3f>()), 1};
    }
    else
    {
        m_BasicShaderAttribs.EmissiveFactor = m_Textures.find(HnTokens->emissiveColor) != m_Textures.end() ? float4{1} : float4{0};
    }

    m_BasicShaderAttribs.AlphaMode = MaterialTagToPbrAlphaMode(m_Network.GetTag());

    m_BasicShaderAttribs.AlphaMaskCutoff   = m_Network.GetOpacityThreshold();
    m_BasicShaderAttribs.BaseColorFactor.a = m_Network.GetOpacity();
}

void HnMaterial::InitTextureAttribs(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer, const TexNameToCoordSetMapType& TexNameToCoordSetMap)
{
    VERIFY_EXPR(m_NumShaderTextureAttribs > 0 && m_ShaderTextureAttribs != nullptr);

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

        HLSL::PBRMaterialTextureAttribs& TexAttribs{m_ShaderTextureAttribs[Idx]};

        TexAttribs.UBias              = 0;
        TexAttribs.VBias              = 0;
        TexAttribs.UVScaleAndRotation = float2x2::Identity().ToVec4<>();

        auto tex_it = m_Textures.find(Name);
        if (tex_it != m_Textures.end())
        {
            if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Transform2d, Name))
            {
                float2x2 UVScaleAndRotation = float2x2::Scale(Param->Transform2d.Scale[0], Param->Transform2d.Scale[1]);
                float    Rotation           = Param->Transform2d.Rotation;
                if (Rotation != 0)
                {
                    UVScaleAndRotation *= float2x2::Rotation(DegToRad(Rotation));
                }

                TexAttribs.UBias = Param->Transform2d.Translation[0];
                TexAttribs.VBias = Param->Transform2d.Translation[1];

                TexAttribs.UVScaleAndRotation = UVScaleAndRotation.ToVec4<>();
            }
        }
        else
        {
            tex_it = m_Textures.emplace(Name, GetDefaultTexture(TexRegistry, Name)).first;
        }

        if (ITextureAtlasSuballocation* pAtlasSuballocation = tex_it->second->pAtlasSuballocation)
        {
            TexAttribs.TextureSlice        = static_cast<float>(pAtlasSuballocation->GetSlice());
            TexAttribs.AtlasUVScaleAndBias = pAtlasSuballocation->GetUVScaleBias();

            m_UsesAtlas = true;
        }
        else
        {
            TexAttribs.TextureSlice        = 0;
            TexAttribs.AtlasUVScaleAndBias = float4{1, 1, 0, 0};
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

static RefCntAutoPtr<Image> CreateDefaultImage(const pxr::TfToken& Name, Uint32 Dimension = 64)
{
    ImageDesc ImgDesc;
    ImgDesc.Width         = Dimension;
    ImgDesc.Height        = Dimension;
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

    if (Name == HnMaterialPrivateTokens->whiteRgba8)
    {
        InitData(4, 255);
    }
    else if (Name == HnMaterialPrivateTokens->blackRgba8)
    {
        InitData(4, 0);
    }
    else if (Name == HnMaterialPrivateTokens->whiteR8)
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
    return pxr::TfToken{std::string{"$Default-"} + Name.GetString()};
}

HnTextureRegistry::TextureHandleSharedPtr HnMaterial::GetDefaultTexture(HnTextureRegistry& TexRegistry, const pxr::TfToken& Name)
{
    pxr::TfToken DefaultTexName;
    if (Name == HnTokens->diffuseColor ||
        Name == HnTokens->emissiveColor)
    {
        DefaultTexName = HnMaterialPrivateTokens->whiteRgba8;
    }
    else if (Name == HnTokens->normal)
    {
        DefaultTexName = HnTokens->normal;
    }
    else if (Name == HnTokens->metallic ||
             Name == HnTokens->roughness ||
             Name == HnTokens->occlusion)
    {
        DefaultTexName = HnMaterialPrivateTokens->whiteR8;
    }
    else
    {
        UNEXPECTED("Unknown texture name '", Name, "'");
        DefaultTexName = HnMaterialPrivateTokens->blackRgba8;
    }

    const pxr::TfToken DefaultTexPath = GetDefaultTexturePath(DefaultTexName);

    pxr::HdSamplerParameters SamplerParams;
    SamplerParams.wrapS     = pxr::HdWrapRepeat;
    SamplerParams.wrapT     = pxr::HdWrapRepeat;
    SamplerParams.wrapR     = pxr::HdWrapRepeat;
    SamplerParams.minFilter = pxr::HdMinFilterLinearMipmapLinear;
    SamplerParams.magFilter = pxr::HdMagFilterLinear;
    return TexRegistry.Allocate(DefaultTexPath, TextureComponentMapping::Identity(), SamplerParams,
                                [&]() {
                                    RefCntAutoPtr<Image> pImage = CreateDefaultImage(DefaultTexName);

                                    TextureLoadInfo               LoadInfo{Name.GetText()};
                                    RefCntAutoPtr<ITextureLoader> pLoader;
                                    CreateTextureLoaderFromImage(pImage, LoadInfo, &pLoader);
                                    VERIFY_EXPR(pLoader);
                                    return pLoader;
                                });
}

static TEXTURE_FORMAT GetMaterialTextureFormat(const pxr::TfToken& Name)
{
    if (Name == HnTokens->diffuseColor ||
        Name == HnTokens->emissiveColor ||
        Name == HnTokens->normal)
    {
        return TEX_FORMAT_RGBA8_UNORM;
    }
    else if (Name == HnTokens->metallic ||
             Name == HnTokens->roughness ||
             Name == HnTokens->occlusion ||
             Name == HnTokens->opacity)
    {
        return TEX_FORMAT_R8_UNORM;
    }
    else
    {
        LOG_ERROR("Unknown texture name '", Name, "'");
        return TEX_FORMAT_UNKNOWN;
    }
}

HnMaterial::TexNameToCoordSetMapType HnMaterial::AllocateTextures(HnTextureRegistry& TexRegistry)
{
    // Texture name to texture coordinate set index (e.g. "diffuseColor" -> 0)
    TexNameToCoordSetMapType TexNameToCoordSetMap;

    // Texture coordinate primvar name to texture coordinate set index (e.g. "st" -> 0)
    std::unordered_map<pxr::TfToken, size_t, pxr::TfToken::HashFunctor> TexCoordPrimvarMapping;
    for (const HnMaterialNetwork::TextureDescriptor& TexDescriptor : m_Network.GetTextures())
    {
        if (TexDescriptor.TextureId.FilePath.IsEmpty())
        {
            LOG_ERROR_MESSAGE("Texture '", TexDescriptor.Name, "' in material '", GetId(), "' has no file path");
            continue;
        }

        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId, GetMaterialTextureFormat(TexDescriptor.Name), TexDescriptor.SamplerParams))
        {
            m_Textures[TexDescriptor.Name] = pTex;
            // Find texture coordinate
            size_t TexCoordIdx = ~size_t{0};
            if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Texture, TexDescriptor.Name))
            {
                if (!Param->SamplerCoords.empty())
                {
                    if (Param->SamplerCoords.size() > 1)
                        LOG_WARNING_MESSAGE("Texture '", TexDescriptor.Name, "' has ", Param->SamplerCoords.size(), " texture coordinates. Only the first set will be used");
                    const pxr::TfToken& TexCoordName = Param->SamplerCoords[0];

                    // Check if the texture coordinate set primvar (e.g. "st0") has already been allocated
                    auto it_inserted = TexCoordPrimvarMapping.emplace(TexCoordName, m_TexCoords.size());
                    TexCoordIdx      = it_inserted.first->second;
                    if (it_inserted.second)
                    {
                        // Add new texture coordinate set
                        VERIFY_EXPR(TexCoordIdx == m_TexCoords.size());
                        m_TexCoords.resize(TexCoordIdx + 1);
                        m_TexCoords[TexCoordIdx] = {TexCoordName};
                    }

                    TexNameToCoordSetMap[TexDescriptor.Name] = TexCoordIdx;
                }
                else
                {
                    LOG_ERROR_MESSAGE("Texture '", TexDescriptor.Name, "' in material '", GetId(), "' has no texture coordinates");
                }
            }

            if (TexCoordIdx == ~size_t{0})
            {
                LOG_ERROR_MESSAGE("Failed to find texture coordinates for texture '", TexDescriptor.Name, "' in material '", GetId(), "'");
            }
        }
    }

    return TexNameToCoordSetMap;
}

RefCntAutoPtr<ITexture> HnMaterial::GetTexture(const pxr::TfToken& Name) const
{
    auto tex_it = m_Textures.find(Name);
    if (tex_it != m_Textures.end())
    {
        const HnTextureRegistry::TextureHandleSharedPtr& pTexHandle = tex_it->second;
        if (pTexHandle->pTexture)
        {
            const auto& TexDesc = pTexHandle->pTexture->GetDesc();
            VERIFY(TexDesc.Type == RESOURCE_DIM_TEX_2D_ARRAY, "2D textures should be loaded as single-slice 2D array textures");
            return pTexHandle->pTexture;
        }
        else if (pTexHandle->pAtlasSuballocation)
        {
            return RefCntAutoPtr<ITexture>{pTexHandle->pAtlasSuballocation->GetAtlas()->GetTexture()};
        }
        else
        {
            UNEXPECTED("Texture '", Name, "' is not initialized. This likely indicates that HnRenderDelegate::CommitResources() was not called.");
            return {};
        }
    }
    else
    {
        UNEXPECTED("Texture '", Name, "' is not found. This is unexpected as at least the default texture must always be set.");
        return {};
    }
}

pxr::HdDirtyBits HnMaterial::GetInitialDirtyBitsMask() const
{
    return pxr::HdMaterial::AllDirty;
}

// {AFEC3E3E-021D-4BA6-9464-CB7E356DE15D}
static const INTERFACE_ID IID_HnMaterialSRBCache =
    {0xafec3e3e, 0x21d, 0x4ba6, {0x94, 0x64, 0xcb, 0x7e, 0x35, 0x6d, 0xe1, 0x5d}};

class HnMaterialSRBCache : public ObjectBase<IObject>
{
public:
    HnMaterialSRBCache(IReferenceCounters* pRefCounters) :
        ObjectBase<IObject>{pRefCounters}
    {}

    static RefCntAutoPtr<IObject> Create()
    {
        return RefCntAutoPtr<IObject>{MakeNewRCObj<HnMaterialSRBCache>()()};
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_HnMaterialSRBCache, ObjectBase<IObject>)

    struct ResourceKey
    {
        std::vector<Int32> UniqueIDs;

        bool operator==(const ResourceKey& rhs) const
        {
            return UniqueIDs == rhs.UniqueIDs;
        }

        struct Hasher
        {
            size_t operator()(const ResourceKey& Key) const
            {
                return ComputeHashRaw(Key.UniqueIDs.data(), Key.UniqueIDs.size() * sizeof(Key.UniqueIDs[0]));
            }
        };
    };

    template <class CreateSRBFuncType>
    RefCntAutoPtr<IShaderResourceBinding> GetSRB(const ResourceKey& Key, CreateSRBFuncType&& CreateSRB)
    {
        return m_Cache.Get(Key, CreateSRB);
    }

private:
    ObjectsRegistry<ResourceKey, RefCntAutoPtr<IShaderResourceBinding>, ResourceKey::Hasher> m_Cache;
};

RefCntAutoPtr<IObject> HnMaterial::CreateSRBCache()
{
    return HnMaterialSRBCache::Create();
}

void HnMaterial::UpdateSRB(IObject*      pSRBCache,
                           USD_Renderer& UsdRenderer,
                           IBuffer*      pFrameAttribs,
                           Uint32        AtlasVersion)
{
    RefCntAutoPtr<HnMaterialSRBCache> Cache{pSRBCache, IID_HnMaterialSRBCache};
    VERIFY_EXPR(Cache);

    if (m_UsesAtlas && AtlasVersion != m_AtlasVersion)
    {
        m_SRB.Release();
        m_PrimitiveAttribsVar = nullptr;
    }

    if (m_SRB)
        return;

    RefCntAutoPtr<ITexture> pDiffuseMap   = GetTexture(HnTokens->diffuseColor);
    RefCntAutoPtr<ITexture> pNormalMap    = GetTexture(HnTokens->normal);
    RefCntAutoPtr<ITexture> pMetallicMap  = GetTexture(HnTokens->metallic);
    RefCntAutoPtr<ITexture> pRoughnessMap = GetTexture(HnTokens->roughness);
    RefCntAutoPtr<ITexture> pOcclusionMap = GetTexture(HnTokens->occlusion);
    RefCntAutoPtr<ITexture> pEmissiveMap  = GetTexture(HnTokens->emissiveColor);

    HnMaterialSRBCache::ResourceKey Key{{
        pDiffuseMap ? pDiffuseMap->GetUniqueID() : 0,
        pNormalMap ? pNormalMap->GetUniqueID() : 0,
        pMetallicMap ? pMetallicMap->GetUniqueID() : 0,
        pRoughnessMap ? pRoughnessMap->GetUniqueID() : 0,
        pOcclusionMap ? pOcclusionMap->GetUniqueID() : 0,
        pEmissiveMap ? pEmissiveMap->GetUniqueID() : 0,
    }};

    m_SRB = Cache->GetSRB(Key, [&]() {
        RefCntAutoPtr<IShaderResourceBinding> pSRB;

        UsdRenderer.CreateResourceBinding(&pSRB);
        VERIFY_EXPR(pSRB);

        if (IShaderResourceVariable* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs"))
        {
            // Primitive attribs buffer is a large buffer that fits multiple primitives.
            // In the render loop, we write multiple primitive attribs into this buffer
            // and use the SetBufferOffset function to select the attribs for the current primitive.
            pVar->SetBufferRange(UsdRenderer.GetPBRPrimitiveAttribsCB(), 0, UsdRenderer.GetPBRPrimitiveAttribsSize());
        }
        else
        {
            UNEXPECTED("Failed to find 'cbPrimitiveAttribs' variable in the shader resource binding");
        }

        UsdRenderer.InitCommonSRBVars(pSRB, pFrameAttribs);

        auto SetTexture = [&](const char* VarName, ITexture* pTex) //
        {
            if (pTex == nullptr)
                return;

            if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName))
                pVar->Set(pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        };

        // clang-format off
        SetTexture("g_ColorMap",     pDiffuseMap);
        SetTexture("g_MetallicMap",  pMetallicMap);
        SetTexture("g_RoughnessMap", pRoughnessMap);
        SetTexture("g_NormalMap",    pNormalMap);
        SetTexture("g_AOMap",        pOcclusionMap);
        SetTexture("g_EmissiveMap",  pEmissiveMap);
        // clang-format on

        return pSRB;
    });

    if (m_SRB)
    {
        m_PrimitiveAttribsVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
        VERIFY_EXPR(m_PrimitiveAttribsVar != nullptr);
    }
    else
    {
        UNEXPECTED("Failed to create shader resource binding for material ", GetId());
    }

    m_AtlasVersion = AtlasVersion;
}

} // namespace USD

} // namespace Diligent
