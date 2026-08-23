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

#include "RadientMaterials.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialImpl.hpp"
#include "RadientTestAssetHelpers.hpp"

#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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

void ExpectInvalidHandle(const RadientMaterialParameterHandle& Handle)
{
    EXPECT_EQ(Handle.Definition, InvalidRadientHandle);
    EXPECT_EQ(Handle.Index, ~Uint32{0});
    EXPECT_EQ(Handle.Reserved, 0u);
    EXPECT_FALSE(Handle);
}

RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                IRadientMaterialDefinition**         ppDefinition)
{
    return RadientMaterialAssetManager::CreateDefinition(DefinitionDesc, ppDefinition);
}

RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionDesc&       DefinitionDesc,
                                const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
                                IRadientMaterialDefinition**               ppDefinition)
{
    return RadientMaterialAssetManager::CreateDefinition(DefinitionDesc, ShaderDataLayout, ppDefinition);
}

RefCntAutoPtr<IRadientMaterialDefinition> CreateDefinition(
    const RadientMaterialParameterDesc* pParameters,
    Uint32                              ParameterCount,
    const char*                         Name = "Test material definition")
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = Name;
    DefinitionDesc.Reference      = {"material-definition://test", 7};
    DefinitionDesc.pParameters    = pParameters;
    DefinitionDesc.ParameterCount = ParameterCount;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return pDefinition;
}

void ExpectInvalidDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                             const char*                          ExpectedError)
{
    TestingEnvironment::ErrorScope            ExpectedErrors{ExpectedError};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pDefinition, nullptr);
}

void ExpectInvalidShaderDataLayout(const RadientMaterialDefinitionDesc&       DefinitionDesc,
                                   const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
                                   const char*                                ExpectedError)
{
    TestingEnvironment::ErrorScope            ExpectedErrors{ExpectedError};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pDefinition, nullptr);
}

struct ShaderParameterPackingTestParam
{
    RADIENT_MATERIAL_PARAMETER_TYPE Type;
    Uint32                          Size;
    const char*                     Name;
};

class RadientMaterialShaderParameterPackingTest :
    public testing::TestWithParam<ShaderParameterPackingTestParam>
{};

TEST(RadientMaterialTest, DefinitionCopiesSchemaAndDefaults)
{
    char          DefinitionName[] = "Copied definition";
    char          ParameterName[]  = "BaseColor";
    char          ReferenceURI[]   = "material-definition://copied";
    RadientFloat4 DefaultColor{0.1f, 0.2f, 0.3f, 0.4f};

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = ParameterName;
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameter.pDefaultValue = &DefaultColor;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = DefinitionName;
    DefinitionDesc.Reference      = {ReferenceURI, 11};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    DefinitionName[0] = 'X';
    ParameterName[0]  = 'X';
    ReferenceURI[0]   = 'X';
    DefaultColor      = {};

    const RadientMaterialDefinitionDesc& StoredDesc = pDefinition->GetDesc();
    EXPECT_STREQ(StoredDesc.Name, "Copied definition");
    EXPECT_EQ(StoredDesc.Type, RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE);
    EXPECT_STREQ(StoredDesc.Reference.URI, "material-definition://copied");
    EXPECT_EQ(StoredDesc.Reference.Version, 11u);
    ASSERT_EQ(StoredDesc.ParameterCount, 1u);
    ASSERT_NE(StoredDesc.pParameters, nullptr);
    EXPECT_STREQ(pDefinition->GetReference().URI, "material-definition://copied");
    EXPECT_EQ(pDefinition->GetReference().Version, 11u);
    EXPECT_EQ(pDefinition->GetType(), RADIENT_ASSET_TYPE_MATERIAL);
    EXPECT_EQ(pDefinition->GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterCount(), 1u);

    const RadientMaterialParameterDesc& StoredParameter = pDefinition->GetParameterDesc(0);
    EXPECT_EQ(&StoredParameter, &StoredDesc.pParameters[0]);
    EXPECT_STREQ(StoredParameter.Name, "BaseColor");
    EXPECT_EQ(StoredParameter.Type, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4);
    ASSERT_NE(StoredParameter.pDefaultValue, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(StoredParameter.pDefaultValue) % alignof(Float32), 0u);

    RadientFloat4 StoredDefault{};
    std::memcpy(&StoredDefault, StoredParameter.pDefaultValue, sizeof(StoredDefault));
    EXPECT_FLOAT_EQ(StoredDefault.x, 0.1f);
    EXPECT_FLOAT_EQ(StoredDefault.y, 0.2f);
    EXPECT_FLOAT_EQ(StoredDefault.z, 0.3f);
    EXPECT_FLOAT_EQ(StoredDefault.w, 0.4f);

    RadientMaterialParameterHandle ByIndex;
    RadientMaterialParameterHandle ByName;
    EXPECT_EQ(pDefinition->GetParameterHandle(0, &ByIndex), RADIENT_STATUS_OK);
    EXPECT_EQ(pDefinition->FindParameter("BaseColor", &ByName), RADIENT_STATUS_OK);
    EXPECT_EQ(ByIndex, ByName);
    EXPECT_TRUE(ByIndex);
    EXPECT_EQ(pDefinition->FindParameter("Missing", &ByName), RADIENT_STATUS_NOT_FOUND);
    EXPECT_FALSE(ByName);
}

TEST(RadientMaterialTest, DefinitionPreservesConcreteSurfaceDescription)
{
    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name         = "Surface definition";
    DefinitionDesc.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
    DefinitionDesc.Features     = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    const RadientMaterialDefinitionDesc& StoredBaseDesc = pDefinition->GetDesc();
    ASSERT_EQ(StoredBaseDesc.Type, RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE);
    const auto& StoredSurfaceDesc =
        static_cast<const RadientSurfaceMaterialDefinitionDesc&>(StoredBaseDesc);
    EXPECT_EQ(StoredSurfaceDesc.ShadingModel, RADIENT_SURFACE_SHADING_MODEL_UNLIT);
    EXPECT_EQ(StoredSurfaceDesc.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
}

TEST(RadientMaterialTest, SurfaceStateIsMutableClonedAndPacked)
{
    struct ShaderSurfaceState
    {
        Uint32  SurfaceMode = ~Uint32{0};
        Float32 AlphaCutoff = -1.f;
    };

    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name = "Surface state definition";

    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.SurfaceModeOffset = offsetof(ShaderSurfaceState, SurfaceMode);
    SurfacePacking.AlphaCutoffOffset = offsetof(ShaderSurfaceState, AlphaCutoff);

    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = sizeof(ShaderSurfaceState);
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{
        pInstance, IID_RadientSurfaceMaterialInstance};
    ASSERT_NE(pSurfaceInstance, nullptr);
    EXPECT_EQ(pSurfaceInstance->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_FLOAT_EQ(pSurfaceInstance->GetAlphaCutoff(), 0.5f);
    EXPECT_FALSE(pSurfaceInstance->IsDoubleSided());

    const Uint64                                  InitialVersion = pInstance->GetVersion();
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialInstanceWriter> pSurfaceWriter{
        pWriter, IID_RadientSurfaceMaterialInstanceWriter};
    ASSERT_NE(pSurfaceWriter, nullptr);

    EXPECT_EQ(pSurfaceWriter->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_COUNT),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pSurfaceWriter->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pSurfaceWriter->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pSurfaceWriter->SetAlphaCutoff(0.25f), RADIENT_STATUS_OK);
    EXPECT_EQ(pSurfaceWriter->SetAlphaCutoff(0.25f), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pSurfaceWriter->SetDoubleSided(True), RADIENT_STATUS_OK);
    EXPECT_EQ(pSurfaceWriter->SetDoubleSided(True), RADIENT_STATUS_NO_CHANGE);
    ASSERT_EQ(pSurfaceWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 1);
    EXPECT_EQ(pSurfaceInstance->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);
    EXPECT_FLOAT_EQ(pSurfaceInstance->GetAlphaCutoff(), 0.25f);
    EXPECT_TRUE(pSurfaceInstance->IsDoubleSided());

    ShaderSurfaceState ShaderState;
    static_cast<RadientMaterialDefinitionImpl&>(*pDefinition)
        .WriteShaderData(*pInstance, &ShaderState);
    EXPECT_EQ(ShaderState.SurfaceMode, static_cast<Uint32>(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT));
    EXPECT_FLOAT_EQ(ShaderState.AlphaCutoff, 0.25f);

    RefCntAutoPtr<IRadientMaterialInstance> pClone;
    ASSERT_EQ(pInstance->Clone(pClone.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceClone{
        pClone, IID_RadientSurfaceMaterialInstance};
    ASSERT_NE(pSurfaceClone, nullptr);
    EXPECT_EQ(pSurfaceClone->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);
    EXPECT_FLOAT_EQ(pSurfaceClone->GetAlphaCutoff(), 0.25f);
    EXPECT_TRUE(pSurfaceClone->IsDoubleSided());
}

TEST(RadientMaterialTest, NonSurfaceInstancesDoNotExposeSurfaceInterfaces)
{
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(nullptr, 0);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);
    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{
        pInstance, IID_RadientSurfaceMaterialInstance};
    EXPECT_EQ(pSurfaceInstance, nullptr);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialInstanceWriter> pSurfaceWriter{
        pWriter, IID_RadientSurfaceMaterialInstanceWriter};
    EXPECT_EQ(pSurfaceWriter, nullptr);
}

TEST(RadientMaterialTest, DefinitionPacksInstanceShaderData)
{
    const RadientFloat4          DefaultColor{0.1f, 0.2f, 0.3f, 0.4f};
    const std::array<Float32, 2> DefaultWeights{0.5f, 0.75f};
    const Uint32                 DefaultMode = 7;

    std::array<RadientMaterialParameterDesc, 3> Parameters{};
    Parameters[0].Name          = "Color";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameters[0].pDefaultValue = &DefaultColor;
    Parameters[1].Name          = "Weights";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1].ArraySize     = static_cast<Uint32>(DefaultWeights.size());
    Parameters[1].pDefaultValue = DefaultWeights.data();
    Parameters[2].Name          = "Mode";
    Parameters[2].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[2].pDefaultValue = &DefaultMode;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = "Packed material definition";
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    std::array<RadientMaterialShaderParameterPacking, 3> Mappings{{
        {1, 4},
        {0, 16},
        {2, 40},
    }};
    RadientMaterialShaderDataLayoutDesc                  ShaderDataLayout{};
    ShaderDataLayout.Size         = 48;
    ShaderDataLayout.pMappings    = Mappings.data();
    ShaderDataLayout.MappingCount = static_cast<Uint32>(Mappings.size());

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    // The definition owns the mappings independently of their source array.
    Mappings = {};

    auto& DefinitionImpl = static_cast<RadientMaterialDefinitionImpl&>(*pDefinition);
    EXPECT_EQ(DefinitionImpl.GetShaderDataSize(), ShaderDataLayout.Size);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    std::array<Uint8, 48> ShaderData;
    ShaderData.fill(0xcd);
    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());

    std::array<Uint8, 48> Expected{};
    std::memcpy(Expected.data() + 4, DefaultWeights.data(), sizeof(DefaultWeights));
    std::memcpy(Expected.data() + 16, &DefaultColor, sizeof(DefaultColor));
    std::memcpy(Expected.data() + 40, &DefaultMode, sizeof(DefaultMode));
    EXPECT_EQ(ShaderData, Expected);

    const RadientFloat4            UpdatedColor{0.9f, 0.8f, 0.7f, 0.6f};
    RadientMaterialParameterHandle ColorHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ColorHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ColorHandle, UpdatedColor), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    std::memcpy(Expected.data() + 16, &UpdatedColor, sizeof(UpdatedColor));
    EXPECT_EQ(ShaderData, Expected);
}

