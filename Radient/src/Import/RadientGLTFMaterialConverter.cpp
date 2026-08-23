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

#include "Import/RadientGLTFConverter.hpp"

#include "GLTFLoader.hpp"
#include "RadientMaterials.h"
#include "RadientStandardMaterialParameters.h"

#include <array>

namespace Diligent
{

namespace
{

struct StandardMaterialTextureSemantic
{
    Uint32                                 TextureAttribId;
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS RequiredFeature;
    const char*                            TextureParameterName;
    const char*                            UVSelectorParameterName;
    const char*                            UVScaleAndRotationParameterName;
    const char*                            UVBiasParameterName;
    const char*                            WrapUParameterName;
    const char*                            WrapVParameterName;
};

#define SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, GLTFName, Name, FeatureFlag)    \
    Semantics[GLTF::Default##GLTFName##TextureAttribId] = StandardMaterialTextureSemantic \
    {                                                                                     \
        GLTF::Default##GLTFName##TextureAttribId, FeatureFlag,                            \
            RadientStandardMaterial##Name##TextureName,                                   \
            RadientStandardMaterial##Name##TextureUVSelectorName,                         \
            RadientStandardMaterial##Name##TextureUVScaleAndRotationName,                 \
            RadientStandardMaterial##Name##TextureUVBiasName,                             \
            RadientStandardMaterial##Name##TextureWrapUName,                              \
            RadientStandardMaterial##Name##TextureWrapVName                               \
    }

constexpr auto MakeStandardMaterialTextureSemantics() noexcept
{
    std::array<StandardMaterialTextureSemantic, GLTF::DefaultThicknessTextureAttribId + 1> Semantics{};

    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, BaseColor, BaseColor, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, MetallicRoughness, MetallicRoughness, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Normal, Normal, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Occlusion, Occlusion, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Emissive, Emissive, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Clearcoat, ClearCoat, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, ClearcoatRoughness, ClearCoatRoughness, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, ClearcoatNormal, ClearCoatNormal, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, SheenColor, SheenColor, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, SheenRoughness, SheenRoughness, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Anisotropy, Anisotropy, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Iridescence, Iridescence, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, IridescenceThickness, IridescenceThickness, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Transmission, Transmission, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION);
    SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC(Semantics, Thickness, Thickness, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME);

    return Semantics;
}
#undef SET_STANDARD_MATERIAL_TEXTURE_SEMANTIC

static constexpr auto StandardMaterialTextureSemantics = MakeStandardMaterialTextureSemantics();

constexpr bool IsStandardMaterialTextureSemanticTableComplete() noexcept
{
    for (const StandardMaterialTextureSemantic& Semantic : StandardMaterialTextureSemantics)
    {
        if (Semantic.TextureParameterName == nullptr)
            return false;
    }
    return true;
}

static_assert(IsStandardMaterialTextureSemanticTableComplete());

static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP) == static_cast<Uint32>(TEXTURE_ADDRESS_WRAP));
static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP) == static_cast<Uint32>(TEXTURE_ADDRESS_CLAMP));


bool HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Features,
                RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Feature) noexcept
{
    return Feature == RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE ||
        (Features & Feature) != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE;
}

RADIENT_STATUS SetParameter(IRadientMaterialDefinition&     Definition,
                            IRadientMaterialInstanceWriter& Writer,
                            const char*                     Name,
                            const void*                     pData,
                            Uint32                          Size)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           FindStatus = Definition.FindParameter(Name, &Handle);
    return FindStatus == RADIENT_STATUS_OK ? Writer.SetParameter(Handle, pData, Size) : FindStatus;
}

RADIENT_STATUS SetTexture(IRadientMaterialDefinition&     Definition,
                          IRadientMaterialInstanceWriter& Writer,
                          const char*                     Name,
                          IRadientTextureAsset*           pTexture)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           FindStatus = Definition.FindParameter(Name, &Handle);
    return FindStatus == RADIENT_STATUS_OK ? Writer.SetTexture(Handle, 0, pTexture) : FindStatus;
}

IRadientTextureAsset* GetTexture(const GLTF::Material&        Material,
                                 Uint32                       TextureAttribId,
                                 IRadientTextureAsset* const* ppTextures,
                                 Uint32                       TextureCount) noexcept
{
    const int TextureId = Material.GetTextureId(TextureAttribId);
    return ppTextures != nullptr && TextureId >= 0 && static_cast<Uint32>(TextureId) < TextureCount ?
        ppTextures[TextureId] :
        nullptr;
}

