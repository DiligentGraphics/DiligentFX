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

#include "RadientStandardMaterialParameters.h"
#include "Assets/RadientAssetManagerImpl.hpp"

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <string>
#include <unordered_set>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

template <typename ValueType>
ValueType GetParameter(IRadientMaterialInstance&      Instance,
                       RadientMaterialParameterHandle Handle)
{
    ValueType Value{};
    EXPECT_EQ(Instance.GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_OK);
    return Value;
}

void ExpectInvalidStandardDefinition(RadientAssetManagerImpl&                           AssetManager,
                                     const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                     const char*                                        ExpectedError)
{
    TestingEnvironment::ErrorScope            ExpectedErrors{ExpectedError};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(AssetManager.CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pDefinition, nullptr);
}

TEST(RadientStandardMaterialTest, DefinitionsAreCached)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT |
        RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_SHEEN;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_BASE_COLOR |
        RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL |
        RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL;

    RefCntAutoPtr<IRadientMaterialDefinition> pFirstDefinition;
    RefCntAutoPtr<IRadientMaterialDefinition> pSecondDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pFirstDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pSecondDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pFirstDefinition, nullptr);
    ASSERT_NE(pSecondDefinition, nullptr);
    EXPECT_EQ(pFirstDefinition, pSecondDefinition);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pFirstDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    RadientMaterialParameterHandle BaseColorHandle;
    ASSERT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialBaseColorFactorName, &BaseColorHandle), RADIENT_STATUS_OK);
    const RadientFloat4 BaseColor = GetParameter<RadientFloat4>(*pInstance, BaseColorHandle);
    EXPECT_FLOAT_EQ(BaseColor.x, 1.f);
    EXPECT_FLOAT_EQ(BaseColor.y, 1.f);
    EXPECT_FLOAT_EQ(BaseColor.z, 1.f);
    EXPECT_FLOAT_EQ(BaseColor.w, 1.f);

    RadientMaterialParameterHandle ClearCoatHandle;
    RadientMaterialParameterHandle SheenHandle;
    RadientMaterialParameterHandle NormalScaleHandle;
    RadientMaterialParameterHandle NormalTextureHandle;
    EXPECT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialClearCoatFactorName, &ClearCoatHandle), RADIENT_STATUS_OK);
    EXPECT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialSheenColorFactorName, &SheenHandle), RADIENT_STATUS_OK);
    EXPECT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialNormalScaleName, &NormalScaleHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialNormalTextureName, &NormalTextureHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pInstance->GetTexture(NormalTextureHandle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture, nullptr);

    DefinitionCI.Textures &= ~RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL;
    RefCntAutoPtr<IRadientMaterialDefinition> pDifferentDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDifferentDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_NE(pFirstDefinition, pDifferentDefinition);
}

