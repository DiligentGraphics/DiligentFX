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
#include "Assets/RadientMaterialDefinitionImpl.hpp"

#include "GLTFBuilder.hpp"
#include "GLTFLoader.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "Import/RadientGLTFConverter.hpp"
#include "RefCntAutoPtr.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

template <typename ValueType>
ValueType GetParameter(IRadientMaterialAsset&         Material,
                       RadientMaterialParameterHandle Handle)
{
    ValueType Value{};
    EXPECT_EQ(Material.GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_OK);
    return Value;
}

void ExpectInvalidStandardDefinition(RadientAssetManagerImpl&                           AssetManager,
                                     const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                     const char*                                        ExpectedError)
{
    TestingEnvironment::ErrorScope                 ExpectedErrors{ExpectedError};
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    EXPECT_EQ(AssetManager.CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pDefinition, nullptr);
}

static constexpr std::array<Uint32, 15> StandardMaterialTextureAttribIds{{
    GLTF::DefaultBaseColorTextureAttribId,
    GLTF::DefaultMetallicRoughnessTextureAttribId,
    GLTF::DefaultNormalTextureAttribId,
    GLTF::DefaultOcclusionTextureAttribId,
    GLTF::DefaultEmissiveTextureAttribId,
    GLTF::DefaultClearcoatTextureAttribId,
    GLTF::DefaultClearcoatRoughnessTextureAttribId,
    GLTF::DefaultClearcoatNormalTextureAttribId,
    GLTF::DefaultSheenColorTextureAttribId,
    GLTF::DefaultSheenRoughnessTextureAttribId,
    GLTF::DefaultAnisotropyTextureAttribId,
    GLTF::DefaultIridescenceTextureAttribId,
    GLTF::DefaultIridescenceThicknessTextureAttribId,
    GLTF::DefaultTransmissionTextureAttribId,
    GLTF::DefaultThicknessTextureAttribId,
}};

GLTF::Material MakeExtendedPBRMaterial()
{
    GLTF::Material Material;
    Material.Attribs.BaseColorFactor          = float4{0.1f, 0.2f, 0.3f, 0.4f};
    Material.Attribs.EmissiveFactor           = float3{0.5f, 0.6f, 0.7f};
    Material.Attribs.NormalScale              = 0.8f;
    Material.Attribs.AlphaMode                = GLTF::Material::ALPHA_MODE_MASK;
    Material.Attribs.AlphaCutoff              = 0.25f;
    Material.Attribs.MetallicFactor           = 0.35f;
    Material.Attribs.RoughnessFactor          = 0.45f;
    Material.Attribs.OcclusionFactor          = 0.55f;
    Material.Attribs.ClearcoatFactor          = 0.65f;
    Material.Attribs.ClearcoatRoughnessFactor = 0.75f;
    Material.Attribs.ClearcoatNormalScale     = 0.85f;
    Material.HasClearcoat                     = true;

    Material.Sheen                  = std::make_unique<GLTF::Material::SheenShaderAttribs>();
    Material.Sheen->ColorFactor     = float3{0.15f, 0.25f, 0.35f};
    Material.Sheen->RoughnessFactor = 0.46f;

    Material.Anisotropy           = std::make_unique<GLTF::Material::AnisotropyShaderAttribs>();
    Material.Anisotropy->Strength = 0.56f;
    Material.Anisotropy->Rotation = 0.66f;

    Material.Iridescence                   = std::make_unique<GLTF::Material::IridescenceShaderAttribs>();
    Material.Iridescence->Factor           = 0.76f;
    Material.Iridescence->IOR              = 1.4f;
    Material.Iridescence->ThicknessMinimum = 120.f;
    Material.Iridescence->ThicknessMaximum = 360.f;

    Material.Transmission         = std::make_unique<GLTF::Material::TransmissionShaderAttribs>();
    Material.Transmission->Factor = 0.86f;
    Material.Transmission->IOR    = 1.45f;

    Material.Volume                      = std::make_unique<GLTF::Material::VolumeShaderAttribs>();
    Material.Volume->ThicknessFactor     = 0.96f;
    Material.Volume->AttenuationColor    = float3{0.2f, 0.4f, 0.6f};
    Material.Volume->AttenuationDistance = 12.f;

    GLTF::MaterialBuilder Builder{Material};
    for (size_t TextureIndex = 0; TextureIndex < StandardMaterialTextureAttribIds.size(); ++TextureIndex)
    {
        const Uint32 TextureAttribId = StandardMaterialTextureAttribIds[TextureIndex];
        Builder.SetTextureId(TextureAttribId, 0);

        GLTF::Material::TextureShaderAttribs& TextureAttribs = Builder.GetTextureAttrib(TextureAttribId);
        TextureAttribs.SetUVSelector(static_cast<int>(TextureIndex % 2));
        TextureAttribs.UVScaleAndRotation = float2x2{
            1.f + static_cast<float>(TextureIndex), 0.1f + static_cast<float>(TextureIndex),
            0.2f + static_cast<float>(TextureIndex), 2.f + static_cast<float>(TextureIndex)};
        TextureAttribs.UBias = 0.01f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.VBias = 0.02f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.SetWrapUMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_MIRROR : TEXTURE_ADDRESS_CLAMP);
        TextureAttribs.SetWrapVMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_CLAMP : TEXTURE_ADDRESS_WRAP);
    }
    Builder.Finalize();
    return Material;
}