RADIENT_STATUS SetTextureBindingParameters(const GLTF::Material&                  Material,
                                           const StandardMaterialTextureSemantic& Semantic,
                                           IRadientMaterialDefinition&            Definition,
                                           IRadientMaterialInstanceWriter&        Writer)
{
    // TextureSlice and AtlasUVScaleAndBias are runtime allocation state and
    // are intentionally not part of the imported material instance.
    const GLTF::Material::TextureShaderAttribs& TextureAttribs = Material.GetTextureAttrib(Semantic.TextureAttribId);
    const Int32                                 UVSelector     = TextureAttribs.GetUVSelector();
    const RadientFloat2                         UVBias{TextureAttribs.UBias, TextureAttribs.VBias};
    const auto                                  WrapU = static_cast<RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE>(TextureAttribs.GetWrapUMode());
    const auto                                  WrapV = static_cast<RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE>(TextureAttribs.GetWrapVMode());

    RADIENT_STATUS Status = SetParameter(Definition, Writer, Semantic.UVSelectorParameterName,
                                         &UVSelector, static_cast<Uint32>(sizeof(UVSelector)));
    if (RADIENT_SUCCEEDED(Status))
        Status = SetParameter(Definition, Writer, Semantic.UVScaleAndRotationParameterName,
                              &TextureAttribs.UVScaleAndRotation, static_cast<Uint32>(sizeof(TextureAttribs.UVScaleAndRotation)));
    if (RADIENT_SUCCEEDED(Status))
        Status = SetParameter(Definition, Writer, Semantic.UVBiasParameterName,
                              &UVBias, static_cast<Uint32>(sizeof(UVBias)));
    if (RADIENT_SUCCEEDED(Status))
        Status = SetParameter(Definition, Writer, Semantic.WrapUParameterName,
                              &WrapU, static_cast<Uint32>(sizeof(WrapU)));
    if (RADIENT_SUCCEEDED(Status))
        Status = SetParameter(Definition, Writer, Semantic.WrapVParameterName,
                              &WrapV, static_cast<Uint32>(sizeof(WrapV)));
    return Status;
}

} // namespace