TEST(RadientMaterialTest, DefinitionOwnsAndAppliesShaderDataInitializations)
{
    const Uint32 DefaultValue = 0x12345678u;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameter.pDefaultValue = &DefaultValue;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = "Shader data initialization test";
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    std::array<Uint8, 4>                                   FixedBytes{0x11, 0x22, 0x33, 0x44};
    std::array<Uint8, 2>                                   OverlappingBytes{0xaa, 0xbb};
    Uint32                                                 OverriddenValue = 0xffffffffu;
    std::array<RadientMaterialShaderDataInitialization, 3> Initializations{{
        {FixedBytes.data(), static_cast<Uint32>(FixedBytes.size()), 2},
        {OverlappingBytes.data(), static_cast<Uint32>(OverlappingBytes.size()), 4},
        {&OverriddenValue, static_cast<Uint32>(sizeof(OverriddenValue)), 8},
    }};
    const RadientMaterialShaderParameterPacking            Mapping{0, 8};

    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size                = 16;
    ShaderDataLayout.pMappings           = &Mapping;
    ShaderDataLayout.MappingCount        = 1;
    ShaderDataLayout.pInitializations    = Initializations.data();
    ShaderDataLayout.InitializationCount = static_cast<Uint32>(Initializations.size());

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    // The definition owns initialization bytes independently of their sources.
    FixedBytes.fill(0);
    OverlappingBytes.fill(0);
    OverriddenValue = 0;
    Initializations = {};

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    std::array<Uint8, 16> ShaderData;
    ShaderData.fill(0xcd);
    static_cast<RadientMaterialDefinitionImpl&>(*pDefinition).WriteShaderData(*pInstance, ShaderData.data());

    std::array<Uint8, 16> Expected{};
    Expected[2] = 0x11;
    Expected[3] = 0x22;
    Expected[4] = 0xaa;
    Expected[5] = 0xbb;
    std::memcpy(Expected.data() + Mapping.Offset, &DefaultValue, sizeof(DefaultValue));
    EXPECT_EQ(ShaderData, Expected);
}

TEST(RadientMaterialTest, DefinitionPacksTextureShaderData)
{
    const Int32                                 DefaultUVSelector = 0;
    const std::array<Float32, 4>                DefaultUVTransform{1.f, 0.f, 0.f, 1.f};
    const RadientFloat2                         DefaultUVBias{0.25f, 0.5f};
    const RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE DefaultWrap = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP;

    std::array<RadientMaterialParameterDesc, 6> Parameters{};
    Parameters[0].Name          = "Texture";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].Name          = "UVSelector";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_INT;
    Parameters[1].pDefaultValue = &DefaultUVSelector;
    Parameters[2].Name          = "UVTransform";
    Parameters[2].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2;
    Parameters[2].pDefaultValue = DefaultUVTransform.data();
    Parameters[3].Name          = "UVBias";
    Parameters[3].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2;
    Parameters[3].pDefaultValue = &DefaultUVBias;
    Parameters[4].Name          = "WrapU";
    Parameters[4].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[4].pDefaultValue = &DefaultWrap;
    Parameters[5].Name          = "WrapV";
    Parameters[5].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[5].pDefaultValue = &DefaultWrap;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = "Texture shader packing test";
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    static constexpr Uint32                                    TextureOffset = 16;
    const std::array<RadientMaterialShaderParameterPacking, 2> Mappings{{
        {2, TextureOffset + offsetof(GLTF::Material::TextureShaderAttribs, UVScaleAndRotation)},
        {3, TextureOffset + offsetof(GLTF::Material::TextureShaderAttribs, UBias)},
    }};
    const RadientMaterialShaderTexturePacking                  TexturePacking{0, 1, 4, 5, TextureOffset};

    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size =
        TextureOffset + static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs)) + 16;
    ShaderDataLayout.pMappings           = Mappings.data();
    ShaderDataLayout.MappingCount        = static_cast<Uint32>(Mappings.size());
    ShaderDataLayout.pTexturePackings    = &TexturePacking;
    ShaderDataLayout.TexturePackingCount = 1;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    auto&              DefinitionImpl = static_cast<RadientMaterialDefinitionImpl&>(*pDefinition);
    std::vector<Uint8> ShaderData(ShaderDataLayout.Size, 0xcd);

    GLTF::Material::TextureShaderAttribs Expected{};
    Expected.SetUVSelector(DefaultUVSelector);
    Expected.SetWrapUMode(TEXTURE_ADDRESS_WRAP);
    Expected.SetWrapVMode(TEXTURE_ADDRESS_WRAP);
    Expected.UBias = DefaultUVBias.x;
    Expected.VBias = DefaultUVBias.y;
    std::memcpy(&Expected.UVScaleAndRotation, DefaultUVTransform.data(), sizeof(Expected.UVScaleAndRotation));
    Expected.AtlasUVScaleAndBias = float4{};

    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    EXPECT_EQ(std::memcmp(ShaderData.data() + TextureOffset, &Expected, sizeof(Expected)), 0);

    RadientMaterialParameterHandle UVSelectorHandle;
    RadientMaterialParameterHandle WrapUHandle;
    RadientMaterialParameterHandle WrapVHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &UVSelectorHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(4, &WrapUHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(5, &WrapVHandle), RADIENT_STATUS_OK);

    const Int32                                   UpdatedUVSelector = 1;
    const RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE   UpdatedWrapU      = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(UVSelectorHandle, UpdatedUVSelector), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(WrapUHandle, UpdatedWrapU), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    Expected.SetUVSelector(UpdatedUVSelector);
    Expected.SetWrapUMode(TEXTURE_ADDRESS_CLAMP);
    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    EXPECT_EQ(std::memcmp(ShaderData.data() + TextureOffset, &Expected, sizeof(Expected)), 0);

    const RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE RestoredWrapU = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP;
    const RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE UpdatedWrapV  = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP;
    pWriter.Release();
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(WrapUHandle, RestoredWrapU), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(WrapVHandle, UpdatedWrapV), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    Expected.SetWrapUMode(TEXTURE_ADDRESS_WRAP);
    Expected.SetWrapVMode(TEXTURE_ADDRESS_CLAMP);
    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    EXPECT_EQ(std::memcmp(ShaderData.data() + TextureOffset, &Expected, sizeof(Expected)), 0);
}

