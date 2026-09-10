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

#include "Scene/Components/RadientMeshComponentStorage.hpp"
#include "RadientTestAssetHelpers.hpp"

#include <utility>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

TEST(RadientMeshComponentStorageTest, AssignKeepsStrongMeshReference)
{
    // Mesh storage should keep the asset alive even if the caller releases its reference.
    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://heap", 7);

    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh;

    MeshComponentStorage Storage;
    Storage.Assign(Mesh);

    const IRadientMeshAsset* pExpectedMesh = pMesh;
    pMesh.Release();

    EXPECT_EQ(Storage.Component.pMesh, pExpectedMesh);
    ASSERT_NE(Storage.pMesh, nullptr);
    EXPECT_STREQ(Storage.pMesh->GetReference().URI, "mesh://heap");
    EXPECT_EQ(Storage.pMesh->GetReference().Version, 7u);
}

TEST(RadientMeshComponentStorageTest, MoveConstructorRepairsMeshPointer)
{
    // Moving storage must repair the component pointer so it points to the
    // moved-to storage object's retained asset reference.
    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://move", 11);

    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh;

    MeshComponentStorage Source;
    Source.Assign(Mesh);

    MeshComponentStorage Moved{std::move(Source)};

    EXPECT_EQ(Moved.Component.pMesh, pMesh.RawPtr());
    EXPECT_EQ(Moved.Component.pMesh, Moved.pMesh.RawPtr());
    ASSERT_NE(Moved.Component.pMesh, nullptr);
    EXPECT_STREQ(Moved.Component.pMesh->GetReference().URI, "mesh://move");
    EXPECT_EQ(Moved.Component.pMesh->GetReference().Version, 11u);
}

TEST(RadientMeshComponentStorageTest, MoveAssignmentRepairsMeshPointer)
{
    // Move assignment must replace the previous retained asset and reconnect
    // the public component pointer to the destination storage.
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = MakeTestMeshAsset("mesh://assigned", 13);
    RefCntAutoPtr<IRadientMeshAsset> pOtherMesh = MakeTestMeshAsset("mesh://other", 1);

    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh;

    MeshComponentStorage Source;
    Source.Assign(Mesh);

    RadientMeshComponent OtherMesh;
    OtherMesh.pMesh = pOtherMesh;

    MeshComponentStorage Destination;
    Destination.Assign(OtherMesh);
    Destination = std::move(Source);

    EXPECT_EQ(Destination.Component.pMesh, pMesh.RawPtr());
    EXPECT_EQ(Destination.Component.pMesh, Destination.pMesh.RawPtr());
    ASSERT_NE(Destination.Component.pMesh, nullptr);
    EXPECT_STREQ(Destination.Component.pMesh->GetReference().URI, "mesh://assigned");
    EXPECT_EQ(Destination.Component.pMesh->GetReference().Version, 13u);
}

TEST(RadientMeshComponentStorageTest, KeepsNullMesh)
{
    // Null mesh assets are valid for an empty mesh component and should stay
    // null through assign and move operations.
    RadientMeshComponent Mesh;

    MeshComponentStorage Storage;
    Storage.Assign(Mesh);

    EXPECT_EQ(Storage.Component.pMesh, nullptr);
    EXPECT_EQ(Storage.pMesh, nullptr);

    MeshComponentStorage Moved;
    Moved = std::move(Storage);

    EXPECT_EQ(Moved.Component.pMesh, nullptr);
    EXPECT_EQ(Moved.pMesh, nullptr);
}

} // namespace