GLTF::Material MakeSpecularGlossinessMaterial()
{
    GLTF::Material Material;
    Material.Attribs.Workflow        = GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS;
    Material.Attribs.BaseColorFactor = float4{0.15f, 0.25f, 0.35f, 0.45f};
    Material.Attribs.SpecularFactor  = float3{0.55f, 0.65f, 0.75f};
    Material.Attribs.RoughnessFactor = 0.85f; // Carries glossiness for this workflow.
    Material.Attribs.EmissiveFactor  = float3{0.12f, 0.23f, 0.34f};
    Material.Attribs.NormalScale     = 0.46f;
    Material.Attribs.OcclusionFactor = 0.57f;
    Material.Attribs.AlphaMode       = GLTF::Material::ALPHA_MODE_MASK;
    Material.Attribs.AlphaCutoff     = 0.68f;

    static constexpr std::array TextureAttribIds{
        GLTF::DefaultDiffuseTextureAttribId,
        GLTF::DefaultSpecularGlossinessTextureAttibId,
        GLTF::DefaultNormalTextureAttribId,
        GLTF::DefaultOcclusionTextureAttribId,
        GLTF::DefaultEmissiveTextureAttribId,
    };

    GLTF::MaterialBuilder Builder{Material};
    for (size_t TextureIndex = 0; TextureIndex < TextureAttribIds.size(); ++TextureIndex)
    {
        const Uint32 TextureAttribId = TextureAttribIds[TextureIndex];
        Builder.SetTextureId(TextureAttribId, 0);

        GLTF::Material::TextureShaderAttribs& TextureAttribs =
            Builder.GetTextureAttrib(TextureAttribId);
        TextureAttribs.SetUVSelector(static_cast<int>(TextureIndex % 2));
        TextureAttribs.UVScaleAndRotation = float2x2{
            1.f + static_cast<float>(TextureIndex), 0.1f + static_cast<float>(TextureIndex),
            0.2f + static_cast<float>(TextureIndex), 2.f + static_cast<float>(TextureIndex)};
        TextureAttribs.UBias = 0.01f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.VBias = 0.02f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.SetWrapUMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_WRAP : TEXTURE_ADDRESS_CLAMP);
        TextureAttribs.SetWrapVMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_CLAMP : TEXTURE_ADDRESS_WRAP);
    }
    Builder.Finalize();
    return Material;
}

const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& GetStandardMaterialTextureAttribIndices()
{
    static const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Indices = [] {
        std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Result{};
        Result.fill(-1);
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR]            = GLTF::DefaultBaseColorTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]                = GLTF::DefaultNormalTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]             = GLTF::DefaultMetallicRoughnessTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]             = GLTF::DefaultOcclusionTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]              = GLTF::DefaultEmissiveTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT]            = GLTF::DefaultClearcoatTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS]  = GLTF::DefaultClearcoatRoughnessTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL]     = GLTF::DefaultClearcoatNormalTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR]           = GLTF::DefaultSheenColorTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS]       = GLTF::DefaultSheenRoughnessTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY]            = GLTF::DefaultAnisotropyTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE]           = GLTF::DefaultIridescenceTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS] = GLTF::DefaultIridescenceThicknessTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION]          = GLTF::DefaultTransmissionTextureAttribId;
        Result[PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS]             = GLTF::DefaultThicknessTextureAttribId;
        return Result;
    }();
    return Indices;
}

Uint32 GetGLTFMaterialShaderDataSize(PBR_Renderer::PSO_FLAGS Flags)
{
    Uint32 Size = static_cast<Uint32>(sizeof(GLTF::Material::ShaderAttribs));
    if (Flags & PBR_Renderer::PSO_FLAG_ENABLE_SHEEN)
        Size += static_cast<Uint32>(sizeof(GLTF::Material::SheenShaderAttribs));
    if (Flags & PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY)
        Size += static_cast<Uint32>(sizeof(GLTF::Material::AnisotropyShaderAttribs));
    if (Flags & PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE)
        Size += static_cast<Uint32>(sizeof(GLTF::Material::IridescenceShaderAttribs));
    if (Flags & PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION)
        Size += static_cast<Uint32>(sizeof(GLTF::Material::TransmissionShaderAttribs));
    if (Flags & PBR_Renderer::PSO_FLAG_ENABLE_VOLUME)
        Size += static_cast<Uint32>(sizeof(GLTF::Material::VolumeShaderAttribs));

    PBR_Renderer::ProcessTexturAttribs(
        Flags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID) {
            Size += static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
        });
    return Size;
}

RefCntAutoPtr<IRadientMaterialAsset> CreateStandardMaterialAsset(
    RadientAssetManagerImpl& AssetManager,
    const GLTF::Material&    Material)
{
    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RADIENT_STATUS                              Status = RadientGLTFConverter::ConvertMaterialDefinition(Material, DefinitionCI);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    Status = AssetManager.CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    Status = AssetManager.CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    Status = pMaterial->CreateWriter(pWriter.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    Status = RadientGLTFConverter::PopulateMaterial(Material, nullptr, 0, *pDefinition, *pWriter);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    Status = pWriter->Commit();
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE);
    if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
        return {};
    return pMaterial;
}