TEST_P(RadientMaterialShaderParameterPackingTest, PacksParameter)
{
    const ShaderParameterPackingTestParam& Param = GetParam();

    std::vector<Uint8> DefaultData(Param.Size, 0);
    std::vector<Uint8> UpdatedData(Param.Size, 0);
    if (Param.Type == RADIENT_MATERIAL_PARAMETER_TYPE_BOOL)
    {
        UpdatedData[0] = 1;
    }
    else
    {
        static constexpr Uint32 ComponentSize = static_cast<Uint32>(sizeof(Uint32));
        ASSERT_EQ(Param.Size % ComponentSize, 0u);
        for (Uint32 Component = 0; Component < Param.Size / ComponentSize; ++Component)
        {
            const Uint32 DefaultValue = 0x3e800000u + Component;
            const Uint32 UpdatedValue = 0x3f000000u + Component;
            std::memcpy(DefaultData.data() + Component * ComponentSize, &DefaultValue, sizeof(DefaultValue));
            std::memcpy(UpdatedData.data() + Component * ComponentSize, &UpdatedValue, sizeof(UpdatedValue));
        }
    }

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = Param.Type;
    Parameter.pDefaultValue = DefaultData.data();

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = "Parameter packing test";
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    static constexpr Uint32                     DestinationOffset = 4;
    const RadientMaterialShaderParameterPacking Packing{0, DestinationOffset};
    const RadientMaterialShaderDataLayoutDesc   ShaderDataLayout{DestinationOffset + Param.Size + 4, &Packing, 1};
    RefCntAutoPtr<IRadientMaterialDefinition>   pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, ShaderDataLayout, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    auto& DefinitionImpl = static_cast<RadientMaterialDefinitionImpl&>(*pDefinition);
    EXPECT_EQ(DefinitionImpl.GetShaderDataSize(), ShaderDataLayout.Size);

    std::vector<Uint8> ShaderData(ShaderDataLayout.Size, 0xcd);
    std::vector<Uint8> Expected(ShaderDataLayout.Size, 0);
    std::memcpy(Expected.data() + DestinationOffset, DefaultData.data(), DefaultData.size());
    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    EXPECT_EQ(ShaderData, Expected);

    RadientMaterialParameterHandle Handle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &Handle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(Handle, UpdatedData.data(), Param.Size), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    std::memcpy(Expected.data() + DestinationOffset, UpdatedData.data(), UpdatedData.size());
    DefinitionImpl.WriteShaderData(*pInstance, ShaderData.data());
    EXPECT_EQ(ShaderData, Expected);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterTypes,
    RadientMaterialShaderParameterPackingTest,
    testing::Values(
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_BOOL, static_cast<Uint32>(sizeof(Bool)), "Bool"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_INT, static_cast<Uint32>(sizeof(Int32)), "Int"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_INT2, static_cast<Uint32>(sizeof(Int32) * 2), "Int2"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_INT3, static_cast<Uint32>(sizeof(Int32) * 3), "Int3"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_INT4, static_cast<Uint32>(sizeof(Int32) * 4), "Int4"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_UINT, static_cast<Uint32>(sizeof(Uint32)), "Uint"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_UINT2, static_cast<Uint32>(sizeof(Uint32) * 2), "Uint2"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_UINT3, static_cast<Uint32>(sizeof(Uint32) * 3), "Uint3"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_UINT4, static_cast<Uint32>(sizeof(Uint32) * 4), "Uint4"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, static_cast<Uint32>(sizeof(Float32)), "Float"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2, static_cast<Uint32>(sizeof(Float32) * 2), "Float2"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, static_cast<Uint32>(sizeof(Float32) * 3), "Float3"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, static_cast<Uint32>(sizeof(Float32) * 4), "Float4"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2, static_cast<Uint32>(sizeof(Float32) * 4), "Float2x2"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3, static_cast<Uint32>(sizeof(Float32) * 9), "Float3x3"},
        ShaderParameterPackingTestParam{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4, static_cast<Uint32>(sizeof(Float32) * 16), "Float4x4"}),
    [](const testing::TestParamInfo<ShaderParameterPackingTestParam>& Info) {
        return Info.param.Name;
    });

TEST(RadientMaterialTest, EmptyShaderDataLayoutWritesNoData)
{
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(nullptr, 0);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    auto& DefinitionImpl = static_cast<RadientMaterialDefinitionImpl&>(*pDefinition);
    EXPECT_EQ(DefinitionImpl.GetShaderDataSize(), 0u);
    DefinitionImpl.WriteShaderData(*pInstance, nullptr);
}

TEST(RadientMaterialTest, RejectsMissingShaderDataMappings)
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    ExpectInvalidShaderDataLayout(DefinitionDesc, {4, nullptr, 1}, "mappings, but pMappings is null");
}

TEST(RadientMaterialTest, RejectsMissingShaderTexturePackings)
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    RadientMaterialShaderDataLayoutDesc  ShaderDataLayout{};
    ShaderDataLayout.Size                = static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
    ShaderDataLayout.TexturePackingCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout, "texture packings, but pTexturePackings is null");
}

TEST(RadientMaterialTest, RejectsInvalidShaderDataParameterIndex)
{
    RadientComputeMaterialDefinitionDesc        DefinitionDesc{};
    const RadientMaterialShaderParameterPacking Mapping{0, 0};
    ExpectInvalidShaderDataLayout(DefinitionDesc, {4, &Mapping, 1}, "references parameter index 0");
}

TEST(RadientMaterialTest, RejectsTextureShaderDataMapping)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "Texture";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    const RadientMaterialShaderParameterPacking Mapping{0, 0};
    ExpectInvalidShaderDataLayout(DefinitionDesc, {4, &Mapping, 1}, "references texture parameter 'Texture'");
}

TEST(RadientMaterialTest, RejectsIncompatibleShaderTexturePackingParameters)
{
    std::array<RadientMaterialParameterDesc, 4> Parameters{};
    Parameters[0].Name = "Texture";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].Name = "UVSelector";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[2].Name = "WrapU";
    Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[3].Name = "WrapV";
    Parameters[3].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    const RadientMaterialShaderTexturePacking TexturePacking{0, 1, 2, 3, 0};
    RadientMaterialShaderDataLayoutDesc       ShaderDataLayout{};
    ShaderDataLayout.Size                = static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
    ShaderDataLayout.pTexturePackings    = &TexturePacking;
    ShaderDataLayout.TexturePackingCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "UV selector parameter 'UVSelector' has an incompatible type or array size");
}

TEST(RadientMaterialTest, RejectsMissingShaderDataInitializations)
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    RadientMaterialShaderDataLayoutDesc  ShaderDataLayout{};
    ShaderDataLayout.Size                = 4;
    ShaderDataLayout.InitializationCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "initializations, but pInitializations is null");
}

TEST(RadientMaterialTest, RejectsNullShaderDataInitialization)
{
    const RadientMaterialShaderDataInitialization Initialization{nullptr, 4, 0};

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    RadientMaterialShaderDataLayoutDesc  ShaderDataLayout{};
    ShaderDataLayout.Size                = 4;
    ShaderDataLayout.pInitializations    = &Initialization;
    ShaderDataLayout.InitializationCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "initialization 0 has null data");
}

TEST(RadientMaterialTest, RejectsZeroSizeShaderDataInitialization)
{
    const Uint32                                  Value = 1;
    const RadientMaterialShaderDataInitialization Initialization{&Value, 0, 0};

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    RadientMaterialShaderDataLayoutDesc  ShaderDataLayout{};
    ShaderDataLayout.Size                = 4;
    ShaderDataLayout.pInitializations    = &Initialization;
    ShaderDataLayout.InitializationCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "initialization 0 has zero size");
}

