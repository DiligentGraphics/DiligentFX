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
#include "RadientStandardMaterialParameters.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "RadientTestAssetHelpers.hpp"

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
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

RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                IRadientMaterialDefinition**         ppDefinition)
{
    return RadientMaterialAssetManager::CreateDefinition(DefinitionDesc, ppDefinition);
}

RefCntAutoPtr<IRadientMaterialDefinition> CreateDefinition(
    const RadientMaterialParameterDesc* pParameters,
    Uint32                              ParameterCount,
    const char*                         Name = "Test material definition")
{
    RadientMaterialDefinitionDesc DefinitionDesc{};
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

TEST(RadientMaterialsTest, DefinitionCopiesSchemaAndDefaults)
{
    char          DefinitionName[] = "Copied definition";
    char          ParameterName[]  = "BaseColor";
    RadientFloat4 DefaultColor{0.1f, 0.2f, 0.3f, 0.4f};

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = ParameterName;
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameter.pDefaultValue = &DefaultColor;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = DefinitionName;
    DefinitionDesc.Domain         = RADIENT_MATERIAL_DOMAIN_SURFACE;
    DefinitionDesc.Reference      = {"material-definition://copied", 11};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    DefinitionName[0] = 'X';
    ParameterName[0]  = 'X';
    DefaultColor      = {};

    const RadientMaterialDefinitionDesc& StoredDesc = pDefinition->GetDesc();
    EXPECT_STREQ(StoredDesc.Name, "Copied definition");
    EXPECT_EQ(StoredDesc.Domain, RADIENT_MATERIAL_DOMAIN_SURFACE);
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

TEST(RadientMaterialsTest, AssetManagerReportsDefinitionStatus)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientMaterialDefinitionDesc             DefinitionDesc{};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionDesc, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pDefinition), RADIENT_STATUS_OK);
}

TEST(RadientMaterialsTest, FindsParametersByNameRegardlessOfDeclarationOrder)
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

TEST(RadientMaterialsTest, WriterUpdatesSharedInstance)
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
    ASSERT_EQ(pWriter->SetParameter(ColorHandle, &UpdatedColor, static_cast<Uint32>(sizeof(UpdatedColor))), RADIENT_STATUS_OK);
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
    ASSERT_EQ(pWriter->SetParameter(ColorHandle, &SecondColor, static_cast<Uint32>(sizeof(SecondColor))), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 2);

    const RadientFloat4 SecondCommittedColor = GetParameter<RadientFloat4>(*pSharedInstance, ColorHandle);
    EXPECT_FLOAT_EQ(SecondCommittedColor.x, SecondColor.x);
    EXPECT_FLOAT_EQ(SecondCommittedColor.y, SecondColor.y);
    EXPECT_FLOAT_EQ(SecondCommittedColor.z, SecondColor.z);
    EXPECT_FLOAT_EQ(SecondCommittedColor.w, SecondColor.w);
}

TEST(RadientMaterialsTest, InstanceSupportsValueAndTextureArrays)
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
    ASSERT_EQ(pFirstWriter->SetParameter(ValuesHandle, UpdatedValues.data(), static_cast<Uint32>(sizeof(UpdatedValues))), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->SetTexture(TexturesHandle, 0, pTexture0), RADIENT_STATUS_OK);
    ASSERT_EQ(pFirstWriter->Commit(), RADIENT_STATUS_OK);

    // The second writer predates the first commit. Committing its texture array
    // replaces the complete parameter value, including the unchanged element.
    ASSERT_EQ(pSecondWriter->SetTexture(TexturesHandle, 1, pTexture1), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->Commit(), RADIENT_STATUS_OK);

    const std::array<Float32, 3> StoredValues =
        GetParameter<std::array<Float32, 3>>(*pInstance, ValuesHandle);
    EXPECT_EQ(StoredValues, UpdatedValues);
    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture0;
    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture1;
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 0, pStoredTexture0.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pInstance->GetTexture(TexturesHandle, 1, pStoredTexture1.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture0, nullptr);
    EXPECT_EQ(pStoredTexture1, pTexture1);
}

TEST(RadientMaterialsTest, UnchangedWriterDoesNotAdvanceVersion)
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