void ExpectShaderDataMatchesGLTF(const GLTF::Material&  Material,
                                 IRadientMaterialAsset& MaterialAsset)
{
    const auto* const pDefinition = static_cast<const RadientMaterialDefinitionImpl*>(MaterialAsset.GetDefinition());
    ASSERT_NE(pDefinition, nullptr);

    const PBR_Renderer::PSO_FLAGS Flags =
        Material.Attribs.Workflow == GLTF::Material::PBR_WORKFLOW_UNLIT ?
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP :
        GLTF_PBR_Renderer::GetMaterialPSOFlags(Material);
    const Uint32 ExpectedSize = GetGLTFMaterialShaderDataSize(Flags);
    ASSERT_EQ(pDefinition->GetShaderDataSize(), ExpectedSize);

    std::vector<Uint8> Actual(ExpectedSize, 0xCD);
    std::vector<Uint8> Expected(ExpectedSize, 0xCD);
    pDefinition->WriteShaderData(MaterialAsset, Actual.data());

    void* const pEnd = GLTF_PBR_Renderer::WritePBRMaterialShaderAttribs(
        Expected.data(),
        {Flags, GetStandardMaterialTextureAttribIndices(), Material});
    ASSERT_EQ(pEnd, Expected.data() + Expected.size());

    const auto Mismatch = std::mismatch(Actual.begin(), Actual.end(), Expected.begin());
    EXPECT_EQ(Mismatch.first, Actual.end())
        << "First shader data mismatch is at byte " << std::distance(Actual.begin(), Mismatch.first);
}

