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
#include <array>

#include "HnRenderDelegate.hpp"
#include "HnTokens.hpp"
#include "HnTypeConversions.hpp"
#include "HnRenderPass.hpp"
#include "HnRenderParam.hpp"
#include "HnMaterialNetwork.hpp"
#include "HnTextureUtils.hpp"

#include "GfTypeConversions.hpp"
#include "DynamicTextureAtlas.h"
#include "DynamicBuffer.hpp"
#include "GLTFResourceManager.hpp"
#include "GLTFBuilder.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "DataBlobImpl.hpp"
#include "VariableSizeAllocationsManager.hpp"
#include "DefaultRawMemoryAllocator.hpp"

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
    (st0)
);
// clang-format on


HnMaterial* HnMaterial::Create(const pxr::SdfPath& id)
{
    return new HnMaterial{id};
}

HnMaterial* HnMaterial::CreateFallback(HnRenderDelegate& RenderDelegate)
{
    return new HnMaterial{RenderDelegate};
}

HnMaterial::HnMaterial(const pxr::SdfPath& id) :
    pxr::HdMaterial{id}
{
    m_MaterialData.Attribs.BaseColorFactor = float4{1, 1, 1, 1};
    m_MaterialData.Attribs.SpecularFactor  = float3{1, 1, 1};
    m_MaterialData.Attribs.NormalScale     = 1;
    m_MaterialData.Attribs.MetallicFactor  = 1;
    m_MaterialData.Attribs.RoughnessFactor = 1;
    m_MaterialData.Attribs.OcclusionFactor = 1;

    m_MaterialData.Attribs.Workflow = PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH;
}


// Default material
HnMaterial::HnMaterial(HnRenderDelegate& RenderDelegate) :
    HnMaterial{pxr::SdfPath{}}
{
    // Sync() is never called for the default material, so we need to initialize texture attributes now.
    AllocateTextures({}, RenderDelegate);
}

void HnMaterial::Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits == pxr::HdMaterial::Clean)
        return;

    if (*DirtyBits & pxr::HdMaterial::DirtyResource)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate());

        // A mapping from the texture name to the texture coordinate set index (e.g. "diffuseColor" -> 0)
        TexNameToCoordSetMapType TexNameToCoordSetMap;

        pxr::VtValue      vtMat = SceneDelegate->GetMaterialResource(GetId());
        HnMaterialNetwork MatNetwork;
        if (vtMat.IsHolding<pxr::HdMaterialNetworkMap>())
        {
            const pxr::HdMaterialNetworkMap& hdNetworkMap = vtMat.UncheckedGet<pxr::HdMaterialNetworkMap>();
            if (!hdNetworkMap.terminals.empty() && !hdNetworkMap.map.empty())
            {
                try
                {
                    MatNetwork = HnMaterialNetwork{GetId(), hdNetworkMap}; // May throw
                    ProcessMaterialNetwork(MatNetwork);
                }
                catch (const std::runtime_error& err)
                {
                    LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": ", err.what());
                    MatNetwork = {};
                }
                catch (...)
                {
                    LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": unknown error");
                    MatNetwork = {};
                }
            }
        }

        // It is important to initialize texture attributes with default values even if there is no material network.
        AllocateTextures(MatNetwork, *RenderDelegate);

        if (RenderParam)
        {
            static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::Material);
        }

        m_GPUDataDirty.store(true);
    }

    *DirtyBits = HdMaterial::Clean;
}

static bool ReadFallbackValue(const HnMaterialNetwork& Network, const pxr::TfToken& Name, float3& Value)
{
    if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Fallback, Name))
    {
        Value = ToFloat3(Param->FallbackValue.Get<pxr::GfVec3f>());
        return true;
    }

    return false;
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

