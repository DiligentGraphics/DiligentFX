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

#include "HnMaterial.hpp"

#include <vector>
#include <set>

#include "HnRenderDelegate.hpp"
#include "HnTokens.hpp"
#include "HnTypeConversions.hpp"
#include "HnRenderPass.hpp"
#include "HnRenderParam.hpp"

#include "GfTypeConversions.hpp"
#include "DynamicTextureAtlas.h"
#include "GLTFResourceManager.hpp"
#include "GLTFBuilder.hpp"
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
    m_MaterialData.Attribs.BaseColorFactor = float4{1, 1, 1, 1};
    m_MaterialData.Attribs.SpecularFactor  = float4{1, 1, 1, 1};
    m_MaterialData.Attribs.MetallicFactor  = 1;
    m_MaterialData.Attribs.RoughnessFactor = 1;
    m_MaterialData.Attribs.OcclusionFactor = 1;

    m_MaterialData.Attribs.Workflow = PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH;
}


// Default material
HnMaterial::HnMaterial(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer) :
    HnMaterial{pxr::SdfPath{}}
{
    // Sync() is never called for the default material, so we need to initialize texture attributes now.
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

    if (RenderParam)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::Material);
    }

    *DirtyBits = HdMaterial::Clean;
}

static bool ReadFallbackValue(const HnMaterialNetwork& Network, const pxr::TfToken& Name, float4& Value)
{
    if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Fallback, Name))
    {
        Value = float4{ToFloat3(Param->FallbackValue.Get<pxr::GfVec3f>()), 1};
        return true;
    }

    return false;
}

static bool ReadFallbackValue(const HnMaterialNetwork& Network, const pxr::TfToken& Name, float& Value)
{
    if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Fallback, Name))
    {
        Value = Param->FallbackValue.Get<float>();
        return true;
    }

    return false;
}

template <typename T>
static void ApplyTextureInputScale(const HnMaterialNetwork& Network, const pxr::TfToken& Name, T& Value)
{
    if (const HnMaterialParameter* TexParam = Network.GetParameter(HnMaterialParameter::ParamType::Texture, Name))
    {
        for (size_t i = 0; i < Value.GetComponentCount(); ++i)
            Value[i] *= TexParam->InputScale[i];
    }
}

static void ApplyTextureInputScale(const HnMaterialNetwork& Network, const pxr::TfToken& Name, float& Value)
{
    if (const HnMaterialParameter* TexParam = Network.GetParameter(HnMaterialParameter::ParamType::Texture, Name))
    {
        Value *= TexParam->InputScale[0];
    }
}

void HnMaterial::ProcessMaterialNetwork()
{
    ReadFallbackValue(m_Network, HnTokens->diffuseColor, m_MaterialData.Attribs.BaseColorFactor);
    ReadFallbackValue(m_Network, HnTokens->metallic, m_MaterialData.Attribs.MetallicFactor);
    ReadFallbackValue(m_Network, HnTokens->roughness, m_MaterialData.Attribs.RoughnessFactor);
    ReadFallbackValue(m_Network, HnTokens->occlusion, m_MaterialData.Attribs.OcclusionFactor);
    if (!ReadFallbackValue(m_Network, HnTokens->emissiveColor, m_MaterialData.Attribs.EmissiveFactor))
    {
        m_MaterialData.Attribs.EmissiveFactor = m_Textures.find(HnTokens->emissiveColor) != m_Textures.end() ? float4{1} : float4{0};
    }

    ApplyTextureInputScale(m_Network, HnTokens->diffuseColor, m_MaterialData.Attribs.BaseColorFactor);
    ApplyTextureInputScale(m_Network, HnTokens->metallic, m_MaterialData.Attribs.MetallicFactor);
    ApplyTextureInputScale(m_Network, HnTokens->roughness, m_MaterialData.Attribs.RoughnessFactor);
    ApplyTextureInputScale(m_Network, HnTokens->occlusion, m_MaterialData.Attribs.OcclusionFactor);
    ApplyTextureInputScale(m_Network, HnTokens->emissiveColor, m_MaterialData.Attribs.EmissiveFactor);

    if (const HnMaterialParameter* Param = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->clearcoat))
    {
        m_MaterialData.Attribs.ClearcoatFactor = Param->FallbackValue.Get<float>();
        if (m_MaterialData.Attribs.ClearcoatFactor > 0)
        {
            m_MaterialData.HasClearcoat = true;

            if (const HnMaterialParameter* RoughnessParam = m_Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->clearcoatRoughness))
            {
                m_MaterialData.Attribs.ClearcoatRoughnessFactor = RoughnessParam->FallbackValue.Get<float>();
            }
        }
    }


    m_MaterialData.Attribs.AlphaMode = MaterialTagToPbrAlphaMode(m_Network.GetTag());

    m_MaterialData.Attribs.AlphaCutoff       = m_Network.GetOpacityThreshold();
    m_MaterialData.Attribs.BaseColorFactor.a = m_Network.GetOpacity();
}