TEST(RadientMaterialTest, RejectsOutOfBoundsShaderDataInitialization)
{
    const Uint32                                  Value = 1;
    const RadientMaterialShaderDataInitialization Initialization{&Value, 4, 2};

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    RadientMaterialShaderDataLayoutDesc  ShaderDataLayout{};
    ShaderDataLayout.Size                = 4;
    ShaderDataLayout.pInitializations    = &Initialization;
    ShaderDataLayout.InitializationCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "exceeds the shader data size 4");
}

TEST(RadientMaterialTest, RejectsOutOfBoundsShaderTexturePacking)
{
    std::array<RadientMaterialParameterDesc, 4> Parameters{};
    Parameters[0].Name = "Texture";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].Name = "UVSelector";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_INT;
    Parameters[2].Name = "WrapU";
    Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[3].Name = "WrapV";
    Parameters[3].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    const RadientMaterialShaderTexturePacking TexturePacking{0, 1, 2, 3, 4};
    RadientMaterialShaderDataLayoutDesc       ShaderDataLayout{};
    ShaderDataLayout.Size                = static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
    ShaderDataLayout.pTexturePackings    = &TexturePacking;
    ShaderDataLayout.TexturePackingCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "exceeds the shader data size");
}

TEST(RadientMaterialTest, RejectsOutOfBoundsShaderDataMapping)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "Value";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    const RadientMaterialShaderParameterPacking Mapping{0, 4};
    ExpectInvalidShaderDataLayout(DefinitionDesc, {16, &Mapping, 1}, "exceeds the shader data size 16");
}

TEST(RadientMaterialTest, RejectsOverlappingShaderDataMappings)
{
    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name = "First";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameters[1].Name = "Second";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    const std::array<RadientMaterialShaderParameterPacking, 2> Mappings{{
        {0, 0},
        {1, 12},
    }};
    ExpectInvalidShaderDataLayout(DefinitionDesc, {20, Mappings.data(), static_cast<Uint32>(Mappings.size())},
                                  "parameter 'First' and parameter 'Second' overlap");
}

TEST(RadientMaterialTest, RejectsShaderParameterOverlappingTextureAtlasData)
{
    std::array<RadientMaterialParameterDesc, 5> Parameters{};
    Parameters[0].Name = "Texture";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].Name = "UVSelector";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_INT;
    Parameters[2].Name = "WrapU";
    Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[3].Name = "WrapV";
    Parameters[3].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[4].Name = "Value";
    Parameters[4].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    const RadientMaterialShaderParameterPacking Mapping{
        4,
        static_cast<Uint32>(offsetof(GLTF::Material::TextureShaderAttribs, AtlasUVScaleAndBias))};
    const RadientMaterialShaderTexturePacking TexturePacking{0, 1, 2, 3, 0};

    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size                = static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
    ShaderDataLayout.pMappings           = &Mapping;
    ShaderDataLayout.MappingCount        = 1;
    ShaderDataLayout.pTexturePackings    = &TexturePacking;
    ShaderDataLayout.TexturePackingCount = 1;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "parameter 'Value' and texture parameter 'Texture' atlas data overlap");
}

TEST(RadientMaterialTest, RejectsOverlappingShaderTexturePackings)
{
    std::array<RadientMaterialParameterDesc, 5> Parameters{};
    Parameters[0].Name = "TextureA";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].Name = "TextureB";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[2].Name = "UVSelector";
    Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_INT;
    Parameters[3].Name = "WrapU";
    Parameters[3].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;
    Parameters[4].Name = "WrapV";
    Parameters[4].Type = RADIENT_MATERIAL_PARAMETER_TYPE_UINT;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    const std::array<RadientMaterialShaderTexturePacking, 2> TexturePackings{{
        {0, 2, 3, 4, 0},
        {1, 2, 3, 4, 0},
    }};

    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size                = static_cast<Uint32>(sizeof(GLTF::Material::TextureShaderAttribs));
    ShaderDataLayout.pTexturePackings    = TexturePackings.data();
    ShaderDataLayout.TexturePackingCount = static_cast<Uint32>(TexturePackings.size());
    ExpectInvalidShaderDataLayout(
        DefinitionDesc,
        ShaderDataLayout,
        "texture parameter 'TextureA' packed properties and texture parameter 'TextureB' packed properties overlap");
}

TEST(RadientMaterialTest, AssetManagerReportsDefinitionStatus)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientComputeMaterialDefinitionDesc      DefinitionDesc{};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pDefinition), RADIENT_STATUS_OK);
}