void HnMaterial::ProcessMaterialNetwork(const HnMaterialNetwork& Network)
{
    ReadFallbackValue(Network, HnTokens->diffuseColor, m_MaterialData.Attribs.BaseColorFactor);
    ReadFallbackValue(Network, HnTokens->metallic, m_MaterialData.Attribs.MetallicFactor);
    ReadFallbackValue(Network, HnTokens->roughness, m_MaterialData.Attribs.RoughnessFactor);
    ReadFallbackValue(Network, HnTokens->occlusion, m_MaterialData.Attribs.OcclusionFactor);
    if (!ReadFallbackValue(Network, HnTokens->emissiveColor, m_MaterialData.Attribs.EmissiveFactor))
    {
        m_MaterialData.Attribs.EmissiveFactor = Network.GetTexture(HnTokens->emissiveColor) != nullptr ? float4{1} : float4{0};
    }

    ApplyTextureInputScale(Network, HnTokens->diffuseColor, m_MaterialData.Attribs.BaseColorFactor);
    ApplyTextureInputScale(Network, HnTokens->metallic, m_MaterialData.Attribs.MetallicFactor);
    ApplyTextureInputScale(Network, HnTokens->roughness, m_MaterialData.Attribs.RoughnessFactor);
    ApplyTextureInputScale(Network, HnTokens->occlusion, m_MaterialData.Attribs.OcclusionFactor);
    ApplyTextureInputScale(Network, HnTokens->emissiveColor, m_MaterialData.Attribs.EmissiveFactor);

    if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->clearcoat))
    {
        m_MaterialData.Attribs.ClearcoatFactor = Param->FallbackValue.Get<float>();
        if (m_MaterialData.Attribs.ClearcoatFactor > 0)
        {
            m_MaterialData.HasClearcoat = true;

            if (const HnMaterialParameter* RoughnessParam = Network.GetParameter(HnMaterialParameter::ParamType::Fallback, HnTokens->clearcoatRoughness))
            {
                m_MaterialData.Attribs.ClearcoatRoughnessFactor = RoughnessParam->FallbackValue.Get<float>();
            }
        }
    }

    m_Tag                            = Network.GetTag();
    m_MaterialData.Attribs.AlphaMode = MaterialTagToPbrAlphaMode(m_Tag);

    m_MaterialData.Attribs.AlphaCutoff       = Network.GetOpacityThreshold();
    m_MaterialData.Attribs.BaseColorFactor.a = Network.GetOpacity();
}

namespace
{

struct TextureParamInfo
{
    const pxr::TfToken&                   Name;
    const PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId;
};

static const std::array<TextureParamInfo, 6> kTextureParams{
    TextureParamInfo{HnTokens->diffuseColor, PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR},
    TextureParamInfo{HnTokens->normal, PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL},
    TextureParamInfo{HnTokens->metallic, PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC},
    TextureParamInfo{HnTokens->roughness, PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS},
    TextureParamInfo{HnTokens->occlusion, PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION},
    TextureParamInfo{HnTokens->emissiveColor, PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE},
};

} // namespace

void HnMaterial::InitTextureAttribs(const HnMaterialNetwork&        Network,
                                    HnTextureRegistry&              TexRegistry,
                                    const USD_Renderer&             UsdRenderer,
                                    const TexNameToCoordSetMapType& TexNameToCoordSetMap)
{
    GLTF::MaterialBuilder MatBuilder{m_MaterialData};

    const auto& TexAttribIndices = UsdRenderer.GetSettings().TextureAttribIndices;
    for (const TextureParamInfo& ParamInfo : kTextureParams)
    {
        const pxr::TfToken& Name = ParamInfo.Name;
        const int           Idx  = TexAttribIndices[ParamInfo.TextureAttribId];

        GLTF::Material::TextureShaderAttribs& TexAttribs = MatBuilder.GetTextureAttrib(Idx);

        auto coord_it = TexNameToCoordSetMap.find(Name);
        TexAttribs.SetUVSelector(coord_it != TexNameToCoordSetMap.end() ? coord_it->second : 0);

        TexAttribs.UBias              = 0;
        TexAttribs.VBias              = 0;
        TexAttribs.UVScaleAndRotation = float2x2::Identity();

        auto tex_it = m_Textures.find(Name);
        if (tex_it != m_Textures.end())
        {
            if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Transform2d, Name))
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

            // Flip V coordinate
            TexAttribs.UVScaleAndRotation._12 *= -1;
            TexAttribs.UVScaleAndRotation._22 *= -1;
            TexAttribs.VBias = 1 - TexAttribs.VBias;

            if (const HnMaterialNetwork::TextureDescriptor* TexDescriptor = Network.GetTexture(Name))
            {
                const pxr::HdSamplerParameters& SamplerParams = TexDescriptor->SamplerParams;
                TexAttribs.SetWrapUMode(HdWrapToAddressMode(SamplerParams.wrapS));
                TexAttribs.SetWrapVMode(HdWrapToAddressMode(SamplerParams.wrapT));
            }
        }
        else
        {
            tex_it = m_Textures.emplace(Name, GetDefaultTexture(TexRegistry, Name)).first;
        }
    };

    MatBuilder.Finalize();
}

