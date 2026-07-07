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
#include "Assets/RadientMeshViewSource.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <memory>
#include <vector>

using namespace Diligent;

namespace
{

bool IsAcceptedOrMissingGPU(RADIENT_STATUS Status)
{
    // This unit test does not provide GPU managers. Mesh source loading should
    // still be accepted; GPU availability is reported separately.
    return (Status == RADIENT_STATUS_OK ||
            Status == RADIENT_STATUS_PENDING);
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

    RadientMeshCreateInfo MeshCI{};
    MeshCI.pPositions  = Positions.data();
    MeshCI.VertexCount = static_cast<Uint32>(Positions.size());
    MeshCI.pIndices    = Indices.data();
    MeshCI.IndexCount  = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType   = RADIENT_INDEX_TYPE_UINT32;

    return std::make_unique<RadientMeshSource>(MeshCI);
}

RadientMeshViewCreateInfo MakeMeshView(RadientMeshPrimitiveCreateInfo& PrimitiveCI,
                                       Uint32                          FirstIndex = 0,
                                       Uint32                          IndexCount = 3)
{
    PrimitiveCI.FirstIndex = FirstIndex;
    PrimitiveCI.IndexCount = IndexCount;

    RadientMeshViewCreateInfo ViewCI{};
    ViewCI.pPrimitives    = &PrimitiveCI;
    ViewCI.PrimitiveCount = 1;
    return ViewCI;
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

RADIENT_STATUS CreateMeshGPUDataHandle(RadientMeshAssetManager&            MeshManager,
                                       IThreadPool&                        ThreadPool,
                                       std::unique_ptr<RadientMeshSource>  pMeshSource,
                                       RefCntAutoPtr<IRadientMeshGPUData>& pGPUData)
{
    IRadientMeshGPUData* pRawGPUData = nullptr;
    const RADIENT_STATUS Status      = MeshManager.CreateMeshGPUData(ThreadPool, std::move(pMeshSource), &pRawGPUData);
    pGPUData.Attach(pRawGPUData);
    return Status;
}

RADIENT_STATUS CreateMeshViewFromSource(RadientMeshAssetManager&            MeshManager,
                                        IThreadPool&                        ThreadPool,
                                        std::unique_ptr<RadientMeshSource>  pMeshSource,
                                        const RadientMeshViewCreateInfo&    ViewCI,
                                        RefCntAutoPtr<IRadientMeshAsset>&   pMesh,
                                        RefCntAutoPtr<IRadientMeshGPUData>* pCreatedGPUData = nullptr)
{
    RefCntAutoPtr<IRadientMeshGPUData> pGPUData;
    const RADIENT_STATUS               Status = CreateMeshGPUDataHandle(MeshManager, ThreadPool, std::move(pMeshSource), pGPUData);
    if (RADIENT_FAILED(Status) || pGPUData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    if (pCreatedGPUData != nullptr)
        *pCreatedGPUData = pGPUData;

    IRadientMeshGPUData* const pGPUDataArray[] = {pGPUData.RawPtr()};
    return MeshManager.CreateMeshView(pGPUDataArray, 1, ViewCI, &pMesh);
}

} // namespace

TEST(RadientMeshAssetManagerTest, CreateMeshGPUDataAcceptsMeshSource)
{
    // Mesh GPU data creation enqueues asynchronous work, so the test needs at
    // least one worker thread before waiting for all tasks to finish.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    // This mesh uses the manager's default layout fallback.
    RadientMeshPrimitiveCreateInfo   DefaultPrimitive{};
    const RadientMeshViewCreateInfo  DefaultView = MakeMeshView(DefaultPrimitive);
    RefCntAutoPtr<IRadientMeshAsset> pDefaultMesh;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSource(), DefaultView, pDefaultMesh)));
    ASSERT_NE(pDefaultMesh, nullptr);

    // These two meshes use the same explicit custom layout and should resolve
    // to the same cached payload.
    RadientMeshPrimitiveCreateInfo   CustomPrimitive0{};
    const RadientMeshViewCreateInfo  CustomView0 = MakeMeshView(CustomPrimitive0);
    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh0;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSource(), CustomView0, pCustomMesh0)));
    ASSERT_NE(pCustomMesh0, nullptr);

    RadientMeshPrimitiveCreateInfo   CustomPrimitive1{};
    const RadientMeshViewCreateInfo  CustomView1 = MakeMeshView(CustomPrimitive1);
    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh1;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSource(), CustomView1, pCustomMesh1)));
    ASSERT_NE(pCustomMesh1, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pDefaultMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pDefaultMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pCustomMesh0), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pCustomMesh0), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pCustomMesh1), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pCustomMesh1), RADIENT_STATUS_NO_GPU_DATA);

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

