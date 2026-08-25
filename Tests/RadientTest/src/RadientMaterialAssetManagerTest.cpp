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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialStorage.hpp"

#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTestAssetHelpers.hpp"

#include "gtest/gtest.h"

#include <array>

using namespace Diligent;

namespace
{

template <typename ValueType>
ValueType GetMaterialParameter(IRadientMaterialAsset& Material, const char* Name)
{
    ValueType                        Value{};
    IRadientMaterialDefinitionAsset* pDefinition = Material.GetDefinition();
    RadientMaterialParameterHandle   Handle;
    EXPECT_NE(pDefinition, nullptr);
    if (pDefinition != nullptr)
    {
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK);
        if (Handle)
        {
            EXPECT_EQ(Material.GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))),
                      RADIENT_STATUS_OK);
        }
    }
    return Value;
}

struct TestMaterialValues
{
    RadientFloat4 BaseColorFactor{1.f, 1.f, 1.f, 1.f};
    Float32       MetallicFactor  = 1.f;
    Float32       RoughnessFactor = 1.f;
    RadientFloat3 EmissiveFactor{};
    Float32       AlphaCutoff = 0.5f;
    Bool          DoubleSided = False;

    IRadientTextureAsset* pBaseColorTexture         = nullptr;
    IRadientTextureAsset* pMetallicRoughnessTexture = nullptr;
    IRadientTextureAsset* pNormalTexture            = nullptr;
    IRadientTextureAsset* pOcclusionTexture         = nullptr;
    IRadientTextureAsset* pEmissiveTexture          = nullptr;
};

TestMaterialValues MakeTestMaterialValues()
{
    TestMaterialValues Values;
    Values.BaseColorFactor = {0.25f, 0.5f, 0.75f, 0.8f};
    Values.MetallicFactor  = 0.2f;
    Values.RoughnessFactor = 0.7f;
    Values.EmissiveFactor  = {1.f, 2.f, 3.f};
    Values.AlphaCutoff     = 0.33f;
    Values.DoubleSided     = True;
    return Values;
}

template <typename ValueType>
RADIENT_STATUS SetMaterialParameter(IRadientMaterialDefinitionAsset& Definition,
                                    IRadientMaterialWriter&          Writer,
                                    const char*                      Name,
                                    const ValueType&                 Value)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           Status = Definition.FindParameter(Name, &Handle);
    return Status == RADIENT_STATUS_OK ?
        Writer.SetParameter(Handle, Value) :
        Status;
}