void HnMaterial::InitTextureAttribs(HnTextureRegistry& TexRegistry, const USD_Renderer& UsdRenderer, const TexNameToCoordSetMapType& TexNameToCoordSetMap)
{
    GLTF::MaterialBuilder MatBuilder{m_MaterialData};

    auto SetTextureParams = [&](const pxr::TfToken& Name, Uint32 Idx) {
        GLTF::Material::TextureShaderAttribs& TexAttribs = MatBuilder.GetTextureAttrib(Idx);

        auto coord_it         = TexNameToCoordSetMap.find(Name);
        TexAttribs.UVSelector = coord_it != TexNameToCoordSetMap.end() ?
            static_cast<float>(coord_it->second) :
            0;

        TexAttribs.UBias              = 0;
        TexAttribs.VBias              = 0;
        TexAttribs.UVScaleAndRotation = float2x2::Identity();

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

                TexAttribs.UVScaleAndRotation = UVScaleAndRotation;
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
        }
        else
        {
            TexAttribs.TextureSlice        = static_cast<float>(tex_it->second->TextureId);
            TexAttribs.AtlasUVScaleAndBias = float4{1, 1, 0, 0};
        }
    };

    const auto& TexAttribIndices = UsdRenderer.GetSettings().TextureAttribIndices;
    // clang-format off
    SetTextureParams(HnTokens->diffuseColor,  TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR]);
    SetTextureParams(HnTokens->normal,        TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]);
    SetTextureParams(HnTokens->metallic,      TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC]);
    SetTextureParams(HnTokens->roughness,     TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS]);
    SetTextureParams(HnTokens->occlusion,     TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]);
    SetTextureParams(HnTokens->emissiveColor, TexAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]);
    // clang-format on

    MatBuilder.Finalize();
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
             Name == HnTokens->occlusion)
    {
        return TEX_FORMAT_R8_UNORM;
    }
    else
    {
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
        TEXTURE_FORMAT Format = GetMaterialTextureFormat(TexDescriptor.Name);
        if (Format == TEX_FORMAT_UNKNOWN)
        {
            LOG_INFO_MESSAGE("Skipping unknown texture '", TexDescriptor.Name, "' in material '", GetId(), "'");
            continue;
        }

        if (TexDescriptor.TextureId.FilePath.IsEmpty())
        {
            LOG_ERROR_MESSAGE("Texture '", TexDescriptor.Name, "' in material '", GetId(), "' has no file path");
            continue;
        }

        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId, Format, TexDescriptor.SamplerParams))
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
    using StaticShaderTextureIdsArrayType = PBR_Renderer::StaticShaderTextureIdsArrayType;
    using ShaderTextureIndexingIdType     = HnMaterial::ShaderTextureIndexingIdType;

    HnMaterialSRBCache(IReferenceCounters* pRefCounters) :
        ObjectBase<IObject>{pRefCounters}
    {}

    static RefCntAutoPtr<IObject> Create()
    {
        return RefCntAutoPtr<IObject>{MakeNewRCObj<HnMaterialSRBCache>()()};
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_HnMaterialSRBCache, ObjectBase<IObject>)

    /// SRB cache key
    ///
    /// The key is the combination of unique IDs of the texture objects used by the SRB.
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

    /// Adds shader texture indexing to the cache and returns its identifier, for example:
    ///     {0, 0, 0, 1, 1, 2} -> 0
    ///     {0, 1, 0, 1, 2, 2} -> 1
    ShaderTextureIndexingIdType AddShaderTextureIndexing(const StaticShaderTextureIdsArrayType& TextureIds)
    {
        std::lock_guard<std::mutex> Lock{m_ShaderTextureIndexingCacheMtx};

        auto it = m_ShaderTextureIndexingCache.find(TextureIds);
        if (it != m_ShaderTextureIndexingCache.end())
            return it->second;

        auto Id = static_cast<ShaderTextureIndexingIdType>(m_ShaderTextureIndexingCache.size());
        it      = m_ShaderTextureIndexingCache.emplace(TextureIds, Id).first;

        m_IdToIndexing.emplace(Id, it->first);

        return Id;
    }

    /// Returns the shader texture indexing by its identifier, for example:
    ///     0 -> {0, 0, 0, 1, 1, 2}
    ///     1 -> {0, 1, 0, 1, 2, 2}
    const StaticShaderTextureIdsArrayType& GetShaderTextureIndexing(Uint32 Id) const
    {
        auto it = m_IdToIndexing.find(Id);
        VERIFY_EXPR(it != m_IdToIndexing.end());
        return it->second;
    }

    void UpdatePrimitiveAttribsBufferRange(const IShaderResourceBinding* pSRB, Uint32 Range)
    {
        std::lock_guard<std::mutex> Lock{m_PrimitiveAttribsBufferRangeMtx};

        auto& SRBRange = m_PrimitiveAttribsBufferRange[pSRB];
        SRBRange       = std::max(SRBRange, Range);
    }

    Uint32 GetPrimitiveAttribsBufferRange(const IShaderResourceBinding* pSRB) const
    {
        std::lock_guard<std::mutex> Lock{m_PrimitiveAttribsBufferRangeMtx};

        auto it = m_PrimitiveAttribsBufferRange.find(pSRB);
        VERIFY(it != m_PrimitiveAttribsBufferRange.end(), "SRB is not found in the cache");
        return it != m_PrimitiveAttribsBufferRange.end() ? it->second : 0;
    }

