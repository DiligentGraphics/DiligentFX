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

#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTestAssetHelpers.hpp"

#include "gtest/gtest.h"

#include <array>

using namespace Diligent;

namespace
{

template <typename ValueType>
ValueType GetInstanceParameter(IRadientMaterialInstance& Instance, const char* Name)
{
    ValueType                        Value{};
    IRadientMaterialDefinitionAsset* pDefinition = Instance.GetDefinition();
    RadientMaterialParameterHandle   Handle;
    EXPECT_NE(pDefinition, nullptr);
    if (pDefinition != nullptr)
    {
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK);
        if (Handle)
        {
            EXPECT_EQ(Instance.GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))),
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
RADIENT_STATUS SetInstanceParameter(IRadientMaterialDefinitionAsset& Definition,
                                    IRadientMaterialInstanceWriter&  Writer,
                                    const char*                      Name,
                                    const ValueType&                 Value)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           Status = Definition.FindParameter(Name, &Handle);
    return Status == RADIENT_STATUS_OK ?
        Writer.SetParameter(Handle, Value) :
        Status;
}

RefCntAutoPtr<IRadientMaterialInstance> CreateTestMaterialInstance(
    RadientMaterialAssetManager& MaterialManager,
    const TestMaterialValues&    Values = {})
{
    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    const RADIENT_STATUS                    Status = Testing::CreateStandardMaterialInstance(
        MaterialManager,
        {},
        [&Values](IRadientMaterialDefinitionAsset& Definition,
                  IRadientMaterialInstanceWriter&  Writer) -> RADIENT_STATUS {
            RefCntAutoPtr<IRadientSurfaceMaterialInstanceWriter> pSurfaceWriter{
                &Writer, IID_RadientSurfaceMaterialInstanceWriter};
            EXPECT_NE(pSurfaceWriter, nullptr);
            if (!pSurfaceWriter)
                return RADIENT_STATUS_INVALID_OPERATION;

            RADIENT_STATUS InitializeStatus = pSurfaceWriter->SetAlphaCutoff(Values.AlphaCutoff);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = pSurfaceWriter->SetDoubleSided(Values.DoubleSided);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetInstanceParameter(Definition, Writer, RadientStandardMaterialBaseColorFactorName, Values.BaseColorFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetInstanceParameter(Definition, Writer, RadientStandardMaterialMetallicFactorName, Values.MetallicFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetInstanceParameter(Definition, Writer, RadientStandardMaterialRoughnessFactorName, Values.RoughnessFactor);
            if (RADIENT_SUCCEEDED(InitializeStatus))
                InitializeStatus = SetInstanceParameter(Definition, Writer, RadientStandardMaterialEmissiveFactorName, Values.EmissiveFactor);

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
        pInstance.GetAddressOfEmpty());
    EXPECT_TRUE(RADIENT_SUCCEEDED(Status));
    return RADIENT_SUCCEEDED(Status) ? pInstance : RefCntAutoPtr<IRadientMaterialInstance>{};
}

void VerifyTestMaterialInstance(IRadientMaterialInstance* pInstance,
                                const TestMaterialValues& Values)
{
    ASSERT_NE(pInstance, nullptr);

    const RadientFloat4 BaseColorFactor =
        GetInstanceParameter<RadientFloat4>(*pInstance, RadientStandardMaterialBaseColorFactorName);
    EXPECT_FLOAT_EQ(BaseColorFactor.x, Values.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(BaseColorFactor.y, Values.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(BaseColorFactor.z, Values.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(BaseColorFactor.w, Values.BaseColorFactor.w);
    EXPECT_FLOAT_EQ(GetInstanceParameter<Float32>(*pInstance, RadientStandardMaterialMetallicFactorName),
                    Values.MetallicFactor);
    EXPECT_FLOAT_EQ(GetInstanceParameter<Float32>(*pInstance, RadientStandardMaterialRoughnessFactorName),
                    Values.RoughnessFactor);

    const RadientFloat3 EmissiveFactor =
        GetInstanceParameter<RadientFloat3>(*pInstance, RadientStandardMaterialEmissiveFactorName);
    EXPECT_FLOAT_EQ(EmissiveFactor.x, Values.EmissiveFactor.x);
    EXPECT_FLOAT_EQ(EmissiveFactor.y, Values.EmissiveFactor.y);
    EXPECT_FLOAT_EQ(EmissiveFactor.z, Values.EmissiveFactor.z);

    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{
        pInstance, IID_RadientSurfaceMaterialInstance};
    ASSERT_NE(pSurfaceInstance, nullptr);
    EXPECT_EQ(pSurfaceInstance->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_FLOAT_EQ(pSurfaceInstance->GetAlphaCutoff(), Values.AlphaCutoff);
    EXPECT_EQ(pSurfaceInstance->IsDoubleSided(), Values.DoubleSided);

    IRadientMaterialDefinitionAsset* const pDefinition = pInstance->GetDefinition();
    ASSERT_NE(pDefinition, nullptr);
    const auto VerifyTexture = [&](const Char*           TextureName,
                                   const Char*           UVSelectorName,
                                   IRadientTextureAsset* pExpectedTexture) {
        RadientMaterialParameterHandle TextureHandle;
        ASSERT_EQ(pDefinition->FindParameter(TextureName, &TextureHandle), RADIENT_STATUS_OK)
            << TextureName;

        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(pInstance->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK)
            << TextureName;
        EXPECT_EQ(pTexture.RawPtr(), pExpectedTexture) << TextureName;
        EXPECT_EQ(GetInstanceParameter<Int32>(*pInstance, UVSelectorName),
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
    // the definition-backed instance used by renderers.
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const TestMaterialValues                Values = MakeTestMaterialValues();
    RefCntAutoPtr<IRadientMaterialInstance> pSourceInstance =
        CreateTestMaterialInstance(*pMaterialManager, Values);
    ASSERT_NE(pSourceInstance, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pSourceInstance, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_NE(pMaterial->GetReference().URI, nullptr);
    EXPECT_NE(pMaterial->GetReference().Version, 0u);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_EQ(pInstance, pSourceInstance);

    const RadientMaterialAssetView MaterialView = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
    EXPECT_EQ(MaterialView.pInstance, pInstance);
    VerifyTestMaterialInstance(pInstance, Values);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pInstance->GetDefinition()->FindParameter("BaseColorTexture", &TextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pInstance->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture, nullptr);
    const RadientMaterialTextureEntry* pTextureEntry = MaterialView.GetTexture(TextureHandle.Index);
    ASSERT_NE(pTextureEntry, nullptr);
    ASSERT_NE(MaterialView.pTextureIndexByParameter, nullptr);
    EXPECT_EQ(MaterialView.ParameterCount, pInstance->GetDefinition()->GetParameterCount());
    ASSERT_NE(MaterialView.pTextureIndexByParameter[TextureHandle.Index],
              RadientMaterialAssetView::InvalidTextureIndex);
    EXPECT_EQ(&MaterialView.pTextures[MaterialView.pTextureIndexByParameter[TextureHandle.Index]],
              pTextureEntry);
    EXPECT_EQ(pTextureEntry->ParameterIndex, TextureHandle.Index);
    EXPECT_EQ(pTextureEntry->ArrayIndex, 0u);
    EXPECT_EQ(pTextureEntry->pTexture, nullptr);

    RadientMaterialParameterHandle BaseColorFactorHandle;
    ASSERT_EQ(pInstance->GetDefinition()->FindParameter("BaseColorFactor", &BaseColorFactorHandle),
              RADIENT_STATUS_OK);
    EXPECT_EQ(MaterialView.pTextureIndexByParameter[BaseColorFactorHandle.Index],
              RadientMaterialAssetView::InvalidTextureIndex);

    RefCntAutoPtr<IRadientMaterialAsset>    pSecondMaterial;
    RefCntAutoPtr<IRadientMaterialInstance> pSecondSourceInstance =
        CreateTestMaterialInstance(*pMaterialManager, Values);
    ASSERT_NE(pSecondSourceInstance, nullptr);
    ASSERT_EQ(pMaterialManager->CreateMaterial(pSecondSourceInstance, pSecondMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMaterialInstance> pSecondInstance = RadientMaterialAssetManager::GetInstance(pSecondMaterial);
    ASSERT_NE(pSecondInstance, nullptr);
    EXPECT_EQ(pSecondInstance->GetDefinition(), pInstance->GetDefinition());
}

TEST(RadientMaterialAssetManagerTest, SharedInstanceAssetsShareFinalizedTextureStorage)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance =
        CreateTestMaterialInstance(*pMaterialManager);
    ASSERT_NE(pInstance, nullptr);
    const Uint64 InitialVersion = pInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialA;
    RefCntAutoPtr<IRadientMaterialAsset> pMaterialB;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pInstance, pMaterialA.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pMaterialManager->CreateMaterial(pInstance, pMaterialB.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    const RadientMaterialAssetView ViewA =
        RadientMaterialAssetManager::GetMaterialView(pMaterialA);
    const RadientMaterialAssetView ViewB =
        RadientMaterialAssetManager::GetMaterialView(pMaterialB);
    ASSERT_TRUE(ViewA);
    ASSERT_TRUE(ViewB);

    EXPECT_EQ(RadientMaterialAssetManager::GetInstance(pMaterialA), pInstance);
    EXPECT_EQ(RadientMaterialAssetManager::GetInstance(pMaterialB), pInstance);
    EXPECT_EQ(ViewA.pInstance, pInstance);
    EXPECT_EQ(ViewB.pInstance, pInstance);
    EXPECT_EQ(ViewA.pTextures, ViewB.pTextures);
    EXPECT_EQ(ViewA.TextureCount, ViewB.TextureCount);
    EXPECT_EQ(ViewA.pTextureIndexByParameter, ViewB.pTextureIndexByParameter);
    EXPECT_EQ(ViewA.ParameterCount, ViewB.ParameterCount);
    EXPECT_EQ(pInstance->GetVersion(), InitialVersion);
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

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    ASSERT_EQ(pDefinition->CreateInstance(pInstance.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pInstance, pMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    EXPECT_EQ(RadientMaterialAssetManager::GetInstance(pMaterial), pInstance);
    const RadientMaterialAssetView MaterialView =
        RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
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

TEST(RadientMaterialAssetManagerTest, CreateMaterialStoresUsedTexturesInInstance)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture =
        Testing::MakeTestTextureAsset("texture://base-color");

    TestMaterialValues Values;
    Values.pBaseColorTexture = pBaseColorTexture;
    RefCntAutoPtr<IRadientMaterialInstance> pSourceInstance =
        CreateTestMaterialInstance(*pMaterialManager, Values);
    ASSERT_NE(pSourceInstance, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(pSourceInstance, pMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);

    RadientMaterialParameterHandle BaseColorTextureHandle;
    ASSERT_EQ(pInstance->GetDefinition()->FindParameter("BaseColorTexture", &BaseColorTextureHandle),
              RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientTextureAsset> pStoredTexture;
    ASSERT_EQ(pInstance->GetTexture(BaseColorTextureHandle, 0, pStoredTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pStoredTexture, pBaseColorTexture);

    RadientMaterialParameterHandle NormalTextureHandle;
    ASSERT_EQ(pInstance->GetDefinition()->FindParameter("NormalTexture", &NormalTextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pNormalTexture;
    EXPECT_EQ(pInstance->GetTexture(NormalTextureHandle, 0, pNormalTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pNormalTexture, nullptr);
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstance, RadientStandardMaterialNormalTextureUVSelectorName), -1);

    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstance, RadientStandardMaterialBaseColorTextureUVSelectorName), 0);

    RefCntAutoPtr<IRadientMaterialAsset>    pMaterialWithoutTextures;
    RefCntAutoPtr<IRadientMaterialInstance> pInstanceWithoutTextureValues =
        CreateTestMaterialInstance(*pMaterialManager);
    ASSERT_NE(pInstanceWithoutTextureValues, nullptr);
    ASSERT_EQ(pMaterialManager->CreateMaterial(pInstanceWithoutTextureValues, pMaterialWithoutTextures.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMaterialInstance> pInstanceWithoutTextures =
        RadientMaterialAssetManager::GetInstance(pMaterialWithoutTextures);
    ASSERT_NE(pInstanceWithoutTextures, nullptr);
    EXPECT_EQ(pInstanceWithoutTextures->GetDefinition(), pInstance->GetDefinition());
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstanceWithoutTextures, RadientStandardMaterialBaseColorTextureUVSelectorName), -1);
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstanceWithoutTextures, RadientStandardMaterialMetallicRoughnessTextureUVSelectorName), -1);
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstanceWithoutTextures, RadientStandardMaterialNormalTextureUVSelectorName), -1);
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstanceWithoutTextures, RadientStandardMaterialOcclusionTextureUVSelectorName), -1);
    EXPECT_EQ(GetInstanceParameter<Int32>(*pInstanceWithoutTextures, RadientStandardMaterialEmissiveTextureUVSelectorName), -1);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialRejectsInvalidArguments)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance =
        CreateTestMaterialInstance(*pMaterialManager, MakeTestMaterialValues());
    ASSERT_NE(pInstance, nullptr);
    EXPECT_EQ(pMaterialManager->CreateMaterial(pInstance, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
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

        RefCntAutoPtr<IRadientMaterialInstance> pInstance =
            CreateTestMaterialInstance(*pMaterialManager, Values);
        ASSERT_NE(pInstance, nullptr);
        ASSERT_EQ(pMaterialManager->CreateMaterial(pInstance, &pMaterial), RADIENT_STATUS_OK);
        ASSERT_NE(pMaterial, nullptr);
    }

    // The asset owns its payload, so the material must remain readable after
    // the manager that created it has been destroyed.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    VerifyTestMaterialInstance(pInstance, Values);

    const RadientMaterialAssetView MaterialView = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialView);
    EXPECT_EQ(MaterialView.pInstance, pInstance);
}

} // namespace