RefCntAutoPtr<IRadientMaterialAsset> CreateTestMaterial(
    RadientMaterialAssetManager& MaterialManager,
    const TestMaterialValues&    Values = {})
{
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    const RADIENT_STATUS                 Status = Testing::CreateStandardMaterialAsset(
        MaterialManager,
        {},
        [&Values](IRadientMaterialDefinitionAsset& Definition,
                  IRadientMaterialWriter&          Writer) -> RADIENT_STATUS {
            RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{
                &Writer, IID_RadientSurfaceMaterialWriter};
            EXPECT_NE(pSurfaceWriter, nullptr);
            if (!pSurfaceWriter)
                return RADIENT_STATUS_INVALID_OPERATION;

            RADIENT_STATUS InitializeStatus = pSurfaceWriter->SetAlphaCutoff(Values.AlphaCutoff);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = pSurfaceWriter->SetDoubleSided(Values.DoubleSided);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetMaterialParameter(Definition, Writer, RadientStandardMaterialBaseColorFactorName, Values.BaseColorFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetMaterialParameter(Definition, Writer, RadientStandardMaterialMetallicFactorName, Values.MetallicFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetMaterialParameter(Definition, Writer, RadientStandardMaterialRoughnessFactorName, Values.RoughnessFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetMaterialParameter(Definition, Writer, RadientStandardMaterialEmissiveFactorName, Values.EmissiveFactor);

            struct TextureValue
            {
                const RadientStandardMaterialTextureParameterNames* pNames;
                IRadientTextureAsset*                               pTexture;
            };
            const TextureValue Textures[] = {
                {&RadientStandardMaterialBaseColorTextureParameterNames, Values.pBaseColorTexture},
                {&RadientStandardMaterialMetallicRoughnessTextureParameterNames, Values.pMetallicRoughnessTexture},
                {&RadientStandardMaterialNormalTextureParameterNames, Values.pNormalTexture},
                {&RadientStandardMaterialOcclusionTextureParameterNames, Values.pOcclusionTexture},
                {&RadientStandardMaterialEmissiveTextureParameterNames, Values.pEmissiveTexture},
            };
            for (const TextureValue& Texture : Textures)
            {
                if (RADIENT_FAILED(InitializeStatus) || Texture.pTexture == nullptr)
                    continue;

                InitializeStatus = SetStandardMaterialTextureParameters(
                    Definition,
                    Writer,
                    *Texture.pNames,
                    RadientStandardMaterialTextureParameters{Texture.pTexture});
            }

            return InitializeStatus;
        },
        pMaterial.GetAddressOfEmpty());
    EXPECT_TRUE(RADIENT_SUCCEEDED(Status));
    return RADIENT_SUCCEEDED(Status) ? pMaterial : RefCntAutoPtr<IRadientMaterialAsset>{};
}

RefCntAutoPtr<IRadientMaterialAsset> CreateUninitializedTestMaterial(
    RadientMaterialAssetManager& MaterialManager)
{
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(Testing::CreateStandardMaterialAsset(
                  MaterialManager,
                  {},
                  pMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    return pMaterial;
}

const RadientMaterialDetail::MaterialStorage* GetMaterialStorage(
    IRadientMaterialAsset* pMaterial)
{
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(pMaterial);
    EXPECT_NE(pStorage, nullptr);
    return pStorage;
}

void VerifyTestMaterial(IRadientMaterialAsset*    pMaterial,
                        const TestMaterialValues& Values)
{
    ASSERT_NE(pMaterial, nullptr);

    const RadientFloat4 BaseColorFactor =
        GetMaterialParameter<RadientFloat4>(*pMaterial, RadientStandardMaterialBaseColorFactorName);
    EXPECT_FLOAT_EQ(BaseColorFactor.x, Values.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(BaseColorFactor.y, Values.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(BaseColorFactor.z, Values.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(BaseColorFactor.w, Values.BaseColorFactor.w);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, RadientStandardMaterialMetallicFactorName),
                    Values.MetallicFactor);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, RadientStandardMaterialRoughnessFactorName),
                    Values.RoughnessFactor);

    const RadientFloat3 EmissiveFactor =
        GetMaterialParameter<RadientFloat3>(*pMaterial, RadientStandardMaterialEmissiveFactorName);
    EXPECT_FLOAT_EQ(EmissiveFactor.x, Values.EmissiveFactor.x);
    EXPECT_FLOAT_EQ(EmissiveFactor.y, Values.EmissiveFactor.y);
    EXPECT_FLOAT_EQ(EmissiveFactor.z, Values.EmissiveFactor.z);

    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurfaceMaterial{
        pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceMaterial, nullptr);
    EXPECT_EQ(pSurfaceMaterial->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_FLOAT_EQ(pSurfaceMaterial->GetAlphaCutoff(), Values.AlphaCutoff);
    EXPECT_EQ(pSurfaceMaterial->IsDoubleSided(), Values.DoubleSided);

    IRadientMaterialDefinitionAsset* const pDefinition = pMaterial->GetDefinition();
    ASSERT_NE(pDefinition, nullptr);
    const auto VerifyTexture = [&](const Char*           TextureName,
                                   const Char*           UVSelectorName,
                                   IRadientTextureAsset* pExpectedTexture) {
        RadientMaterialParameterHandle TextureHandle;
        ASSERT_EQ(pDefinition->FindParameter(TextureName, &TextureHandle), RADIENT_STATUS_OK)
            << TextureName;

        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pMaterial->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK)
            << TextureName;
        EXPECT_EQ(pTexture.RawPtr(), pExpectedTexture) << TextureName;
        EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterial, UVSelectorName),
                  pExpectedTexture != nullptr ? 0 : -1)
            << UVSelectorName;
    };

    VerifyTexture(RadientStandardMaterialBaseColorTextureName,
                  RadientStandardMaterialBaseColorTextureUVSelectorName,
                  Values.pBaseColorTexture);
    VerifyTexture(RadientStandardMaterialMetallicRoughnessTextureName,
                  RadientStandardMaterialMetallicRoughnessTextureUVSelectorName,
                  Values.pMetallicRoughnessTexture);
    VerifyTexture(RadientStandardMaterialNormalTextureName,
                  RadientStandardMaterialNormalTextureUVSelectorName,
                  Values.pNormalTexture);
    VerifyTexture(RadientStandardMaterialOcclusionTextureName,
                  RadientStandardMaterialOcclusionTextureUVSelectorName,
                  Values.pOcclusionTexture);
    VerifyTexture(RadientStandardMaterialEmissiveTextureName,
                  RadientStandardMaterialEmissiveTextureUVSelectorName,
                  Values.pEmissiveTexture);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterial)
{
    // Creating a material should allocate a stable asset reference and retain
    // the definition used by renderers.
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const TestMaterialValues             Values = MakeTestMaterialValues();
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateTestMaterial(*pMaterialManager, Values);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_NE(pMaterial->GetReference().URI, nullptr);
    EXPECT_NE(pMaterial->GetReference().Version, 0u);

    const RadientMaterialAssetView MaterialView = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
    EXPECT_EQ(MaterialView.pMaterial, pMaterial);
    VerifyTestMaterial(pMaterial, Values);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter("BaseColorTexture", &TextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pMaterial->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture, nullptr);
    const RadientMaterialTextureEntry* pTextureEntry = MaterialView.GetTexture(TextureHandle.Index);
    ASSERT_NE(pTextureEntry, nullptr);
    ASSERT_NE(MaterialView.pTextureIndexByParameter, nullptr);
    EXPECT_EQ(MaterialView.ParameterCount, pMaterial->GetDefinition()->GetParameterCount());
    ASSERT_NE(MaterialView.pTextureIndexByParameter[TextureHandle.Index],
              RadientMaterialAssetView::InvalidTextureIndex);
    EXPECT_EQ(&MaterialView.pTextures[MaterialView.pTextureIndexByParameter[TextureHandle.Index]],
              pTextureEntry);
    EXPECT_EQ(pTextureEntry->ParameterIndex, TextureHandle.Index);
    EXPECT_EQ(pTextureEntry->ArrayIndex, 0u);
    EXPECT_EQ(pTextureEntry->pTexture, nullptr);

    RadientMaterialParameterHandle BaseColorFactorHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter("BaseColorFactor", &BaseColorFactorHandle),
              RADIENT_STATUS_OK);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[BaseColorFactorHandle.Index],
              RadientMaterialAssetView::InvalidTextureIndex);

    RefCntAutoPtr<IRadientMaterialAsset> pSecondMaterial =
        CreateTestMaterial(*pMaterialManager, Values);
    ASSERT_NE(pSecondMaterial, nullptr);
    EXPECT_NE(pSecondMaterial, pMaterial);
    EXPECT_EQ(pSecondMaterial->GetDefinition(), pMaterial->GetDefinition());
}