TEST(RadientMeshAssetManagerTest, CreateMeshViewAcceptsPrecreatedGPUData)
{
    // This exercises the split path used by GLTF planning: geometry data is
    // created once, while multiple mesh views can reference it independently.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RefCntAutoPtr<IRadientMeshGPUData> pGPUData;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGPUDataHandle(*pMeshManager, *pThreadPool, MakeMeshSource(), pGPUData)));
    ASSERT_NE(pGPUData, nullptr);

    RadientMeshPrimitiveCreateInfo   WholePrimitive{};
    const RadientMeshViewCreateInfo  WholeView = MakeMeshView(WholePrimitive, 0, 3);
    RefCntAutoPtr<IRadientMeshAsset> pWholeMesh;
    IRadientMeshGPUData* const       pGPUDataArray[] = {pGPUData.RawPtr()};
    EXPECT_EQ(pMeshManager->CreateMeshView(pGPUDataArray, 1, WholeView, &pWholeMesh), RADIENT_STATUS_OK);
    ASSERT_NE(pWholeMesh, nullptr);

    RadientMeshPrimitiveCreateInfo   SubrangePrimitive{};
    const RadientMeshViewCreateInfo  SubrangeView = MakeMeshView(SubrangePrimitive, 1, 2);
    RefCntAutoPtr<IRadientMeshAsset> pSubrangeMesh;
    EXPECT_EQ(pMeshManager->CreateMeshView(pGPUDataArray, 1, SubrangeView, &pSubrangeMesh), RADIENT_STATUS_OK);
    ASSERT_NE(pSubrangeMesh, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pWholeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pWholeMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pSubrangeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pSubrangeMesh), RADIENT_STATUS_NO_GPU_DATA);

    EXPECT_EQ(RadientMeshAssetManager::GetMeshGPUData(pWholeMesh), pGPUData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGPUData(pSubrangeMesh), pGPUData.RawPtr());
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pWholeMesh),
              RadientMeshAssetManager::GetMeshPayload(pSubrangeMesh));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, DifferentPrimitiveViewsShareGPUData)
{
    // Mesh views should cache separately from uploaded geometry. Two meshes
    // may use different primitive ranges while sharing vertex/index data.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RadientMeshPrimitiveCreateInfo     WholePrimitive{};
    const RadientMeshViewCreateInfo    WholeView = MakeMeshView(WholePrimitive, 0, 3);
    RefCntAutoPtr<IRadientMeshAsset>   pWholeMesh;
    RefCntAutoPtr<IRadientMeshGPUData> pGPUData;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSource(), WholeView, pWholeMesh, std::addressof(pGPUData))));
    ASSERT_NE(pWholeMesh, nullptr);
    ASSERT_NE(pGPUData, nullptr);

    RadientMeshPrimitiveCreateInfo   SubrangePrimitive{};
    const RadientMeshViewCreateInfo  SubrangeView = MakeMeshView(SubrangePrimitive, 1, 2);
    RefCntAutoPtr<IRadientMeshAsset> pSubrangeMesh;
    IRadientMeshGPUData* const       pGPUDataArray[] = {pGPUData.RawPtr()};
    EXPECT_EQ(pMeshManager->CreateMeshView(pGPUDataArray, 1, SubrangeView, &pSubrangeMesh), RADIENT_STATUS_OK);
    ASSERT_NE(pSubrangeMesh, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pWholeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pWholeMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pSubrangeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pSubrangeMesh), RADIENT_STATUS_NO_GPU_DATA);

    const MeshPayloadImpl* pWholePayload    = RadientMeshAssetManager::GetMeshPayload(pWholeMesh);
    const MeshPayloadImpl* pSubrangePayload = RadientMeshAssetManager::GetMeshPayload(pSubrangeMesh);
    ASSERT_NE(pWholePayload, nullptr);
    ASSERT_NE(pSubrangePayload, nullptr);

    EXPECT_NE(pWholePayload, pSubrangePayload);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGPUData(pWholeMesh),
              RadientMeshAssetManager::GetMeshGPUData(pSubrangeMesh));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, CreateMeshAcceptsMultipleGeometrySources)
{
    // A single drawable mesh view may reference multiple geometry sources. This
    // is the shape needed by GLTF imports where one logical mesh can contain
    // primitives backed by different vertex layouts.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RefCntAutoPtr<IRadientMeshGPUData> pDefaultGPUData;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGPUDataHandle(*pMeshManager, *pThreadPool, MakeMeshSource(), pDefaultGPUData)));
    ASSERT_NE(pDefaultGPUData, nullptr);

    RefCntAutoPtr<IRadientMeshGPUData> pCustomGPUData;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGPUDataHandle(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSource(), pCustomGPUData)));
    ASSERT_NE(pCustomGPUData, nullptr);

    std::array<RadientMeshPrimitiveCreateInfo, 2> Primitives{};
    Primitives[0].FirstIndex = 0;
    Primitives[0].IndexCount = 3;
    Primitives[1].FirstIndex = 0;
    Primitives[1].IndexCount = 3;

    const std::array<Uint32, 2> GeometryIndices{0, 1};

    RadientMeshViewCreateInfo ViewCI{};
    ViewCI.pPrimitives      = Primitives.data();
    ViewCI.PrimitiveCount   = static_cast<Uint32>(Primitives.size());
    ViewCI.pGeometryIndices = GeometryIndices.data();

    RefCntAutoPtr<IRadientMeshAsset>          pMesh;
    const std::array<IRadientMeshGPUData*, 2> MeshGPUData{
        pDefaultGPUData.RawPtr(),
        pCustomGPUData.RawPtr()};
    EXPECT_EQ(pMeshManager->CreateMeshView(MeshGPUData.data(), static_cast<Uint32>(MeshGPUData.size()), ViewCI, &pMesh), RADIENT_STATUS_OK);
    ASSERT_NE(pMesh, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh), nullptr);

    pThreadPool->StopThreads();
}
