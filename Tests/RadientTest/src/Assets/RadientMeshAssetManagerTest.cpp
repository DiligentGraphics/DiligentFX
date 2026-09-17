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
#include "RadientMaterialTestHelpers.hpp"
#include "RadientTestAssetHelpers.hpp"

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

RefCntAutoPtr<IRadientMaterialAsset> CreateDefaultMaterial(RadientMaterialAssetManager& MaterialManager)
{
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(Testing::CreateStandardMaterialAsset(MaterialManager, {}, pMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    return pMaterial;
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

MeshSources MakeMeshSources(std::array<Uint32, 3> Indices = {0, 1, 2}, IRadientDataBlob* pBlob = nullptr, IRadientDataBlob* pIndexDataBlob = nullptr)
{
    // Leave destination vertex attributes unset. RadientMeshAssetManager should
    // resolve the default GLTF layout before computing the mesh cache key.
    static constexpr std::array<RadientFloat3, 3> Positions{
        RadientFloat3{0.f, 0.f, 0.f},
        RadientFloat3{1.f, 0.f, 0.f},
        RadientFloat3{0.f, 1.f, 0.f}};
    RadientMeshCreateInfo            MeshCI{};
    const RadientVertexAttributeDesc VertexAttributes[]{
        {
            "POSITION",
            0,
            RADIENT_VERTEX_AUTO_OFFSET,
            RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
            3,
            false,
        },
    };
    const RadientVertexBufferLayoutDesc VertexBuffers[1]{};

    const RefCntAutoPtr<IRadientDataBlob> pVertexBlob = pBlob != nullptr ?
        RefCntAutoPtr<IRadientDataBlob>{pBlob} :
        Testing::MakeTestDataBlob(Positions.data(), sizeof(Positions));

    const RefCntAutoPtr<IRadientDataBlob> pIndexBlob = pIndexDataBlob != nullptr ?
        RefCntAutoPtr<IRadientDataBlob>{pIndexDataBlob} :
        Testing::MakeTestDataBlob(Indices.data(), sizeof(Indices));

    IRadientDataBlob* const VertexData[]{pVertexBlob};
    const Uint32            VertexBufferCount = 1;

    MeshCI.VertexLayout    = {VertexAttributes, VertexBufferCount, VertexBuffers, VertexBufferCount};
    MeshCI.ppVertexBuffers = VertexData;
    MeshCI.VertexCount     = static_cast<Uint32>(Positions.size());
    MeshCI.pIndexBuffer    = pIndexBlob;
    MeshCI.IndexCount      = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType       = RADIENT_INDEX_TYPE_UINT32;

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

TEST(RadientMeshAssetManagerTest, DrawableMeshRemainsPendingUntilPayloadIsReady)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RadientMeshPrimitiveCreateInfo   Primitive{};
    const RadientMeshViewCreateInfo  View = MakeMeshView(Primitive);
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateMeshViewFromSource(*pMeshManager, *pThreadPool, MakeMeshSources(), View, pMesh),
              RADIENT_STATUS_PENDING);
    ASSERT_NE(pMesh, nullptr);

    // The drawable cache queries mesh handles before their worker task may have
    // published a payload. This state must remain retryable rather than looking
    // like a terminal resolution failure.
    const RadientDrawableMeshResolveResult PendingResult =
        RadientMeshAssetManager::GetDrawableMesh(pMesh, false);
    EXPECT_EQ(PendingResult.Status, RADIENT_STATUS_PENDING);
    EXPECT_EQ(PendingResult.pMesh, nullptr);
    const RadientMeshAssetDesc& PendingDesc = pMesh->GetDesc();
    EXPECT_EQ(PendingDesc.GeometryCount, 0u);
    EXPECT_EQ(PendingDesc.pGeometries, nullptr);
    EXPECT_EQ(PendingDesc.PrimitiveCount, 0u);
    EXPECT_EQ(PendingDesc.pPrimitives, nullptr);

    DrainThreadPool(*pThreadPool);

    const RadientDrawableMeshResolveResult ReadyResult =
        RadientMeshAssetManager::GetDrawableMesh(pMesh, false);
    EXPECT_EQ(ReadyResult.Status, RADIENT_STATUS_OK);
    EXPECT_NE(ReadyResult.pMesh, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_NO_GPU_DATA);

    // CPU loading publishes the stored layout, including default-filled fields
    // in active buffers, without requiring any GPU resources.
    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    ASSERT_NE(Desc.pGeometries, nullptr);
    const RadientMeshGeometryDesc& Geometry = Desc.pGeometries[0];
    EXPECT_EQ(Geometry.VertexCount, 3u);
    EXPECT_EQ(Geometry.IndexType, RADIENT_INDEX_TYPE_UINT32);
    EXPECT_EQ(Geometry.IndexCount, 3u);
    const RadientVertexLayoutDesc& Layout = Geometry.VertexLayout;
    ASSERT_EQ(Layout.BufferCount, 1u);
    ASSERT_NE(Layout.pBuffers, nullptr);
    EXPECT_EQ(Layout.pBuffers[0].ByteStride, 32u);
    ASSERT_EQ(Layout.AttributeCount, 3u);
    ASSERT_NE(Layout.pAttributes, nullptr);
    const std::array<const char*, 3> Semantics{"POSITION", "NORMAL", "TEXCOORD_0"};
    const std::array<Uint32, 3>      Offsets{0, 12, 24};
    const std::array<Uint32, 3>      ComponentCounts{3, 3, 2};
    for (Uint32 AttributeIndex = 0; AttributeIndex < Layout.AttributeCount; ++AttributeIndex)
    {
        const RadientVertexAttributeDesc& Attribute = Layout.pAttributes[AttributeIndex];
        EXPECT_STREQ(Attribute.Semantic, Semantics[AttributeIndex]);
        EXPECT_EQ(Attribute.BufferIndex, 0u);
        EXPECT_EQ(Attribute.ByteOffset, Offsets[AttributeIndex]);
        EXPECT_EQ(Attribute.ComponentType, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32);
        EXPECT_EQ(Attribute.ComponentCount, ComponentCounts[AttributeIndex]);
        EXPECT_FALSE(Attribute.Normalized);
    }
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    ASSERT_NE(Desc.pPrimitives, nullptr);
    EXPECT_EQ(Desc.pPrimitives[0].GeometryIndex, 0u);
    EXPECT_EQ(Desc.pPrimitives[0].FirstElement, 0u);
    EXPECT_EQ(Desc.pPrimitives[0].ElementCount, 3u);
    EXPECT_EQ(Desc.pPrimitives[0].pMaterial, nullptr);
    EXPECT_EQ(Desc.MorphTargetCount, 0u);
    EXPECT_EQ(Desc.pMorphTargets, nullptr);
}

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

TEST(RadientMeshAssetManagerTest, ReflectionReportsStoredIndexType)
{
    auto                               pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto                               pMeshManager = RadientMeshAssetManager::Create({});
    const std::array<RadientFloat3, 3> Positions{
        RadientFloat3{0, 0, 0}, RadientFloat3{1, 0, 0}, RadientFloat3{0, 1, 0}};
    const std::array<Uint16, 3>      Indices{0, 1, 2};
    auto                             pVertexBlob = Testing::MakeTestDataBlob(Positions.data(), sizeof(Positions));
    auto                             pIndexBlob  = Testing::MakeTestDataBlob(Indices.data(), sizeof(Indices));
    IRadientDataBlob* const          VertexBuffers[]{pVertexBlob};
    const RadientVertexAttributeDesc Attribute{
        "POSITION", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False};
    const RadientVertexBufferLayoutDesc Buffer{};
    RadientMeshPrimitiveCreateInfo      Primitive{};
    Primitive.IndexCount = static_cast<Uint32>(Indices.size());
    RadientMeshCreateInfo MeshCI{};
    MeshCI.VertexLayout    = {&Attribute, 1, &Buffer, 1};
    MeshCI.ppVertexBuffers = VertexBuffers;
    MeshCI.VertexCount     = static_cast<Uint32>(Positions.size());
    MeshCI.pIndexBuffer    = pIndexBlob;
    MeshCI.IndexCount      = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType       = RADIENT_INDEX_TYPE_UINT16;
    MeshCI.pPrimitives     = &Primitive;
    MeshCI.PrimitiveCount  = 1;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    ASSERT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_PENDING);
    ASSERT_NE(pMesh, nullptr);
    pVertexBlob.Release();
    pIndexBlob.Release();
    DrainThreadPool(*pThreadPool);

    ASSERT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    ASSERT_NE(Desc.pGeometries, nullptr);
    EXPECT_EQ(Desc.pGeometries[0].IndexType, RADIENT_INDEX_TYPE_UINT32);
    EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
    EXPECT_EQ(Desc.pGeometries[0].VertexCount, 3u);
}

TEST(RadientMeshAssetManagerTest, MeshViewCacheSharesPayloadAndPreservesPrimitiveNames)
{
    auto                pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto                pMeshManager = RadientMeshAssetManager::Create({});
    MeshGeometryHandles Geometry;
    ASSERT_EQ(CreateMeshGeometryData(*pMeshManager, *pThreadPool, MakeMeshSources(), Geometry),
              RADIENT_STATUS_PENDING);
    DrainThreadPool(*pThreadPool);
    const RadientMeshGeometryData                              GeometryData = MakeGeometryData(Geometry);
    const std::array<const char*, 4>                           Names{nullptr, "", "first primitive", "second primitive"};
    std::array<RefCntAutoPtr<IRadientMeshAsset>, Names.size()> Meshes;
    for (size_t MeshIndex = 0; MeshIndex < Meshes.size(); ++MeshIndex)
    {
        std::string                     Name = Names[MeshIndex] != nullptr ? Names[MeshIndex] : "";
        RadientMeshPrimitiveCreateInfo  Primitive{};
        const RadientMeshViewCreateInfo View = MakeMeshView(Primitive);
        Primitive.Name                       = Names[MeshIndex] != nullptr ? Name.c_str() : nullptr;
        EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, View, &Meshes[MeshIndex])));
        ASSERT_NE(Meshes[MeshIndex], nullptr);
        Name.assign("overwritten source name");
    }
    DrainThreadPool(*pThreadPool);

    for (size_t MeshIndex = 0; MeshIndex < Meshes.size(); ++MeshIndex)
    {
        ASSERT_EQ(RadientMeshAssetManager::GetLoadStatus(Meshes[MeshIndex]), RADIENT_STATUS_OK);
        ASSERT_NE(RadientMeshAssetManager::GetMeshPayload(Meshes[MeshIndex]), nullptr);
        const RadientMeshAssetDesc& Desc = Meshes[MeshIndex]->GetDesc();
        ASSERT_EQ(Desc.GeometryCount, 1u);
        ASSERT_NE(Desc.pGeometries, nullptr);
        ASSERT_EQ(Desc.PrimitiveCount, 1u);
        ASSERT_NE(Desc.pPrimitives, nullptr);
        EXPECT_STREQ(Desc.pPrimitives[0].Name, Names[MeshIndex]);
        EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(Meshes[MeshIndex]), Geometry.pVertexData.RawPtr());
        EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(Meshes[MeshIndex]), Geometry.pIndexData.RawPtr());
        for (size_t OtherIndex = 0; OtherIndex < MeshIndex; ++OtherIndex)
        {
            EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(Meshes[MeshIndex]),
                      RadientMeshAssetManager::GetMeshPayload(Meshes[OtherIndex]));
            EXPECT_EQ(Desc.pGeometries, Meshes[OtherIndex]->GetDesc().pGeometries);
            EXPECT_NE(Desc.pPrimitives, Meshes[OtherIndex]->GetDesc().pPrimitives);
        }
    }

    // A cache hit after the payload is ready also keeps its own copied name.
    RefCntAutoPtr<IRadientMeshAsset> pCachedMesh;
    {
        std::string                     Name = "cached primitive";
        RadientMeshPrimitiveCreateInfo  Primitive{};
        const RadientMeshViewCreateInfo View = MakeMeshView(Primitive);
        Primitive.Name                       = Name.c_str();
        EXPECT_TRUE(IsAcceptedOrMissingGPU(pMeshManager->CreateMeshView(*pThreadPool, &GeometryData, 1, View, &pCachedMesh)));
    }
    ASSERT_NE(pCachedMesh, nullptr);
    DrainThreadPool(*pThreadPool);
    ASSERT_EQ(RadientMeshAssetManager::GetLoadStatus(pCachedMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(pCachedMesh),
              RadientMeshAssetManager::GetMeshPayload(Meshes[0]));
    const RadientMeshAssetDesc CachedDesc = pCachedMesh->GetDesc();
    ASSERT_EQ(CachedDesc.GeometryCount, 1u);
    ASSERT_NE(CachedDesc.pGeometries, nullptr);
    ASSERT_EQ(CachedDesc.PrimitiveCount, 1u);
    ASSERT_NE(CachedDesc.pPrimitives, nullptr);
    EXPECT_STREQ(CachedDesc.pPrimitives[0].Name, "cached primitive");
    EXPECT_NE(CachedDesc.pPrimitives, Meshes[0]->GetDesc().pPrimitives);
    const Char* const pCachedName = CachedDesc.pPrimitives[0].Name;

    pThreadPool->StopThreads();
    for (auto& pMesh : Meshes)
        pMesh.Release();
    Geometry = {};
    pMeshManager.reset();

    EXPECT_EQ(pCachedMesh->GetDesc().pGeometries, CachedDesc.pGeometries);
    EXPECT_EQ(pCachedMesh->GetDesc().pPrimitives, CachedDesc.pPrimitives);
    EXPECT_EQ(pCachedMesh->GetDesc().pPrimitives[0].Name, pCachedName);
    EXPECT_STREQ(pCachedName, "cached primitive");
    EXPECT_EQ(CachedDesc.pGeometries[0].VertexCount, 3u);
}