TEST(RadientMaterialAssetManagerTest, MaterialsFromCachedDefinitionHaveDistinctIdentity)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialA =
        CreateTestMaterial(*pMaterialManager);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterialB =
        CreateTestMaterial(*pMaterialManager);
    ASSERT_NE(pMaterialA, nullptr);
    ASSERT_NE(pMaterialB, nullptr);
    EXPECT_NE(pMaterialA, pMaterialB);
    EXPECT_NE(pMaterialA->GetReference(), pMaterialB->GetReference());
    EXPECT_EQ(pMaterialA->GetDefinition(), pMaterialB->GetDefinition());

    const RadientMaterialAssetView ViewA =
        RadientMaterialAssetManager::GetMaterialView(pMaterialA);
    const RadientMaterialAssetView ViewB =
        RadientMaterialAssetManager::GetMaterialView(pMaterialB);
    ASSERT_TRUE(ViewA);
    ASSERT_TRUE(ViewB);

    EXPECT_EQ(ViewA.pMaterial, pMaterialA);
    EXPECT_EQ(ViewB.pMaterial, pMaterialB);
    EXPECT_EQ(ViewA.TextureCount, ViewB.TextureCount);
    EXPECT_EQ(ViewA.ParameterCount, ViewB.ParameterCount);
}