TEST(RadientMaterialTest, EmptyDefinitionSupportsInstanceLifecycle)
{
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(nullptr, 0);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pDefinition->GetParameterCount(), 0u);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_GT(pInstance->GetVersion(), 0u);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);

    RefCntAutoPtr<IRadientMaterialInstance> pClone;
    ASSERT_EQ(pInstance->Clone(pClone.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClone, nullptr);
    EXPECT_EQ(pClone->GetDefinition(), pDefinition);
    EXPECT_GT(pClone->GetVersion(), 0u);

    pWriter.Release();
    ASSERT_EQ(pClone->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientMaterialTest, FindsParametersByNameRegardlessOfDeclarationOrder)
{
    std::array<RadientMaterialParameterDesc, 3> Parameters{};
    Parameters[0].Name = "Zeta";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1].Name = "Alpha";
    Parameters[1].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[2].Name = "Middle";
    Parameters[2].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    for (Uint32 Index = 0; Index < static_cast<Uint32>(Parameters.size()); ++Index)
    {
        RadientMaterialParameterHandle Handle;
        EXPECT_EQ(pDefinition->FindParameter(Parameters[Index].Name, &Handle), RADIENT_STATUS_OK);
        EXPECT_EQ(Handle.Index, Index);
    }

    RadientMaterialParameterHandle MissingHandle;
    EXPECT_EQ(pDefinition->FindParameter("Missing", &MissingHandle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_FALSE(MissingHandle);
}

TEST(RadientMaterialTest, WriterUpdatesSharedInstance)
{
    const RadientFloat4                 DefaultColor{1.f, 1.f, 1.f, 1.f};
    RefCntAutoPtr<IRadientTextureAsset> pDefaultTexture = MakeTestTextureAsset("texture://default");
    RefCntAutoPtr<IRadientTextureAsset> pUpdatedTexture = MakeTestTextureAsset("texture://updated");

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name            = "BaseColor";
    Parameters[0].Type            = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameters[0].pDefaultValue   = &DefaultColor;
    Parameters[1].Name            = "BaseColorTexture";
    Parameters[1].Type            = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].pDefaultTexture = pDefaultTexture;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ColorHandle;
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->FindParameter("BaseColor", &ColorHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->FindParameter("BaseColorTexture", &TextureHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_EQ(pInstance->GetDefinition(), pDefinition);

    RefCntAutoPtr<IRadientMaterialInstance> pSharedInstance = pInstance;
    const Uint64                            InitialVersion  = pInstance->GetVersion();

    const RadientFloat4 InitialColor = GetParameter<RadientFloat4>(*pInstance, ColorHandle);
    EXPECT_FLOAT_EQ(InitialColor.x, 1.f);
    EXPECT_FLOAT_EQ(InitialColor.y, 1.f);
    EXPECT_FLOAT_EQ(InitialColor.z, 1.f);
    EXPECT_FLOAT_EQ(InitialColor.w, 1.f);

    RefCntAutoPtr<IRadientTextureAsset> pDefaultInstanceTexture;
    ASSERT_EQ(pInstance->GetTexture(TextureHandle, 0, pDefaultInstanceTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pDefaultInstanceTexture, pDefaultTexture);

    const RadientFloat4                           UpdatedColor{0.25f, 0.5f, 0.75f, 1.f};
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ColorHandle, UpdatedColor), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pUpdatedTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_GT(pInstance->GetVersion(), InitialVersion);
    EXPECT_EQ(pSharedInstance->GetVersion(), pInstance->GetVersion());

    const RadientFloat4 CommittedColor = GetParameter<RadientFloat4>(*pSharedInstance, ColorHandle);
    EXPECT_FLOAT_EQ(CommittedColor.x, UpdatedColor.x);
    EXPECT_FLOAT_EQ(CommittedColor.y, UpdatedColor.y);
    EXPECT_FLOAT_EQ(CommittedColor.z, UpdatedColor.z);
    EXPECT_FLOAT_EQ(CommittedColor.w, UpdatedColor.w);
    RefCntAutoPtr<IRadientTextureAsset> pUpdatedInstanceTexture;
    ASSERT_EQ(pSharedInstance->GetTexture(TextureHandle, 0, pUpdatedInstanceTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pUpdatedInstanceTexture, pUpdatedTexture);

    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);

    const RadientFloat4 SecondColor{0.75f, 0.5f, 0.25f, 1.f};
    ASSERT_EQ(pWriter->SetParameter(ColorHandle, SecondColor), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 2);

    const RadientFloat4 SecondCommittedColor = GetParameter<RadientFloat4>(*pSharedInstance, ColorHandle);
    EXPECT_FLOAT_EQ(SecondCommittedColor.x, SecondColor.x);
    EXPECT_FLOAT_EQ(SecondCommittedColor.y, SecondColor.y);
    EXPECT_FLOAT_EQ(SecondCommittedColor.z, SecondColor.z);
    EXPECT_FLOAT_EQ(SecondCommittedColor.w, SecondColor.w);
}

TEST(RadientMaterialTest, WriterSetParameterInfersValueSize)
{
    static constexpr Uint32 ParameterCount = 4;

    std::array<RadientMaterialParameterDesc, ParameterCount> Parameters{};
    Parameters[0].Name      = "Enabled";
    Parameters[0].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_BOOL;
    Parameters[1].Name      = "Direction";
    Parameters[1].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3;
    Parameters[2].Name      = "Transform";
    Parameters[2].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2;
    Parameters[3].Name      = "Weights";
    Parameters[3].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[3].ArraySize = 3;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    std::array<RadientMaterialParameterHandle, ParameterCount> Handles{};
    for (Uint32 Index = 0; Index < static_cast<Uint32>(Handles.size()); ++Index)
        ASSERT_EQ(pDefinition->GetParameterHandle(Index, &Handles[Index]), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const Bool                   UpdatedEnabled = True;
    const RadientFloat3          UpdatedDirection{1.f, 2.f, 3.f};
    const Float32                UpdatedTransform[4]{1.f, 2.f, 3.f, 4.f};
    const std::array<Float32, 3> UpdatedWeights{0.25f, 0.5f, 0.75f};

    EXPECT_EQ(pWriter->SetParameter(Handles[0], UpdatedEnabled), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetParameter(Handles[1], UpdatedDirection), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetParameter(Handles[2], UpdatedTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetParameter(Handles[3], UpdatedWeights), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ(GetParameter<Bool>(*pInstance, Handles[0]), UpdatedEnabled);
    EXPECT_EQ(GetParameter<RadientFloat3>(*pInstance, Handles[1]), UpdatedDirection);

    const std::array<Float32, 4> StoredTransform =
        GetParameter<std::array<Float32, 4>>(*pInstance, Handles[2]);
    EXPECT_TRUE(std::equal(StoredTransform.begin(), StoredTransform.end(), UpdatedTransform));
    EXPECT_EQ((GetParameter<std::array<Float32, 3>>(*pInstance, Handles[3])), UpdatedWeights);
}

TEST(RadientMaterialTest, InstanceSupportsValueAndTextureArrays)
{
    const std::array<Float32, 3> DefaultValues{0.25f, 0.5f, 0.75f};
    const std::array<Float32, 3> UpdatedValues{1.f, 2.f, 3.f};

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Values";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].ArraySize     = static_cast<Uint32>(DefaultValues.size());
    Parameters[0].pDefaultValue = DefaultValues.data();
    Parameters[1].Name          = "Textures";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize     = 2;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ValuesHandle;
    RadientMaterialParameterHandle TexturesHandle;
    ASSERT_EQ(pDefinition->FindParameter("Values", &ValuesHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->FindParameter("Textures", &TexturesHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const std::array<Float32, 3> InitialValues =
        GetParameter<std::array<Float32, 3>>(*pInstance, ValuesHandle);
    EXPECT_EQ(InitialValues, DefaultValues);

    for (Uint32 Index = 0; Index < Parameters[1].ArraySize; ++Index)
    {
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        EXPECT_EQ(pInstance->GetTexture(TexturesHandle, Index, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, nullptr);
    }

    RefCntAutoPtr<IRadientTextureAsset>           pTexture0 = MakeTestTextureAsset("texture://array-0");
    RefCntAutoPtr<IRadientTextureAsset>           pTexture1 = MakeTestTextureAsset("texture://array-1");
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pFirstWriter;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pSecondWriter;
    ASSERT_EQ(pInstance->CreateWriter(pFirstWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->CreateWriter(pSecondWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->SetParameter(ValuesHandle, UpdatedValues), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->SetTexture(TexturesHandle, 0, pTexture0), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->SetTexture(TexturesHandle, 1, pTexture1), RADIENT_STATUS_OK);

    // Texture elements are independent changes even when both writers prepare
    // their updates before either commit.
    ASSERT_EQ(pFirstWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->Commit(), RADIENT_STATUS_OK);

    const std::array<Float32, 3> StoredValues =
        GetParameter<std::array<Float32, 3>>(*pInstance, ValuesHandle);
    EXPECT_EQ(StoredValues, UpdatedValues);
    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture0;
    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture1;
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 0, pStoredTexture0.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 1, pStoredTexture1.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture0, pTexture0);
    EXPECT_EQ(pStoredTexture1, pTexture1);
}

TEST(RadientMaterialTest, SupportsEveryValueParameterType)
{
    struct TypeInfo
    {
        RADIENT_MATERIAL_PARAMETER_TYPE Type;
        Uint32                          ElementSize;
    };

    static constexpr std::array TypeInfos{
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_BOOL, sizeof(Bool)},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_INT, sizeof(Int32)},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_INT2, sizeof(Int32) * 2},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_INT3, sizeof(Int32) * 3},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_INT4, sizeof(Int32) * 4},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_UINT, sizeof(Uint32)},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_UINT2, sizeof(Uint32) * 2},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_UINT3, sizeof(Uint32) * 3},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_UINT4, sizeof(Uint32) * 4},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, sizeof(Float32)},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2, sizeof(Float32) * 2},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, sizeof(Float32) * 3},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, sizeof(Float32) * 4},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2, sizeof(Float32) * 4},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3, sizeof(Float32) * 9},
        TypeInfo{RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4, sizeof(Float32) * 16},
    };

    static constexpr Uint32 ArraySize   = 2;
    static constexpr Uint32 MaxDataSize = sizeof(Float32) * 16 * ArraySize;

    std::array<std::string, TypeInfos.size()>                    Names;
    std::array<RadientMaterialParameterDesc, TypeInfos.size()>   Parameters{};
    std::array<std::array<Uint8, MaxDataSize>, TypeInfos.size()> DefaultData{};
    std::array<std::array<Uint8, MaxDataSize>, TypeInfos.size()> UpdatedData{};
    for (size_t TypeIndex = 0; TypeIndex < TypeInfos.size(); ++TypeIndex)
    {
        const Uint32 DataSize = TypeInfos[TypeIndex].ElementSize * ArraySize;
        Names[TypeIndex]      = "Type" + std::to_string(TypeIndex);
        for (Uint32 Byte = 0; Byte < DataSize; ++Byte)
        {
            DefaultData[TypeIndex][Byte] = static_cast<Uint8>((TypeIndex * 17 + Byte + 1) & 0xFFu);
            UpdatedData[TypeIndex][Byte] = DefaultData[TypeIndex][Byte] ^ Uint8 { 0xA5u };
        }

        Parameters[TypeIndex].Name          = Names[TypeIndex].c_str();
        Parameters[TypeIndex].Type          = TypeInfos[TypeIndex].Type;
        Parameters[TypeIndex].ArraySize     = ArraySize;
        Parameters[TypeIndex].pDefaultValue = DefaultData[TypeIndex].data();
    }

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    std::array<Uint8, MaxDataSize + 1> ReadData{};
    for (Uint32 TypeIndex = 0; TypeIndex < static_cast<Uint32>(TypeInfos.size()); ++TypeIndex)
    {
        const Uint32                        DataSize   = TypeInfos[TypeIndex].ElementSize * ArraySize;
        const RadientMaterialParameterDesc& StoredDesc = pDefinition->GetParameterDesc(TypeIndex);
        EXPECT_EQ(StoredDesc.Type, TypeInfos[TypeIndex].Type);
        EXPECT_EQ(StoredDesc.ArraySize, ArraySize);
        ASSERT_NE(StoredDesc.pDefaultValue, nullptr);
        EXPECT_EQ(std::memcmp(StoredDesc.pDefaultValue, DefaultData[TypeIndex].data(), DataSize), 0);

        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(TypeIndex, &Handle), RADIENT_STATUS_OK);
        ASSERT_EQ(pInstance->GetParameter(Handle, ReadData.data(), DataSize), RADIENT_STATUS_OK);
        EXPECT_EQ(std::memcmp(ReadData.data(), DefaultData[TypeIndex].data(), DataSize), 0);
        EXPECT_EQ(pInstance->GetParameter(Handle, ReadData.data(), DataSize - 1), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pInstance->GetParameter(Handle, ReadData.data(), DataSize + 1), RADIENT_STATUS_INVALID_ARGUMENT);

        EXPECT_EQ(pWriter->SetParameter(Handle, UpdatedData[TypeIndex].data(), DataSize - 1), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pWriter->SetParameter(Handle, UpdatedData[TypeIndex].data(), DataSize + 1), RADIENT_STATUS_INVALID_ARGUMENT);
        ASSERT_EQ(pWriter->SetParameter(Handle, UpdatedData[TypeIndex].data(), DataSize), RADIENT_STATUS_OK);
    }

    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    for (Uint32 TypeIndex = 0; TypeIndex < static_cast<Uint32>(TypeInfos.size()); ++TypeIndex)
    {
        const Uint32                   DataSize = TypeInfos[TypeIndex].ElementSize * ArraySize;
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(TypeIndex, &Handle), RADIENT_STATUS_OK);
        ASSERT_EQ(pInstance->GetParameter(Handle, ReadData.data(), DataSize), RADIENT_STATUS_OK);
        EXPECT_EQ(std::memcmp(ReadData.data(), UpdatedData[TypeIndex].data(), DataSize), 0);
    }
}