TEST(RadientMaterialsTest, WriterTracksChangesAcrossMaskWords)
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
        ASSERT_EQ(pWriter->SetParameter(Handle, &UpdatedValue, static_cast<Uint32>(sizeof(UpdatedValue))), RADIENT_STATUS_OK);
    }

    RadientMaterialParameterHandle RevertedHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(ChangedIndices.back(), &RevertedHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(RevertedHandle, &DefaultValue, static_cast<Uint32>(sizeof(DefaultValue))), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    for (Uint32 Index : ChangedIndices)
    {
        RadientMaterialParameterHandle Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(Index, &Handle), RADIENT_STATUS_OK);
        EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, Handle),
                        Index == ChangedIndices.back() ? DefaultValue : UpdatedValue);
    }
}

TEST(RadientMaterialsTest, DefinitionSpecificHandlesAreRejected)
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

TEST(RadientMaterialsTest, CloneCreatesIndependentInstance)
{
    const Float32 DefaultValue = 1.f;
    const Float32 UpdatedValue = 2.f;

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

    RefCntAutoPtr<IRadientMaterialInstance> pClone;
    ASSERT_EQ(pInstance->Clone(pClone.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClone, pInstance);

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    ASSERT_EQ(pClone->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetParameter(Handle, &UpdatedValue, static_cast<Uint32>(sizeof(UpdatedValue))), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, Handle), DefaultValue);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pClone, Handle), UpdatedValue);
}

TEST(RadientMaterialsTest, WritersApplyIndependentChanges)
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
    ASSERT_EQ(pFirstWriter->SetParameter(FirstHandle, &FirstValue, static_cast<Uint32>(sizeof(FirstValue))), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->SetParameter(SecondHandle, &SecondValue, static_cast<Uint32>(sizeof(SecondValue))), RADIENT_STATUS_OK);

    ASSERT_EQ(pFirstWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(pSecondWriter->Commit(), RADIENT_STATUS_OK);

    EXPECT_EQ(pInstance->GetVersion(), InitialVersion + 2);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, FirstHandle), FirstValue);
    EXPECT_FLOAT_EQ(GetParameter<Float32>(*pInstance, SecondHandle), SecondValue);
}

TEST(RadientMaterialsTest, InstanceRetainsDefinitionAndTextures)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://retained");
    RefCntWeakPtr<IRadientTextureAsset> WeakTexture{pTexture};

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "Texture";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.pDefaultTexture = pTexture;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition = CreateDefinition(&Parameter, 1);
    ASSERT_NE(pDefinition, nullptr);
    RefCntWeakPtr<IRadientMaterialDefinition> WeakDefinition{pDefinition};

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pDefinition->GetParameterHandle(0, &TextureHandle), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pInstanceTexture;
    ASSERT_EQ(pInstance->GetTexture(TextureHandle, 0, pInstanceTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pInstanceTexture, pTexture);

    pTexture.Release();
    pDefinition.Release();
    EXPECT_NE(WeakTexture.Lock(), nullptr);
    EXPECT_NE(WeakDefinition.Lock(), nullptr);

    pInstance.Release();
    EXPECT_NE(WeakTexture.Lock(), nullptr);
    EXPECT_EQ(WeakDefinition.Lock(), nullptr);

    pInstanceTexture.Release();
    EXPECT_EQ(WeakTexture.Lock(), nullptr);
}

TEST(RadientMaterialsTest, RejectsInvalidDefinitionDomain)
{
    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Domain = RADIENT_MATERIAL_DOMAIN_COUNT;
    ExpectInvalidDefinition(DefinitionDesc, "Invalid material definition domain");
}

TEST(RadientMaterialsTest, RejectsMissingDefinitionParameterArray)
{
    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "parameters, but pParameters is null");
}

TEST(RadientMaterialsTest, RejectsNullMaterialParameterName)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 0 name must not be null");
}

TEST(RadientMaterialsTest, RejectsEmptyMaterialParameterName)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "";
    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 0 name must not be empty");
}

TEST(RadientMaterialsTest, RejectsInvalidMaterialParameterType)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name = "InvalidType";

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;

    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'InvalidType' has invalid type");

    Parameter.Type = RADIENT_MATERIAL_PARAMETER_TYPE_COUNT;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'InvalidType' has invalid type");
}

TEST(RadientMaterialsTest, RejectsZeroMaterialParameterArraySize)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name      = "ZeroArraySize";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.ArraySize = 0;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'ZeroArraySize' array size must not be zero");
}

TEST(RadientMaterialsTest, RejectsMaterialParameterDataSizeOverflow)
{
    RadientMaterialParameterDesc Parameter{};
    Parameter.Name      = "OversizedArray";
    Parameter.Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4;
    Parameter.ArraySize = std::numeric_limits<Uint32>::max();

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 'OversizedArray' data size exceeds the supported limit");
}

