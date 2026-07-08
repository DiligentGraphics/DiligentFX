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

#include "../../../Radient/src/Assets/RadientMeshViewCreateInfoSnapshot.hpp"

#include "Assets/RadientMaterialAssetManager.hpp"

#include "gtest/gtest.h"

#include <array>
#include <string>
#include <utility>

using namespace Diligent;

namespace
{

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

RadientMeshViewCreateInfo MakeViewCI(const RadientMeshPrimitiveCreateInfo* pPrimitives,
                                     Uint32                                PrimitiveCount,
                                     const Uint32*                         pGeometryIndices)
{
    RadientMeshViewCreateInfo ViewCI{};
    ViewCI.pPrimitives      = pPrimitives;
    ViewCI.PrimitiveCount   = PrimitiveCount;
    ViewCI.pGeometryIndices = pGeometryIndices;
    return ViewCI;
}

} // namespace

TEST(RadientMeshViewCreateInfoSnapshotTest, CopiesPrimitiveGeometryNameAndMaterialArrays)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial0 = CreateMaterial(*pMaterialManager, "snapshot material 0");
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial1 = CreateMaterial(*pMaterialManager, "snapshot material 1");
    ASSERT_NE(pMaterial0, nullptr);
    ASSERT_NE(pMaterial1, nullptr);

    IRadientMaterialAsset* const pRawMaterial0 = pMaterial0.RawPtr();
    IRadientMaterialAsset* const pRawMaterial1 = pMaterial1.RawPtr();

    std::array<std::string, 2> PrimitiveNames{
        "snapshot primitive 0",
        "snapshot primitive 1"};

    std::array<RadientMeshPrimitiveCreateInfo, 2> Primitives{};
    Primitives[0].Name       = PrimitiveNames[0].c_str();
    Primitives[0].FirstIndex = 1;
    Primitives[0].IndexCount = 2;
    Primitives[0].pMaterial  = pMaterial0;
    Primitives[1].Name       = PrimitiveNames[1].c_str();
    Primitives[1].FirstIndex = 3;
    Primitives[1].IndexCount = 4;
    Primitives[1].pMaterial  = pMaterial1;

    std::array<Uint32, 2> GeometryIndices{1, 0};

    MeshViewCreateInfoSnapshot Snapshot{
        MakeViewCI(Primitives.data(), static_cast<Uint32>(Primitives.size()), GeometryIndices.data())};
    ASSERT_EQ(Snapshot.GetStatus(), RADIENT_STATUS_OK);

    PrimitiveNames[0] = "mutated primitive 0";
    PrimitiveNames[1] = "mutated primitive 1";
    Primitives        = {};
    GeometryIndices   = {7, 8};
    pMaterial0.Release();
    pMaterial1.Release();
    pMaterialManager.reset();

    const RadientMeshViewCreateInfo CopiedCI = Snapshot.GetCreateInfo();
    ASSERT_EQ(CopiedCI.PrimitiveCount, 2u);
    ASSERT_NE(CopiedCI.pPrimitives, nullptr);
    ASSERT_NE(CopiedCI.pGeometryIndices, nullptr);

    EXPECT_EQ(CopiedCI.pGeometryIndices[0], 1u);
    EXPECT_EQ(CopiedCI.pGeometryIndices[1], 0u);

    EXPECT_EQ(CopiedCI.pPrimitives[0].FirstIndex, 1u);
    EXPECT_EQ(CopiedCI.pPrimitives[0].IndexCount, 2u);
    ASSERT_NE(CopiedCI.pPrimitives[0].Name, nullptr);
    EXPECT_STREQ(CopiedCI.pPrimitives[0].Name, "snapshot primitive 0");
    EXPECT_NE(CopiedCI.pPrimitives[0].Name, PrimitiveNames[0].c_str());
    EXPECT_EQ(CopiedCI.pPrimitives[0].pMaterial, pRawMaterial0);

    EXPECT_EQ(CopiedCI.pPrimitives[1].FirstIndex, 3u);
    EXPECT_EQ(CopiedCI.pPrimitives[1].IndexCount, 4u);
    ASSERT_NE(CopiedCI.pPrimitives[1].Name, nullptr);
    EXPECT_STREQ(CopiedCI.pPrimitives[1].Name, "snapshot primitive 1");
    EXPECT_NE(CopiedCI.pPrimitives[1].Name, PrimitiveNames[1].c_str());
    EXPECT_EQ(CopiedCI.pPrimitives[1].pMaterial, pRawMaterial1);

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(CopiedCI.pPrimitives[0].pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(CopiedCI.pPrimitives[1].pMaterial), RADIENT_STATUS_OK);
}

TEST(RadientMeshViewCreateInfoSnapshotTest, MoveConstructorKeepsCopiedArraysUsable)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateMaterial(*pMaterialManager, "snapshot move material");
    ASSERT_NE(pMaterial, nullptr);

    IRadientMaterialAsset* const pRawMaterial = pMaterial.RawPtr();

    std::string PrimitiveName = "snapshot moved primitive";

    RadientMeshPrimitiveCreateInfo Primitive{};
    Primitive.Name       = PrimitiveName.c_str();
    Primitive.FirstIndex = 2;
    Primitive.IndexCount = 5;
    Primitive.pMaterial  = pMaterial;

    Uint32 GeometryIndex = 3;

    MeshViewCreateInfoSnapshot Snapshot{MakeViewCI(&Primitive, 1, &GeometryIndex)};
    ASSERT_EQ(Snapshot.GetStatus(), RADIENT_STATUS_OK);

    MeshViewCreateInfoSnapshot MovedSnapshot{std::move(Snapshot)};

    PrimitiveName = "mutated moved primitive";
    Primitive     = {};
    GeometryIndex = 9;
    pMaterial.Release();
    pMaterialManager.reset();

    const RadientMeshViewCreateInfo MovedCI = MovedSnapshot.GetCreateInfo();
    ASSERT_EQ(MovedCI.PrimitiveCount, 1u);
    ASSERT_NE(MovedCI.pPrimitives, nullptr);
    ASSERT_NE(MovedCI.pGeometryIndices, nullptr);

    EXPECT_EQ(MovedCI.pGeometryIndices[0], 3u);
    EXPECT_EQ(MovedCI.pPrimitives[0].FirstIndex, 2u);
    EXPECT_EQ(MovedCI.pPrimitives[0].IndexCount, 5u);
    ASSERT_NE(MovedCI.pPrimitives[0].Name, nullptr);
    EXPECT_STREQ(MovedCI.pPrimitives[0].Name, "snapshot moved primitive");
    EXPECT_NE(MovedCI.pPrimitives[0].Name, PrimitiveName.c_str());
    EXPECT_EQ(MovedCI.pPrimitives[0].pMaterial, pRawMaterial);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(MovedCI.pPrimitives[0].pMaterial), RADIENT_STATUS_OK);
}