TEST(RadientMaterialAssetManagerTest, TracksEffectiveMaterialChanges)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 0u);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateUninitializedTestMaterial(*pMaterialManager);
    ASSERT_NE(pMaterial, nullptr);
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        GetMaterialStorage(pMaterial);
    ASSERT_NE(pStorage, nullptr);

    const auto ExpectVersions =
        [&](Uint64 Version,
            Uint64 ShaderDataVersion,
            Uint64 TextureBindingsVersion,
            Uint64 RenderStateVersion) {
            const RadientMaterialDetail::MaterialChangeVersions& Versions =
                pStorage->GetChangeVersions();
            EXPECT_EQ(Versions.Version, Version);
            EXPECT_EQ(Versions.ShaderDataVersion, ShaderDataVersion);
            EXPECT_EQ(Versions.TextureBindingsVersion, TextureBindingsVersion);
            EXPECT_EQ(Versions.RenderStateVersion, RenderStateVersion);
            EXPECT_EQ(pMaterial->GetVersion(), Version);
        };

    ExpectVersions(1, 1, 1, 1);
    EXPECT_NE(pStorage->GetIdentity().ID, RadientMaterialDetail::InvalidMaterialID);
    EXPECT_NE(pStorage->GetIdentity().pChangeTracker, nullptr);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 0u);

    RadientMaterialParameterHandle BaseColorHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter(
                  RadientStandardMaterialBaseColorFactorName,
                  &BaseColorHandle),
              RADIENT_STATUS_OK);

    const RadientFloat4 BaseColor{0.25f, 0.5f, 0.75f, 1.f};
    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetParameter(BaseColorHandle, BaseColor), RADIENT_STATUS_OK);
        EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    }
    ExpectVersions(2, 2, 1, 1);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 1u);

    // A fresh writer records the assignment, but Commit() detects that it does
    // not effectively change the material.
    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetParameter(BaseColorHandle, BaseColor), RADIENT_STATUS_OK);
        EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    }
    ExpectVersions(2, 2, 1, 1);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 1u);

    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{
            pWriter.RawPtr(), IID_RadientSurfaceMaterialWriter};
        ASSERT_NE(pSurfaceWriter, nullptr);
        ASSERT_EQ(pSurfaceWriter->SetAlphaCutoff(0.25f), RADIENT_STATUS_OK);
        EXPECT_EQ(pSurfaceWriter->Commit(), RADIENT_STATUS_OK);
    }
    ExpectVersions(3, 3, 1, 1);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 2u);

    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{
            pWriter.RawPtr(), IID_RadientSurfaceMaterialWriter};
        ASSERT_NE(pSurfaceWriter, nullptr);
        ASSERT_EQ(pSurfaceWriter->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT),
                  RADIENT_STATUS_OK);
        const RadientFloat4 UpdatedBaseColor{0.5f, 0.25f, 0.75f, 1.f};
        ASSERT_EQ(pSurfaceWriter->SetParameter(BaseColorHandle, UpdatedBaseColor), RADIENT_STATUS_OK);
        EXPECT_EQ(pSurfaceWriter->Commit(), RADIENT_STATUS_OK);
    }
    ExpectVersions(4, 4, 1, 2);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 3u);

    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{
            pWriter.RawPtr(), IID_RadientSurfaceMaterialWriter};
        ASSERT_NE(pSurfaceWriter, nullptr);
        ASSERT_EQ(pSurfaceWriter->SetDoubleSided(True), RADIENT_STATUS_OK);
        EXPECT_EQ(pSurfaceWriter->Commit(), RADIENT_STATUS_OK);
    }
    ExpectVersions(5, 4, 1, 3);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 4u);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter(
                  RadientStandardMaterialBaseColorTextureName,
                  &TextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pTexture =
        Testing::MakeTestTextureAsset("texture://change-tracking");
    {
        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    }
    ExpectVersions(6, 5, 2, 3);
    EXPECT_EQ(pMaterialManager->GetMaterialChangeRevision(), 5u);
}