namespace RadientGLTFConverter
{

RADIENT_STATUS ConvertMaterialDefinition(
    const GLTF::Material&                        Material,
    RadientStandardMaterialDefinitionCreateInfo& DefinitionCI)
{
    DefinitionCI = {};

    switch (Material.Attribs.Workflow)
    {
        case GLTF::Material::PBR_WORKFLOW_METALL_ROUGH:
            DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS;
            break;

        case GLTF::Material::PBR_WORKFLOW_UNLIT:
            DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
            break;

        case GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS:
            return RADIENT_STATUS_UNSUPPORTED;

        default:
            return RADIENT_STATUS_INVALID_DATA;
    }

    if (DefinitionCI.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
    {
        if (Material.HasClearcoat)
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
        if (Material.Sheen)
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN;
        if (Material.Anisotropy)
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY;
        if (Material.Iridescence)
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE;
        if (Material.Transmission)
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION;
        if (Material.Volume)
        {
            // Volume is defined on transmitted light, so its schema includes
            // transmission even if malformed source data omitted that extension.
            DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION |
                RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME;
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS PopulateMaterialInstance(
    const GLTF::Material&           Material,
    IRadientTextureAsset* const*    ppTextures,
    Uint32                          TextureCount,
    IRadientMaterialDefinition&     Definition,
    IRadientMaterialInstanceWriter& Writer)
{
    if (ppTextures == nullptr && TextureCount != 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RADIENT_STATUS                              Status = ConvertMaterialDefinition(Material, DefinitionCI);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    if (Material.Attribs.AlphaMode < GLTF::Material::ALPHA_MODE_OPAQUE ||
        Material.Attribs.AlphaMode >= GLTF::Material::ALPHA_MODE_NUM_MODES)
    {
        return RADIENT_STATUS_INVALID_DATA;
    }

    static_assert(static_cast<Uint32>(GLTF::Material::ALPHA_MODE_OPAQUE) == RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    static_assert(static_cast<Uint32>(GLTF::Material::ALPHA_MODE_MASK) == RADIENT_MATERIAL_SURFACE_MODE_MASKED);
    static_assert(static_cast<Uint32>(GLTF::Material::ALPHA_MODE_BLEND) == RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);

    RefCntAutoPtr<IRadientSurfaceMaterialInstanceWriter> pSurfaceWriter{
        &Writer, IID_RadientSurfaceMaterialInstanceWriter};
    if (!pSurfaceWriter)
        return RADIENT_STATUS_INVALID_OPERATION;

    Status = pSurfaceWriter->SetSurfaceMode(
        static_cast<RADIENT_MATERIAL_SURFACE_MODE>(Material.Attribs.AlphaMode));
    if (RADIENT_SUCCEEDED(Status))
        Status = pSurfaceWriter->SetAlphaCutoff(Material.Attribs.AlphaCutoff);
    if (RADIENT_SUCCEEDED(Status))
        Status = pSurfaceWriter->SetDoubleSided(Material.DoubleSided ? True : False);
    if (RADIENT_FAILED(Status))
        return Status;

    auto SetMaterialParameter =
        [&](const char* Name, const auto& Value) //
    {
        if (RADIENT_SUCCEEDED(Status))
            Status = SetParameter(Definition, Writer, Name, &Value, static_cast<Uint32>(sizeof(Value)));
    };

    const RadientFloat4 BaseColorFactor{
        Material.Attribs.BaseColorFactor.x,
        Material.Attribs.BaseColorFactor.y,
        Material.Attribs.BaseColorFactor.z,
        Material.Attribs.BaseColorFactor.w,
    };
    SetMaterialParameter(RadientStandardMaterialBaseColorFactorName, BaseColorFactor);

    if (DefinitionCI.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
    {
        const RadientFloat3 EmissiveFactor{
            Material.Attribs.EmissiveFactor.x,
            Material.Attribs.EmissiveFactor.y,
            Material.Attribs.EmissiveFactor.z,
        };
        SetMaterialParameter(RadientStandardMaterialMetallicFactorName, Material.Attribs.MetallicFactor);
        SetMaterialParameter(RadientStandardMaterialRoughnessFactorName, Material.Attribs.RoughnessFactor);
        SetMaterialParameter(RadientStandardMaterialEmissiveFactorName, EmissiveFactor);

        SetMaterialParameter(RadientStandardMaterialNormalScaleName, Material.Attribs.NormalScale);
        SetMaterialParameter(RadientStandardMaterialOcclusionStrengthName, Material.Attribs.OcclusionFactor);

        if (Material.HasClearcoat)
        {
            SetMaterialParameter(RadientStandardMaterialClearCoatFactorName, Material.Attribs.ClearcoatFactor);
            SetMaterialParameter(RadientStandardMaterialClearCoatRoughnessFactorName, Material.Attribs.ClearcoatRoughnessFactor);
            SetMaterialParameter(RadientStandardMaterialClearCoatNormalScaleName, Material.Attribs.ClearcoatNormalScale);
        }
        if (Material.Sheen)
        {
            const RadientFloat3 SheenColorFactor{
                Material.Sheen->ColorFactor.x,
                Material.Sheen->ColorFactor.y,
                Material.Sheen->ColorFactor.z,
            };
            SetMaterialParameter(RadientStandardMaterialSheenColorFactorName, SheenColorFactor);
            SetMaterialParameter(RadientStandardMaterialSheenRoughnessFactorName, Material.Sheen->RoughnessFactor);
        }
        if (Material.Anisotropy)
        {
            SetMaterialParameter(RadientStandardMaterialAnisotropyStrengthName, Material.Anisotropy->Strength);
            SetMaterialParameter(RadientStandardMaterialAnisotropyRotationName, Material.Anisotropy->Rotation);
        }
        if (Material.Iridescence)
        {
            SetMaterialParameter(RadientStandardMaterialIridescenceFactorName, Material.Iridescence->Factor);
            SetMaterialParameter(RadientStandardMaterialIridescenceIORName, Material.Iridescence->IOR);
            SetMaterialParameter(RadientStandardMaterialIridescenceThicknessMinimumName, Material.Iridescence->ThicknessMinimum);
            SetMaterialParameter(RadientStandardMaterialIridescenceThicknessMaximumName, Material.Iridescence->ThicknessMaximum);
        }
        if (Material.Transmission)
        {
            SetMaterialParameter(RadientStandardMaterialTransmissionFactorName, Material.Transmission->Factor);
            SetMaterialParameter(RadientStandardMaterialIORName, Material.Transmission->IOR);
        }
        if (Material.Volume)
        {
            const RadientFloat3 AttenuationColor{
                Material.Volume->AttenuationColor.x,
                Material.Volume->AttenuationColor.y,
                Material.Volume->AttenuationColor.z,
            };
            SetMaterialParameter(RadientStandardMaterialThicknessFactorName, Material.Volume->ThicknessFactor);
            SetMaterialParameter(RadientStandardMaterialAttenuationColorName, AttenuationColor);
            SetMaterialParameter(RadientStandardMaterialAttenuationDistanceName, Material.Volume->AttenuationDistance);
        }
    }

    if (RADIENT_FAILED(Status))
        return Status;

    for (const StandardMaterialTextureSemantic& Semantic : StandardMaterialTextureSemantics)
    {
        if (DefinitionCI.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_UNLIT &&
            Semantic.TextureAttribId != GLTF::DefaultBaseColorTextureAttribId)
        {
            continue;
        }
        if (!HasFeature(DefinitionCI.Features, Semantic.RequiredFeature))
            continue;

        Status = SetTextureBindingParameters(Material, Semantic, Definition, Writer);
        if (RADIENT_FAILED(Status))
            return Status;

        IRadientTextureAsset* pTexture =
            GetTexture(Material, Semantic.TextureAttribId, ppTextures, TextureCount);
        if (pTexture == nullptr)
            continue;

        Status = SetTexture(Definition, Writer, Semantic.TextureParameterName, pTexture);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

} // namespace RadientGLTFConverter

} // namespace Diligent