private:
    ObjectsRegistry<ResourceKey, RefCntAutoPtr<IShaderResourceBinding>, ResourceKey::Hasher> m_Cache;

    struct ShaderTextureIndexingTypeHasher
    {
        size_t operator()(const PBR_Renderer::StaticShaderTextureIdsArrayType& TexIds) const
        {
            return ComputeHashRaw(TexIds.data(), TexIds.size() * sizeof(TexIds[0]));
        }
    };

    std::mutex m_ShaderTextureIndexingCacheMtx;
    std::unordered_map<StaticShaderTextureIdsArrayType,
                       ShaderTextureIndexingIdType,
                       ShaderTextureIndexingTypeHasher>
        m_ShaderTextureIndexingCache;

    std::unordered_map<ShaderTextureIndexingIdType, const StaticShaderTextureIdsArrayType&> m_IdToIndexing;

    // A mapping from the SRB to the maximum buffer range required by a material that uses the SRB.
    mutable std::mutex                                        m_PrimitiveAttribsBufferRangeMtx;
    std::unordered_map<const IShaderResourceBinding*, Uint32> m_PrimitiveAttribsBufferRange;
};

const PBR_Renderer::StaticShaderTextureIdsArrayType& HnMaterial::GetStaticShaderTextureIds(IObject* SRBCache, ShaderTextureIndexingIdType Id)
{
    return ClassPtrCast<HnMaterialSRBCache>(SRBCache)->GetShaderTextureIndexing(Id);
}

RefCntAutoPtr<IObject> HnMaterial::CreateSRBCache()
{
    return HnMaterialSRBCache::Create();
}