TEST(RadientMaterialAssetManagerTest, MaterialChangeIdentitiesAreManagerScoped)
{
    RadientMaterialAssetManagerSharedPtr pFirstManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pFirstManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pFirstMaterial =
        CreateUninitializedTestMaterial(*pFirstManager);
    RefCntAutoPtr<IRadientMaterialAsset> pSecondMaterial =
        CreateUninitializedTestMaterial(*pFirstManager);
    ASSERT_NE(pFirstMaterial, nullptr);
    ASSERT_NE(pSecondMaterial, nullptr);

    const RadientMaterialDetail::MaterialStorage* const pFirstStorage =
        GetMaterialStorage(pFirstMaterial);
    const RadientMaterialDetail::MaterialStorage* const pSecondStorage =
        GetMaterialStorage(pSecondMaterial);
    ASSERT_NE(pFirstStorage, nullptr);
    ASSERT_NE(pSecondStorage, nullptr);
    EXPECT_EQ(pFirstStorage->GetIdentity().pChangeTracker,
              pSecondStorage->GetIdentity().pChangeTracker);
    EXPECT_NE(pFirstStorage->GetIdentity().ID,
              pSecondStorage->GetIdentity().ID);

    RadientMaterialAssetManagerSharedPtr pOtherManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pOtherManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pOtherMaterial =
        CreateUninitializedTestMaterial(*pOtherManager);
    ASSERT_NE(pOtherMaterial, nullptr);
    const RadientMaterialDetail::MaterialStorage* const pOtherStorage =
        GetMaterialStorage(pOtherMaterial);
    ASSERT_NE(pOtherStorage, nullptr);
    EXPECT_NE(pFirstStorage->GetIdentity().pChangeTracker,
              pOtherStorage->GetIdentity().pChangeTracker);
}

