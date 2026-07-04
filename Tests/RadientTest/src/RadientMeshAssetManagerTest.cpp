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

#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMeshSource.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <memory>

using namespace Diligent;

namespace
{

bool IsAcceptedOrMissingGPU(RADIENT_STATUS Status)
{
    // This unit test does not provide GPU managers, so scheduling may fail
    // after the mesh asset handle has been accepted.
    return (Status == RADIENT_STATUS_OK ||
            Status == RADIENT_STATUS_PENDING ||
            Status == RADIENT_STATUS_INVALID_OPERATION);
}

std::unique_ptr<RadientMeshSource> MakeMeshSource()
{
    // Leave destination vertex attributes unset. RadientMeshAssetManager should
    // resolve the default GLTF layout before computing the mesh cache key.
    static constexpr std::array<RadientFloat3, 3> Positions{
        RadientFloat3{0.f, 0.f, 0.f},
        RadientFloat3{1.f, 0.f, 0.f},
        RadientFloat3{0.f, 1.f, 0.f}};
    static constexpr std::array<Uint32, 3> Indices{0, 1, 2};

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = static_cast<Uint32>(Indices.size());

    RadientMeshCreateInfo MeshCI{};
    MeshCI.pPositions     = Positions.data();
    MeshCI.VertexCount    = static_cast<Uint32>(Positions.size());
    MeshCI.pIndices       = Indices.data();
    MeshCI.IndexCount     = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType      = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives    = &PrimitiveCI;
    MeshCI.PrimitiveCount = 1;

    return std::make_unique<RadientMeshSource>(MeshCI);
}

std::unique_ptr<RadientMeshSource> MakeCustomLayoutMeshSource()
{
    std::unique_ptr<RadientMeshSource> pSource = MakeMeshSource();

    // Preconfigure a custom destination layout. The manager must preserve this
    // layout instead of overwriting it with the default GLTF layout.
    const std::array<GLTF::VertexAttributeDesc, 1> PaddedPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}}};
    EXPECT_EQ(pSource->SetVertexAttributes(PaddedPosition.data(), static_cast<Uint32>(PaddedPosition.size())),
              RADIENT_STATUS_OK);

    return pSource;
}

} // namespace

TEST(RadientMeshAssetManagerTest, CreateMeshAcceptsMeshSource)
{
    // CreateMesh() enqueues asynchronous work, so the test needs at least one
    // worker thread before waiting for all tasks to finish.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    // This mesh uses the manager's default layout fallback.
    RefCntAutoPtr<IRadientMeshAsset> pDefaultMesh;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMesh(*pThreadPool, MakeMeshSource(), &pDefaultMesh)));
    ASSERT_NE(pDefaultMesh, nullptr);

    // These two meshes use the same explicit custom layout and should resolve
    // to the same cached payload.
    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh0;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMesh(*pThreadPool, MakeCustomLayoutMeshSource(), &pCustomMesh0)));
    ASSERT_NE(pCustomMesh0, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh1;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMesh(*pThreadPool, MakeCustomLayoutMeshSource(), &pCustomMesh1)));
    ASSERT_NE(pCustomMesh1, nullptr);

    pThreadPool->WaitForAllTasks();

    const MeshPayloadImpl* pDefaultPayload = RadientMeshAssetManager::GetMeshPayload(pDefaultMesh);
    const MeshPayloadImpl* pCustomPayload0 = RadientMeshAssetManager::GetMeshPayload(pCustomMesh0);
    const MeshPayloadImpl* pCustomPayload1 = RadientMeshAssetManager::GetMeshPayload(pCustomMesh1);

    ASSERT_NE(pDefaultPayload, nullptr);
    ASSERT_NE(pCustomPayload0, nullptr);
    // Default and custom layouts produce different cache keys, while identical
    // custom layouts deduplicate to the same payload.
    EXPECT_NE(pCustomPayload0, pDefaultPayload);
    EXPECT_EQ(pCustomPayload1, pCustomPayload0);

    pThreadPool->StopThreads();
}
