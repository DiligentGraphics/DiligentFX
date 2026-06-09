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

#include "gtest/gtest.h"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientMeshPrimitives.h"

using namespace Diligent;

namespace
{

RefCntAutoPtr<RadientAssetManagerImpl> CreateAssetManager()
{
    return RadientAssetManagerImpl::Create({});
}

void ExpectValidMeshAsset(IRadientMeshAsset* pMesh)
{
    ASSERT_NE(pMesh, nullptr);
    EXPECT_EQ(pMesh->GetType(), RADIENT_ASSET_TYPE_MESH);
    EXPECT_NE(pMesh->GetReference().URI, nullptr);
    EXPECT_NE(pMesh->GetReference().Version, 0u);
}

} // namespace

TEST(RadientMeshPrimitivesTest, CreateCubeMesh)
{
    // Cube helper should generate a valid mesh asset through the generic asset
    // manager mesh creation path.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Name         = "Test cube";
    CubeCI.Size         = 2.f;
    CubeCI.Subdivisions = 2;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, &pMesh), RADIENT_STATUS_OK);
    ExpectValidMeshAsset(pMesh);
}

TEST(RadientMeshPrimitivesTest, CreateSphereMesh)
{
    // Sphere helper should generate indexed mesh data with positions, normals,
    // and UVs before creating the mesh asset.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientSphereMeshCreateInfo SphereCI{};
    SphereCI.Name         = "Test sphere";
    SphereCI.Radius       = 1.5f;
    SphereCI.Subdivisions = 4;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateRadientSphereMesh(pAssetManager, SphereCI, &pMesh), RADIENT_STATUS_OK);
    ExpectValidMeshAsset(pMesh);
}

TEST(RadientMeshPrimitivesTest, RejectInvalidArguments)
{
    // Primitive helpers should reject missing output pointers, missing asset
    // managers, and invalid dimensions before creating any asset.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Size = 1.f;

    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateRadientCubeMesh(nullptr, CubeCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);

    CubeCI.Size = 0.f;
    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);

    RadientSphereMeshCreateInfo SphereCI{};
    SphereCI.Radius = 0.f;
    EXPECT_EQ(CreateRadientSphereMesh(pAssetManager, SphereCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
}