TEST(RadientMaterialTest, NullValueDefaultIsZeroInitialized)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name      = "Matrices";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3;
    Parameter.ArraySize = 2;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle Handle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &Handle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    std::array<Uint8, sizeof(Float32) * 9 * 2> Data;
    Data.fill(0xFFu);
    ASSERT_EQ(pInstance->GetParameter(Handle, Data.data(), static_cast<Uint32>(Data.size())), RADIENT_STATUS_OK);
    EXPECT_TRUE(std::all_of(Data.begin(), Data.end(), [](Uint8 Byte) { return Byte == 0; }));
}

TEST(RadientMaterialTest, UnchangedWriterDoesNotAdvanceVersion)
{
    const Float32 DefaultRoughness = 0.5f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Roughness";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultValue = &DefaultRoughness;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion);

    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion);
}

TEST(RadientMaterialTest, WriterReversionDoesNotAdvanceVersion)
{
    const Float32 DefaultValue = 0.f;
    const Float32 UpdatedValue = 1.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Value";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Texture";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ValueHandle;
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ValueHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &TextureHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    EXPECT_EQ(pWriter->SetParameter(ValueHandle, DefaultValue), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);

    EXPECT_EQ(pWriter->SetParameter(ValueHandle, UpdatedValue), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetParameter(ValueHandle, DefaultValue), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);

    EXPECT_EQ(pWriter->SetTexture(TextureHandle, 0, nullptr), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);

    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://writer-reversion");
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, nullptr), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion);

    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    const Uint64 TextureVersion = pInstance->GetVersion();

    EXPECT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pInstance->GetVersion(), TextureVersion);
}

TEST(RadientMaterialTest, WriterOverwritesPendingParameterValues)
{
    const Float32 DefaultValue = 0.f;
    const Float32 FirstValue   = 1.f;
    const Float32 SecondValue  = 2.f;
    const Float32 FinalValue   = 3.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Value";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Textures";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize     = 2;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ValueHandle;
    RadientMaterialParameterHandle TexturesHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ValueHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &TexturesHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientTextureAsset> pTextureA = MakeTestTextureAsset("texture://writer-overwritten");
    RefCntAutoPtr<IRadientTextureAsset> pTextureB = MakeTestTextureAsset("texture://writer-final-0");
    RefCntAutoPtr<IRadientTextureAsset> pTextureC = MakeTestTextureAsset("texture://writer-final-1");
    RefCntWeakPtr<IRadientTextureAsset> WeakTextureA{pTextureA};

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValueHandle, FirstValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValueHandle, SecondValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValueHandle, FinalValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValueHandle, FinalValue), RADIENT_STATUS_NO_CHANGE);

    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 0, pTextureA), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 0, pTextureB), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 1, pTextureC), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 1, pTextureC), RADIENT_STATUS_NO_CHANGE);
    pTextureA.Release();
    EXPECT_EQ(WeakTextureA.Lock(), nullptr);

    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 1);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, ValueHandle), FinalValue);

    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture;
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pTextureB);
    pStoredTexture.Release();
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 1, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pTextureC);
}