TEST(RadientStandardMaterialTest, DefinitionUsesPublishedParameterSchema)
{
    struct ExpectedParameter
    {
        const Char*                     Name;
        RADIENT_MATERIAL_PARAMETER_TYPE Type;
    };

    struct TextureSemanticParameters
    {
        const Char* Texture;
        const Char* UVSelector;
        const Char* UVScaleAndRotation;
        const Char* UVBias;
        const Char* WrapU;
        const Char* WrapV;
    };

#define EXPECTED_PARAMETER(Name, Type) \
    ExpectedParameter { Name, Type }

#define STANDARD_TEXTURE_PARAMETERS(Name)                                                                                           \
    EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureName, RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE),                        \
        EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureUVSelectorName, RADIENT_MATERIAL_PARAMETER_TYPE_INT),              \
        EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureUVScaleAndRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2), \
        EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureUVBiasName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2),               \
        EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureWrapUName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT),                  \
        EXPECTED_PARAMETER(RadientStandardMaterial##Name##TextureWrapVName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT)

    static constexpr ExpectedParameter ExpectedParameters[] = {
        {RadientStandardMaterialBaseColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4},
        {RadientStandardMaterialMetallicFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialEmissiveFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3},
        {RadientStandardMaterialNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialOcclusionStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialClearCoatFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialClearCoatRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialClearCoatNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialSheenColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3},
        {RadientStandardMaterialSheenRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialAnisotropyStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialAnisotropyRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialIridescenceFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialIridescenceIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialIridescenceThicknessMinimumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialIridescenceThicknessMaximumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialTransmissionFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialThicknessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialAttenuationColorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3},
        {RadientStandardMaterialAttenuationDistanceName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialAlphaModeName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT},
        {RadientStandardMaterialAlphaCutoffName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT},
        {RadientStandardMaterialDoubleSidedName, RADIENT_MATERIAL_PARAMETER_TYPE_BOOL},
        STANDARD_TEXTURE_PARAMETERS(BaseColor),
        STANDARD_TEXTURE_PARAMETERS(MetallicRoughness),
        STANDARD_TEXTURE_PARAMETERS(Normal),
        STANDARD_TEXTURE_PARAMETERS(Occlusion),
        STANDARD_TEXTURE_PARAMETERS(Emissive),
        STANDARD_TEXTURE_PARAMETERS(ClearCoat),
        STANDARD_TEXTURE_PARAMETERS(ClearCoatRoughness),
        STANDARD_TEXTURE_PARAMETERS(ClearCoatNormal),
        STANDARD_TEXTURE_PARAMETERS(SheenColor),
        STANDARD_TEXTURE_PARAMETERS(SheenRoughness),
        STANDARD_TEXTURE_PARAMETERS(Anisotropy),
        STANDARD_TEXTURE_PARAMETERS(Iridescence),
        STANDARD_TEXTURE_PARAMETERS(IridescenceThickness),
        STANDARD_TEXTURE_PARAMETERS(Transmission),
        STANDARD_TEXTURE_PARAMETERS(Thickness),
    };

#define TEXTURE_SEMANTIC_PARAMETERS(Name)                                 \
    TextureSemanticParameters                                             \
    {                                                                     \
        RadientStandardMaterial##Name##TextureName,                       \
            RadientStandardMaterial##Name##TextureUVSelectorName,         \
            RadientStandardMaterial##Name##TextureUVScaleAndRotationName, \
            RadientStandardMaterial##Name##TextureUVBiasName,             \
            RadientStandardMaterial##Name##TextureWrapUName,              \
            RadientStandardMaterial##Name##TextureWrapVName               \
    }

    static constexpr std::array TextureSemantics{
        TEXTURE_SEMANTIC_PARAMETERS(BaseColor),
        TEXTURE_SEMANTIC_PARAMETERS(MetallicRoughness),
        TEXTURE_SEMANTIC_PARAMETERS(Normal),
        TEXTURE_SEMANTIC_PARAMETERS(Occlusion),
        TEXTURE_SEMANTIC_PARAMETERS(Emissive),
        TEXTURE_SEMANTIC_PARAMETERS(ClearCoat),
        TEXTURE_SEMANTIC_PARAMETERS(ClearCoatRoughness),
        TEXTURE_SEMANTIC_PARAMETERS(ClearCoatNormal),
        TEXTURE_SEMANTIC_PARAMETERS(SheenColor),
        TEXTURE_SEMANTIC_PARAMETERS(SheenRoughness),
        TEXTURE_SEMANTIC_PARAMETERS(Anisotropy),
        TEXTURE_SEMANTIC_PARAMETERS(Iridescence),
        TEXTURE_SEMANTIC_PARAMETERS(IridescenceThickness),
        TEXTURE_SEMANTIC_PARAMETERS(Transmission),
        TEXTURE_SEMANTIC_PARAMETERS(Thickness),
    };

#undef TEXTURE_SEMANTIC_PARAMETERS
#undef STANDARD_TEXTURE_PARAMETERS
#undef EXPECTED_PARAMETER

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS_ALL;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS_ALL;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetDesc().Reference.Version, RadientStandardMaterialSchemaVersion);
    ASSERT_EQ(pDefinition->GetParameterCount(), static_cast<Uint32>(sizeof(ExpectedParameters) / sizeof(ExpectedParameters[0])));

    std::unordered_set<std::string> Names;
    for (const ExpectedParameter& Expected : ExpectedParameters)
    {
        ASSERT_TRUE(Names.emplace(Expected.Name).second) << "Duplicate published parameter name '" << Expected.Name << "'";

        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->FindParameter(Expected.Name, &Handle), RADIENT_STATUS_OK) << Expected.Name;
        const RadientMaterialParameterDesc& Desc = pDefinition->GetParameterDesc(Handle.Index);
        EXPECT_STREQ(Desc.Name, Expected.Name);
        EXPECT_EQ(Desc.Type, Expected.Type) << Expected.Name;
        EXPECT_EQ(Desc.ArraySize, 1u) << Expected.Name;
    }

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    const std::array<Float32, 4> ExpectedUVScaleAndRotation{1.f, 0.f, 0.f, 1.f};
    const RadientFloat2          ExpectedUVBias{0.f, 0.f};
    for (const TextureSemanticParameters& Semantic : TextureSemantics)
    {
        const auto FindHandle = [&](const Char* Name) {
            RadientMaterialParameterHandle Handle;
            EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
            return Handle;
        };

        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pInstance->GetTexture(FindHandle(Semantic.Texture), 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, nullptr);
        EXPECT_EQ(GetParameter<Int32>(*pInstance, FindHandle(Semantic.UVSelector)), 0);
        EXPECT_EQ((GetParameter<std::array<Float32, 4>>(*pInstance, FindHandle(Semantic.UVScaleAndRotation))), ExpectedUVScaleAndRotation);

        const RadientFloat2 UVBias = GetParameter<RadientFloat2>(*pInstance, FindHandle(Semantic.UVBias));
        EXPECT_FLOAT_EQ(UVBias.x, ExpectedUVBias.x);
        EXPECT_FLOAT_EQ(UVBias.y, ExpectedUVBias.y);
        EXPECT_EQ(GetParameter<Uint32>(*pInstance, FindHandle(Semantic.WrapU)), static_cast<Uint32>(TEXTURE_ADDRESS_WRAP));
        EXPECT_EQ(GetParameter<Uint32>(*pInstance, FindHandle(Semantic.WrapV)), static_cast<Uint32>(TEXTURE_ADDRESS_WRAP));
    }
}