TEST(RadientMeshAssetManagerTest, RetainsQueuedVertexBlobUntilWorkerFinishes)
{
    auto   pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto   pMeshManager = RadientMeshAssetManager::Create({});
    Uint32 ReadReleases = 0;
    auto   pBlob        = Testing::MakeTestMutableDataBlob(nullptr, 3 * sizeof(RadientFloat3), Testing::CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RefCntWeakPtr<IRadientDataBlob>       WeakBlob{pBlob.RawPtr()};
    MeshSources                           Sources = MakeMeshSources({0, 1, 2}, pBlob);
    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    ASSERT_EQ(CreateMeshVertexDataHandle(*pMeshManager, *pThreadPool, std::move(Sources.pVertexSource), pVertexData),
              RADIENT_STATUS_PENDING);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(4 * sizeof(RadientFloat3)), RADIENT_STATUS_INVALID_OPERATION);
    pBlob.Release();
    EXPECT_NE(WeakBlob.Lock(), nullptr);
    EXPECT_EQ(ReadReleases, 0u);
    DrainThreadPool(*pThreadPool);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pVertexData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, RetainsQueuedIndexBlobUntilWorkerFinishes)
{
    auto         pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto         pMeshManager = RadientMeshAssetManager::Create({});
    Uint32       ReadReleases = 0;
    const Uint32 Indices[]{0, 1, 2};
    auto         pBlob = Testing::MakeTestMutableDataBlob(Indices, sizeof(Indices), Testing::CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RefCntWeakPtr<IRadientDataBlob>      WeakBlob{pBlob.RawPtr()};
    MeshSources                          Sources = MakeMeshSources({0, 1, 2}, nullptr, pBlob);
    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;
    ASSERT_EQ(CreateMeshIndexDataHandle(*pMeshManager, *pThreadPool, std::move(Sources.pIndexSource), pIndexData),
              RADIENT_STATUS_PENDING);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(4 * sizeof(Uint32)), RADIENT_STATUS_INVALID_OPERATION);
    pBlob.Release();
    EXPECT_NE(WeakBlob.Lock(), nullptr);
    EXPECT_EQ(ReadReleases, 0u);
    DrainThreadPool(*pThreadPool);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pIndexData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, RejectsUnreadableIndexBlobBeforeQueuingLoad)
{
    auto         pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto         pMeshManager = RadientMeshAssetManager::Create({});
    auto         pVertexBlob  = Testing::MakeTestMutableDataBlob(nullptr, 3 * sizeof(RadientFloat3));
    const Uint32 Indices[]{0, 1, 2};
    auto         pIndexBlob = Testing::MakeTestMutableDataBlob(Indices, sizeof(Indices));
    ASSERT_NE(pVertexBlob, nullptr);
    ASSERT_NE(pIndexBlob, nullptr);
    const RadientVertexAttributeDesc    Attribute{"POSITION", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False};
    const RadientVertexBufferLayoutDesc BufferLayout{};
    IRadientDataBlob* const             VertexBuffers[]{pVertexBlob};
    RadientMeshPrimitiveCreateInfo      Primitive;
    Primitive.IndexCount = 3;
    RadientMeshCreateInfo MeshCI;
    MeshCI.VertexLayout    = {&Attribute, 1, &BufferLayout, 1};
    MeshCI.ppVertexBuffers = VertexBuffers;
    MeshCI.VertexCount     = 3;
    MeshCI.pIndexBuffer    = pIndexBlob;
    MeshCI.IndexCount      = 3;
    MeshCI.IndexType       = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives     = &Primitive;
    MeshCI.PrimitiveCount  = 1;

    void* pWriteData = nullptr;
    ASSERT_EQ(pIndexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMesh, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    ASSERT_EQ(pIndexBlob->EndWrite(), RADIENT_STATUS_OK);
    ASSERT_EQ(pVertexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    ASSERT_EQ(pVertexBlob->EndWrite(), RADIENT_STATUS_OK);

    ASSERT_EQ(pIndexBlob->Resize(sizeof(Indices) - 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    ASSERT_EQ(pVertexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    ASSERT_EQ(pVertexBlob->EndWrite(), RADIENT_STATUS_OK);
    ASSERT_EQ(pIndexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    ASSERT_EQ(pIndexBlob->EndWrite(), RADIENT_STATUS_OK);

    pIndexBlob = Testing::MakeTestMutableDataBlob(Indices, sizeof(Indices));
    ASSERT_NE(pIndexBlob, nullptr);
    MeshCI.pIndexBuffer = pIndexBlob;
    EXPECT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_PENDING);
    DrainThreadPool(*pThreadPool);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(pIndexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pIndexBlob->EndWrite(), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, RejectsActiveVertexBlobWriterBeforeQueuingLoad)
{
    auto   pThreadPool  = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto   pMeshManager = RadientMeshAssetManager::Create({});
    Uint32 ReadReleases = 0;
    auto   pBlob        = Testing::MakeTestMutableDataBlob(nullptr, 3 * sizeof(RadientFloat3), Testing::CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    const RadientVertexAttributeDesc    Attribute{"POSITION", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False};
    const RadientVertexBufferLayoutDesc BufferLayout{};
    IRadientDataBlob* const             VertexBuffers[]{pBlob};
    const Uint32                        Indices[]{0, 1, 2};
    const auto                          pIndexBlob = Testing::MakeTestDataBlob(Indices, sizeof(Indices));
    RadientMeshPrimitiveCreateInfo      Primitive;
    Primitive.IndexCount = 3;
    RadientMeshCreateInfo MeshCI;
    MeshCI.VertexLayout    = {&Attribute, 1, &BufferLayout, 1};
    MeshCI.ppVertexBuffers = VertexBuffers;
    MeshCI.VertexCount     = 3;
    MeshCI.pIndexBuffer    = pIndexBlob;
    MeshCI.IndexCount      = 3;
    MeshCI.IndexType       = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives     = &Primitive;
    MeshCI.PrimitiveCount  = 1;

    void* pWriteData = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMesh, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(ReadReleases, 0u);
    ASSERT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);

    EXPECT_EQ(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh), RADIENT_STATUS_PENDING);
    DrainThreadPool(*pThreadPool);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pBlob->Resize(4 * sizeof(RadientFloat3)), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientMeshAssetManagerTest, ReleasesVertexBlobReadAccessWhenEnqueueFails)
{
    auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    pThreadPool->StopThreads();
    auto   pMeshManager = RadientMeshAssetManager::Create({});
    Uint32 ReadReleases = 0;
    auto   pBlob        = Testing::MakeTestMutableDataBlob(nullptr, 3 * sizeof(RadientFloat3), Testing::CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    MeshSources                           Sources = MakeMeshSources({0, 1, 2}, pBlob);
    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
    {
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Enqueue on a stopped ThreadPool"};
        EXPECT_EQ(CreateMeshVertexDataHandle(*pMeshManager, *pThreadPool, std::move(Sources.pVertexSource), pVertexData),
                  RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pBlob->Resize(4 * sizeof(RadientFloat3)), RADIENT_STATUS_OK);
    void* pWriteData = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
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

    auto pMaterialManagerA = RadientMaterialAssetManager::Create();
    auto pMaterialManagerB = RadientMaterialAssetManager::Create();
    auto pMaterialA        = CreateDefaultMaterial(*pMaterialManagerA);
    auto pMaterialB        = CreateDefaultMaterial(*pMaterialManagerB);
    ASSERT_NE(pMaterialA, nullptr);
    ASSERT_NE(pMaterialB, nullptr);

    std::array<RadientMeshPrimitiveCreateInfo, 3> Primitives{};
    Primitives[0].Name       = "custom whole";
    Primitives[0].FirstIndex = 0;
    Primitives[0].IndexCount = 3;
    Primitives[0].pMaterial  = pMaterialA;
    Primitives[1].Name       = "default subrange";
    Primitives[1].FirstIndex = 1;
    Primitives[1].IndexCount = 2;
    Primitives[2].Name       = "custom subrange";
    Primitives[2].FirstIndex = 2;
    Primitives[2].IndexCount = 1;
    Primitives[2].pMaterial  = pMaterialB;

    const std::array<Uint32, 3> GeometryIndices{1, 0, 1};

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

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 2u);
    ASSERT_NE(Desc.pGeometries, nullptr);
    // Geometry indices are compacted in first-use order: custom, then default.
    const RadientVertexLayoutDesc& CustomLayout = Desc.pGeometries[0].VertexLayout;
    ASSERT_EQ(CustomLayout.BufferCount, 1u);
    ASSERT_NE(CustomLayout.pBuffers, nullptr);
    EXPECT_EQ(CustomLayout.pBuffers[0].ByteStride, 28u);
    ASSERT_EQ(CustomLayout.AttributeCount, 1u);
    ASSERT_NE(CustomLayout.pAttributes, nullptr);
    EXPECT_STREQ(CustomLayout.pAttributes[0].Semantic, "POSITION");
    EXPECT_EQ(CustomLayout.pAttributes[0].ByteOffset, 16u);
    EXPECT_EQ(CustomLayout.pAttributes[0].BufferIndex, 0u);
    const RadientVertexLayoutDesc& DefaultLayout = Desc.pGeometries[1].VertexLayout;
    ASSERT_EQ(DefaultLayout.BufferCount, 1u);
    ASSERT_NE(DefaultLayout.pBuffers, nullptr);
    EXPECT_EQ(DefaultLayout.pBuffers[0].ByteStride, 32u);
    EXPECT_EQ(DefaultLayout.AttributeCount, 3u);
    for (Uint32 GeometryIndex = 0; GeometryIndex < Desc.GeometryCount; ++GeometryIndex)
    {
        EXPECT_EQ(Desc.pGeometries[GeometryIndex].VertexCount, 3u);
        EXPECT_EQ(Desc.pGeometries[GeometryIndex].IndexCount, 3u);
        EXPECT_EQ(Desc.pGeometries[GeometryIndex].IndexType, RADIENT_INDEX_TYPE_UINT32);
    }

    ASSERT_EQ(Desc.PrimitiveCount, 3u);
    ASSERT_NE(Desc.pPrimitives, nullptr);
    const std::array<Uint32, 3> ReflectedGeometryIndices{0, 1, 0};
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < Desc.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveDesc& Primitive = Desc.pPrimitives[PrimitiveIndex];
        EXPECT_STREQ(Primitive.Name, Primitives[PrimitiveIndex].Name);
        EXPECT_EQ(Primitive.GeometryIndex, ReflectedGeometryIndices[PrimitiveIndex]);
        EXPECT_EQ(Primitive.FirstElement, Primitives[PrimitiveIndex].FirstIndex);
        EXPECT_EQ(Primitive.ElementCount, Primitives[PrimitiveIndex].IndexCount);
        EXPECT_EQ(Primitive.pMaterial, Primitives[PrimitiveIndex].pMaterial);
    }

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

    const RadientMeshAssetDesc& DescA = pMeshA->GetDesc();
    const RadientMeshAssetDesc& DescB = pMeshB->GetDesc();
    ASSERT_EQ(DescA.GeometryCount, 1u);
    ASSERT_EQ(DescB.GeometryCount, 1u);
    ASSERT_EQ(DescB.PrimitiveCount, 1u);
    ASSERT_NE(DescB.pGeometries, nullptr);
    ASSERT_NE(DescB.pPrimitives, nullptr);
    EXPECT_EQ(DescB.pPrimitives[0].GeometryIndex, 0u);
    EXPECT_EQ(DescA.pGeometries, DescB.pGeometries);
    EXPECT_NE(DescA.pPrimitives, DescB.pPrimitives);

    pThreadPool->StopThreads();
    pMeshA.Release();
    UsedGeometry = {};
    pMeshManager.reset();
    EXPECT_EQ(pMeshB->GetDesc().pGeometries, DescB.pGeometries);
    EXPECT_EQ(DescB.pGeometries[0].VertexCount, 3u);
    EXPECT_STREQ(DescB.pGeometries[0].VertexLayout.pAttributes[0].Semantic, "POSITION");
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

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialA = CreateDefaultMaterial(*pMaterialManagerA);
    ASSERT_NE(pMaterialA, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterialB = CreateDefaultMaterial(*pMaterialManagerB);
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

    const RadientDrawableMeshResolveResult DrawableA =
        RadientMeshAssetManager::GetDrawableMesh(pMeshA, false);
    ASSERT_EQ(DrawableA.Status, RADIENT_STATUS_OK);
    ASSERT_NE(DrawableA.pMesh, nullptr);
    ASSERT_EQ(DrawableA.pMesh->Primitives.size(), 1u);
    EXPECT_EQ(DrawableA.pMesh->Primitives[0].pMaterialAsset, pMaterialA.RawPtr());

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
    {
        MeshSources                     Sources  = MakeMeshSources();
        const std::string               Semantic = "POSITION";
        const GLTF::VertexAttributeDesc Attribute{Semantic.c_str(), 0, VT_FLOAT32, 3};
        ASSERT_EQ(Sources.pVertexSource->SetVertexAttributes(&Attribute, 1), RADIENT_STATUS_OK);
        EXPECT_EQ(CreateMeshGeometryData(*pMeshManager, *pThreadPool, std::move(Sources), Geometry),
                  RADIENT_STATUS_PENDING);
    }
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
    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    ASSERT_NE(Desc.pGeometries, nullptr);
    const RadientVertexLayoutDesc& Layout = Desc.pGeometries[0].VertexLayout;
    ASSERT_EQ(Layout.AttributeCount, 1u);
    ASSERT_NE(Layout.pAttributes, nullptr);
    EXPECT_STREQ(Layout.pAttributes[0].Semantic, "POSITION");
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    ASSERT_NE(Desc.pPrimitives, nullptr);
    EXPECT_STREQ(Desc.pPrimitives[0].Name, "temporary primitive name");
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