TEST(RadientMaterialTest, WritersResolveOverlappingChangesAtCommit)
{
    const Float32 DefaultValue = 0.f;
    const Float32 FirstValue   = 1.f;
    const Float32 SecondValue  = 2.f;
    const Float32 ThirdValue   = 3.f;
    const Float32 FourthValue  = 4.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultValue = &DefaultValue;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle Handle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &Handle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriterA;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriterB;
    ASSERT_EQ(pInstance->CreateWriter(pWriterA.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->CreateWriter(pWriterB.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterA->SetParameter(Handle, FirstValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterB->SetParameter(Handle, FirstValue), RADIENT_STATUS_OK);

    EXPECT_EQ(pWriterA->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriterB->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 1);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriterC;
    ASSERT_EQ(pInstance->CreateWriter(pWriterC.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterC->SetParameter(Handle, SecondValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterC->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ(pWriterB->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, Handle), SecondValue);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriterD;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriterE;
    ASSERT_EQ(pInstance->CreateWriter(pWriterD.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->CreateWriter(pWriterE.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterD->SetParameter(Handle, ThirdValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriterE->SetParameter(Handle, FourthValue), RADIENT_STATUS_OK);

    EXPECT_EQ(pWriterD->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriterE->Commit(), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, Handle), FourthValue);
}

TEST(RadientMaterialTest, WritersResolveOverlappingTextureChangesAtCommit)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "Texture";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle Handle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &Handle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pFirstTexture  = MakeTestTextureAsset("texture://writer-first");
    RefCntAutoPtr<IRadientTextureAsset> pSecondTexture = MakeTestTextureAsset("texture://writer-second");

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pFirstWriter;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pSecondWriter;
    ASSERT_EQ(pInstance->CreateWriter(pFirstWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->CreateWriter(pSecondWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->SetTexture(Handle, 0, pFirstTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->SetTexture(Handle, 0, pSecondTexture), RADIENT_STATUS_OK);

    ASSERT_EQ(pFirstWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->Commit(), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture;
    ASSERT_EQ(pInstance->GetTexture(Handle, 0, pStoredTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pSecondTexture);
}

TEST(RadientMaterialTest, WriterUpdatesSparseParameters)
{
    static constexpr Uint32 ParameterCount = 66;

    const Float32                                            DefaultValue = 0.f;
    std::array<std::string, ParameterCount>                  Names;
    std::array<RadientMaterialParameterDesc, ParameterCount> Parameters{};
    for (Uint32 Index = 0; Index < ParameterCount; ++Index)
    {
        Names[Index]                    = "Value" + std::to_string(Index);
        Parameters[Index].Name          = Names[Index].c_str();
        Parameters[Index].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
        Parameters[Index].pDefaultValue = &DefaultValue;
    }

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), ParameterCount);
    ASSERT_NE(pDefinition, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Uint32, 4> ChangedIndices{0, 63, 64, 65};
    const Float32               UpdatedValue = 1.f;
    for (Uint32 Index : ChangedIndices)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(Index, &Handle), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetParameter(Handle, UpdatedValue), RADIENT_STATUS_OK);
    }

    RadientMaterialParameterHandle RevertedHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(ChangedIndices.back(), &RevertedHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(RevertedHandle, DefaultValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    for (Uint32 Index : ChangedIndices)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(Index, &Handle), RADIENT_STATUS_OK);
        EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, Handle),
                        Index == ChangedIndices.back() ? DefaultValue : UpdatedValue);
    }
}

TEST(RadientMaterialTest, DefinitionSpecificHandlesAreRejected)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultValue = &DefaultValue;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition0 = CreateDefinition(&Parameter, 1, "Definition 0");
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition1 = CreateDefinition(&Parameter, 1, "Definition 1");
    ASSERT_NE(pDefinition0, nullptr);
    ASSERT_NE(pDefinition1, nullptr);

    RadientMaterialParameterHandle Handle0;
    RadientMaterialParameterHandle Handle1;
    ASSERT_EQ(pDefinition0->GetParameterHandle(0, &Handle0), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition1->GetParameterHandle(0, &Handle1), RADIENT_STATUS_OK);
    EXPECT_NE(Handle0, Handle1);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition0->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetParameter(Handle1, &DefaultValue, static_cast<Uint32>(sizeof(DefaultValue))),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMaterialTest, RejectsNonZeroReservedHandles)
{
    const Float32 DefaultValue = 1.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Value";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Texture";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ValueHandle;
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ValueHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &TextureHandle), RADIENT_STATUS_OK);
    EXPECT_EQ(ValueHandle.Reserved, 0u);
    EXPECT_EQ(TextureHandle.Reserved, 0u);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    ValueHandle.Reserved   = 1;
    TextureHandle.Reserved = 1;

    Float32 Value = 0.f;
    EXPECT_EQ(pInstance->GetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pInstance->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pTexture, nullptr);

    EXPECT_EQ(pWriter->SetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetTexture(TextureHandle, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMaterialTest, PublicAPIRejectsInvalidArgumentsAndClearsOutputs)
{
    const Float32 DefaultValue = 1.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Value";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Textures";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize     = 2;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()), "Definition 0");
    RefCntAutoPtr<IRadientMaterialDefinition> pForeignDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()), "Definition 1");
    ASSERT_NE(pDefinition, nullptr);
    ASSERT_NE(pForeignDefinition, nullptr);

    RadientMaterialParameterHandle ValueHandle;
    RadientMaterialParameterHandle TextureHandle;
    RadientMaterialParameterHandle ForeignValueHandle;
    RadientMaterialParameterHandle ForeignTextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ValueHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &TextureHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pForeignDefinition->GetParameterHandle(0, &ForeignValueHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pForeignDefinition->GetParameterHandle(1, &ForeignTextureHandle), RADIENT_STATUS_OK);

    EXPECT_EQ(pDefinition->GetParameterHandle(0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pDefinition->FindParameter("Value", nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMaterialParameterHandle ResetHandle = ValueHandle;
    EXPECT_EQ(pDefinition->GetParameterHandle(2, &ResetHandle), RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectInvalidHandle(ResetHandle);

    ResetHandle = ValueHandle;
    EXPECT_EQ(pDefinition->FindParameter(nullptr, &ResetHandle), RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectInvalidHandle(ResetHandle);

    ResetHandle = ValueHandle;
    EXPECT_EQ(pDefinition->FindParameter("value", &ResetHandle), RADIENT_STATUS_NOT_FOUND);
    ExpectInvalidHandle(ResetHandle);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_EQ(pDefinition->CreateInstance(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->CreateWriter(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->Clone(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMaterialParameterHandle OutOfRangeValueHandle   = ValueHandle;
    OutOfRangeValueHandle.Index                            = 2;
    RadientMaterialParameterHandle OutOfRangeTextureHandle = TextureHandle;
    OutOfRangeTextureHandle.Index                          = 2;

    Float32 Value = 0.f;
    EXPECT_EQ(pInstance->GetParameter({}, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->GetParameter(ForeignValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->GetParameter(OutOfRangeValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->GetParameter(TextureHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pInstance->GetParameter(ValueHandle, nullptr, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->GetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value) - 1)), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInstance->GetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value) + 1)), RADIENT_STATUS_INVALID_ARGUMENT);

    EXPECT_EQ(pInstance->GetTexture(TextureHandle, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<IRadientTextureAsset> pSentinelTexture = MakeTestTextureAsset("texture://sentinel");
    ASSERT_NE(pSentinelTexture, nullptr);
    const auto ExpectGetTextureFailure = [&](RadientMaterialParameterHandle Handle,
                                             Uint32                         ArrayIndex,
                                             RADIENT_STATUS                 ExpectedStatus) {
        IRadientTextureAsset* pOutput = pSentinelTexture;
        EXPECT_EQ(pInstance->GetTexture(Handle, ArrayIndex, &pOutput), ExpectedStatus);
        EXPECT_EQ(pOutput, nullptr);
    };
    ExpectGetTextureFailure({}, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectGetTextureFailure(ForeignTextureHandle, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectGetTextureFailure(OutOfRangeTextureHandle, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectGetTextureFailure(ValueHandle, 0, RADIENT_STATUS_INVALID_OPERATION);
    ExpectGetTextureFailure(TextureHandle, 2, RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    EXPECT_EQ(pWriter->SetParameter({}, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(ForeignValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(OutOfRangeValueHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(TextureHandle, &Value, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(ValueHandle, nullptr, static_cast<Uint32>(sizeof(Value))), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value) - 1)), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetParameter(ValueHandle, &Value, static_cast<Uint32>(sizeof(Value) + 1)), RADIENT_STATUS_INVALID_ARGUMENT);

    EXPECT_EQ(pWriter->SetTexture({}, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetTexture(ForeignTextureHandle, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetTexture(OutOfRangeTextureHandle, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetTexture(ValueHandle, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWriter->SetTexture(TextureHandle, 2, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    EXPECT_EQ(CreateDefinition(pDefinition->GetDesc(), nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientComputeMaterialDefinitionDesc InvalidDefinitionDesc{};
    InvalidDefinitionDesc.Type                    = RADIENT_MATERIAL_DEFINITION_TYPE_COUNT;
    IRadientMaterialDefinition* pDefinitionOutput = pDefinition;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Invalid material definition type"};
        EXPECT_EQ(CreateDefinition(InvalidDefinitionDesc, &pDefinitionOutput), RADIENT_STATUS_INVALID_ARGUMENT);
    }
    EXPECT_EQ(pDefinitionOutput, nullptr);
}

TEST(RadientMaterialTest, CloneCopiesCommittedStateAndRemainsIndependent)
{
    const std::array<Float32, 2> DefaultValues{1.f, 2.f};
    const std::array<Float32, 2> ClonedValues{3.f, 4.f};
    const std::array<Float32, 2> SourceValues{5.f, 6.f};
    const std::array<Float32, 2> UpdatedCloneValues{7.f, 8.f};

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "Values";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].ArraySize     = static_cast<Uint32>(DefaultValues.size());
    Parameters[0].pDefaultValue = DefaultValues.data();
    Parameters[1].Name          = "Textures";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize     = 2;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle ValuesHandle;
    RadientMaterialParameterHandle TexturesHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &ValuesHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &TexturesHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pTextureA = MakeTestTextureAsset("texture://clone-a");
    RefCntAutoPtr<IRadientTextureAsset> pTextureB = MakeTestTextureAsset("texture://clone-b");
    RefCntAutoPtr<IRadientTextureAsset> pTextureC = MakeTestTextureAsset("texture://clone-c");
    RefCntAutoPtr<IRadientTextureAsset> pTextureD = MakeTestTextureAsset("texture://clone-d");

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValuesHandle, ClonedValues), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 0, pTextureA), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 1, pTextureB), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    pWriter.Release();

    RefCntAutoPtr<IRadientMaterialInstance> pClone;
    ASSERT_EQ(pInstance->Clone(pClone.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClone, pInstance);
    EXPECT_EQ(pClone->GetDefinition(), pDefinition);
    EXPECT_EQ((GetParameter<std::array<Float32, 2>>(*pClone, ValuesHandle)), ClonedValues);

    const auto GetTexture = [&](IRadientMaterialInstance& Instance, Uint32 Index) {
        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        EXPECT_EQ(Instance.GetTexture(TexturesHandle, Index, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        return pTexture;
    };
    EXPECT_EQ(GetTexture(*pClone, 0), pTextureA);
    EXPECT_EQ(GetTexture(*pClone, 1), pTextureB);

    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValuesHandle, SourceValues), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 0, pTextureC), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    pWriter.Release();

    EXPECT_EQ((GetParameter<std::array<Float32, 2>>(*pInstance, ValuesHandle)), SourceValues);
    EXPECT_EQ(GetTexture(*pInstance, 0), pTextureC);
    EXPECT_EQ(GetTexture(*pInstance, 1), pTextureB);
    EXPECT_EQ((GetParameter<std::array<Float32, 2>>(*pClone, ValuesHandle)), ClonedValues);
    EXPECT_EQ(GetTexture(*pClone, 0), pTextureA);
    EXPECT_EQ(GetTexture(*pClone, 1), pTextureB);

    ASSERT_EQ(pClone->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(ValuesHandle, UpdatedCloneValues), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TexturesHandle, 1, pTextureD), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ((GetParameter<std::array<Float32, 2>>(*pInstance, ValuesHandle)), SourceValues);
    EXPECT_EQ(GetTexture(*pInstance, 0), pTextureC);
    EXPECT_EQ(GetTexture(*pInstance, 1), pTextureB);
    EXPECT_EQ((GetParameter<std::array<Float32, 2>>(*pClone, ValuesHandle)), UpdatedCloneValues);
    EXPECT_EQ(GetTexture(*pClone, 0), pTextureA);
    EXPECT_EQ(GetTexture(*pClone, 1), pTextureD);
}

TEST(RadientMaterialTest, WritersApplyIndependentChanges)
{
    const Float32 DefaultValue = 0.f;
    const Float32 FirstValue   = 1.f;
    const Float32 SecondValue  = 2.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "First";
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Second";
    Parameters[1].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1].pDefaultValue = &DefaultValue;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
        CreateDefinition(Parameters.data(), static_cast<Uint32>(Parameters.size()));
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle FirstHandle;
    RadientMaterialParameterHandle SecondHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &FirstHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterHandle(1, &SecondHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pFirstWriter;
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pSecondWriter;
    ASSERT_EQ(pInstance->CreateWriter(pFirstWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->CreateWriter(pSecondWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->SetParameter(FirstHandle, FirstValue), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->SetParameter(SecondHandle, SecondValue), RADIENT_STATUS_OK);

    ASSERT_EQ(pFirstWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 2);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, FirstHandle), FirstValue);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, SecondHandle), SecondValue);
}

TEST(RadientMaterialTest, DefinitionRetainsDefaultTexture)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://definition-default");
    RefCntWeakPtr<IRadientTextureAsset> WeakTexture{pTexture};

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "Texture";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.pDefaultTexture = pTexture;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    pTexture.Release();
    EXPECT_NE(WeakTexture.Lock(), nullptr);

    pDefinition.Release();
    EXPECT_EQ(WeakTexture.Lock(), nullptr);
}

TEST(RadientMaterialTest, InstanceRetainsAssignedTexture)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "Texture";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &TextureHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://instance-assigned");
    RefCntWeakPtr<IRadientTextureAsset> WeakTexture{pTexture};

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    pTexture.Release();
    pWriter.Release();
    EXPECT_NE(WeakTexture.Lock(), nullptr);

    pInstance.Release();
    EXPECT_EQ(WeakTexture.Lock(), nullptr);
}

TEST(RadientMaterialTest, WriterRetainsUncommittedTexture)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "Texture";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &TextureHandle), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://writer-uncommitted");
    RefCntWeakPtr<IRadientTextureAsset> WeakTexture{pTexture};

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pInstance->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_OK);

    pTexture.Release();
    EXPECT_NE(WeakTexture.Lock(), nullptr);

    pWriter.Release();
    EXPECT_EQ(WeakTexture.Lock(), nullptr);
}

TEST(RadientMaterialTest, RejectsInvalidDefinitionType)
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Type = RADIENT_MATERIAL_DEFINITION_TYPE_COUNT;
    ExpectInvalidDefinition(DefinitionDesc, "Invalid material definition type");
}

TEST(RadientMaterialTest, RejectsInvalidSurfaceShadingModel)
{
    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_COUNT;
    ExpectInvalidDefinition(DefinitionDesc, "Invalid surface shading model");
}

TEST(RadientMaterialTest, RejectsUnsupportedSurfaceFeatures)
{
    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Features = static_cast<RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS>(
        static_cast<Uint32>(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL) + 1u);
    ExpectInvalidDefinition(DefinitionDesc, "Surface material feature flags contain unsupported bits");
}

TEST(RadientMaterialTest, RejectsFeaturesForUnlitSurface)
{
    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
    DefinitionDesc.Features     = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    ExpectInvalidDefinition(DefinitionDesc,
                            "Unlit surface materials do not support optional material features");
}

TEST(RadientMaterialTest, RejectsVolumeSurfaceWithoutTransmission)
{
    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Features = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME;
    ExpectInvalidDefinition(DefinitionDesc,
                            "The volume surface feature requires the transmission surface feature");
}

TEST(RadientMaterialTest, RejectsSurfaceModePackingForNonSurfaceDefinition)
{
    RadientComputeMaterialDefinitionDesc         DefinitionDesc{};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.SurfaceModeOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Uint32));
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "Only surface material definitions may pack a surface mode");
}

TEST(RadientMaterialTest, RejectsOutOfBoundsSurfaceModePacking)
{
    RadientSurfaceMaterialDefinitionDesc         DefinitionDesc{};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.SurfaceModeOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Uint32) - 1);
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "surface mode byte range exceeds the shader data size");
}

TEST(RadientMaterialTest, RejectsAlphaCutoffPackingForNonSurfaceDefinition)
{
    RadientComputeMaterialDefinitionDesc         DefinitionDesc{};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.AlphaCutoffOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Float32));
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "Only surface material definitions may pack an alpha cutoff");
}

TEST(RadientMaterialTest, RejectsOutOfBoundsAlphaCutoffPacking)
{
    RadientSurfaceMaterialDefinitionDesc         DefinitionDesc{};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.AlphaCutoffOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Float32) - 1);
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "alpha cutoff byte range exceeds the shader data size");
}