TEST(RadientMaterialsTest, RejectsTextureMaterialParameterDefaultValue)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Texture";
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.pDefaultValue = &DefaultValue;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Texture material parameter 'Texture' must use pDefaultTexture instead of pDefaultValue");
}

TEST(RadientMaterialsTest, RejectsTextureArrayDefaultTexture)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://invalid-array-default");

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "TextureArray";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameter.ArraySize       = 2;
    Parameter.pDefaultTexture = pTexture;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Texture array material parameter 'TextureArray' must not specify pDefaultTexture");
}

TEST(RadientMaterialsTest, RejectsNonTextureMaterialParameterDefaultTexture)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture = MakeTestTextureAsset("texture://invalid-value-default");

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name            = "Value";
    Parameter.Type            = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameter.pDefaultTexture = pTexture;

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = &Parameter;
    DefinitionDesc.ParameterCount = 1;
    ExpectInvalidDefinition(DefinitionDesc, "Non-texture material parameter 'Value' must not specify pDefaultTexture");
}

TEST(RadientMaterialsTest, RejectsDuplicateMaterialParameterNames)
{
    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name = "Duplicate";
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1]      = Parameters[0];

    RadientMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());
    ExpectInvalidDefinition(DefinitionDesc, "Material parameter 1 name 'Duplicate' duplicates parameter 0");
}

TEST(RadientMaterialsTest, StandardDefinitionsAreCached)
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

TEST(RadientMaterialsTest, StandardDefinitionUsesPublishedParameterSchema)
{
    struct ExpectedParameter
    {
        const Char*                     Name;
        RADIENT_MATERIAL_PARAMETER_TYPE Type;
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
    }
}

TEST(RadientMaterialsTest, StandardUnlitMaterialHasOnlyApplicableSchema)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_BASE_COLOR;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(pAssetManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    RadientMaterialParameterHandle Handle;
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialBaseColorFactorName, &Handle), RADIENT_STATUS_OK);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialBaseColorTextureName, &Handle), RADIENT_STATUS_OK);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialAlphaModeName, &Handle), RADIENT_STATUS_OK);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialMetallicFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pDefinition->FindParameter(RadientStandardMaterialEmissiveFactorName, &Handle), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientMaterialsTest, RejectsInvalidStandardMaterialModel)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model = RADIENT_STANDARD_MATERIAL_MODEL_COUNT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI, "Invalid standard material model");
}

TEST(RadientMaterialsTest, RejectsUnsupportedStandardMaterialFeatureFlags)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = static_cast<RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS>(
        static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS_ALL) + 1u);
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Standard material feature flags contain unsupported bits");
}

TEST(RadientMaterialsTest, RejectsUnsupportedStandardMaterialTextureFlags)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = static_cast<RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS>(
        static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS_ALL) + 1u);
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Standard material texture flags contain unsupported bits");
}

TEST(RadientMaterialsTest, RejectsOptionalFeaturesForUnlitStandardMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Unlit standard materials do not support optional material features");
}

TEST(RadientMaterialsTest, RejectsNonBaseColorTextureForUnlitStandardMaterial)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Model    = RADIENT_STANDARD_MATERIAL_MODEL_UNLIT;
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Unlit standard materials only support the base-color texture semantic");
}

TEST(RadientMaterialsTest, RejectsClearCoatTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Clear-coat texture semantics require the clear-coat material feature");
}

TEST(RadientMaterialsTest, RejectsSheenTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_COLOR;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Sheen texture semantics require the sheen material feature");
}

TEST(RadientMaterialsTest, RejectsAnisotropyTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_ANISOTROPY;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The anisotropy texture semantic requires the anisotropy material feature");
}

TEST(RadientMaterialsTest, RejectsIridescenceTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE_THICKNESS;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "Iridescence texture semantics require the iridescence material feature");
}

TEST(RadientMaterialsTest, RejectsTransmissionTextureWithoutFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_TRANSMISSION;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The transmission texture semantic requires the transmission material feature");
}

TEST(RadientMaterialsTest, RejectsThicknessTextureWithoutVolumeFeature)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_THICKNESS;
    ExpectInvalidStandardDefinition(*pAssetManager, DefinitionCI,
                                    "The thickness texture semantic requires the volume material feature");
}

TEST(RadientMaterialsTest, VolumeStandardMaterialRequiresTransmission)
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