bool HnMaterial::InitTextureAddressingAttribs(const USD_Renderer& UsdRenderer,
                                              HnTextureRegistry&  TexRegistry)
{
    for (const auto& tex_it : m_Textures)
    {
        if (!tex_it.second->IsInitialized())
        {
            return false;
        }
    }

    const auto& TexAttribIndices = UsdRenderer.GetSettings().TextureAttribIndices;
    for (const TextureParamInfo& ParamInfo : kTextureParams)
    {
        auto tex_it = m_Textures.find(ParamInfo.Name);
        if (tex_it == m_Textures.end())
        {
            UNEXPECTED("Texture '", ParamInfo.Name, "' not found. This should never happen as all textures are initialized in InitTextureAttribs()");
            continue;
        }

        HnTextureRegistry::TextureHandleSharedPtr& pTexHandle = tex_it->second;
        if (!pTexHandle->IsLoaded())
        {
            LOG_ERROR_MESSAGE("Texture '", ParamInfo.Name, "' in material '", GetId(), "' is not loaded.");
            pTexHandle = GetDefaultTexture(TexRegistry, ParamInfo.Name);
            if (!pTexHandle)
            {
                LOG_ERROR_MESSAGE("Failed to get default texture '", ParamInfo.Name, "' for material '", GetId(), "'");
                continue;
            }
            if (!pTexHandle->IsLoaded())
            {
                UNEXPECTED("Default texture '", ParamInfo.Name, "' is not loaded. This appears to be a bug as default textures should always be loaded.");
                continue;
            }
        }

        const int                             Idx        = TexAttribIndices[ParamInfo.TextureAttribId];
        GLTF::Material::TextureShaderAttribs& TexAttribs = m_MaterialData.GetTextureAttrib(Idx);
        if (ITextureAtlasSuballocation* pAtlasSuballocation = pTexHandle->GetAtlasSuballocation())
        {
            TexAttribs.TextureSlice        = static_cast<float>(pAtlasSuballocation->GetSlice());
            TexAttribs.AtlasUVScaleAndBias = pAtlasSuballocation->GetUVScaleBias();
        }
        else
        {
            // Write texture Id into the slice field. It will be used by the bindless shader to
            // index into the texture array.
            TexAttribs.TextureSlice        = static_cast<float>(pTexHandle->GetId());
            TexAttribs.AtlasUVScaleAndBias = float4{1, 1, 0, 0};
        }
    }

    m_TextureAddressingAttribsDirty.store(false);
    return true;
}

static RefCntAutoPtr<Image> CreateDefaultImage(const pxr::TfToken& Name, Uint32 Dimension = 32)
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

        Uint8* pDst = pData->GetDataPtr<Uint8>();
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
    Image::CreateFromPixels(ImgDesc, pData, &pImage);
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
                                /*IsAsync = */ false, // Allocate default textures synchronously to make
                                                      // them immediately available after the first Sync
                                                      // for the fallback material.
                                [&](Int64 /*MemoryBudget*/, size_t /*LoaderMemorySize*/) {
                                    RefCntAutoPtr<Image> pImage = CreateDefaultImage(DefaultTexName);

                                    TextureLoadInfo LoadInfo{Name.GetText()};
                                    LoadInfo.CompressMode = TexRegistry.GetCompressMode();
                                    HnLoadTextureResult LoadResult;
                                    CreateTextureLoaderFromImage(pImage, LoadInfo, &LoadResult.pLoader);
                                    LoadResult.LoadStatus = LoadResult.pLoader ? HN_LOAD_TEXTURE_STATUS_SUCCESS : HN_LOAD_TEXTURE_STATUS_FAILED;
                                    VERIFY_EXPR(LoadResult);
                                    return LoadResult;
                                });
}