// If possible, applies standard texture indexing to reduce the number of shader permutations, e.g.:
//
//  StdTexIds     TexIds0      TexArray     StdTexArray0
//     0            1           Atlas0        Atlas1
//     1 - - - - - >2 - .       Atlas1        Atlas2
//     2            0     ' - ->Atlas2        Atlas0            X is the texture not used by the renderer, e.g. PHYS_DESC
//     X           -1           Null          Atlas0              when UseSeparateMetallicRoughnessTextures is true.
//     3            0           Null
//
//  StdTexIds     TexIds1      TexArray     StdTexArray1
//     0            2           Atlas0        Atlas2
//     1            0           Atlas1        Atlas0
//     2            1     . - ->Atlas2        Atlas1
//     X           -1   .'      Null          Atlas2
//     3 - - - - - >2 -'        Null
//
//
// \note    HnRenderPass always enables the following textures (see HnRenderPass::GetMaterialPSOFlags):
//            - COLOR_MAP
//            - NORMAL_MAP
//            - METALLIC_MAP
//		      - ROUGHNESS_MAP
//            - AO_MAP
//		      - EMISSIVE_MAP
//          For pipelines that only use these textures (which is the most common case), we can use
//          standard texture indexing and have different SRBs for different texture combinations
//          instead of heaving a single SRB and multiple PSOs with different indexings.
//
//          For pipelines that use additional textures, we can still use standard indexing for the
//		    first TexturesArraySize PBR textures and use custom indexing for the rest.
static bool ApplyStandardStaticTextureIndexing(const PBR_Renderer::CreateInfo&                RendererSettings,
                                               PBR_Renderer::StaticShaderTextureIdsArrayType& StaticShaderTexIds,
                                               std::vector<ITexture*>&                        TexArray)
{
    const Uint32 TexturesArraySize = RendererSettings.MaterialTexturesArraySize;

    PBR_Renderer::StaticShaderTextureIdsArrayType StdStaticShaderTexIds;
    StdStaticShaderTexIds.fill(decltype(PBR_Renderer::InvalidMaterialTextureId){PBR_Renderer::InvalidMaterialTextureId});

    // Remapped texture array
    std::vector<ITexture*> StdTexArray(TexArray.size());
    VERIFY_EXPR(StdTexArray.size() == TexturesArraySize);

    // Skip texture attribs not used by the renderer.
    const Uint32 DisabledAttribsMask = RendererSettings.UseSeparateMetallicRoughnessTextures ?
        (1u << PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC) :
        (1u << PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC) | (1u << PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS);

    Uint16 Slot = 0;
    for (Uint32 TexAttribId = 0; TexAttribId < StaticShaderTexIds.size(); ++TexAttribId)
    {
        if ((DisabledAttribsMask & (1 << TexAttribId)) != 0)
        {
            VERIFY(StaticShaderTexIds[TexAttribId] == PBR_Renderer::InvalidMaterialTextureId, "Disabled texture slot must not be used");
            continue;
        }

        if (Slot < TexturesArraySize)
        {
            // Use first TexturesArraySize textures for standard indexing
            StdStaticShaderTexIds[TexAttribId] = Slot;

            //  StdTexIds   StdTexArray        TexIds       TexArray
            //      0        [0] Atlas2          2     . - ->Atlas0
            //      1 - - - >[1] Atlas0 - - - - >0 - '       Atlas1
            //      2        [2] Atlas1          1     . - ->Atlas2
            //      X    . ->[3] Atlas2 - .     -1   .'      Null
            //      3 - '                  ' - ->2 -'        Null
            const auto TexId = StaticShaderTexIds[TexAttribId];
            if (TexId != PBR_Renderer::InvalidMaterialTextureId)
            {
                VERIFY(TexId < TexturesArraySize, "Texture index exceeds the array size. This must be a bug.");
                VERIFY(TexArray[TexId] != nullptr, "Texture can't be null. This appears to be a bug.");
                StdTexArray[Slot] = TexArray[TexId];
            }

            ++Slot;
        }
        else if (StaticShaderTexIds[TexAttribId] != PBR_Renderer::InvalidMaterialTextureId)
        {
            // The texture doesn't fit into the standard array
            return false;
        }
    }

    StaticShaderTexIds = StdStaticShaderTexIds;
    TexArray.swap(StdTexArray);

    return true;
}