TEST(RadientStandardMaterialTest, MinimalSchemasAreExact)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    const auto ExpectParameters = [](IRadientMaterialDefinition& pDefinition,
                                     const auto&                 ExpectedNames) {
        ASSERT_EQ(pDefinition.GetParameterCount(), static_cast<Uint32>(ExpectedNames.size()));
        for (const Char* Name : ExpectedNames)
        {
            RadientMaterialParameterHandle Handle;
            EXPECT_EQ(pDefinition.FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
        }
    };

    static constexpr std::array MetallicRoughnessParameters{
        RadientStandardMaterialBaseColorFactorName,
        RadientStandardMaterialMetallicFactorName,
        RadientStandardMaterialRoughnessFactorName,
        RadientStandardMaterialEmissiveFactorName,
        RadientStandardMaterialAlphaModeName,
        RadientStandardMaterialAlphaCutoffName,
        RadientStandardMaterialDoubleSidedName,
    };

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialDefinition>   pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetDesc().Domain, RADIENT_MATERIAL_DOMAIN_SURFACE);
    EXPECT_STREQ(pDefinition->GetDesc().Reference.URI, "standard-material:0:0:0");
    EXPECT_EQ(pDefinition->GetDesc().Reference.Version, RadientStandardMaterialSchemaVersion);
    ExpectParameters(*pDefinition, MetallicRoughnessParameters);

    RadientMaterialParameterHandle Handle;
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialNormalScaleName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialOcclusionStrengthName, &Handle), RADIENT_STATUS_NOT_FOUND);

    static constexpr std::array UnlitParameters{
        RadientStandardMaterialBaseColorFactorName,
        RadientStandardMaterialAlphaModeName,
        RadientStandardMaterialAlphaCutoffName,
        RadientStandardMaterialDoubleSidedName,
    };

    DefinitionCI.Model = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    pDefinition.Release();
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    ExpectParameters(*pDefinition, UnlitParameters);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientStandardMaterialTest, UnlitMaterialHasOnlyApplicableSchema)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_BASE_COLOR;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetParameterCount(), 10u);

    static constexpr std::array ExpectedParameters{
        RadientStandardMaterialBaseColorFactorName,
        RadientStandardMaterialAlphaModeName,
        RadientStandardMaterialAlphaCutoffName,
        RadientStandardMaterialDoubleSidedName,
        RadientStandardMaterialBaseColorTextureName,
        RadientStandardMaterialBaseColorTextureUVSelectorName,
        RadientStandardMaterialBaseColorTextureUVScaleAndRotationName,
        RadientStandardMaterialBaseColorTextureUVBiasName,
        RadientStandardMaterialBaseColorTextureWrapUName,
        RadientStandardMaterialBaseColorTextureWrapVName,
    };

    RadientMaterialParameterHandle Handle;
    for (const Char* Name : ExpectedParameters)
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialEmissiveFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientStandardMaterialTest, RejectsInvalidModel)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model = RADIENT_STANDARD_MATERIAL_MODEL_COUNT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI, "Invalid standard material model");
}

TEST(RadientStandardMaterialTest, RejectsNullOutput)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    EXPECT_EQ(pAssetManager->CreateStandardMaterialDefinition({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientStandardMaterialTest, RejectsUnsupportedFeatureFlags)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = static_cast<RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS>(
        static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS_ALL) + 1u);
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Standard material feature flags contain unsupported bits");
}

TEST(RadientStandardMaterialTest, RejectsUnsupportedTextureFlags)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = static_cast<RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS>(
        static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS_ALL) + 1u);
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Standard material texture flags contain unsupported bits");
}

TEST(RadientStandardMaterialTest, RejectsOptionalFeaturesForUnlitMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Unlit standard materials do not support optional material features");
}

TEST(RadientStandardMaterialTest, RejectsNonBaseColorTextureForUnlitMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Unlit standard materials only support the base-color texture semantic");
}

TEST(RadientStandardMaterialTest, RejectsClearCoatTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Clear-coat texture semantics require the clear-coat material feature");
}

TEST(RadientStandardMaterialTest, RejectsSheenTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_COLOR;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Sheen texture semantics require the sheen material feature");
}

TEST(RadientStandardMaterialTest, RejectsAnisotropyTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_ANISOTROPY;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The anisotropy texture semantic requires the anisotropy material feature");
}

TEST(RadientStandardMaterialTest, RejectsIridescenceTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE_THICKNESS;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Iridescence texture semantics require the iridescence material feature");
}

TEST(RadientStandardMaterialTest, RejectsTransmissionTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_TRANSMISSION;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The transmission texture semantic requires the transmission material feature");
}

TEST(RadientStandardMaterialTest, RejectsThicknessTextureWithoutVolumeFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_THICKNESS;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The thickness texture semantic requires the volume material feature");
}

TEST(RadientStandardMaterialTest, VolumeRequiresTransmission)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The volume material feature requires the transmission material feature");

    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME |
        RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_TRANSMISSION;
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
}

} // namespace