static TEXTURE_FORMAT GetMaterialTextureFormat(const pxr::TfToken& Name, TEXTURE_LOAD_COMPRESS_MODE CompressMode)
{
    if (Name == HnTokens->diffuseColor ||
        Name == HnTokens->emissiveColor)
    {
        return TEX_FORMAT_RGBA8_UNORM;
    }
    else if (Name == HnTokens->normal)
    {
        // It is essential to use two-channel BC5 compression and not BC1
        // for normal maps to avoid artifacts
        return CompressMode != TEXTURE_LOAD_COMPRESS_MODE_NONE ? TEX_FORMAT_RG8_UNORM : TEX_FORMAT_RGBA8_UNORM;
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

void HnMaterial::AllocateTextures(const HnMaterialNetwork& Network,
                                  HnRenderDelegate&        RenderDelegate)
{
    HnTextureRegistry&  TexRegistry = RenderDelegate.GetTextureRegistry();
    const USD_Renderer& UsdRenderer = *RenderDelegate.GetUSDRenderer();

    // Keep old textures alive in the cache
    auto OldTextures = std::move(m_Textures);

    m_Textures.clear();
    m_TexCoords.clear();

    // Texture name to texture coordinate set index (e.g. "diffuseColor" -> 0)
    TexNameToCoordSetMapType TexNameToCoordSetMap;

    // Texture coordinate primvar name to texture coordinate set index (e.g. "st" -> 0)
    std::unordered_map<pxr::TfToken, size_t, pxr::TfToken::HashFunctor> TexCoordPrimvarMapping;
    for (const HnMaterialNetwork::TextureDescriptor& TexDescriptor : Network.GetTextures())
    {
        TEXTURE_FORMAT Format = GetMaterialTextureFormat(TexDescriptor.Name, TexRegistry.GetCompressMode());
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

        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId, Format, TexDescriptor.SamplerParams, /*IsAsync = */ true))
        {
            m_Textures[TexDescriptor.Name] = std::move(pTex);

            // Find texture coordinate
            if (const HnMaterialParameter* Param = Network.GetParameter(HnMaterialParameter::ParamType::Texture, TexDescriptor.Name))
            {
                const pxr::TfToken& TexCoordName = !Param->SamplerCoords.empty() ? Param->SamplerCoords[0] : HnMaterialPrivateTokens->st0;
                if (Param->SamplerCoords.empty())
                {
                    LOG_WARNING_MESSAGE("Texture '", TexDescriptor.Name, "' in material '", GetId(), "' has no texture coordinates. Using ", TexCoordName, " as fallback.");
                }
                else if (Param->SamplerCoords.size() > 1)
                {
                    LOG_WARNING_MESSAGE("Texture '", TexDescriptor.Name, "' has ", Param->SamplerCoords.size(), " texture coordinates. Using the first one (", TexCoordName, ").");
                }

                // Check if the texture coordinate set primvar (e.g. "st0") has already been allocated
                auto   it_inserted = TexCoordPrimvarMapping.emplace(TexCoordName, m_TexCoords.size());
                size_t TexCoordIdx = it_inserted.first->second;
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
                UNEXPECTED("Texture parameter '", TexDescriptor.Name,
                           "' is not found in the material network. This looks like a bug since we obtained the texture descriptor from the network, "
                           "and all textures in the network should have a corresponding parameter.");
            }
        }
    }

    InitTextureAttribs(Network, TexRegistry, UsdRenderer, TexNameToCoordSetMap);
    // Update texture addressing attribs when all textures are loaded
    m_TextureAddressingAttribsDirty.store(true);

    AllocateBufferSpace(RenderDelegate);

    // Make sure we recreate the SRB next time UpdateSRB() is called
    m_ResourceCacheVersion = ~0u;
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
        ObjectBase<IObject>{pRefCounters},
        m_Cache{/*NumRequestsToPurge = */ 128},
        m_BufferRegionMgr{
            VariableSizeAllocationsManager::CreateInfo{
                DefaultRawMemoryAllocator::GetAllocator(),
                64 << 10,
                true, // DisableDebugValidation,
            },
        },
        m_MaterialAttribsBuffer{
            nullptr,
            DynamicBufferCreateInfo{
                BufferDesc{
                    "Material attribs buffer",
                    0,
                    BIND_UNIFORM_BUFFER,
                    USAGE_DEFAULT,
                },
            },
        }
    {}

    ~HnMaterialSRBCache()
    {
        VERIFY(m_BufferRegionMgr.GetUsedSize() == 0, "Not all buffer regions have been released");
    }

    static RefCntAutoPtr<IObject> Create()
    {
        return RefCntAutoPtr<IObject>{MakeNewRCObj<HnMaterialSRBCache>()()};
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_HnMaterialSRBCache, ObjectBase<IObject>)

    void Initialize(HnRenderDelegate& RenderDelegate)
    {
        m_ConstantBufferOffsetAlignment = RenderDelegate.GetDevice()->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment;
        m_MaxAttribsDataSize            = RenderDelegate.GetUSDRenderer()->GetPBRMaterialAttribsSize(PBR_Renderer::PSO_FLAG_ALL);
    }

    /// SRB cache key
    ///
    /// The key is the combination of unique IDs of the texture objects used by the SRB.
    struct ResourceKey
    {
        const Uint32 ResourceCacheVersion;

        std::vector<Int32> UniqueIDs;

        bool operator==(const ResourceKey& rhs) const
        {
            return ResourceCacheVersion == rhs.ResourceCacheVersion && UniqueIDs == rhs.UniqueIDs;
        }

        struct Hasher
        {
            size_t operator()(const ResourceKey& Key) const
            {
                return ComputeHash(
                    Key.ResourceCacheVersion,
                    ComputeHashRaw(Key.UniqueIDs.data(), Key.UniqueIDs.size() * sizeof(Key.UniqueIDs[0])));
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

    void AllocateBufferOffset(Uint32& Offset, Uint32& Size, Uint32 RequiredSize)
    {
        std::lock_guard<std::mutex> Lock{m_BufferRegionMgrMtx};

        VERIFY(m_ConstantBufferOffsetAlignment != 0 && m_MaxAttribsDataSize != 0, "The cache is not initialized");
        // Release the previously allocated region
        if (Offset != ~0u)
        {
            VERIFY_EXPR(Size != 0);
            VERIFY_EXPR(AlignUp(Offset, m_ConstantBufferOffsetAlignment) == Offset);
            m_BufferRegionMgr.Free(Offset, AlignUp(Size, m_ConstantBufferOffsetAlignment));
        }

        Size = RequiredSize;
        if (RequiredSize != 0)
        {
            RequiredSize = AlignUp(RequiredSize, m_ConstantBufferOffsetAlignment);

            VariableSizeAllocationsManager::Allocation Allocation = m_BufferRegionMgr.Allocate(RequiredSize, 1);
            if (!Allocation.IsValid())
            {
                VERIFY_EXPR(RequiredSize <= m_BufferRegionMgr.GetMaxSize());
                m_BufferRegionMgr.Extend(m_BufferRegionMgr.GetMaxSize());
                Allocation = m_BufferRegionMgr.Allocate(RequiredSize, 1);
                VERIFY_EXPR(Allocation.IsValid());
            }

            Offset = Allocation.UnalignedOffset;
            // Reserve enough space for the maximum possible attribs data size.
            m_RequiredBufferSize = std::max(m_RequiredBufferSize, AlignUp(Offset + m_MaxAttribsDataSize, m_ConstantBufferOffsetAlignment));
        }
        else
        {
            Offset = ~0u;
        }
    }

    IBuffer* PrepareMaterialAttribsBuffer(IRenderDevice* pDevice, IDeviceContext* pContext)
    {
        if (m_RequiredBufferSize > m_MaterialAttribsBuffer.GetDesc().Size)
        {
            m_MaterialAttribsData.resize(m_RequiredBufferSize);
            return m_MaterialAttribsBuffer.Resize(pDevice, pContext, m_RequiredBufferSize);
        }
        else
        {
            return m_MaterialAttribsBuffer.GetBuffer();
        }
    }

    IBuffer* GetMaterialAttribsBuffer() const
    {
        VERIFY(m_RequiredBufferSize == m_MaterialAttribsBuffer.GetDesc().Size, "The buffer needs to be resized.");
        return m_MaterialAttribsBuffer.GetBuffer();
    }

    void UpdateMaterialAttribsData(Uint32 Offset, Uint32 Size, const GLTF_PBR_Renderer::PBRMaterialShaderAttribsData AttribsData)
    {
        if (Offset + Size > m_MaterialAttribsData.size())
        {
            UNEXPECTED("Offset + Size (", Offset + Size, ") exceeds the size of the material attribs data buffer (", m_MaterialAttribsData.size(), ")");
            return;
        }
        void* pEnd = GLTF_PBR_Renderer::WritePBRMaterialShaderAttribs(&m_MaterialAttribsData[Offset], AttribsData);
        VERIFY_EXPR(static_cast<Uint32>(reinterpret_cast<const Uint8*>(pEnd) - &m_MaterialAttribsData[Offset]) == Size);
        (void)pEnd;
        m_DirtyRangeStart = std::min(m_DirtyRangeStart, Offset);
        m_DirtyRangeEnd   = std::max(m_DirtyRangeEnd, Offset + Size);
    }

    IBuffer* CommitUpdates(IRenderDevice* pDevice, IDeviceContext* pContext)
    {
        VERIFY(m_RequiredBufferSize == m_MaterialAttribsBuffer.GetDesc().Size, "The buffer needs to be resized.");
        IBuffer* pBuffer = m_MaterialAttribsBuffer.GetBuffer();
        if (m_DirtyRangeStart < m_DirtyRangeEnd)
        {
            if (pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11)
            {
                // Direct3D11 for unknown reason does not support partial constant buffer updates
                m_DirtyRangeStart = 0;
                m_DirtyRangeEnd   = m_MaterialAttribsData.size();
            }
            pContext->UpdateBuffer(pBuffer, m_DirtyRangeStart, m_DirtyRangeEnd - m_DirtyRangeStart, &m_MaterialAttribsData[m_DirtyRangeStart], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_DirtyRangeStart = 0;
            m_DirtyRangeEnd   = 0;
        }
        return pBuffer;
    }

    Uint32 GetMaxAttribsDataSize() const
    {
        return m_MaxAttribsDataSize;
    }

    Uint32 GetMaterialAttribsBufferVersion() const
    {
        return m_MaterialAttribsBuffer.GetVersion();
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

    Uint32 m_ConstantBufferOffsetAlignment = 0;
    Uint32 m_MaxAttribsDataSize            = 0;

    std::mutex                     m_BufferRegionMgrMtx;
    VariableSizeAllocationsManager m_BufferRegionMgr;
    Uint32                         m_RequiredBufferSize = 0;

    // Material attribs data resides in a single buffer shared by all SRBs.
    DynamicBuffer      m_MaterialAttribsBuffer;
    std::vector<Uint8> m_MaterialAttribsData;
    Uint32             m_DirtyRangeStart = 0;
    Uint32             m_DirtyRangeEnd   = 0;
};

void HnMaterial::AllocateBufferSpace(HnRenderDelegate& RenderDelegate)
{
    const USD_Renderer& UsdRenderer = *RenderDelegate.GetUSDRenderer();

    m_PSOFlags         = HnRenderPass::GetMaterialPSOFlags(*this);
    Uint32 AttribsSize = UsdRenderer.GetPBRMaterialAttribsSize(m_PSOFlags);
    if (m_PBRMaterialAttribsBufferOffset == ~0u || AttribsSize != m_PBRMaterialAttribsSize)
    {
        if (!m_SRBCache)
        {
            m_SRBCache = RefCntAutoPtr<HnMaterialSRBCache>{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache};
            VERIFY_EXPR(m_SRBCache);
        }
        else
        {
            VERIFY_EXPR(m_SRBCache == RenderDelegate.GetMaterialSRBCache());
        }
        m_SRBCache->AllocateBufferOffset(m_PBRMaterialAttribsBufferOffset, m_PBRMaterialAttribsSize, AttribsSize);
    }
}

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

Uint32 HnMaterial::GetResourceCacheVersion(HnRenderDelegate& RenderDelegate)
{
    HnTextureRegistry& TexRegistry = RenderDelegate.GetTextureRegistry();

    RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache};
    VERIFY_EXPR(SRBCache);

    const HN_MATERIAL_TEXTURES_BINDING_MODE BindingMode = static_cast<const HnRenderParam*>(RenderDelegate.GetRenderParam())->GetConfig().TextureBindingMode;

    Uint32 ResourceCacheVersion = SRBCache->GetMaterialAttribsBufferVersion();
    if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
    {
        // Need to recreate SRBs if the atlas texture has changed
        ResourceCacheVersion += TexRegistry.GetAtlasVersion();
    }
    else if (BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC)
    {
        // Need to recreate the SRB if any new texture is created
        ResourceCacheVersion += TexRegistry.GetStorageVersion();
    }

    return ResourceCacheVersion;
}

bool HnMaterial::UpdateSRB(HnRenderDelegate& RenderDelegate)
{
    HnTextureRegistry&  TexRegistry = RenderDelegate.GetTextureRegistry();
    const USD_Renderer& UsdRenderer = *RenderDelegate.GetUSDRenderer();

    const Uint32 ResourceCacheVersion = GetResourceCacheVersion(RenderDelegate);
    if (m_ResourceCacheVersion != ResourceCacheVersion)
    {
        m_SRB.Release();
        m_SRBVars              = {};
        m_ResourceCacheVersion = ResourceCacheVersion;
    }

    if (m_TextureAddressingAttribsDirty.load())
    {
        if (!InitTextureAddressingAttribs(UsdRenderer, TexRegistry))
        {
            // Wait for all textures to be loaded
            return false;
        }
    }

    RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache};
    VERIFY_EXPR(SRBCache);

    if (m_GPUDataDirty.load())
    {
        VERIFY_EXPR(m_PSOFlags == HnRenderPass::GetMaterialPSOFlags(*this));
        SRBCache->UpdateMaterialAttribsData(m_PBRMaterialAttribsBufferOffset, m_PBRMaterialAttribsSize,
                                            {
                                                m_PSOFlags,
                                                UsdRenderer.GetSettings().TextureAttribIndices,
                                                m_MaterialData,
                                            });
        m_GPUDataDirty.store(false);
    }

    if (m_SRB)
    {
        return true;
    }

    const PBR_Renderer::CreateInfo& RendererSettings  = UsdRenderer.GetSettings();
    const Uint32                    TexturesArraySize = RendererSettings.MaterialTexturesArraySize;

    // Texture array to bind to "g_MaterialTextures"
    std::vector<ITexture*> TexArray;

    // Standalone textures not allocated in the atlas.
    std::unordered_map<PBR_Renderer::TEXTURE_ATTRIB_ID, ITexture*> StandaloneTextures;

    HnMaterialSRBCache::ResourceKey SRBKey{m_ResourceCacheVersion};

    const HN_MATERIAL_TEXTURES_BINDING_MODE BindingMode = static_cast<const HnRenderParam*>(RenderDelegate.GetRenderParam())->GetConfig().TextureBindingMode;
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
            AtlasFormats = RenderDelegate.GetResourceManager().GetAllocatedAtlasFormats();
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
            VERIFY(pTexHandle->IsInitialized(), "Texture '", TexName, "' must be initialized because we checked it in InitTextureAddressingAttribs");
            if (pTexHandle->GetTexture())
            {
                pTexture                   = pTexHandle->GetTexture();
                const TextureDesc& TexDesc = pTexture->GetDesc();
                VERIFY(TexDesc.Type == RESOURCE_DIM_TEX_2D_ARRAY, "2D textures should be loaded as single-slice 2D array textures");

                StandaloneTextures.emplace(ID, pTexture);
            }
            else if (pTexHandle->GetAtlasSuballocation())
            {
                VERIFY_EXPR(BindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS);
                pTexture = pTexHandle->GetAtlasSuballocation()->GetAtlas()->GetTexture();

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
                UNEXPECTED("If a texture is not loaded, a default texture should be set in InitTextureAddressingAttribs.");
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
                    ITexture*      pAtlasTexture = RenderDelegate.GetResourceManager().GetTexture(AtlasFmt);
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
                        ITexture* pAtlasTexture = RenderDelegate.GetResourceManager().GetTexture(AtlasFmt);
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

        // NOTE: We cannot set the cbMaterialAttribs buffer here because different materials that use the same SRB
        //       may require different buffer ranges. We first compute the maximum buffer range for each SRB
        //       and then set the buffer in BindMaterialAttribsBuffer().
        constexpr bool BindPrimitiveAttribsBuffer = false;
        constexpr bool BindMaterialAttribsBuffer  = false;
        UsdRenderer.InitCommonSRBVars(pSRB,
                                      nullptr, // Frame attribs buffer is in SRB0
                                      BindPrimitiveAttribsBuffer,
                                      BindMaterialAttribsBuffer);

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

                    TexRegistry.ProcessTextures(
                        [this, &TexArray](const pxr::TfToken& Name, const HnTextureRegistry::TextureHandle& Handle) {
                            if (!Handle.IsInitialized())
                            {
                                // Skip textures that are not initialized
                                return;
                            }

                            if (!Handle.GetTexture())
                            {
                                LOG_ERROR_MESSAGE("Texture '", Name, "' in material '", GetId(), "' is not loaded.");
                                return;
                            }

                            const Uint32 TextureId = Handle.GetId();
                            if (TextureId >= TexArray.size())
                            {
                                LOG_ERROR_MESSAGE("Texture ", Name, " uses texture array slot ", TextureId, " which is greater than the texture array size ", TexArray.size());
                                return;
                            }

                            VERIFY_EXPR(TextureId != ~0u);
                            VERIFY(TexArray[TextureId] == nullptr, "Texture ", TextureId, " is already initialized");
                            TexArray[TextureId] = Handle.GetTexture();
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
                            WhiteTex = GetDefaultTexture(RenderDelegate.GetTextureRegistry(), HnTokens->diffuseColor);
                            VERIFY(WhiteTex->IsLoaded(), "Default texture must be loaded");
                        }
                        if (WhiteTex->GetTexture())
                            Tex = WhiteTex->GetTexture();
                        else if (WhiteTex->GetAtlasSuballocation())
                            Tex = WhiteTex->GetAtlasSuballocation()->GetAtlas()->GetTexture();
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
        m_SRBVars.MaterialAttribs = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbMaterialAttribs");
        VERIFY_EXPR(m_SRBVars.MaterialAttribs != nullptr);
        if (m_SRBVars.MaterialAttribs->Get() == nullptr)
        {
            IBuffer* pMaterialAttribsBuffer = SRBCache->GetMaterialAttribsBuffer();
            // Bind maximum possible buffer range
            const Uint32 PBRMaterialAttribsMaxSize = SRBCache->GetMaxAttribsDataSize();
            m_SRBVars.MaterialAttribs->SetBufferRange(pMaterialAttribsBuffer, 0, PBRMaterialAttribsMaxSize);
        }

        m_SRBVars.JointTransforms = m_SRB->GetVariableByName(SHADER_TYPE_VERTEX, UsdRenderer.GetJointTransformsVarName());
        VERIFY_EXPR(m_SRBVars.JointTransforms != nullptr || RendererSettings.MaxJointCount == 0);

        const Uint32 PBRPrimitiveAttribsSize = UsdRenderer.GetPBRPrimitiveAttribsSize(PBR_Renderer::PSO_FLAG_ALL);
        const Uint32 PrimitiveArraySize      = std::max(UsdRenderer.GetSettings().PrimitiveArraySize, 1u);
        m_PBRPrimitiveAttribsBufferRange     = PBRPrimitiveAttribsSize * PrimitiveArraySize;

        m_SRBVars.PrimitiveAttribs = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
        VERIFY_EXPR(m_SRBVars.PrimitiveAttribs != nullptr);
        if (m_SRBVars.PrimitiveAttribs->Get() == nullptr)
        {
            m_SRBVars.PrimitiveAttribs->SetBufferRange(UsdRenderer.GetPBRPrimitiveAttribsCB(), 0, m_PBRPrimitiveAttribsBufferRange);
        }
    }
    else
    {
        UNEXPECTED("Failed to create shader resource binding for material ", GetId());
    }

    return true;
}

void HnMaterial::InitSRBCache(HnRenderDelegate& RenderDelegate)
{
    if (RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache})
    {
        SRBCache->Initialize(RenderDelegate);
    }
    else
    {
        UNEXPECTED("Material SRB cache must not be null");
    }
}

void HnMaterial::BeginResourceUpdate(HnRenderDelegate& RenderDelegate)
{
    if (RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache})
    {
        SRBCache->PrepareMaterialAttribsBuffer(RenderDelegate.GetDevice(), RenderDelegate.GetDeviceContext());
    }
    else
    {
        UNEXPECTED("Material SRB cache must not be null");
    }
}

void HnMaterial::EndResourceUpdate(HnRenderDelegate& RenderDelegate)
{
    if (RefCntAutoPtr<HnMaterialSRBCache> SRBCache{RenderDelegate.GetMaterialSRBCache(), IID_HnMaterialSRBCache})
    {
        IDeviceContext*     pContext = RenderDelegate.GetDeviceContext();
        IBuffer*            pBuffer  = SRBCache->CommitUpdates(RenderDelegate.GetDevice(), pContext);
        StateTransitionDesc Barrier{pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
        pContext->TransitionResourceStates(1, &Barrier);
    }
    else
    {
        UNEXPECTED("Material SRB cache must not be null");
    }
}

HnMaterial::~HnMaterial()
{
    if (m_SRBCache)
    {
        m_SRBCache->AllocateBufferOffset(m_PBRMaterialAttribsBufferOffset, m_PBRMaterialAttribsSize, 0);
    }
}

} // namespace USD

} // namespace Diligent