void HnMaterial::UpdateSRB(HnRenderDelegate& RendererDelegate)
{
    RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RendererDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache};
    VERIFY_EXPR(SRBCache);

    HN_MATERIAL_TEXTURES_BINDING_MODE BindingMode = static_cast<const HnRenderParam*>(RendererDelegate.GetRenderParam())->GetTextureBindingMode();

    const Uint32 AtlasVersion = RendererDelegate.GetTextureRegistry().GetAtlasVersion();
    if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS && AtlasVersion != m_AtlasVersion)
    {
        m_SRB.Release();
        m_PrimitiveAttribsVar            = nullptr;
        m_PBRPrimitiveAttribsBufferRange = 0;
        m_AtlasVersion                   = AtlasVersion;
    }

    if (m_SRB)
        return;

    const USD_Renderer&             UsdRenderer       = *RendererDelegate.GetUSDRenderer();
    const PBR_Renderer::CreateInfo& RendererSettings  = UsdRenderer.GetSettings();
    const Uint32                    TexturesArraySize = RendererSettings.MaterialTexturesArraySize;

    // Texture array to bind to "g_MaterialTextures"
    std::vector<ITexture*> TexArray;

    // Standalone textures not allocated in the atlas.
    std::unordered_map<PBR_Renderer::TEXTURE_ATTRIB_ID, ITexture*> StandaloneTextures;

    HnMaterialSRBCache::ResourceKey SRBKey;

    if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY ||
        BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
    {
        // Allocated texture atlas formats, for example:
        //     {RGBA8_UNORM, R8_UNORM, RGBA8_UNORM_SRGB}
        std::vector<TEXTURE_FORMAT> AtlasFormats;

        // Texture atlas format to atlas id, for example:
        //     RGBA8_UNORM      -> 0
        //     R8_UNORM         -> 1
        //     RGBA8_UNORM_SRGB -> 2
        std::unordered_map<TEXTURE_FORMAT, Uint32> AtlasFormatIds;
        if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
        {
            AtlasFormats = RendererDelegate.GetResourceManager().GetAllocatedAtlasFormats();
            for (Uint32 i = 0; i < AtlasFormats.size(); ++i)
            {
                AtlasFormatIds.emplace(AtlasFormats[i], i);
            }
        }

        TexArray.resize(TexturesArraySize);

        PBR_Renderer::StaticShaderTextureIdsArrayType StaticShaderTexIds;
        StaticShaderTexIds.fill(decltype(PBR_Renderer::InvalidMaterialTextureId){PBR_Renderer::InvalidMaterialTextureId});

        for (Uint32 id = 0; id < PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT; ++id)
        {
            const PBR_Renderer::TEXTURE_ATTRIB_ID ID = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(id);

            const pxr::TfToken& TexName = PBRTextureAttribIdToPxrName(ID);
            if (TexName.IsEmpty())
                continue;

            auto tex_it = m_Textures.find(TexName);
            if (tex_it == m_Textures.end())
            {
                UNEXPECTED("Texture '", TexName, "' is not found. This is unexpected as at least the default texture must always be set.");
                continue;
            }

            ITexture* pTexture = nullptr;

            const HnTextureRegistry::TextureHandleSharedPtr& pTexHandle = tex_it->second;
            if (pTexHandle->pTexture)
            {
                const auto& TexDesc = pTexHandle->pTexture->GetDesc();
                VERIFY(TexDesc.Type == RESOURCE_DIM_TEX_2D_ARRAY, "2D textures should be loaded as single-slice 2D array textures");
                pTexture = pTexHandle->pTexture;

                StandaloneTextures.emplace(ID, pTexture);
            }
            else if (pTexHandle->pAtlasSuballocation)
            {
                VERIFY_EXPR(BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS);
                pTexture = pTexHandle->pAtlasSuballocation->GetAtlas()->GetTexture();

                const TEXTURE_FORMAT AtlasFmt = pTexture->GetDesc().Format;

                auto it = AtlasFormatIds.find(AtlasFmt);
                if (it != AtlasFormatIds.end())
                {
                    // StaticShaderTexIds[TEXTURE_ATTRIB_ID_BASE_COLOR] -> Atlas 0
                    // StaticShaderTexIds[TEXTURE_ATTRIB_ID_METALLIC]   -> Atlas 1
                    StaticShaderTexIds[id] = it->second;
                }
                else
                {
                    UNEXPECTED("Texture atlas '", TexName, "' was not found in AtlasFormatIds. This looks to be a bug.");
                }
            }
            else
            {
                UNEXPECTED("Texture '", TexName, "' is not initialized. This likely indicates that HnRenderDelegate::CommitResources() was not called.");
                continue;
            }

            if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY)
            {
                SRBKey.UniqueIDs.push_back(pTexture ? pTexture->GetUniqueID() : 0);
            }
        }

        if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
        {
            if (StandaloneTextures.empty())
            {
                // Set texture atlases according to their indices in AtlasFormatIds, for example
                // TexArray[0] -> Atlas 0 (RGBA8_UNORM)
                // TexArray[1] -> Atlas 1 (R8_UNORM)
                // TexArray[2] -> Atlas 2 (RGBA8_UNORM_SRGB)
                for (size_t i = 0; i < AtlasFormats.size(); ++i)
                {
                    TEXTURE_FORMAT AtlasFmt      = AtlasFormats[i];
                    ITexture*      pAtlasTexture = RendererDelegate.GetResourceManager().GetTexture(AtlasFmt);
                    VERIFY_EXPR(pAtlasTexture != nullptr);
                    TexArray[i] = pAtlasTexture;
                }
            }
            else
            {
                // Track free slots in the texture array
                std::set<Uint32> FreeSlots;
                for (Uint32 i = 0; i < TexturesArraySize; ++i)
                    FreeSlots.insert(i);

                // Process slots uses by texture atlases
                for (Uint32 AtlasId : StaticShaderTexIds)
                {
                    if (AtlasId == PBR_Renderer::InvalidMaterialTextureId)
                        continue;

                    FreeSlots.erase(AtlasId);
                    if (TexArray[AtlasId] == nullptr)
                    {
                        TEXTURE_FORMAT AtlasFmt = AtlasFormats[AtlasId];
                        VERIFY_EXPR(AtlasFormatIds.find(AtlasFmt)->second == AtlasId);
                        auto* pAtlasTexture = RendererDelegate.GetResourceManager().GetTexture(AtlasFmt);
                        VERIFY_EXPR(pAtlasTexture != nullptr);
                        TexArray[AtlasId] = pAtlasTexture;
                    }
                }

                // Allocate slots for standalone textures
                for (auto& it : StandaloneTextures)
                {
                    if (FreeSlots.empty())
                    {
                        LOG_ERROR_MESSAGE("Not enough texture array slots to allocate standalone texture '", it.first, "' in material '", GetId(), "'.");
                        break;
                    }

                    const PBR_Renderer::TEXTURE_ATTRIB_ID ID       = it.first;
                    ITexture*                             pTexture = it.second;

                    Uint32 Slot = *FreeSlots.begin();
                    FreeSlots.erase(FreeSlots.begin());
                    VERIFY_EXPR(TexArray[Slot] == nullptr);
                    TexArray[Slot] = pTexture;

                    StaticShaderTexIds[ID] = Slot;
                }
            }

            // Try to use standard texture indexing to reduce the number of shader permutations.
            ApplyStandardStaticTextureIndexing(RendererSettings, StaticShaderTexIds, TexArray);

            // Construct SRB key from texture atlas object ids
            for (auto& Tex : TexArray)
            {
                SRBKey.UniqueIDs.push_back(Tex ? Tex->GetUniqueID() : 0);
            }

            m_ShaderTextureIndexingId = SRBCache->AddShaderTextureIndexing(StaticShaderTexIds);
        }
    }

    m_SRB = SRBCache->GetSRB(SRBKey, [&]() {
        RefCntAutoPtr<IShaderResourceBinding> pSRB;

        UsdRenderer.CreateResourceBinding(&pSRB, 1);
        VERIFY_EXPR(pSRB);

        // NOTE: We cannot set the cbPrimitiveAttribs buffer here because different materials that use the same SRB
        //       may require different buffer ranges. We first compute the maximum buffer range for each SRB
        //       and then set the buffer in BindPrimitiveAttribsBuffer().
        constexpr bool BindPrimitiveAttribsBuffer = false;
        UsdRenderer.InitCommonSRBVars(pSRB,
                                      nullptr, // Frame attribs buffer is in SRB0
                                      BindPrimitiveAttribsBuffer);

        if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS ||
            BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC)
        {
            if (IShaderResourceVariable* pVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_MaterialTextures"))
            {
                if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC)
                {
                    // With dynamic texture binding, there is only a single SRB
                    // that contains all textures.
                    TexArray.resize(TexturesArraySize);

                    HnTextureRegistry& TexRegistry = RendererDelegate.GetTextureRegistry();
                    TexRegistry.ProcessTextures(
                        [&TexArray](const pxr::TfToken& Name, const HnTextureRegistry::TextureHandle& Handle) {
                            if (!Handle.pTexture)
                            {
                                UNEXPECTED("Texture '", Name, "' is not initialized.");
                                return;
                            }

                            if (Handle.TextureId >= TexArray.size())
                            {
                                LOG_ERROR_MESSAGE("Texture ", Name, " uses texture array slot ", Handle.TextureId, " which is greater than the texture array size ", TexArray.size());
                                return;
                            }

                            VERIFY_EXPR(Handle.TextureId != ~0u);
                            VERIFY(TexArray[Handle.TextureId] == nullptr, "Texture ", Handle.TextureId, " is already initialized");
                            TexArray[Handle.TextureId] = Handle.pTexture;
                        });
                }

                // Set unused textures to white texture
                HnTextureRegistry::TextureHandleSharedPtr WhiteTex;
                for (auto& Tex : TexArray)
                {
                    if (!Tex)
                    {
                        if (!WhiteTex)
                        {
                            WhiteTex = GetDefaultTexture(RendererDelegate.GetTextureRegistry(), HnTokens->diffuseColor);
                            VERIFY_EXPR(WhiteTex->pTexture || WhiteTex->pAtlasSuballocation);
                        }
                        if (WhiteTex->pTexture)
                            Tex = WhiteTex->pTexture;
                        else if (WhiteTex->pAtlasSuballocation)
                            Tex = WhiteTex->pAtlasSuballocation->GetAtlas()->GetTexture();
                    }
                }

                std::vector<IDeviceObject*> TextureViews(TexturesArraySize);
                for (Uint32 i = 0; i < TexturesArraySize; ++i)
                {
                    VERIFY_EXPR(TexArray[i]);
                    TextureViews[i] = TexArray[i] ? TexArray[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
                }

                pVar->SetArray(TextureViews.data(), 0, TexturesArraySize);
            }
        }
        else
        {
            VERIFY_EXPR(BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY);
            for (Uint32 id = 0; id < PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT; ++id)
            {
                const PBR_Renderer::TEXTURE_ATTRIB_ID ID = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(id);

                const pxr::TfToken& TexName = PBRTextureAttribIdToPxrName(ID);
                if (TexName.IsEmpty())
                    continue; // Skip unrecognized attributes such as TEXTURE_ATTRIB_ID_PHYS_DESC

                auto tex_it = StandaloneTextures.find(ID);
                if (tex_it == StandaloneTextures.end())
                {
                    UNEXPECTED("Texture '", PBRTextureAttribIdToPxrName(ID), "' is not found. This is unexpected as at least the default texture must always be set.");
                    continue;
                }
                VERIFY_EXPR(tex_it->second);
                UsdRenderer.SetMaterialTexture(pSRB, tex_it->second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), ID);
            }
        }

        return pSRB;
    });

    if (m_SRB)
    {
        const auto   PSOFlags                = HnRenderPass::GetMaterialPSOFlags(*this);
        const Uint32 PBRPrimitiveAttribsSize = UsdRenderer.GetPBRPrimitiveAttribsSize(PSOFlags);
        const Uint32 PrimitiveArraySize      = std::max(UsdRenderer.GetSettings().PrimitiveArraySize, 1u);
        SRBCache->UpdatePrimitiveAttribsBufferRange(m_SRB, PBRPrimitiveAttribsSize * PrimitiveArraySize);
    }
    else
    {
        UNEXPECTED("Failed to create shader resource binding for material ", GetId());
    }
}