TEST(RadientMaterialTest, RejectsOverlappingSurfaceStatePacking)
{
    RadientSurfaceMaterialDefinitionDesc         DefinitionDesc{};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.SurfaceModeOffset = 0;
    SurfacePacking.AlphaCutoffOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Uint32));
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "surface mode and alpha cutoff overlap");
}

TEST(RadientMaterialTest, RejectsShaderParameterOverlappingSurfaceMode)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultValue = &DefaultValue;

    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    const RadientMaterialShaderParameterPacking  Mapping{0, 0};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.SurfaceModeOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Uint32));
    ShaderDataLayout.pMappings       = &Mapping;
    ShaderDataLayout.MappingCount    = 1;
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "surface mode and parameter 'Value' overlap");
}

TEST(RadientMaterialTest, RejectsShaderParameterOverlappingAlphaCutoff)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultValue = &DefaultValue;

    RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    const RadientMaterialShaderParameterPacking  Mapping{0, 0};
    RadientSurfaceMaterialShaderParameterPacking SurfacePacking{};
    SurfacePacking.AlphaCutoffOffset = 0;
    RadientMaterialShaderDataLayoutDesc ShaderDataLayout{};
    ShaderDataLayout.Size            = static_cast<Uint32>(sizeof(Float32));
    ShaderDataLayout.pMappings       = &Mapping;
    ShaderDataLayout.MappingCount    = 1;
    ShaderDataLayout.pSurfacePacking = &SurfacePacking;
    ExpectInvalidShaderDataLayout(DefinitionDesc, ShaderDataLayout,
                                  "alpha cutoff and parameter 'Value' overlap");
}

TEST(RadientMaterialTest, RejectsMissingDefinitionParameterArray)
{
    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "parameters, but pParameters is null");
}

TEST(RadientMaterialTest, RejectsNullMaterialParameterName)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 0 name must not be null");
}

TEST(RadientMaterialTest, RejectsEmptyMaterialParameterName)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 0 name must not be empty");
}

TEST(RadientMaterialTest, RejectsInvalidMaterialParameterType)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "InvalidType";

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'InvalidType' has invalid type");

    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_COUNT;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'InvalidType' has invalid type");
}

TEST(RadientMaterialTest, RejectsZeroMaterialParameterArraySize)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name      = "ZeroArraySize";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.ArraySize = 0;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'ZeroArraySize' array size must not be zero");
}

TEST(RadientMaterialTest, RejectsMaterialParameterDataSizeOverflow)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name      = "OversizedArray";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4;
    Parameter.ArraySize = std::numeric_limits<Uint32>::max();

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'OversizedArray' data size exceeds the supported limit");
}

TEST(RadientMaterialTest, RejectsTextureMaterialParameterDefaultValue)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Texture";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.pDefaultValue = &DefaultValue;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Texture material parameter 'Texture' must use pDefaultTexture instead of pDefaultValue");
}

TEST(RadientMaterialTest, RejectsTextureArrayDefaultTexture)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://invalid-array-default");

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "TextureArray";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.ArraySize       = 2;
    Parameter.pDefaultTexture = pTexture;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Texture array material parameter 'TextureArray' must not specify pDefaultTexture");
}

TEST(RadientMaterialTest, RejectsNonTextureMaterialParameterDefaultTexture)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://invalid-value-default");

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "Value";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultTexture = pTexture;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Non-texture material parameter 'Value' must not specify pDefaultTexture");
}

TEST(RadientMaterialTest, RejectsDuplicateMaterialParameterNames)
{
    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name = "Duplicate";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1]      = Parameters[0];

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 1 name 'Duplicate' duplicates parameter 0");
}


} // namespace
