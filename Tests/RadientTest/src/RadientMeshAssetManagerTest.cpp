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
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "Assets/RadientMeshViewSource.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "TestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
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

void DrainThreadPool(IThreadPool& ThreadPool)
{
    // The zero-thread pool gives the test deterministic scheduling. A task may
    // be requeued if its prerequisites are not ready yet, so drain with a guard
    // instead of assuming a fixed number of ProcessTask() calls.
    for (Uint32 Iteration = 0; ThreadPool.GetQueueSize() != 0 && Iteration < 16; ++Iteration)
    {
        ASSERT_TRUE(ThreadPool.ProcessTask(0, false));
    }
    EXPECT_EQ(ThreadPool.GetQueueSize(), 0u);
}

struct MeshSources
{
    std::unique_ptr<RadientMeshVertexSource> pVertexSource;
    std::unique_ptr<RadientMeshIndexSource>  pIndexSource;
};

MeshSources MakeMeshSources(std::array<Uint32, 3> Indices = {0, 1, 2})
{
    // Leave destination vertex attributes unset. RadientMeshAssetManager should
    // resolve the default GLTF layout before computing the mesh cache key.
    static constexpr std::array<RadientFloat3, 3> Positions{
        RadientFloat3{0.f, 0.f, 0.f},
        RadientFloat3{1.f, 0.f, 0.f},
        RadientFloat3{0.f, 1.f, 0.f}};
    RadientMeshCreateInfo MeshCI{};
    MeshCI.pPositions  = Positions.data();
    MeshCI.VertexCount = static_cast<Uint32>(Positions.size());
    MeshCI.pIndices    = Indices.data();
    MeshCI.IndexCount  = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType   = RADIENT_INDEX_TYPE_UINT32;

    MeshSources Sources;
    Sources.pVertexSource = std::make_unique<RadientMeshVertexSource>(MeshCI);
    Sources.pIndexSource  = std::make_unique<RadientMeshIndexSource>(MeshCI);
    return Sources;
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

MeshSources MakeCustomLayoutMeshSources()
{
    MeshSources Sources = MakeMeshSources();

    // Preconfigure a custom destination layout. The manager must preserve this
    // layout instead of overwriting it with the default GLTF layout.
    const std::array<GLTF::VertexAttributeDesc, 1> PaddedPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}}};
    EXPECT_EQ(Sources.pVertexSource->SetVertexAttributes(PaddedPosition.data(), static_cast<Uint32>(PaddedPosition.size())),
              RADIENT_STATUS_OK);

    return Sources;
}

struct MeshGeometryHandles
{
    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    RefCntAutoPtr<IRadientMeshIndexData>  pIndexData;
};

RADIENT_STATUS CreateMeshVertexDataHandle(RadientMeshAssetManager&                 MeshManager,
                                          IThreadPool&                             ThreadPool,
                                          std::unique_ptr<RadientMeshVertexSource> pVertexSource,
                                          RefCntAutoPtr<IRadientMeshVertexData>&   pVertexData)
{
    IRadientMeshVertexData* pRawVertexData = nullptr;
    const RADIENT_STATUS    Status         = MeshManager.CreateMeshVertexData(
        ThreadPool,
        std::move(pVertexSource),
        &pRawVertexData);
    pVertexData.Attach(pRawVertexData);
    return Status;
}

RADIENT_STATUS CreateMeshIndexDataHandle(RadientMeshAssetManager&                MeshManager,
                                         IThreadPool&                            ThreadPool,
                                         std::unique_ptr<RadientMeshIndexSource> pIndexSource,
                                         RefCntAutoPtr<IRadientMeshIndexData>&   pIndexData)
{
    IRadientMeshIndexData* pRawIndexData = nullptr;
    const RADIENT_STATUS   Status        = MeshManager.CreateMeshIndexData(
        ThreadPool,
        std::move(pIndexSource),
        &pRawIndexData);
    pIndexData.Attach(pRawIndexData);
    return Status;
}

