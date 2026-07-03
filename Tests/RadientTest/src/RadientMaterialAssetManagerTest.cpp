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

#include "gtest/gtest.h"

using namespace Diligent;

namespace
{

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

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetMaterial(pMaterial);
    ASSERT_NE(pGLTFMaterial, nullptr);

    VerifyTestMaterial(*pGLTFMaterial, MaterialCI);
}

TEST(RadientMaterialAssetManagerTest, CreateMaterialRejectsNullOutput)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    EXPECT_EQ(pMaterialManager->CreateMaterial(MakeTestMaterialCreateInfo(), nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);
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

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetMaterial(pMaterial);
    ASSERT_NE(pGLTFMaterial, nullptr);
    VerifyTestMaterial(*pGLTFMaterial, MaterialCI);
}

} // namespace
