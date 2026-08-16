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
#include "RadientTestAssetHelpers.hpp"

#include "RefCntAutoPtr.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstring>

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

RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionCreateInfo& DefinitionCI,
                                IRadientMaterialDefinition**               ppDefinition)
{
    // Definitions own their copied schema and defaults and may outlive the manager.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    return pAssetManager != nullptr ?
        pAssetManager->CreateMaterialDefinition(DefinitionCI, ppDefinition) :
        RADIENT_STATUS_FAILED;
}

RefCntAutoPtr<IRadientMaterialDefinition> CreateDefinition(
    const RadientMaterialParameterDesc* pParameters,
    Uint32                              ParameterCount,
    const char*                         Name = "Test material definition")
{
    RadientMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Desc.Name      = Name;
    DefinitionCI.Reference      = {"material-definition://test", 7};
    DefinitionCI.pParameters    = pParameters;
    DefinitionCI.ParameterCount = ParameterCount;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    EXPECT_EQ(CreateDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return pDefinition;
}

TEST(RadientMaterialsTest, DefinitionCopiesSchemaAndDefaults)
{
    char          DefinitionName[] = "Copied definition";
    char          ParameterName[]  = "BaseColor";
    RadientFloat4 DefaultColor{0.1f, 0.2f, 0.3f, 0.4f};

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = ParameterName;
    Parameter.ID            = 17;
    Parameter.Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameter.pDefaultValue = &DefaultColor;

    RadientMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Desc.Name      = DefinitionName;
    DefinitionCI.Desc.Domain    = RADIENT_MATERIAL_DOMAIN_SURFACE;
    DefinitionCI.Reference      = {"material-definition://copied", 11};
    DefinitionCI.pParameters    = &Parameter;
    DefinitionCI.ParameterCount = 1;

    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(CreateDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);

    DefinitionName[0] = 'X';
    ParameterName[0]  = 'X';
    DefaultColor      = {};

    EXPECT_STREQ(pDefinition->GetDesc().Name, "Copied definition");
    EXPECT_EQ(pDefinition->GetDesc().Domain, RADIENT_MATERIAL_DOMAIN_SURFACE);
    EXPECT_STREQ(pDefinition->GetReference().URI, "material-definition://copied");
    EXPECT_EQ(pDefinition->GetReference().Version, 11u);
    EXPECT_EQ(pDefinition->GetType(), RADIENT_ASSET_TYPE_MATERIAL);
    EXPECT_EQ(pDefinition->GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(pDefinition->GetParameterCount(), 1u);

    const RadientMaterialParameterDesc& StoredParameter = pDefinition->GetParameterDesc(0);
    EXPECT_STREQ(StoredParameter.Name, "BaseColor");
    EXPECT_EQ(StoredParameter.ID, 17u);
    EXPECT_EQ(StoredParameter.Type, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4);
    ASSERT_NE(StoredParameter.pDefaultValue, nullptr);

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

    RadientMaterialDefinitionCreateInfo       DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;
    ASSERT_EQ(pAssetManager->CreateMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pDefinition), RADIENT_STATUS_OK);
}

TEST(RadientMaterialsTest, WriterUpdatesSharedInstance)
{
    const RadientFloat4                 DefaultColor{1.f, 1.f, 1.f, 1.f};
    RefCntAutoPtr<IRadientTextureAsset> pDefaultTexture = MakeTestTextureAsset("texture://default");
    RefCntAutoPtr<IRadientTextureAsset> pUpdatedTexture = MakeTestTextureAsset("texture://updated");

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name            = "BaseColor";
    Parameters[0].ID              = 1;
    Parameters[0].Type            = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    Parameters[0].pDefaultValue   = &DefaultColor;
    Parameters[1].Name            = "BaseColorTexture";
    Parameters[1].ID              = 2;
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

TEST(RadientMaterialsTest, UnchangedWriterDoesNotAdvanceVersion)
{
    const Float32 DefaultRoughness = 0.5f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Roughness";
    Parameter.ID            = 1;
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

TEST(RadientMaterialsTest, DefinitionSpecificHandlesAreRejected)
{
    const Float32 DefaultValue = 1.f;

    RadientMaterialParameterDesc Parameter{};
    Parameter.Name          = "Value";
    Parameter.ID            = 1;
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
    Parameter.ID            = 1;
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

TEST(RadientMaterialsTest, WritersMergeChangesToDifferentParameters)
{
    const Float32 DefaultValue = 0.f;
    const Float32 FirstValue   = 1.f;
    const Float32 SecondValue  = 2.f;

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name          = "First";
    Parameters[0].ID            = 1;
    Parameters[0].Type          = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[0].pDefaultValue = &DefaultValue;
    Parameters[1].Name          = "Second";
    Parameters[1].ID            = 2;
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
    Parameter.ID              = 1;
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

TEST(RadientMaterialsTest, RejectsInvalidDefinitionSchema)
{
    RadientMaterialDefinitionCreateInfo       DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialDefinition> pDefinition;

    DefinitionCI.Desc.Domain = RADIENT_MATERIAL_DOMAIN_COUNT;
    EXPECT_EQ(CreateDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);

    std::array<RadientMaterialParameterDesc, 2> Parameters{};
    Parameters[0].Name = "Duplicate";
    Parameters[0].ID   = 1;
    Parameters[0].Type = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1]      = Parameters[0];

    DefinitionCI.Desc.Domain    = RADIENT_MATERIAL_DOMAIN_SURFACE;
    DefinitionCI.pParameters    = Parameters.data();
    DefinitionCI.ParameterCount = static_cast<Uint32>(Parameters.size());
    EXPECT_EQ(CreateDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);

    Parameters[1].Name      = "TextureArray";
    Parameters[1].ID        = 2;
    Parameters[1].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize = 0;
    EXPECT_EQ(CreateDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

} // namespace