TEST(RadientStandardMaterialTest, DefinitionsAreCached)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pFirstDefinition;
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pSecondDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pFirstDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pSecondDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pFirstDefinition, nullptr);
    ASSERT_NE(pSecondDefinition, nullptr);
    EXPECT_EQ(pFirstDefinition, pSecondDefinition);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pAssetManager->CreateMaterial(pFirstDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    RadientMaterialParameterHandle BaseColorHandle;
    ASSERT_EQ(pFirstDefinition->FindParameter(RadientStandardMaterialBaseColorFactorName, &BaseColorHandle), RADIENT_STATUS_OK);
    const RadientFloat4 BaseColor = GetParameter<RadientFloat4>(*pMaterial, BaseColorHandle);
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
    EXPECT_EQ(pMaterial->GetTexture(NormalTextureHandle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture, nullptr);

    DefinitionCI.Features &= ~RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN;
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDifferentDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDifferentDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_NE(pFirstDefinition, pDifferentDefinition);
}

TEST(RadientStandardMaterialTest, MaterialsUseDefinitionTextureDefaults)
{
    RefCntAutoPtr<IRadientTextureAsset> pWhite        = MakeTestTextureAsset("texture://white");
    RefCntAutoPtr<IRadientTextureAsset> pBlack        = MakeTestTextureAsset("texture://black");
    RefCntAutoPtr<IRadientTextureAsset> pNormal       = MakeTestTextureAsset("texture://normal");
    RefCntAutoPtr<IRadientTextureAsset> pPhysicalDesc = MakeTestTextureAsset("texture://physical-desc");

    RadientMaterialAssetManager::CreateInfo ManagerCI;
    ManagerCI.DefaultTextures.pWhite        = pWhite;
    ManagerCI.DefaultTextures.pBlack        = pBlack;
    ManagerCI.DefaultTextures.pNormal       = pNormal;
    ManagerCI.DefaultTextures.pPhysicalDesc = pPhysicalDesc;

    const RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create(ManagerCI);
    ASSERT_NE(pMaterialManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    ASSERT_EQ(pMaterialManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    struct ExpectedTextureDefault
    {
        const char*           Name;
        IRadientTextureAsset* pTexture;
    };

    const std::array<ExpectedTextureDefault, 15> ExpectedDefaults{{
        {RadientStandardMaterialBaseColorTextureName, pWhite},
        {RadientStandardMaterialMetallicRoughnessTextureName, pPhysicalDesc},
        {RadientStandardMaterialNormalTextureName, pNormal},
        {RadientStandardMaterialOcclusionTextureName, pWhite},
        {RadientStandardMaterialEmissiveTextureName, pBlack},
        {RadientStandardMaterialClearCoatTextureName, pWhite},
        {RadientStandardMaterialClearCoatRoughnessTextureName, pWhite},
        {RadientStandardMaterialClearCoatNormalTextureName, pNormal},
        {RadientStandardMaterialSheenColorTextureName, pWhite},
        {RadientStandardMaterialSheenRoughnessTextureName, pWhite},
        {RadientStandardMaterialAnisotropyTextureName, pWhite},
        {RadientStandardMaterialIridescenceTextureName, pWhite},
        {RadientStandardMaterialIridescenceThicknessTextureName, pWhite},
        {RadientStandardMaterialTransmissionTextureName, pWhite},
        {RadientStandardMaterialThicknessTextureName, pWhite},
    }};

    for (const ExpectedTextureDefault& Expected : ExpectedDefaults)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->FindParameter(Expected.Name, &Handle), RADIENT_STATUS_OK) << Expected.Name;
        EXPECT_EQ(pDefinition->GetParameterDesc(Handle.Index).pDefaultTexture, Expected.pTexture) << Expected.Name;

        RefCntAutoPtr<IRadientTextureAsset> pMaterialTexture;
        ASSERT_EQ(pMaterial->GetTexture(Handle, 0, pMaterialTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK) << Expected.Name;
        EXPECT_EQ(pMaterialTexture, Expected.pTexture) << Expected.Name;
    }

    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS;
    DefinitionCI.Features     = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE;
    pDefinition.Release();
    ASSERT_EQ(pMaterialManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    pMaterial.Release();
    ASSERT_EQ(pMaterialManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    const std::array<ExpectedTextureDefault, 5> SpecularGlossinessDefaults{{
        {RadientStandardMaterialDiffuseTextureName, pWhite},
        {RadientStandardMaterialSpecularGlossinessTextureName, pWhite},
        {RadientStandardMaterialNormalTextureName, pNormal},
        {RadientStandardMaterialOcclusionTextureName, pWhite},
        {RadientStandardMaterialEmissiveTextureName, pBlack},
    }};

    for (const ExpectedTextureDefault& Expected : SpecularGlossinessDefaults)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->FindParameter(Expected.Name, &Handle), RADIENT_STATUS_OK) << Expected.Name;
        EXPECT_EQ(pDefinition->GetParameterDesc(Handle.Index).pDefaultTexture, Expected.pTexture) << Expected.Name;

        RefCntAutoPtr<IRadientTextureAsset> pMaterialTexture;
        ASSERT_EQ(pMaterial->GetTexture(Handle, 0, pMaterialTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK) << Expected.Name;
        EXPECT_EQ(pMaterialTexture, Expected.pTexture) << Expected.Name;
    }
}

TEST(RadientStandardMaterialTest, StandardTextureParameterHelper)
{
    RefCntAutoPtr<IRadientTextureAsset> pDefaultTexture = MakeTestTextureAsset("texture://default");
    RefCntAutoPtr<IRadientTextureAsset> pUpdatedTexture = MakeTestTextureAsset("texture://updated");

    const RadientStandardMaterialTextureParameters NoTextureParameters;
    EXPECT_EQ(NoTextureParameters.pTexture, nullptr);
    EXPECT_EQ(NoTextureParameters.UVSelector, -1);

    const RadientStandardMaterialTextureParameters TextureParameters{pUpdatedTexture};
    EXPECT_EQ(TextureParameters.pTexture, pUpdatedTexture);
    EXPECT_EQ(TextureParameters.UVSelector, 0);

    const RadientStandardMaterialTextureParameters NullTextureParameters{nullptr};
    EXPECT_EQ(NullTextureParameters.pTexture, nullptr);
    EXPECT_EQ(NullTextureParameters.UVSelector, -1);

    RadientMaterialAssetManager::CreateInfo ManagerCI;
    ManagerCI.DefaultTextures.pWhite = pDefaultTexture;

    const RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create(ManagerCI);
    ASSERT_NE(pMaterialManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    ASSERT_EQ(pMaterialManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    static constexpr std::array<const RadientStandardMaterialTextureParameterNames*, 15> TextureSemantics{{
        &RadientStandardMaterialBaseColorTextureParameterNames,
        &RadientStandardMaterialMetallicRoughnessTextureParameterNames,
        &RadientStandardMaterialNormalTextureParameterNames,
        &RadientStandardMaterialOcclusionTextureParameterNames,
        &RadientStandardMaterialEmissiveTextureParameterNames,
        &RadientStandardMaterialClearCoatTextureParameterNames,
        &RadientStandardMaterialClearCoatRoughnessTextureParameterNames,
        &RadientStandardMaterialClearCoatNormalTextureParameterNames,
        &RadientStandardMaterialSheenColorTextureParameterNames,
        &RadientStandardMaterialSheenRoughnessTextureParameterNames,
        &RadientStandardMaterialAnisotropyTextureParameterNames,
        &RadientStandardMaterialIridescenceTextureParameterNames,
        &RadientStandardMaterialIridescenceThicknessTextureParameterNames,
        &RadientStandardMaterialTransmissionTextureParameterNames,
        &RadientStandardMaterialThicknessTextureParameterNames,
    }};

    for (const RadientStandardMaterialTextureParameterNames* pNames : TextureSemantics)
    {
        const Char* ParameterNames[] = {
            pNames->Texture,
            pNames->UVSelector,
            pNames->UVScaleAndRotation,
            pNames->UVBias,
            pNames->WrapU,
            pNames->WrapV,
        };
        for (const Char* Name : ParameterNames)
        {
            RadientMaterialParameterHandle Handle;
            EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
        }
    }

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientStandardMaterialTextureParameters Parameters{pUpdatedTexture};
    Parameters.UVSelector         = 3;
    Parameters.UVScaleAndRotation = {{2.f, 0.1f, 0.2f, 3.f}};
    Parameters.UVBias             = {0.25f, 0.5f};
    Parameters.WrapU              = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP;
    Parameters.WrapV              = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP;

    EXPECT_EQ(SetStandardMaterialTextureParameters(
                  *pDefinition,
                  *pWriter,
                  RadientStandardMaterialBaseColorTextureParameterNames,
                  Parameters),
              RADIENT_STATUS_OK);
    EXPECT_EQ(SetStandardMaterialTextureParameters(
                  *pDefinition,
                  *pWriter,
                  RadientStandardMaterialBaseColorTextureParameterNames,
                  Parameters),
              RADIENT_STATUS_NO_CHANGE);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    const auto FindHandle = [&](const Char* Name) {
        RadientMaterialParameterHandle Handle;
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
        return Handle;
    };

    const RadientStandardMaterialTextureParameterNames& Names =
        RadientStandardMaterialBaseColorTextureParameterNames;
    const RadientMaterialParameterHandle TextureHandle = FindHandle(Names.Texture);

    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture;
    ASSERT_EQ(pMaterial->GetTexture(TextureHandle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pUpdatedTexture);
    EXPECT_EQ(GetParameter<Int32>(*pMaterial, FindHandle(Names.UVSelector)), Parameters.UVSelector);
    EXPECT_EQ((GetParameter<std::array<Float32, 4>>(*pMaterial, FindHandle(Names.UVScaleAndRotation))),
              Parameters.UVScaleAndRotation);

    const RadientFloat2 UVBias = GetParameter<RadientFloat2>(*pMaterial, FindHandle(Names.UVBias));
    EXPECT_FLOAT_EQ(UVBias.x, Parameters.UVBias.x);
    EXPECT_FLOAT_EQ(UVBias.y, Parameters.UVBias.y);
    EXPECT_EQ(GetParameter<RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE>(*pMaterial, FindHandle(Names.WrapU)),
              Parameters.WrapU);
    EXPECT_EQ(GetParameter<RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE>(*pMaterial, FindHandle(Names.WrapV)),
              Parameters.WrapV);

    const Uint64 VersionBeforeRedundantUpdate = pMaterial->GetVersion();
    EXPECT_EQ(SetStandardMaterialTextureParameters(*pDefinition, *pWriter, Names, Parameters),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pMaterial->GetVersion(), VersionBeforeRedundantUpdate);

    Parameters.pTexture = nullptr;
    EXPECT_EQ(SetStandardMaterialTextureParameters(*pDefinition, *pWriter, Names, Parameters),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    pStoredTexture.Release();
    ASSERT_EQ(pMaterial->GetTexture(TextureHandle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pDefaultTexture);

    const Uint64                                 VersionBeforeInvalidUpdate = pMaterial->GetVersion();
    RadientStandardMaterialTextureParameterNames InvalidNames               = Names;
    InvalidNames.WrapV                                                      = "MissingTextureWrapV";
    Parameters.pTexture                                                     = pUpdatedTexture;
    Parameters.UVSelector                                                   = 7;
    EXPECT_EQ(SetStandardMaterialTextureParameters(*pDefinition, *pWriter, InvalidNames, Parameters),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pMaterial->GetVersion(), VersionBeforeInvalidUpdate);

    EXPECT_EQ(GetParameter<Int32>(*pMaterial, FindHandle(Names.UVSelector)), 3);
    pStoredTexture.Release();
    ASSERT_EQ(pMaterial->GetTexture(TextureHandle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pDefaultTexture);

    RadientStandardMaterialTextureParameterNames InvalidTypes = Names;
    InvalidTypes.WrapV                                        = RadientStandardMaterialBaseColorFactorName;
    EXPECT_EQ(SetStandardMaterialTextureParameters(*pDefinition, *pWriter, InvalidTypes, Parameters),
              RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pMaterial->GetVersion(), VersionBeforeInvalidUpdate);

    EXPECT_EQ(SetStandardMaterialTextureParameters(*pDefinition, *pWriter, Names, NoTextureParameters),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(GetParameter<Int32>(*pMaterial, FindHandle(Names.UVSelector)), -1);
    pStoredTexture.Release();
    ASSERT_EQ(pMaterial->GetTexture(TextureHandle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pDefaultTexture);
}

TEST(RadientStandardMaterialTest, ExtendedPBRShaderDataMatchesGLTFPacking)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    const GLTF::Material                 Material = MakeExtendedPBRMaterial();
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateStandardMaterialAsset(*pAssetManager, Material);
    ASSERT_NE(pMaterial, nullptr);

    ExpectShaderDataMatchesGLTF(Material, *pMaterial);
}

TEST(RadientStandardMaterialTest, DefaultPBRShaderDataMatchesGLTFPacking)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Material Material;
    Material.Attribs.BaseColorFactor = float4{0.2f, 0.3f, 0.4f, 0.5f};
    Material.Attribs.EmissiveFactor  = float3{0.6f, 0.7f, 0.8f};
    Material.Attribs.AlphaMode       = GLTF::Material::ALPHA_MODE_MASK;
    Material.Attribs.AlphaCutoff     = 0.35f;
    Material.Attribs.MetallicFactor  = 0.45f;
    Material.Attribs.RoughnessFactor = 0.55f;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateStandardMaterialAsset(*pAssetManager, Material);
    ASSERT_NE(pMaterial, nullptr);

    ExpectShaderDataMatchesGLTF(Material, *pMaterial);
}

TEST(RadientStandardMaterialTest, SpecularGlossinessShaderDataMatchesGLTFPacking)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    const GLTF::Material                 Material = MakeSpecularGlossinessMaterial();
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateStandardMaterialAsset(*pAssetManager, Material);
    ASSERT_NE(pMaterial, nullptr);

    ExpectShaderDataMatchesGLTF(Material, *pMaterial);
}

TEST(RadientStandardMaterialTest, UnlitPBRShaderDataMatchesGLTFPacking)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Material Material;
    Material.Attribs.Workflow        = GLTF::Material::PBR_WORKFLOW_UNLIT;
    Material.Attribs.BaseColorFactor = float4{0.25f, 0.5f, 0.75f, 0.8f};
    Material.Attribs.AlphaMode       = GLTF::Material::ALPHA_MODE_BLEND;
    Material.Attribs.AlphaCutoff     = 0.2f;

    GLTF::MaterialBuilder Builder{Material};
    Builder.SetTextureId(GLTF::DefaultBaseColorTextureAttribId, 0);
    GLTF::Material::TextureShaderAttribs& TextureAttribs =
        Builder.GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId);
    TextureAttribs.SetUVSelector(1);
    TextureAttribs.SetWrapUMode(TEXTURE_ADDRESS_CLAMP);
    TextureAttribs.SetWrapVMode(TEXTURE_ADDRESS_WRAP);
    TextureAttribs.UVScaleAndRotation = float2x2{2.f, 0.1f, 0.2f, 3.f};
    TextureAttribs.UBias              = 0.15f;
    TextureAttribs.VBias              = 0.25f;
    Builder.Finalize();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateStandardMaterialAsset(*pAssetManager, Material);
    ASSERT_NE(pMaterial, nullptr);

    ExpectShaderDataMatchesGLTF(Material, *pMaterial);
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
    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
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

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pAssetManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

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
        ASSERT_EQ(pMaterial->GetTexture(FindHandle(Semantic.Texture), 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, nullptr);
        EXPECT_EQ(GetParameter<Int32>(*pMaterial, FindHandle(Semantic.UVSelector)), -1);
        EXPECT_EQ((GetParameter<std::array<Float32, 4>>(*pMaterial, FindHandle(Semantic.UVScaleAndRotation))), ExpectedUVScaleAndRotation);

        const RadientFloat2 UVBias = GetParameter<RadientFloat2>(*pMaterial, FindHandle(Semantic.UVBias));
        EXPECT_FLOAT_EQ(UVBias.x, ExpectedUVBias.x);
        EXPECT_FLOAT_EQ(UVBias.y, ExpectedUVBias.y);
        EXPECT_EQ(GetParameter<Uint32>(*pMaterial, FindHandle(Semantic.WrapU)), static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP));
        EXPECT_EQ(GetParameter<Uint32>(*pMaterial, FindHandle(Semantic.WrapV)), static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP));
    }
}

TEST(RadientStandardMaterialTest, MinimalSchemasAreExact)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    const auto ExpectParameters = [](IRadientMaterialDefinitionAsset& pDefinition,
                                     const auto&                      ExpectedNames) {
        ASSERT_EQ(pDefinition.GetParameterCount(), static_cast<Uint32>(ExpectedNames.size()));
        for (size_t Index = 0; Index < ExpectedNames.size(); ++Index)
        {
            const Char* const Name = ExpectedNames[Index];
            EXPECT_STREQ(pDefinition.GetParameterDesc(static_cast<Uint32>(Index)).Name, Name);

            RadientMaterialParameterHandle Handle;
            EXPECT_EQ(pDefinition.FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
            EXPECT_EQ(Handle.Index, Index) << Name;
        }
    };

    static constexpr std::array MetallicRoughnessParameters{
        RadientStandardMaterialBaseColorFactorName,
        RadientStandardMaterialMetallicFactorName,
        RadientStandardMaterialRoughnessFactorName,
        RadientStandardMaterialEmissiveFactorName,
        RadientStandardMaterialNormalScaleName,
        RadientStandardMaterialOcclusionStrengthName,
        RadientStandardMaterialBaseColorTextureName,
        RadientStandardMaterialBaseColorTextureUVSelectorName,
        RadientStandardMaterialBaseColorTextureUVScaleAndRotationName,
        RadientStandardMaterialBaseColorTextureUVBiasName,
        RadientStandardMaterialBaseColorTextureWrapUName,
        RadientStandardMaterialBaseColorTextureWrapVName,
        RadientStandardMaterialMetallicRoughnessTextureName,
        RadientStandardMaterialMetallicRoughnessTextureUVSelectorName,
        RadientStandardMaterialMetallicRoughnessTextureUVScaleAndRotationName,
        RadientStandardMaterialMetallicRoughnessTextureUVBiasName,
        RadientStandardMaterialMetallicRoughnessTextureWrapUName,
        RadientStandardMaterialMetallicRoughnessTextureWrapVName,
        RadientStandardMaterialNormalTextureName,
        RadientStandardMaterialNormalTextureUVSelectorName,
        RadientStandardMaterialNormalTextureUVScaleAndRotationName,
        RadientStandardMaterialNormalTextureUVBiasName,
        RadientStandardMaterialNormalTextureWrapUName,
        RadientStandardMaterialNormalTextureWrapVName,
        RadientStandardMaterialOcclusionTextureName,
        RadientStandardMaterialOcclusionTextureUVSelectorName,
        RadientStandardMaterialOcclusionTextureUVScaleAndRotationName,
        RadientStandardMaterialOcclusionTextureUVBiasName,
        RadientStandardMaterialOcclusionTextureWrapUName,
        RadientStandardMaterialOcclusionTextureWrapVName,
        RadientStandardMaterialEmissiveTextureName,
        RadientStandardMaterialEmissiveTextureUVSelectorName,
        RadientStandardMaterialEmissiveTextureUVScaleAndRotationName,
        RadientStandardMaterialEmissiveTextureUVBiasName,
        RadientStandardMaterialEmissiveTextureWrapUName,
        RadientStandardMaterialEmissiveTextureWrapVName,
    };

    static constexpr std::array SpecularGlossinessParameters{
        RadientStandardMaterialDiffuseFactorName,
        RadientStandardMaterialSpecularFactorName,
        RadientStandardMaterialGlossinessFactorName,
        RadientStandardMaterialEmissiveFactorName,
        RadientStandardMaterialNormalScaleName,
        RadientStandardMaterialOcclusionStrengthName,
        RadientStandardMaterialDiffuseTextureName,
        RadientStandardMaterialDiffuseTextureUVSelectorName,
        RadientStandardMaterialDiffuseTextureUVScaleAndRotationName,
        RadientStandardMaterialDiffuseTextureUVBiasName,
        RadientStandardMaterialDiffuseTextureWrapUName,
        RadientStandardMaterialDiffuseTextureWrapVName,
        RadientStandardMaterialSpecularGlossinessTextureName,
        RadientStandardMaterialSpecularGlossinessTextureUVSelectorName,
        RadientStandardMaterialSpecularGlossinessTextureUVScaleAndRotationName,
        RadientStandardMaterialSpecularGlossinessTextureUVBiasName,
        RadientStandardMaterialSpecularGlossinessTextureWrapUName,
        RadientStandardMaterialSpecularGlossinessTextureWrapVName,
        RadientStandardMaterialNormalTextureName,
        RadientStandardMaterialNormalTextureUVSelectorName,
        RadientStandardMaterialNormalTextureUVScaleAndRotationName,
        RadientStandardMaterialNormalTextureUVBiasName,
        RadientStandardMaterialNormalTextureWrapUName,
        RadientStandardMaterialNormalTextureWrapVName,
        RadientStandardMaterialOcclusionTextureName,
        RadientStandardMaterialOcclusionTextureUVSelectorName,
        RadientStandardMaterialOcclusionTextureUVScaleAndRotationName,
        RadientStandardMaterialOcclusionTextureUVBiasName,
        RadientStandardMaterialOcclusionTextureWrapUName,
        RadientStandardMaterialOcclusionTextureWrapVName,
        RadientStandardMaterialEmissiveTextureName,
        RadientStandardMaterialEmissiveTextureUVSelectorName,
        RadientStandardMaterialEmissiveTextureUVScaleAndRotationName,
        RadientStandardMaterialEmissiveTextureUVBiasName,
        RadientStandardMaterialEmissiveTextureWrapUName,
        RadientStandardMaterialEmissiveTextureWrapVName,
    };

    static constexpr std::array SpecularGlossinessParameterTypes{
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT,
        RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,
        RADIENT_MATERIAL_PARAMETER_TYPE_INT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,
        RADIENT_MATERIAL_PARAMETER_TYPE_INT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,
        RADIENT_MATERIAL_PARAMETER_TYPE_INT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,
        RADIENT_MATERIAL_PARAMETER_TYPE_INT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE,
        RADIENT_MATERIAL_PARAMETER_TYPE_INT,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2,
        RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
        RADIENT_MATERIAL_PARAMETER_TYPE_UINT,
    };
    static_assert(SpecularGlossinessParameters.size() == SpecularGlossinessParameterTypes.size());

    RadientStandardMaterialDefinitionCreateInfo    DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetDesc().Type, RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE);
    const auto& SurfaceDesc =
        static_cast<const RadientSurfaceMaterialDefinitionDesc&>(pDefinition->GetDesc());
    EXPECT_EQ(SurfaceDesc.ShadingModel, RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS);
    EXPECT_EQ(SurfaceDesc.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    EXPECT_STREQ(pDefinition->GetDesc().Reference.URI, "standard-material:0:0");
    EXPECT_EQ(pDefinition->GetDesc().Reference.Version, RadientStandardMaterialSchemaVersion);
    ExpectParameters(*pDefinition, MetallicRoughnessParameters);

    RadientMaterialParameterHandle Handle;

    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS;
    pDefinition.Release();
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    const auto& SpecularGlossinessSurfaceDesc =
        static_cast<const RadientSurfaceMaterialDefinitionDesc&>(pDefinition->GetDesc());
    EXPECT_EQ(SpecularGlossinessSurfaceDesc.ShadingModel,
              RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS);
    EXPECT_EQ(SpecularGlossinessSurfaceDesc.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    EXPECT_STREQ(pDefinition->GetDesc().Reference.URI, "standard-material:1:0");
    EXPECT_EQ(pDefinition->GetDesc().Reference.Version, RadientStandardMaterialSchemaVersion);
    ExpectParameters(*pDefinition, SpecularGlossinessParameters);
    for (size_t Index = 0; Index < SpecularGlossinessParameterTypes.size(); ++Index)
    {
        const RadientMaterialParameterDesc& Parameter =
            pDefinition->GetParameterDesc(static_cast<Uint32>(Index));
        EXPECT_EQ(Parameter.Type, SpecularGlossinessParameterTypes[Index]) << Parameter.Name;
        EXPECT_EQ(Parameter.ArraySize, 1u) << Parameter.Name;
    }
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialBaseColorFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialRoughnessFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialBaseColorTextureName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicRoughnessTextureName, &Handle), RADIENT_STATUS_NOT_FOUND);

    RefCntAutoPtr<IRadientMaterialAsset> pSpecularGlossinessMaterial;
    ASSERT_EQ(pAssetManager->CreateMaterial(pDefinition, pSpecularGlossinessMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pSpecularGlossinessMaterial, nullptr);

    const auto FindSpecularGlossinessHandle = [&](const Char* Name) {
        RadientMaterialParameterHandle Parameter;
        EXPECT_EQ(pDefinition->FindParameter(Name, &Parameter), RADIENT_STATUS_OK) << Name;
        return Parameter;
    };

    const RadientFloat4 DiffuseFactor = GetParameter<RadientFloat4>(
        *pSpecularGlossinessMaterial,
        FindSpecularGlossinessHandle(RadientStandardMaterialDiffuseFactorName));
    EXPECT_FLOAT_EQ(DiffuseFactor.x, 1.f);
    EXPECT_FLOAT_EQ(DiffuseFactor.y, 1.f);
    EXPECT_FLOAT_EQ(DiffuseFactor.z, 1.f);
    EXPECT_FLOAT_EQ(DiffuseFactor.w, 1.f);

    const RadientFloat3 SpecularFactor = GetParameter<RadientFloat3>(
        *pSpecularGlossinessMaterial,
        FindSpecularGlossinessHandle(RadientStandardMaterialSpecularFactorName));
    EXPECT_FLOAT_EQ(SpecularFactor.x, 1.f);
    EXPECT_FLOAT_EQ(SpecularFactor.y, 1.f);
    EXPECT_FLOAT_EQ(SpecularFactor.z, 1.f);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(
                        *pSpecularGlossinessMaterial,
                        FindSpecularGlossinessHandle(RadientStandardMaterialGlossinessFactorName)),
                    1.f);

    for (const RadientStandardMaterialTextureParameterNames* pNames : {
             &RadientStandardMaterialDiffuseTextureParameterNames,
             &RadientStandardMaterialSpecularGlossinessTextureParameterNames,
         })
    {
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pSpecularGlossinessMaterial->GetTexture(
                      FindSpecularGlossinessHandle(pNames->Texture), 0, pTexture.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, nullptr);
        EXPECT_EQ(GetParameter<Int32>(*pSpecularGlossinessMaterial,
                                      FindSpecularGlossinessHandle(pNames->UVSelector)),
                  -1);
    }

    static constexpr std::array UnlitParameters{
        RadientStandardMaterialBaseColorFactorName,
        RadientStandardMaterialBaseColorTextureName,
        RadientStandardMaterialBaseColorTextureUVSelectorName,
        RadientStandardMaterialBaseColorTextureUVScaleAndRotationName,
        RadientStandardMaterialBaseColorTextureUVBiasName,
        RadientStandardMaterialBaseColorTextureWrapUName,
        RadientStandardMaterialBaseColorTextureWrapVName,
    };

    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
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
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetParameterCount(), 7u);

    static constexpr std::array ExpectedParameters{
        RadientStandardMaterialBaseColorFactorName,
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
    EXPECT_EQ(pDefinition->FindParameter("AlphaCutoff", &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialEmissiveFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientStandardMaterialTest, RejectsInvalidModel)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_COUNT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI, "Invalid surface shading model");
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
    DefinitionCI.Features = static_cast<RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS>(
        static_cast<Uint32>(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL) + 1u);
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Standard material feature flags contain unsupported bits");
}

TEST(RadientStandardMaterialTest, RejectsOptionalFeaturesForUnlitMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
    DefinitionCI.Features     = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Unlit standard materials do not support optional material features");
}

TEST(RadientStandardMaterialTest, RejectsOptionalFeaturesForSpecularGlossinessMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS;
    DefinitionCI.Features     = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    ExpectInvalidStandardDefinition(
        *pAssetManager,
        DefinitionCI,
        "Specular-glossiness standard materials do not support optional material features");
}

TEST(RadientStandardMaterialTest, VolumeRequiresTransmission)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The volume material feature requires the transmission material feature");

    DefinitionCI.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION;
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    EXPECT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
}

} // namespace