RADIENT_STATUS CreateMeshGeometryData(RadientMeshAssetManager& MeshManager,
                                      IThreadPool&             ThreadPool,
                                      MeshSources              Sources,
                                      MeshGeometryHandles&     Geometry)
{
    RADIENT_STATUS Status = CreateMeshVertexDataHandle(MeshManager,
                                                       ThreadPool,
                                                       std::move(Sources.pVertexSource),
                                                       Geometry.pVertexData);
    if (RADIENT_FAILED(Status) || Geometry.pVertexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    Status = CreateMeshIndexDataHandle(MeshManager,
                                       ThreadPool,
                                       std::move(Sources.pIndexSource),
                                       Geometry.pIndexData);
    if (RADIENT_FAILED(Status) || Geometry.pIndexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    return Status;
}

RadientMeshGeometryData MakeGeometryData(const MeshGeometryHandles& Geometry)
{
    return RadientMeshGeometryData{
        Geometry.pVertexData.RawPtr(),
        Geometry.pIndexData.RawPtr()};
}

RADIENT_STATUS CreateMeshViewFromSource(RadientMeshAssetManager&          MeshManager,
                                        IThreadPool&                      ThreadPool,
                                        MeshSources                       Sources,
                                        const RadientMeshViewCreateInfo&  ViewCI,
                                        RefCntAutoPtr<IRadientMeshAsset>& pMesh,
                                        MeshGeometryHandles*              pCreatedGeometry = nullptr)
{
    MeshGeometryHandles  Geometry;
    const RADIENT_STATUS Status = CreateMeshGeometryData(MeshManager, ThreadPool, std::move(Sources), Geometry);
    if (RADIENT_FAILED(Status))
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

    if (pCreatedGeometry != nullptr)
        *pCreatedGeometry = Geometry;

    const RadientMeshGeometryData GeometryData = MakeGeometryData(Geometry);
    return MeshManager.CreateMeshView(ThreadPool, &GeometryData, 1, ViewCI, &pMesh);
}

} // namespace

TEST(RadientMeshAssetManagerTest, CreateMeshDataAcceptsVertexAndIndexSources)
{
    // Mesh data creation enqueues asynchronous work, so the test needs at
    // least one worker thread before waiting for all tasks to finish.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    // This mesh uses the manager's default layout fallback.
    RadientMeshPrimitiveCreateInfo   DefaultPrimitive{};
    const RadientMeshViewCreateInfo  DefaultView = MakeMeshView(DefaultPrimitive);
    RefCntAutoPtr<IRadientMeshAsset> pDefaultMesh;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSources(), DefaultView, pDefaultMesh)));
    ASSERT_NE(pDefaultMesh, nullptr);

    // These two meshes use the same explicit custom layout and should resolve
    // to the same cached payload.
    RadientMeshPrimitiveCreateInfo   CustomPrimitive0{};
    const RadientMeshViewCreateInfo  CustomView0 = MakeMeshView(CustomPrimitive0);
    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh0;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSources(), CustomView0, pCustomMesh0)));
    ASSERT_NE(pCustomMesh0, nullptr);

    RadientMeshPrimitiveCreateInfo   CustomPrimitive1{};
    const RadientMeshViewCreateInfo  CustomView1 = MakeMeshView(CustomPrimitive1);
    RefCntAutoPtr<IRadientMeshAsset> pCustomMesh1;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSources(), CustomView1, pCustomMesh1)));
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
    EXPECT_NE(RadientMeshAssetManager::GetMeshVertexDataPayload(pCustomMesh0, 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(pDefaultMesh, 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(pCustomMesh0, 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(pDefaultMesh, 0));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, ConcurrentIdenticalSourcesSharePayloads)
{
    // A storm of identical source requests should converge on one cached
    // vertex payload and one cached index payload. This is CPU-only: without
    // GPU managers, the geometry load succeeds and reports NO_GPU_DATA.
    constexpr Uint32 ThreadCount = 32;

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{ThreadCount});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    std::vector<MeshGeometryHandles> Geometries(ThreadCount);
    std::vector<RADIENT_STATUS>      VertexStatuses(ThreadCount, RADIENT_STATUS_INVALID_OPERATION);
    std::vector<RADIENT_STATUS>      IndexStatuses(ThreadCount, RADIENT_STATUS_INVALID_OPERATION);
    std::vector<std::thread>         Threads;
    Threads.reserve(ThreadCount);

    Threading::Signal   StartSignal;
    std::atomic<Uint32> ReadyCount{0};

    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back(
            [pMeshManager, pThreadPool, &Geometries, &VertexStatuses, &IndexStatuses, &StartSignal, &ReadyCount, ThreadIndex]() //
            {
                MeshSources Sources = MakeMeshSources();

                ReadyCount.fetch_add(1, std::memory_order_release);
                StartSignal.Wait(true, ThreadCount);

                VertexStatuses[ThreadIndex] = CreateMeshVertexDataHandle(*pMeshManager,
                                                                         *pThreadPool,
                                                                         std::move(Sources.pVertexSource),
                                                                         Geometries[ThreadIndex].pVertexData);

                IndexStatuses[ThreadIndex] = CreateMeshIndexDataHandle(*pMeshManager,
                                                                       *pThreadPool,
                                                                       std::move(Sources.pIndexSource),
                                                                       Geometries[ThreadIndex].pIndexData);
            });
    }

    while (ReadyCount.load(std::memory_order_acquire) != ThreadCount)
        std::this_thread::yield();

    StartSignal.Trigger(true);

    for (std::thread& Thread : Threads)
        Thread.join();

    pThreadPool->WaitForAllTasks();

    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        EXPECT_TRUE(IsAcceptedOrMissingGPU(VertexStatuses[ThreadIndex]));
        EXPECT_TRUE(IsAcceptedOrMissingGPU(IndexStatuses[ThreadIndex]));
        ASSERT_NE(Geometries[ThreadIndex].pVertexData, nullptr);
        ASSERT_NE(Geometries[ThreadIndex].pIndexData, nullptr);
        EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Geometries[ThreadIndex].pVertexData), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Geometries[ThreadIndex].pIndexData), RADIENT_STATUS_OK);
    }

    std::vector<RefCntAutoPtr<IRadientMeshAsset>> Meshes(ThreadCount);
    for (Uint32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        RadientMeshPrimitiveCreateInfo  Primitive{};
        const RadientMeshViewCreateInfo ViewCI       = MakeMeshView(Primitive);
        const RadientMeshGeometryData   GeometryData = MakeGeometryData(Geometries[ThreadIndex]);
        EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, ViewCI, &Meshes[ThreadIndex])));
        ASSERT_NE(Meshes[ThreadIndex], nullptr);
    }

    pThreadPool->WaitForAllTasks();

    ASSERT_NE(Meshes[0], nullptr);
    const MeshVertexDataPayloadImpl* const pVertexPayload = RadientMeshAssetManager::GetMeshVertexDataPayload(Meshes[0], 0);
    const MeshIndexDataPayloadImpl* const  pIndexPayload  = RadientMeshAssetManager::GetMeshIndexDataPayload(Meshes[0], 0);
    ASSERT_NE(pVertexPayload, nullptr);
    ASSERT_NE(pIndexPayload, nullptr);

    for (const RefCntAutoPtr<IRadientMeshAsset>& pMesh : Meshes)
    {
        ASSERT_NE(pMesh, nullptr);
        EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);
        EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 0), pVertexPayload);
        EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 0), pIndexPayload);
    }

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, CreateMeshDataFailsWhenThreadPoolIsStopped)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);
    pThreadPool->StopThreads();

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshSources Sources = MakeMeshSources();

    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    {
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(CreateMeshVertexDataHandle(*pMeshManager,
                                             *pThreadPool,
                                             std::move(Sources.pVertexSource),
                                             pVertexData),
                  RADIENT_STATUS_INVALID_OPERATION);
    }
    ASSERT_NE(pVertexData, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pVertexData), RADIENT_STATUS_INVALID_OPERATION);

    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;
    {
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(CreateMeshIndexDataHandle(*pMeshManager,
                                            *pThreadPool,
                                            std::move(Sources.pIndexSource),
                                            pIndexData),
                  RADIENT_STATUS_INVALID_OPERATION);
    }
    ASSERT_NE(pIndexData, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pIndexData), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientMeshAssetManagerTest, CreateMeshViewAcceptsPrecreatedGeometryData)
{
    // This exercises the split path used by GLTF planning: geometry data is
    // created once, while multiple mesh views can reference it independently.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshGeometryHandles Geometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), Geometry)));
    ASSERT_NE(Geometry.pVertexData, nullptr);
    ASSERT_NE(Geometry.pIndexData, nullptr);
    const RadientMeshGeometryData GeometryData = MakeGeometryData(Geometry);

    RadientMeshPrimitiveCreateInfo   WholePrimitive{};
    const RadientMeshViewCreateInfo  WholeView = MakeMeshView(WholePrimitive, 0, 3);
    RefCntAutoPtr<IRadientMeshAsset> pWholeMesh;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, WholeView, &pWholeMesh)));
    ASSERT_NE(pWholeMesh, nullptr);

    RadientMeshPrimitiveCreateInfo   SubrangePrimitive{};
    const RadientMeshViewCreateInfo  SubrangeView = MakeMeshView(SubrangePrimitive, 1, 2);
    RefCntAutoPtr<IRadientMeshAsset> pSubrangeMesh;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, SubrangeView, &pSubrangeMesh)));
    ASSERT_NE(pSubrangeMesh, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pWholeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pWholeMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pSubrangeMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pSubrangeMesh), RADIENT_STATUS_NO_GPU_DATA);

    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pWholeMesh), Geometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pWholeMesh), Geometry.pIndexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pSubrangeMesh), Geometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pSubrangeMesh), Geometry.pIndexData.RawPtr());
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pWholeMesh),
              RadientMeshAssetManager::GetMeshPayload(pSubrangeMesh));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, CreateMeshViewFailsWhenThreadPoolIsStopped)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshGeometryHandles Geometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), Geometry)));
    ASSERT_NE(Geometry.pVertexData, nullptr);
    ASSERT_NE(Geometry.pIndexData, nullptr);

    pThreadPool->WaitForAllTasks();
    pThreadPool->StopThreads();

    const RadientMeshGeometryData   GeometryData = MakeGeometryData(Geometry);
    RadientMeshPrimitiveCreateInfo  Primitive{};
    const RadientMeshViewCreateInfo ViewCI = MakeMeshView(Primitive);

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    {
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, ViewCI, &pMesh),
                  RADIENT_STATUS_INVALID_OPERATION);
    }

    ASSERT_NE(pMesh, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientMeshAssetManagerTest, DifferentPrimitiveViewsShareGeometryData)
{
    // Mesh views should cache separately from uploaded geometry. Two meshes
    // may use different primitive ranges while sharing vertex/index data.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RadientMeshPrimitiveCreateInfo   WholePrimitive{};
    const RadientMeshViewCreateInfo  WholeView = MakeMeshView(WholePrimitive, 0, 3);
    RefCntAutoPtr<IRadientMeshAsset> pWholeMesh;
    MeshGeometryHandles              Geometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSources(), WholeView, pWholeMesh, std::addressof(Geometry))));
    ASSERT_NE(pWholeMesh, nullptr);
    ASSERT_NE(Geometry.pVertexData, nullptr);
    ASSERT_NE(Geometry.pIndexData, nullptr);

    RadientMeshPrimitiveCreateInfo   SubrangePrimitive{};
    const RadientMeshViewCreateInfo  SubrangeView = MakeMeshView(SubrangePrimitive, 1, 2);
    RefCntAutoPtr<IRadientMeshAsset> pSubrangeMesh;
    const RadientMeshGeometryData    GeometryData = MakeGeometryData(Geometry);
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, SubrangeView, &pSubrangeMesh)));
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
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pWholeMesh),
              RadientMeshAssetManager::GetMeshVertexData(pSubrangeMesh));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pWholeMesh),
              RadientMeshAssetManager::GetMeshIndexData(pSubrangeMesh));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, MeshViewsCanShareVertexDataWithDifferentIndexData)
{
    // Vertex and index data are independent assets. Two geometries can reuse
    // the same vertex upload while drawing through different index buffers.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshSources SourcesA = MakeMeshSources({0, 1, 2});
    MeshSources SourcesB = MakeMeshSources({0, 2, 1});

    RefCntAutoPtr<IRadientMeshVertexData> pSharedVertexData;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshVertexDataHandle(*pMeshManager,
                                                                  *pThreadPool,
                                                                  std::move(SourcesA.pVertexSource),
                                                                  pSharedVertexData)));
    ASSERT_NE(pSharedVertexData, nullptr);

    RefCntAutoPtr<IRadientMeshIndexData> pIndexDataA;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshIndexDataHandle(*pMeshManager,
                                                                 *pThreadPool,
                                                                 std::move(SourcesA.pIndexSource),
                                                                 pIndexDataA)));
    ASSERT_NE(pIndexDataA, nullptr);

    RefCntAutoPtr<IRadientMeshIndexData> pIndexDataB;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshIndexDataHandle(*pMeshManager,
                                                                 *pThreadPool,
                                                                 std::move(SourcesB.pIndexSource),
                                                                 pIndexDataB)));
    ASSERT_NE(pIndexDataB, nullptr);

    RadientMeshPrimitiveCreateInfo  PrimitiveA{};
    const RadientMeshViewCreateInfo ViewA = MakeMeshView(PrimitiveA);
    const RadientMeshGeometryData   GeometryA{
        pSharedVertexData.RawPtr(),
        pIndexDataA.RawPtr()};

    RefCntAutoPtr<IRadientMeshAsset> pMeshA;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryA, 1, ViewA, &pMeshA)));
    ASSERT_NE(pMeshA, nullptr);

    RadientMeshPrimitiveCreateInfo  PrimitiveB{};
    const RadientMeshViewCreateInfo ViewB = MakeMeshView(PrimitiveB);
    const RadientMeshGeometryData   GeometryB{
        pSharedVertexData.RawPtr(),
        pIndexDataB.RawPtr()};

    RefCntAutoPtr<IRadientMeshAsset> pMeshB;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryB, 1, ViewB, &pMeshB)));
    ASSERT_NE(pMeshB, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshA), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshB), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMeshA), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMeshB), RADIENT_STATUS_NO_GPU_DATA);

    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshA), pSharedVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshB), pSharedVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshA), pIndexDataA.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshB), pIndexDataB.RawPtr());
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMeshA),
              RadientMeshAssetManager::GetMeshPayload(pMeshB));

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

    MeshGeometryHandles DefaultGeometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), DefaultGeometry)));
    ASSERT_NE(DefaultGeometry.pVertexData, nullptr);
    ASSERT_NE(DefaultGeometry.pIndexData, nullptr);

    MeshGeometryHandles CustomGeometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeCustomLayoutMeshSources(), CustomGeometry)));
    ASSERT_NE(CustomGeometry.pVertexData, nullptr);
    ASSERT_NE(CustomGeometry.pIndexData, nullptr);

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

    RefCntAutoPtr<IRadientMeshAsset>             pMesh;
    const std::array<RadientMeshGeometryData, 2> GeometryData{
        MakeGeometryData(DefaultGeometry),
        MakeGeometryData(CustomGeometry)};
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, GeometryData.data(), static_cast<Uint32>(GeometryData.size()), ViewCI, &pMesh)));
    ASSERT_NE(pMesh, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh), nullptr);

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, MeshViewCacheUsesCanonicalGeometryPayload)
{
    // Unused geometries are intentionally ignored by the mesh-view cache key.
    // The cached payload must use the same compact representation, otherwise
    // public geometry counts and readiness depend on which equivalent request
    // wins the cache race.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshGeometryHandles UsedGeometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), UsedGeometry)));
    ASSERT_NE(UsedGeometry.pVertexData, nullptr);
    ASSERT_NE(UsedGeometry.pIndexData, nullptr);

    pThreadPool->WaitForAllTasks();

    RadientMeshPrimitiveCreateInfo  PrimitiveWithUnusedGeometry{};
    const RadientMeshViewCreateInfo ViewWithUnusedGeometry = MakeMeshView(PrimitiveWithUnusedGeometry);
    const std::array<Uint32, 1>     GeometryIndexOne{1};

    RadientMeshViewCreateInfo ViewA = ViewWithUnusedGeometry;
    ViewA.pGeometryIndices          = GeometryIndexOne.data();

    const std::array<RadientMeshGeometryData, 2> GeometryDataWithUnused{
        RadientMeshGeometryData{},
        MakeGeometryData(UsedGeometry)};

    RefCntAutoPtr<IRadientMeshAsset> pMeshA;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool,
                                                                    GeometryDataWithUnused.data(),
                                                                    static_cast<Uint32>(GeometryDataWithUnused.size()),
                                                                    ViewA,
                                                                    &pMeshA)));
    ASSERT_NE(pMeshA, nullptr);

    RadientMeshPrimitiveCreateInfo   PrimitiveCompact{};
    const RadientMeshViewCreateInfo  ViewB               = MakeMeshView(PrimitiveCompact);
    const RadientMeshGeometryData    CompactGeometryData = MakeGeometryData(UsedGeometry);
    RefCntAutoPtr<IRadientMeshAsset> pMeshB;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool,
                                                                    &CompactGeometryData,
                                                                    1,
                                                                    ViewB,
                                                                    &pMeshB)));
    ASSERT_NE(pMeshB, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshA), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshB), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(pMeshA),
              RadientMeshAssetManager::GetMeshPayload(pMeshB));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGeometryCount(pMeshA), 1u);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGeometryCount(pMeshB), 1u);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshA), UsedGeometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshA), UsedGeometry.pIndexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshB), UsedGeometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshB), UsedGeometry.pIndexData.RawPtr());

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, MeshViewCacheDistinguishesMaterialsFromDifferentManagers)
{
    // Mesh-view cache keys include material asset references. Asset references
    // must therefore be unique across manager instances, not only within one
    // material manager.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshGeometryHandles Geometry;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), Geometry)));
    ASSERT_NE(Geometry.pVertexData, nullptr);
    ASSERT_NE(Geometry.pIndexData, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManagerA = RadientMaterialAssetManager::Create();
    RadientMaterialAssetManagerSharedPtr pMaterialManagerB = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManagerA, nullptr);
    ASSERT_NE(pMaterialManagerB, nullptr);

    RadientMaterialCreateInfo MaterialCIA{};
    MaterialCIA.Name            = "material from manager A";
    MaterialCIA.BaseColorFactor = {1.f, 0.f, 0.f, 1.f};
    RefCntAutoPtr<IRadientMaterialAsset> pMaterialA;
    ASSERT_EQ(pMaterialManagerA->CreateMaterial(MaterialCIA, pMaterialA.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterialA, nullptr);

    RadientMaterialCreateInfo MaterialCIB{};
    MaterialCIB.Name            = "material from manager B";
    MaterialCIB.BaseColorFactor = {0.f, 0.f, 1.f, 1.f};
    RefCntAutoPtr<IRadientMaterialAsset> pMaterialB;
    ASSERT_EQ(pMaterialManagerB->CreateMaterial(MaterialCIB, pMaterialB.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterialB, nullptr);

    const RadientAssetReference& MaterialRefA = pMaterialA->GetReference();
    const RadientAssetReference& MaterialRefB = pMaterialB->GetReference();
    ASSERT_NE(MaterialRefA.URI, nullptr);
    ASSERT_NE(MaterialRefB.URI, nullptr);
    EXPECT_STRNE(MaterialRefA.URI, MaterialRefB.URI);
    EXPECT_EQ(MaterialRefA.Version, MaterialRefB.Version);

    RadientMeshPrimitiveCreateInfo  PrimitiveA{};
    const RadientMeshViewCreateInfo ViewA = MakeMeshView(PrimitiveA);
    PrimitiveA.pMaterial                  = pMaterialA;

    RadientMeshPrimitiveCreateInfo  PrimitiveB{};
    const RadientMeshViewCreateInfo ViewB = MakeMeshView(PrimitiveB);
    PrimitiveB.pMaterial                  = pMaterialB;

    const RadientMeshGeometryData GeometryData = MakeGeometryData(Geometry);

    RefCntAutoPtr<IRadientMeshAsset> pMeshA;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, ViewA, &pMeshA)));
    ASSERT_NE(pMeshA, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMeshB;
    EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, ViewB, &pMeshB)));
    ASSERT_NE(pMeshB, nullptr);

    pThreadPool->WaitForAllTasks();

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshA), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMeshB), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshA), Geometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshA), Geometry.pIndexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(pMeshB), Geometry.pVertexData.RawPtr());
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(pMeshB), Geometry.pIndexData.RawPtr());
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMeshA),
              RadientMeshAssetManager::GetMeshPayload(pMeshB));

    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, MeshViewCopiesCreateInfoBeforeAsyncTaskRuns)
{
    // The view task runs asynchronously, so CreateMeshView() must copy primitive
    // ranges, optional geometry indices, primitive names, and material refs
    // before returning. This test lets all caller-owned input memory go out of
    // scope before the queued view task executes.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    MeshGeometryHandles Geometry;
    EXPECT_EQ(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), Geometry),
              RADIENT_STATUS_PENDING);
    ASSERT_NE(Geometry.pVertexData, nullptr);
    ASSERT_NE(Geometry.pIndexData, nullptr);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(pThreadPool->GetQueueSize(), 0u);

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    {
        const std::string PrimitiveName = "temporary primitive name";

        std::array<RadientMeshPrimitiveCreateInfo, 1> Primitives{};
        Primitives[0].Name       = PrimitiveName.c_str();
        Primitives[0].FirstIndex = 0;
        Primitives[0].IndexCount = 3;

        std::array<Uint32, 1> GeometryIndices{0};

        RadientMeshViewCreateInfo ViewCI{};
        ViewCI.pPrimitives      = Primitives.data();
        ViewCI.PrimitiveCount   = static_cast<Uint32>(Primitives.size());
        ViewCI.pGeometryIndices = GeometryIndices.data();

        const RadientMeshGeometryData GeometryData = MakeGeometryData(Geometry);
        EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, ViewCI, &pMesh)));
        ASSERT_NE(pMesh, nullptr);
    }

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh), nullptr);
}

TEST(RadientMeshAssetManagerTest, MeshTasksKeepManagerAliveUntilCompletion)
{
    // Mesh data creation and CreateMeshView() enqueue work that captures
    // the manager. Releasing the caller's shared_ptr before the tasks run must
    // not leave the returned mesh handle pending or touch a destroyed manager.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    {
        RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
        ASSERT_NE(pMeshManager, nullptr);

        RadientMeshPrimitiveCreateInfo  Primitive{};
        const RadientMeshViewCreateInfo ViewCI = MakeMeshView(Primitive);
        EXPECT_TRUE(IsAcceptedOrMissingGPU(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSources(), ViewCI, pMesh)));
        ASSERT_NE(pMesh, nullptr);
    }

    DrainThreadPool(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh), nullptr);
}
