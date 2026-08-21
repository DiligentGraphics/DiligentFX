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

#include "RadientStandardMaterialParameters.h"
#include "RadientTestAssetHelpers.hpp"

#include "gtest/gtest.h"

#include <utility>

using namespace Diligent;

namespace
{

template <typename ValueType>
ValueType GetInstanceParameter(IRadientMaterialInstance& Instance, const char* Name)
{
    ValueType                      Value{};
    IRadientMaterialDefinition*    pDefinition = Instance.GetDefinition();
    RadientMaterialParameterHandle Handle;
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

RadientMaterialCreateInfo MakeTestMaterialCreateInfo()
{
    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name            = "Radient test material";
    MaterialCI.BaseColorFactor = {0.25f, 0.5f, 0.75f, 0.8f};
    MaterialCI.MetallicFactor  = 0.2f;
    MaterialCI.RoughnessFactor = 0.7f;
    MaterialCI.EmissiveFactor  = {1.f, 2.f, 3.f};
    MaterialCI.AlphaCutoff     = 0.33f;
    MaterialCI.DoubleSided     = True;
    return MaterialCI;
}

void VerifyTestMaterial(const GLTF::Material&            GLTFMaterial,
                        const RadientMaterialCreateInfo& MaterialCI)
{
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.BaseColorFactor.x, MaterialCI.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.BaseColorFactor.y, MaterialCI.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.BaseColorFactor.z, MaterialCI.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.BaseColorFactor.w, MaterialCI.BaseColorFactor.w);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.MetallicFactor, MaterialCI.MetallicFactor);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.RoughnessFactor, MaterialCI.RoughnessFactor);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.EmissiveFactor.x, MaterialCI.EmissiveFactor.x);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.EmissiveFactor.y, MaterialCI.EmissiveFactor.y);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.EmissiveFactor.z, MaterialCI.EmissiveFactor.z);
    EXPECT_FLOAT_EQ(GLTFMaterial.Attribs.AlphaCutoff, MaterialCI.AlphaCutoff);
    EXPECT_TRUE(GLTFMaterial.DoubleSided);

    EXPECT_EQ(GLTFMaterial.GetTextureId(GLTF::DefaultBaseColorTextureAttribId), -1);
    EXPECT_EQ(GLTFMaterial.GetTextureId(GLTF::DefaultNormalTextureAttribId), -1);
    EXPECT_EQ(GLTFMaterial.GetTextureId(GLTF::DefaultMetallicRoughnessTextureAttribId), -1);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterial)
{
    // Creating a material should allocate a stable asset reference and build the
    // internal GLTF material representation used by the renderer.
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const RadientMaterialCreateInfo MaterialCI = MakeTestMaterialCreateInfo();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_NE(pMaterial->GetReference().URI, nullptr);
    EXPECT_NE(pMaterial->GetReference().Version, 0u);

    const RadientMaterialRenderData RenderData = RadientMaterialAssetManager::GetRenderData(pMaterial);
    ASSERT_TRUE(RenderData);
    EXPECT_EQ(RenderData.TextureCount, 0u);

    VerifyTestMaterial(*RenderData.pMaterial, MaterialCI);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_FLOAT_EQ(GetInstanceParameter<RadientFloat4>(*pInstance, "BaseColorFactor").x,
                    MaterialCI.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(GetInstanceParameter<Float32>(*pInstance, "MetallicFactor"),
                    MaterialCI.MetallicFactor);
    EXPECT_FLOAT_EQ(GetInstanceParameter<Float32>(*pInstance, "RoughnessFactor"),
                    MaterialCI.RoughnessFactor);
    EXPECT_FLOAT_EQ(GetInstanceParameter<RadientFloat3>(*pInstance, "EmissiveFactor").z,
                    MaterialCI.EmissiveFactor.z);
    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{
        pInstance, IID_RadientSurfaceMaterialInstance};
    ASSERT_NE(pSurfaceInstance, nullptr);
    EXPECT_EQ(pSurfaceInstance->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_FLOAT_EQ(pSurfaceInstance->GetAlphaCutoff(), MaterialCI.AlphaCutoff);
    EXPECT_EQ(pSurfaceInstance->IsDoubleSided(), MaterialCI.DoubleSided);

    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(pInstance->GetDefinition()->FindParameter("BaseColorTexture", &TextureHandle),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pInstance->GetTexture(TextureHandle, 0, pTexture.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pTexture, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pSecondMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, pSecondMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMaterialInstance> pSecondInstance = RadientMaterialAssetManager::GetInstance(pSecondMaterial);
    ASSERT_NE(pSecondInstance, nullptr);
    EXPECT_EQ(pSecondInstance->GetDefinition(), pInstance->GetDefinition());
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialStoresUsedTexturesInInstance)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture =
        Testing::MakeTestTextureAsset("texture://base-color");

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.pBaseColorTexture = pBaseColorTexture;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, pMaterial.GetAddressOfEmpty()),
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

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialWithoutTextures;
    ASSERT_EQ(pMaterialManager->CreateMaterial({}, pMaterialWithoutTextures.GetAddressOfEmpty()),
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

TEST(RadientMaterialAssetManagerTest, CreateMaterialRejectsNullOutput)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    EXPECT_EQ(pMaterialManager->CreateMaterial(MakeTestMaterialCreateInfo(), nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMaterialAssetManagerTest, CreateGLTFMaterialWithoutTextureDependencies)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const float4 BaseColorFactor{0.1f, 0.2f, 0.3f, 0.4f};

    GLTF::Material Material;
    Material.Attribs.BaseColorFactor = BaseColorFactor;
    Material.DoubleSided             = true;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateGLTFMaterial(std::move(Material), nullptr, 0, &pMaterial),
              RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetRenderData(pMaterial).pMaterial;
    ASSERT_NE(pGLTFMaterial, nullptr);
    EXPECT_EQ(pGLTFMaterial->GetNumActiveTextureAttribs(), 0u);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.x, BaseColorFactor.x);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.y, BaseColorFactor.y);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.z, BaseColorFactor.z);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.w, BaseColorFactor.w);
    EXPECT_TRUE(pGLTFMaterial->DoubleSided);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    const RadientFloat4 StoredBaseColor = GetInstanceParameter<RadientFloat4>(*pInstance, "BaseColorFactor");
    EXPECT_FLOAT_EQ(StoredBaseColor.x, BaseColorFactor.x);
    EXPECT_FLOAT_EQ(StoredBaseColor.y, BaseColorFactor.y);
    EXPECT_FLOAT_EQ(StoredBaseColor.z, BaseColorFactor.z);
    EXPECT_FLOAT_EQ(StoredBaseColor.w, BaseColorFactor.w);
    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{
        pInstance, IID_RadientSurfaceMaterialInstance};
    ASSERT_NE(pSurfaceInstance, nullptr);
    EXPECT_EQ(pSurfaceInstance->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
    EXPECT_TRUE(pSurfaceInstance->IsDoubleSided());
}

TEST(RadientMaterialAssetManagerTest, CreateGLTFMaterialRejectsInvalidArguments)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    EXPECT_EQ(pMaterialManager->CreateGLTFMaterial(GLTF::Material{}, nullptr, 0, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(pMaterialManager->CreateGLTFMaterial(GLTF::Material{}, nullptr, 1, &pMaterial),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMaterial, nullptr);
}

TEST(RadientMaterialAssetManagerTest, MaterialHandleMayOutliveManager)
{
    const RadientMaterialCreateInfo MaterialCI = MakeTestMaterialCreateInfo();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    {
        RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
        ASSERT_NE(pMaterialManager, nullptr);

        ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
        ASSERT_NE(pMaterial, nullptr);
    }

    // The asset owns its payload, so the material must remain readable after
    // the manager that created it has been destroyed.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    EXPECT_FLOAT_EQ(GetInstanceParameter<Float32>(*pInstance, "RoughnessFactor"),
                    MaterialCI.RoughnessFactor);

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetRenderData(pMaterial).pMaterial;
    ASSERT_NE(pGLTFMaterial, nullptr);
    VerifyTestMaterial(*pGLTFMaterial, MaterialCI);
}

} // namespace