void HnMaterial::BindPrimitiveAttribsBuffer(HnRenderDelegate& RendererDelegate)
{
    if (m_PrimitiveAttribsVar != nullptr)
        return;

    m_PrimitiveAttribsVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
    if (m_PrimitiveAttribsVar == nullptr)
    {
        UNEXPECTED("Failed to find 'cbPrimitiveAttribs' variable in the shader resource binding.");
        return;
    }

    RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RendererDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache};
    VERIFY_EXPR(SRBCache);

    m_PBRPrimitiveAttribsBufferRange = SRBCache->GetPrimitiveAttribsBufferRange(m_SRB);
    VERIFY(m_PBRPrimitiveAttribsBufferRange != 0,
           "PBRRimitiveAttribsBufferRange is zero, which indicates the SRB was not found in the cache. This appears to be a bug.");

    if (m_PrimitiveAttribsVar->Get() != nullptr)
    {
        // The buffer has already been set by another material that uses the same SRB
        return;
    }

    USD_Renderer& UsdRenderer = *RendererDelegate.GetUSDRenderer();
    m_PrimitiveAttribsVar->SetBufferRange(UsdRenderer.GetPBRPrimitiveAttribsCB(), 0, m_PBRPrimitiveAttribsBufferRange);
}

} // namespace USD

} // namespace Diligent
