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
#include "Assets/RadientMeshViewSource.hpp"

#include "gtest/gtest.h"

using namespace Diligent;

namespace
{

RadientMeshPrimitiveCreateInfo MakePrimitive(Uint32 FirstIndex = 0,
                                             Uint32 IndexCount = 3)
{
    RadientMeshPrimitiveCreateInfo Primitive{};
    Primitive.FirstIndex = FirstIndex;
    Primitive.IndexCount = IndexCount;
    return Primitive;
}

RadientMeshViewCreateInfo MakeViewCI(const RadientMeshPrimitiveCreateInfo* pPrimitives,
                                     Uint32                                PrimitiveCount)
{
    RadientMeshViewCreateInfo ViewCI{};
    ViewCI.pPrimitives    = pPrimitives;
    ViewCI.PrimitiveCount = PrimitiveCount;
    return ViewCI;
}

RADIENT_STATUS CreateViewStatus(const RadientMeshViewCreateInfo& ViewCI,
                                Uint32                           IndexCount)
{
    return RadientMeshViewSource{ViewCI, IndexCount}.GetStatus();
}

RefCntAutoPtr<IRadientMaterialAsset> CreateMaterial(RadientMaterialAssetManager& MaterialManager,
                                                    const char*                  Name)
{
    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name = Name;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(MaterialManager.CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    EXPECT_NE(pMaterial, nullptr);
    return pMaterial;
}

} // namespace

TEST(RadientMeshViewSourceTest, RejectsInvalidCreateInfo)
{
    RadientMeshPrimitiveCreateInfo  Primitive = MakePrimitive();
    const RadientMeshViewCreateInfo ValidView = MakeViewCI(&Primitive, 1);

    EXPECT_EQ(CreateViewStatus(ValidView, 0), RADIENT_STATUS_INVALID_ARGUMENT);

    EXPECT_EQ(CreateViewStatus(MakeViewCI(nullptr, 1), 3), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(CreateViewStatus(MakeViewCI(&Primitive, 0), 3), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMeshPrimitiveCreateInfo InvalidPrimitive = Primitive;
    InvalidPrimitive.IndexCount                     = 0;
    EXPECT_EQ(CreateViewStatus(MakeViewCI(&InvalidPrimitive, 1), 3), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidPrimitive            = Primitive;
    InvalidPrimitive.FirstIndex = 3;
    EXPECT_EQ(CreateViewStatus(MakeViewCI(&InvalidPrimitive, 1), 3), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidPrimitive            = Primitive;
    InvalidPrimitive.FirstIndex = 2;
    InvalidPrimitive.IndexCount = 2;
    EXPECT_EQ(CreateViewStatus(MakeViewCI(&InvalidPrimitive, 1), 3), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshViewSourceTest, CopiesPrimitiveRangesAndKeepsMaterialsAlive)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateMaterial(*pMaterialManager, "mesh view material");
    ASSERT_NE(pMaterial, nullptr);

    RadientMeshPrimitiveCreateInfo Primitive = MakePrimitive(1, 2);
    Primitive.pMaterial                      = pMaterial;

    RadientMeshViewSource View{MakeViewCI(&Primitive, 1), 3};
    ASSERT_EQ(View.GetStatus(), RADIENT_STATUS_OK);

    Primitive.FirstIndex = 0;
    Primitive.IndexCount = 1;
    Primitive.pMaterial  = nullptr;
    pMaterial.Release();

    ASSERT_EQ(View.GetPrimitiveCount(), 1u);
    EXPECT_EQ(View.GetPrimitive(0).FirstIndex, 1u);
    EXPECT_EQ(View.GetPrimitive(0).IndexCount, 2u);

    IRadientMaterialAsset* pKeptMaterial = View.GetMaterial(0);
    ASSERT_NE(pKeptMaterial, nullptr);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pKeptMaterial), RADIENT_STATUS_OK);
}

TEST(RadientMeshViewSourceTest, CacheKeyIncludesPrimitiveRangesAndMaterials)
{
    static constexpr const char* MeshSourceCacheKey = "mesh-source:test";

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial0 = CreateMaterial(*pMaterialManager, "mesh view material 0");
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial1 = CreateMaterial(*pMaterialManager, "mesh view material 1");
    ASSERT_NE(pMaterial0, nullptr);
    ASSERT_NE(pMaterial1, nullptr);

    RadientMeshPrimitiveCreateInfo Primitive = MakePrimitive();
    Primitive.pMaterial                      = pMaterial0;

    RadientMeshPrimitiveCreateInfo SamePrimitive = MakePrimitive();
    SamePrimitive.pMaterial                      = pMaterial0;

    RadientMeshPrimitiveCreateInfo DifferentRange = MakePrimitive(1, 2);
    DifferentRange.pMaterial                      = pMaterial0;

    RadientMeshPrimitiveCreateInfo DifferentMaterial = MakePrimitive();
    DifferentMaterial.pMaterial                      = pMaterial1;

    RadientMeshViewSource View{MakeViewCI(&Primitive, 1), 3};
    RadientMeshViewSource SameView{MakeViewCI(&SamePrimitive, 1), 3};
    RadientMeshViewSource DifferentRangeView{MakeViewCI(&DifferentRange, 1), 3};
    RadientMeshViewSource DifferentMaterialView{MakeViewCI(&DifferentMaterial, 1), 3};

    const std::string CacheKey = View.MakeCacheKey(MeshSourceCacheKey);
    ASSERT_FALSE(CacheKey.empty());

    EXPECT_EQ(CacheKey, SameView.MakeCacheKey(MeshSourceCacheKey));
    EXPECT_NE(CacheKey, DifferentRangeView.MakeCacheKey(MeshSourceCacheKey));
    EXPECT_NE(CacheKey, DifferentMaterialView.MakeCacheKey(MeshSourceCacheKey));
    EXPECT_NE(CacheKey, View.MakeCacheKey("mesh-source:other"));

    EXPECT_TRUE(View.MakeCacheKey(nullptr).empty());
    EXPECT_TRUE(View.MakeCacheKey("").empty());

    RadientMeshViewSource InvalidView{MakeViewCI(nullptr, 1), 3};
    EXPECT_TRUE(InvalidView.MakeCacheKey(MeshSourceCacheKey).empty());
}