TEST(RadientMaterialAssetManagerTest, MaterialChangeTrackerOutlivesManager)
{
    RefCntAutoPtr<IRadientMaterialAsset>                        pMaterial;
    std::weak_ptr<RadientMaterialDetail::MaterialChangeTracker> WeakTracker;
    {
        RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
        ASSERT_NE(pMaterialManager, nullptr);
        pMaterial = CreateUninitializedTestMaterial(*pMaterialManager);
        ASSERT_NE(pMaterial, nullptr);

        const RadientMaterialDetail::MaterialStorage* const pStorage =
            GetMaterialStorage(pMaterial);
        ASSERT_NE(pStorage, nullptr);
        WeakTracker = pStorage->GetIdentity().pChangeTracker;
    }

    std::shared_ptr<RadientMaterialDetail::MaterialChangeTracker> pTracker = WeakTracker.lock();
    ASSERT_NE(pTracker, nullptr);
    EXPECT_EQ(pTracker->GetRevision(), 0u);

    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(pMaterial->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{
        pWriter.RawPtr(), IID_RadientSurfaceMaterialWriter};
    ASSERT_NE(pSurfaceWriter, nullptr);
    ASSERT_EQ(pSurfaceWriter->SetDoubleSided(True), RADIENT_STATUS_OK);
    EXPECT_EQ(pSurfaceWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pTracker->GetRevision(), 1u);

    pSurfaceWriter.Release();
    pWriter.Release();
    pMaterial.Release();
    pTracker.reset();
    EXPECT_TRUE(WeakTracker.expired());
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialPreservesGenericTextureLayout)
{
    std::array<RadientMaterialParameterDesc, 4> Parameters{};
    Parameters[0].Name      = "LeadingValue";
    Parameters[0].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT;
    Parameters[1].Name      = "TextureArray";
    Parameters[1].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Parameters[1].ArraySize = 2;
    Parameters[2].Name      = "MiddleValue";
    Parameters[2].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3;
    Parameters[3].Name      = "SingleTexture";
    Parameters[3].Type      = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    RadientComputeMaterialDefinitionDesc DefinitionDesc{};
    DefinitionDesc.Name           = "Generic material asset test definition";
    DefinitionDesc.Reference      = {"material-definition://generic-asset-test", 1};
    DefinitionDesc.pParameters    = Parameters.data();
    DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    ASSERT_EQ(RadientMaterialAssetManager::CreateDefinition(
                  DefinitionDesc, pDefinition.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    EXPECT_EQ(pMaterial->GetDefinition(), pDefinition);
    const RadientMaterialAssetView MaterialView =
        RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
    EXPECT_EQ(MaterialView.pMaterial, pMaterial);
    ASSERT_EQ(MaterialView.ParameterCount, Parameters.size());
    ASSERT_EQ(MaterialView.TextureCount, 3u);
    ASSERT_NE(MaterialView.pTextureIndexByParameter, nullptr);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[0], RadientMaterialAssetView::InvalidTextureIndex);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[1], 0u);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[2], RadientMaterialAssetView::InvalidTextureIndex);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[3], 2u);

    const RadientMaterialTextureEntry* pArrayEntry0 = MaterialView.GetTexture(1, 0);
    const RadientMaterialTextureEntry* pArrayEntry1 = MaterialView.GetTexture(1, 1);
    const RadientMaterialTextureEntry* pSingleEntry = MaterialView.GetTexture(3, 0);
    ASSERT_NE(pArrayEntry0, nullptr);
    ASSERT_NE(pArrayEntry1, nullptr);
    ASSERT_NE(pSingleEntry, nullptr);
    EXPECT_EQ(pArrayEntry0->pTexture, nullptr);
    EXPECT_EQ(pArrayEntry1->pTexture, nullptr);
    EXPECT_EQ(pSingleEntry->pTexture, nullptr);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialStoresUsedTextures)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture =
        Testing::MakeTestTextureAsset("texture://base-color");

    TestMaterialValues Values;
    Values.pBaseColorTexture = pBaseColorTexture;
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial =
        CreateTestMaterial(*pMaterialManager, Values);
    ASSERT_NE(pMaterial, nullptr);

    RadientMaterialParameterHandle BaseColorTextureHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter("BaseColorTexture", &BaseColorTextureHandle),
              RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture;
    ASSERT_EQ(pMaterial->GetTexture(BaseColorTextureHandle, 0, pStoredTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pBaseColorTexture);

    RadientMaterialParameterHandle NormalTextureHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter("NormalTexture", &NormalTextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pNormalTexture;
    EXPECT_EQ(pMaterial->GetTexture(NormalTextureHandle, 0, pNormalTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pNormalTexture, nullptr);
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterial, RadientStandardMaterialNormalTextureUVSelectorName), -1);

    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterial, RadientStandardMaterialBaseColorTextureUVSelectorName), 0);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialWithoutTextures =
        CreateTestMaterial(*pMaterialManager);
    ASSERT_NE(pMaterialWithoutTextures, nullptr);
    EXPECT_EQ(pMaterialWithoutTextures->GetDefinition(), pMaterial->GetDefinition());
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterialWithoutTextures, RadientStandardMaterialBaseColorTextureUVSelectorName), -1);
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterialWithoutTextures, RadientStandardMaterialMetallicRoughnessTextureUVSelectorName), -1);
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterialWithoutTextures, RadientStandardMaterialNormalTextureUVSelectorName), -1);
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterialWithoutTextures, RadientStandardMaterialOcclusionTextureUVSelectorName), -1);
    EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterialWithoutTextures, RadientStandardMaterialEmissiveTextureUVSelectorName), -1);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialRejectsInvalidArguments)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pExistingMaterial =
        CreateTestMaterial(*pMaterialManager, MakeTestMaterialValues());
    ASSERT_NE(pExistingMaterial, nullptr);
    IRadientMaterialDefinitionAsset* const pDefinition = pExistingMaterial->GetDefinition();
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(pMaterialManager->CreateMaterial(pDefinition, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(pMaterialManager->CreateMaterial(nullptr, &pMaterial), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMaterial, nullptr);
}

TEST(RadientMaterialAssetManagerTest, MaterialHandleMayOutliveManager)
{
    const TestMaterialValues Values = MakeTestMaterialValues();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    {
        RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
        ASSERT_NE(pMaterialManager, nullptr);

        pMaterial =
            CreateTestMaterial(*pMaterialManager, Values);
        ASSERT_NE(pMaterial, nullptr);
    }

    // The asset owns its payload, so the material must remain readable after
    // the manager that created it has been destroyed.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    VerifyTestMaterial(pMaterial, Values);

    const RadientMaterialAssetView MaterialView = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
    EXPECT_EQ(MaterialView.pMaterial, pMaterial);
}

} // namespace
