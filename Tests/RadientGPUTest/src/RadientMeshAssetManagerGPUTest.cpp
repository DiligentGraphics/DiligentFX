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
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMorphTargetData.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTypesX.hpp"
#include "GPUUploadManager.h"
#include "MapHelper.hpp"
#include "VertexPool.h"
#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstring>
#include <thread>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;
using namespace std::chrono_literals;

namespace
{

RadientMeshAssetManagerSharedPtr CreateMeshManager(IRenderDevice*         pDevice,
                                                   GLTF::ResourceManager* pResourceManager,
                                                   IGPUUploadManager*     pUploadManager)
{
    RadientMeshAssetManager::CreateInfo CI;
    CI.pDevice          = pDevice;
    CI.pResourceManager = pResourceManager;
    CI.pUploadManager   = pUploadManager;
    return RadientMeshAssetManager::Create(CI);
}

bool WaitForMeshDrawableStatus(IRadientMeshAsset* pMesh,
                               RADIENT_STATUS     ExpectedStatus,
                               IGPUUploadManager* pUploadManager = nullptr,
                               IDeviceContext*    pContext       = nullptr)
{
    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientDrawableMeshResolveResult Result =
            RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
        if (Result.Status == ExpectedStatus)
            return true;

        if (pUploadManager != nullptr && pContext != nullptr)
            PumpUploadManager(*pUploadManager, *pContext);

        std::this_thread::sleep_for(1ms);
    }

    return RadientMeshAssetManager::GetDrawableMesh(pMesh, true).Status == ExpectedStatus;
}

bool WaitForMeshLoadStatus(IRadientMeshAsset* pMesh,
                           RADIENT_STATUS     ExpectedStatus)
{
    for (Uint32 i = 0; i < 256; ++i)
    {
        if (RadientMeshAssetManager::GetLoadStatus(pMesh) == ExpectedStatus)
            return true;

        std::this_thread::sleep_for(1ms);
    }

    return RadientMeshAssetManager::GetLoadStatus(pMesh) == ExpectedStatus;
}

TEST(RadientMeshAssetManagerGPUTest, WaitsForPendingMaterial)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreadPool{pThreadPool};

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pTextureManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    // Keep the texture copy callback queued so the material remains pending
    // for GPU resource readiness while the mesh source itself can still be processed.
    ASSERT_TRUE(WaitForPendingCopyCommandEnqueueCallbacks(pTextureManager));
    ASSERT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(
                  *pMaterialManager,
                  {},
                  [&](IRadientMaterialDefinitionAsset& Definition,
                      IRadientMaterialWriter&          Writer) {
                      return SetStandardMaterialTextureParameters(
                          Definition,
                          Writer,
                          RadientStandardMaterialBaseColorTextureParameterNames,
                          RadientStandardMaterialTextureParameters{pTexture});
                  },
                  &pMaterial),
              RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);
    ASSERT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_PENDING);

    const RadientFloat3 Positions[] =
        {
            {0.f, 0.f, 0.f},
            {1.f, 0.f, 0.f},
            {0.f, 1.f, 0.f},
        };

    const RadientColorRGBA8 Colors[] =
        {
            {255, 0, 0, 255},
            {0, 255, 0, 255},
            {0, 0, 255, 255},
        };

    const Uint32 Indices[] = {0, 1, 2};

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.FirstElement = 0;
    PrimitiveCI.ElementCount = 3;
    PrimitiveCI.pMaterial    = pMaterial;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name = "Radient mesh waiting for material";
    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    VertexLayout.AddBuffer().AddAttribute("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, 1, RADIENT_VERTEX_AUTO_OFFSET, True);

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Positions);
    BlobCI.pData = Positions;
    RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(Colors);
    BlobCI.pData = Colors;
    RefCntAutoPtr<IRadientDataBlob> pColorsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pColorsBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(Indices);
    BlobCI.pData = Indices;
    RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pIndexBlob), RADIENT_STATUS_OK);
    IRadientDataBlob* const VertexData[]{pPositionsBlob, pColorsBlob};

    MeshCI.VertexData.VertexLayout    = VertexLayout;
    MeshCI.VertexData.ppVertexBuffers = VertexData;
    MeshCI.VertexData.VertexCount     = 3;
    MeshCI.IndexData.pIndexBuffer     = pIndexBlob;
    MeshCI.IndexData.IndexCount       = 3;
    MeshCI.IndexData.IndexType        = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives                = &PrimitiveCI;
    MeshCI.PrimitiveCount             = 1;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh)));
    ASSERT_NE(pMesh, nullptr);

    // Mesh geometry loading is asynchronous. Wait until source/view processing
    // has completed so a pending drawable status below comes from GPU/material
    // readiness, not from the mesh source task still being in flight.
    ASSERT_TRUE(WaitForMeshLoadStatus(pMesh, RADIENT_STATUS_OK));
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);

    // The mesh payload is available, but render-ready drawable resolution must
    // stay pending until dependent texture GPU work has been enqueued.
    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_PENDING));
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_PENDING);
    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    EXPECT_EQ(Desc.pGeometries[0].VertexCount, 3u);
    EXPECT_EQ(Desc.pGeometries[0].IndexType, RADIENT_INDEX_TYPE_UINT32);
    EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
    EXPECT_EQ(Desc.pPrimitives[0].pMaterial, pMaterial.RawPtr());
    {
        const RadientDrawableMeshResolveResult Result =
            RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
        EXPECT_EQ(Result.Status, RADIENT_STATUS_PENDING);
        EXPECT_EQ(Result.pMesh, nullptr);
    }

    ASSERT_TRUE(WaitForTextureManagerIdle(pTextureManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_OK, pUploadManager, pContext));
    const RadientDrawableMeshResolveResult Result =
        RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pMesh, nullptr);
    ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
    EXPECT_EQ(Result.pMesh->Primitives[0].pMaterialAsset, pMaterial);

    pThreadPool->StopThreads();
    StopThreadPool.pThreadPool = nullptr;
}

TEST(RadientMeshAssetManagerGPUTest, AllocatesOnlyActiveVertexBuffersAndPreservesDataOnPoolGrowth)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreadPool{pThreadPool};

    // The first triangle fits; the second forces the same pool to grow after
    // the first upload has completed. Both allocations must retain their data.
    GLTF::ResourceManager::CreateInfo ResourceManagerCI = MakeResourceManagerCI();
    ResourceManagerCI.DefaultPoolDesc.VertexCount       = 4;
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager =
        GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
    ASSERT_NE(pResourceManager, nullptr);
    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    const std::array<std::array<RadientFloat3, 3>, 2> Positions{{
        {{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}},
        {{{2.f, 0.f, 0.f}, {3.f, 0.f, 0.f}, {2.f, 1.f, 0.f}}},
    }};
    const std::array<RadientFloat4, 3>                Colors{{
        {1.f, 0.f, 0.f, 1.f},
        {0.f, 1.f, 0.f, 1.f},
        {0.f, 0.f, 1.f, 1.f},
    }};
    const std::array<Uint32, 3>                       Indices{0, 1, 2};

    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    VertexLayout.AddBuffer().AddAttribute("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, 1);

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Colors);
    BlobCI.pData = Colors.data();
    RefCntAutoPtr<IRadientDataBlob> pColorsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pColorsBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(Indices);
    BlobCI.pData = Indices.data();
    RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pIndexBlob), RADIENT_STATUS_OK);

    RadientMeshPrimitiveCreateInfo PrimitiveCI;
    PrimitiveCI.ElementCount = static_cast<Uint32>(Indices.size());
    std::array<RefCntAutoPtr<IRadientMeshAsset>, 2> Meshes;
    IVertexPool*                                    pPool              = nullptr;
    Uint32                                          InitialPoolVersion = 0;
    Uint32                                          InitialBaseVertex  = 0;
    for (Uint32 MeshIndex = 0; MeshIndex < Meshes.size(); ++MeshIndex)
    {
        BlobCI.Size  = sizeof(Positions[MeshIndex]);
        BlobCI.pData = Positions[MeshIndex].data();
        RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
        IRadientDataBlob* const VertexBuffers[]{pPositionsBlob, pColorsBlob};

        RadientMeshCreateInfo MeshCI;
        MeshCI.VertexData.VertexLayout    = VertexLayout;
        MeshCI.VertexData.ppVertexBuffers = VertexBuffers;
        MeshCI.VertexData.VertexCount     = static_cast<Uint32>(Positions[MeshIndex].size());
        MeshCI.IndexData.pIndexBuffer     = pIndexBlob;
        MeshCI.IndexData.IndexCount       = static_cast<Uint32>(Indices.size());
        MeshCI.IndexData.IndexType        = RADIENT_INDEX_TYPE_UINT32;
        MeshCI.pPrimitives                = &PrimitiveCI;
        MeshCI.PrimitiveCount             = 1;
        EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &Meshes[MeshIndex])));
        ASSERT_NE(Meshes[MeshIndex], nullptr);
        ASSERT_TRUE(WaitForMeshLoadStatus(Meshes[MeshIndex], RADIENT_STATUS_OK));
        ASSERT_TRUE(WaitForMeshDrawableStatus(Meshes[MeshIndex], RADIENT_STATUS_OK, pUploadManager, pContext));

        const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Meshes[MeshIndex], true);
        ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
        ASSERT_NE(Result.pMesh, nullptr);
        ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
        const RadientDrawableMeshGeometry& Geometry = Result.pMesh->Geometries[0];
        ASSERT_NE(Geometry.pVertexPool, nullptr);
        EXPECT_EQ(Geometry.VertexBufferMask, (1u << 0) | (1u << 4));
        if (MeshIndex == 0)
        {
            pPool              = Geometry.pVertexPool;
            InitialPoolVersion = pPool->GetVersion();
            InitialBaseVertex  = Geometry.BaseVertex;
        }
        else
        {
            EXPECT_EQ(Geometry.pVertexPool, pPool);
            EXPECT_NE(Geometry.BaseVertex, InitialBaseVertex);
            EXPECT_GT(pPool->GetVersion(), InitialPoolVersion);
        }
    }

    // POSITION and COLOR_0 occupy separate 12-byte and 16-byte streams.
    // Absent attributes must not occupy storage in either the pool or reflection.
    const VertexPoolDesc& PoolDesc = pPool->GetDesc();
    ASSERT_EQ(PoolDesc.NumElements, 2u);
    EXPECT_EQ(PoolDesc.pElements[0].Size, 12u);
    EXPECT_EQ(PoolDesc.pElements[1].Size, 16u);
    EXPECT_GE(PoolDesc.VertexCount, 6u);
    ASSERT_NE(pPool->GetBuffer(0), nullptr);
    ASSERT_NE(pPool->GetBuffer(1), nullptr);

    BufferDesc ReadbackDesc;
    ReadbackDesc.Name           = "Compact vertex pool readback";
    ReadbackDesc.Size           = 3u * (12u + 16u);
    ReadbackDesc.Usage          = USAGE_STAGING;
    ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pReadbackBuffer;
    pDevice->CreateBuffer(ReadbackDesc, nullptr, &pReadbackBuffer);
    ASSERT_NE(pReadbackBuffer, nullptr);

    for (Uint32 MeshIndex = 0; MeshIndex < Meshes.size(); ++MeshIndex)
    {
        SCOPED_TRACE(MeshIndex);
        const RadientMeshAssetDesc& Desc = Meshes[MeshIndex]->GetDesc();
        ASSERT_EQ(Desc.GeometryCount, 1u);
        const RadientVertexLayoutDesc& Layout = Desc.pGeometries[0].VertexLayout;
        ASSERT_EQ(Layout.BufferCount, 2u);
        EXPECT_EQ(Layout.pBuffers[0].ByteStride, 12u);
        EXPECT_EQ(Layout.pBuffers[1].ByteStride, 16u);
        ASSERT_EQ(Layout.AttributeCount, 2u);
        EXPECT_STREQ(Layout.pAttributes[0].Semantic, "POSITION");
        EXPECT_EQ(Layout.pAttributes[0].BufferIndex, 0u);
        EXPECT_STREQ(Layout.pAttributes[1].Semantic, "COLOR_0");
        EXPECT_EQ(Layout.pAttributes[1].BufferIndex, 1u);

        const RadientDrawableMeshResolveResult Result     = RadientMeshAssetManager::GetDrawableMesh(Meshes[MeshIndex], true);
        const Uint64                           BaseVertex = Result.pMesh->Geometries[0].BaseVertex;
        pContext->CopyBuffer(pPool->GetBuffer(0), BaseVertex * 12u, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             pReadbackBuffer, 0, 3u * 12u, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->CopyBuffer(pPool->GetBuffer(1), BaseVertex * 16u, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             pReadbackBuffer, 3u * 12u, 3u * 16u, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->WaitForIdle();
        MapHelper<Float32>   MappedData{pContext, pReadbackBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT};
        const Float32* const pData = MappedData;
        ASSERT_NE(pData, nullptr);
        for (Uint32 Vertex = 0; Vertex < Positions[MeshIndex].size(); ++Vertex)
        {
            EXPECT_FLOAT_EQ(pData[Vertex * 3 + 0], Positions[MeshIndex][Vertex].x);
            EXPECT_FLOAT_EQ(pData[Vertex * 3 + 1], Positions[MeshIndex][Vertex].y);
            EXPECT_FLOAT_EQ(pData[Vertex * 3 + 2], Positions[MeshIndex][Vertex].z);
            EXPECT_FLOAT_EQ(pData[9 + Vertex * 4 + 0], Colors[Vertex].x);
            EXPECT_FLOAT_EQ(pData[9 + Vertex * 4 + 1], Colors[Vertex].y);
            EXPECT_FLOAT_EQ(pData[9 + Vertex * 4 + 2], Colors[Vertex].z);
            EXPECT_FLOAT_EQ(pData[9 + Vertex * 4 + 3], Colors[Vertex].w);
        }
    }
}

// The shared index buffer stores native 16-bit and 32-bit ranges. A three-index
// 16-bit range needs two padding bytes for four-byte-aligned GPU copies; UINT8
// input is widened to the same representation. Read back each range, including
// its padding, after all uploads to detect overlap or incorrect element offsets.
TEST(RadientMeshAssetManagerGPUTest, UploadsMixedIndexTypesWithAlignedPadding)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment*             pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*                     pDevice  = pEnv->GetDevice();
    IDeviceContext*                    pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard                  StopThreadPool{pThreadPool};
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);
    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    // Ensure every tested index range has a nonzero byte offset.
    const RefCntAutoPtr<IBufferSuballocation> pPrefix = pResourceManager->AllocateIndices(4);
    ASSERT_NE(pPrefix, nullptr);
    const RadientFloat3 Positions[]{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    const Uint16        Indices16[]{0, 2, 1};
    const Uint32        Indices32[]{2, 1, 0};
    const Uint8         Indices8[]{1, 0, 2};
    const Uint16        WidenedIndices8[]{1, 0, 2};
    struct IndexCase
    {
        RADIENT_INDEX_TYPE SourceType;
        const void*        pData;
        size_t             DataSize;
        VALUE_TYPE         StoredType;
        const void*        pExpected;
        Uint32             ExpectedSize;
    };
    const IndexCase Cases[]{
        {RADIENT_INDEX_TYPE_UINT16, Indices16, sizeof(Indices16), VT_UINT16, Indices16, sizeof(Indices16)},
        {RADIENT_INDEX_TYPE_UINT32, Indices32, sizeof(Indices32), VT_UINT32, Indices32, sizeof(Indices32)},
        {RADIENT_INDEX_TYPE_UINT8, Indices8, sizeof(Indices8), VT_UINT16, WidenedIndices8, sizeof(WidenedIndices8)},
    };

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Positions);
    BlobCI.pData = Positions;
    RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
    IRadientDataBlob* const  VertexBuffers[]{pPositionsBlob};
    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    RadientMeshPrimitiveCreateInfo Primitive;
    Primitive.ElementCount = 3;
    std::array<RefCntAutoPtr<IRadientMeshAsset>, 3> Meshes;
    for (Uint32 CaseIndex = 0; CaseIndex < Meshes.size(); ++CaseIndex)
    {
        SCOPED_TRACE(CaseIndex);
        const IndexCase& Case = Cases[CaseIndex];
        BlobCI.Size           = Case.DataSize;
        BlobCI.pData          = Case.pData;
        RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pIndexBlob), RADIENT_STATUS_OK);
        RadientMeshCreateInfo MeshCI;
        MeshCI.VertexData     = {VertexLayout, VertexBuffers, 3};
        MeshCI.IndexData      = {pIndexBlob, 3, Case.SourceType};
        MeshCI.pPrimitives    = &Primitive;
        MeshCI.PrimitiveCount = 1;
        EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &Meshes[CaseIndex])));
        ASSERT_NE(Meshes[CaseIndex], nullptr);
        ASSERT_TRUE(WaitForMeshLoadStatus(Meshes[CaseIndex], RADIENT_STATUS_OK));
        ASSERT_TRUE(WaitForMeshDrawableStatus(Meshes[CaseIndex], RADIENT_STATUS_OK, pUploadManager, pContext));
    }
    ASSERT_EQ(pResourceManager->GetIndexBufferCount(), 1u);
    IBuffer* const pIndexBuffer = pResourceManager->GetIndexBuffer();
    ASSERT_NE(pIndexBuffer, nullptr);
    BufferDesc ReadbackDesc;
    ReadbackDesc.Name           = "Mixed index types readback";
    ReadbackDesc.Size           = sizeof(Indices32);
    ReadbackDesc.Usage          = USAGE_STAGING;
    ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pReadbackBuffer;
    pDevice->CreateBuffer(ReadbackDesc, nullptr, &pReadbackBuffer);
    ASSERT_NE(pReadbackBuffer, nullptr);

    for (Uint32 CaseIndex = 0; CaseIndex < Meshes.size(); ++CaseIndex)
    {
        SCOPED_TRACE(CaseIndex);
        const IndexCase&                       Case   = Cases[CaseIndex];
        const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Meshes[CaseIndex], true);
        ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
        ASSERT_NE(Result.pMesh, nullptr);
        ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
        const RadientDrawableMeshGeometry& Geometry = Result.pMesh->Geometries[0];
        EXPECT_EQ(Geometry.IndexType, Case.StoredType);
        const RadientMeshAssetDesc& Desc = Meshes[CaseIndex]->GetDesc();
        ASSERT_EQ(Desc.GeometryCount, 1u);
        EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
        EXPECT_EQ(Desc.pGeometries[0].IndexType, Case.StoredType == VT_UINT16 ? RADIENT_INDEX_TYPE_UINT16 : RADIENT_INDEX_TYPE_UINT32);
        const Uint32 ElementSize = Case.StoredType == VT_UINT16 ? sizeof(Uint16) : sizeof(Uint32);
        const Uint64 ByteOffset  = Uint64{Geometry.FirstIndexLocation} * ElementSize;
        const Uint32 CopySize    = Case.StoredType == VT_UINT16 ? 8u : 12u;
        EXPECT_GE(ByteOffset, 4u);
        EXPECT_EQ(ByteOffset % 4u, 0u);
        pContext->CopyBuffer(pIndexBuffer, ByteOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             pReadbackBuffer, 0, CopySize, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->WaitForIdle();
        MapHelper<Uint8>   MappedData{pContext, pReadbackBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT};
        const Uint8* const pData = MappedData;
        ASSERT_NE(pData, nullptr);
        EXPECT_EQ(std::memcmp(pData, Case.pExpected, Case.ExpectedSize), 0);
        for (Uint32 Byte = Case.ExpectedSize; Byte < CopySize; ++Byte)
        {
            EXPECT_EQ(pData[Byte], 0u);
        }
    }
}

// Start with no GPU index buffer and upload a nonindexed vertex range into a
// nonzero pool location. Index storage must remain absent after processing and
// uploading the mesh, while the complete vertex data remains available.
TEST(RadientMeshAssetManagerGPUTest, UploadsNonIndexedVertexRangesWithoutIndexStorage)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment*             pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*                     pDevice  = pEnv->GetDevice();
    IDeviceContext*                    pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard               StopThreadPool{pThreadPool};
    GLTF::ResourceManager::CreateInfo ResourceManagerCI   = MakeResourceManagerCI();
    ResourceManagerCI.IndexAllocatorCI.Desc.Size          = 0;
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
    ASSERT_NE(pResourceManager, nullptr);
    ASSERT_EQ(pResourceManager->GetIndexBuffer(), nullptr);
    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);
    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    GLTF::ResourceManager::VertexLayoutKey PoolLayout;
    PoolLayout.Elements.emplace_back(static_cast<Uint32>(sizeof(RadientFloat3)), BIND_VERTEX_BUFFER);
    const RefCntAutoPtr<IVertexPoolAllocation> pPrefix = pResourceManager->AllocateVertices(PoolLayout, 4);
    ASSERT_NE(pPrefix, nullptr);
    const RadientFloat3 Positions[]{
        {},
        {},
        {},
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f},
    };
    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Positions);
    BlobCI.pData = Positions;
    RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
    IRadientDataBlob* const  VertexBuffers[]{pPositionsBlob};
    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    RadientMeshPrimitiveCreateInfo Primitive;
    Primitive.FirstElement = 3;
    Primitive.ElementCount = 3;
    RadientMeshCreateInfo MeshCI;
    MeshCI.VertexData     = {VertexLayout, VertexBuffers, 6};
    MeshCI.pPrimitives    = &Primitive;
    MeshCI.PrimitiveCount = 1;
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh)));
    ASSERT_NE(pMesh, nullptr);
    ASSERT_TRUE(WaitForMeshLoadStatus(pMesh, RADIENT_STATUS_OK));
    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_OK, pUploadManager, pContext));

    const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pMesh, nullptr);
    ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
    ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
    const RadientDrawableMeshGeometry& Geometry = Result.pMesh->Geometries[0];
    EXPECT_GE(Geometry.BaseVertex, 4u);
    EXPECT_EQ(Geometry.IndexType, VT_UNDEFINED);
    EXPECT_FALSE(Result.pMesh->Primitives[0].IsIndexed);
    EXPECT_EQ(Result.pMesh->Primitives[0].FirstElement, 3u);
    EXPECT_EQ(Result.pMesh->Primitives[0].ElementCount, 3u);
    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    EXPECT_EQ(Desc.pGeometries[0].IndexType, RADIENT_INDEX_TYPE_NONE);
    EXPECT_EQ(Desc.pGeometries[0].IndexCount, 0u);
    EXPECT_EQ(Desc.pGeometries[0].VertexCount, 6u);
    EXPECT_EQ(pResourceManager->GetIndexBufferUsageStats().AllocationCount, 0u);
    EXPECT_EQ(pResourceManager->GetIndexBufferUsageStats().UsedSize, 0u);
    EXPECT_EQ(pResourceManager->GetIndexBuffer(), nullptr);

    BufferDesc ReadbackDesc;
    ReadbackDesc.Name           = "Nonindexed vertex range readback";
    ReadbackDesc.Size           = sizeof(Positions);
    ReadbackDesc.Usage          = USAGE_STAGING;
    ReadbackDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pReadbackBuffer;
    pDevice->CreateBuffer(ReadbackDesc, nullptr, &pReadbackBuffer);
    ASSERT_NE(pReadbackBuffer, nullptr);
    ASSERT_NE(Geometry.pVertexPool, nullptr);
    pContext->CopyBuffer(Geometry.pVertexPool->GetBuffer(0), Uint64{Geometry.BaseVertex} * sizeof(RadientFloat3), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                         pReadbackBuffer, 0, sizeof(Positions), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->WaitForIdle();
    MapHelper<Uint8>   MappedData{pContext, pReadbackBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT};
    const Uint8* const pData = MappedData;
    ASSERT_NE(pData, nullptr);
    EXPECT_EQ(std::memcmp(pData, Positions, sizeof(Positions)), 0);
}

TEST(RadientMeshAssetManagerGPUTest, DisablesMorphTargetsWhenAllocatorSizeIsZero)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    IRenderDevice* pDevice = GPUTestingEnvironment::GetInstance()->GetDevice();
    ASSERT_NE(pDevice, nullptr);

    GLTF::ResourceManager::CreateInfo ResourceManagerCI = MakeResourceManagerCI();
    ResourceManagerCI.MorphTargetAllocatorCI            = {};
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager =
        GLTF::ResourceManager::Create(pDevice, ResourceManagerCI);
    ASSERT_NE(pResourceManager, nullptr);
    EXPECT_EQ(pResourceManager->GetMorphTargetBuffer(), nullptr);
    EXPECT_EQ(pResourceManager->AllocateMorphTargetData(sizeof(Float32)), nullptr);
    EXPECT_EQ(pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount, 0u);
}

TEST(RadientMeshAssetManagerGPUTest, MeshViewAcceptsPendingMorphTargetCacheHit)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment*             pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*                     pDevice  = pEnv->GetDevice();
    IDeviceContext*                    pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pOwnerThreadPool     = CreateThreadPool(ThreadPoolCreateInfo{2});
    RefCntAutoPtr<IThreadPool> pDuplicateThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pOwnerThreadPool, nullptr);
    ASSERT_NE(pDuplicateThreadPool, nullptr);
    ThreadPoolStopGuard StopOwnerThreadPool{pOwnerThreadPool};
    ThreadPoolStopGuard StopDuplicateThreadPool{pDuplicateThreadPool};

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);
    // Without a context, no upload pages can be mapped until we pump uploads.
    // Both original data tasks stay inside ScheduleBufferUpdate after publishing
    // their payloads, so duplicate tasks can finish with pending cache entries.
    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, nullptr);
    ASSERT_NE(pUploadManager, nullptr);
    struct UploadStopGuard
    {
        IGPUUploadManager& UploadManager;
        IDeviceContext&    Context;

        ~UploadStopGuard()
        {
            // Fatal assertions must unblock uploads before the pool guards join workers.
            UploadManager.Stop(&Context);
        }
    } StopUploads{*pUploadManager, *pContext};

    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    const RadientFloat3       Positions[] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    const RadientFloat3       Deltas[]    = {{0.f, 0.f, 0.f}, {0.1f, 0.2f, 0.3f}, {-0.1f, -0.2f, -0.3f}};
    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Positions);
    BlobCI.pData = Positions;
    RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(Deltas);
    BlobCI.pData = Deltas;
    RefCntAutoPtr<IRadientDataBlob> pDeltaBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pDeltaBlob), RADIENT_STATUS_OK);

    IRadientDataBlob* const  VertexBuffers[] = {pPositionsBlob};
    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    RadientMorphTargetAttributeDesc Attribute;
    Attribute.Semantic       = RadientMorphTargetPositionSemantic;
    Attribute.ComponentCount = 3;
    RadientMorphTargetAttributeCreateInfo AttributeData;
    AttributeData.pDeltaBuffer = pDeltaBlob;
    RadientMorphTargetCreateInfo Target;
    Target.Desc.Name           = "Pending morph target";
    Target.Desc.pAttributes    = &Attribute;
    Target.Desc.AttributeCount = 1;
    Target.pAttributeData      = &AttributeData;
    RadientMeshPrimitiveCreateInfo Primitive;
    Primitive.ElementCount = 3;
    RadientMeshCreateInfo MeshCI;
    MeshCI.VertexData                       = {VertexLayout, VertexBuffers, 3};
    MeshCI.MorphTargetData.pMorphTargets    = &Target;
    MeshCI.MorphTargetData.MorphTargetCount = 1;
    MeshCI.pPrimitives                      = &Primitive;
    MeshCI.PrimitiveCount                   = 1;

    RefCntAutoPtr<IRadientMeshAsset> pOwnerMesh;
    ASSERT_EQ(pMeshManager->CreateMesh(*pOwnerThreadPool, MeshCI, &pOwnerMesh), RADIENT_STATUS_PENDING);
    ASSERT_NE(pOwnerMesh, nullptr);
    const std::chrono::steady_clock::time_point AllocationDeadline = std::chrono::steady_clock::now() + 5s;
    while ((pResourceManager->GetVertexPoolUsageStats().AllocationCount != 1 ||
            pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount != 1) &&
           std::chrono::steady_clock::now() < AllocationDeadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_EQ(pResourceManager->GetVertexPoolUsageStats().AllocationCount, 1u);
    ASSERT_EQ(pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount, 1u);
    ASSERT_EQ(RadientMeshAssetManager::GetLoadStatus(pOwnerMesh), RADIENT_STATUS_PENDING);
    ASSERT_EQ(RadientMeshAssetManager::GetMeshPayload(pOwnerMesh), nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pDuplicateMesh;
    ASSERT_EQ(pMeshManager->CreateMesh(*pDuplicateThreadPool, MeshCI, &pDuplicateMesh), RADIENT_STATUS_PENDING);
    ASSERT_NE(pDuplicateMesh, nullptr);
    // Run the vertex cache hit, morph cache hit, and dependent view explicitly.
    // The original workers remain blocked because uploads have not been pumped.
    ASSERT_EQ(pDuplicateThreadPool->GetQueueSize(), 3u);
    ASSERT_TRUE(pDuplicateThreadPool->ProcessTask(0, false));
    ASSERT_EQ(pDuplicateThreadPool->GetQueueSize(), 2u);
    ASSERT_TRUE(pDuplicateThreadPool->ProcessTask(0, false));
    ASSERT_EQ(pDuplicateThreadPool->GetQueueSize(), 1u);
    ASSERT_TRUE(pDuplicateThreadPool->ProcessTask(0, false));
    ASSERT_EQ(pDuplicateThreadPool->GetQueueSize(), 0u);
    ASSERT_EQ(RadientMeshAssetManager::GetLoadStatus(pDuplicateMesh), RADIENT_STATUS_PENDING);
    const MeshPayloadImpl*                pPendingMeshPayload = RadientMeshAssetManager::GetMeshPayload(pDuplicateMesh);
    const MeshMorphTargetDataPayloadImpl* pPendingMorphPayload =
        RadientMeshAssetManager::GetMeshMorphTargetDataPayload(pDuplicateMesh, 0);
    ASSERT_NE(pPendingMeshPayload, nullptr);
    ASSERT_NE(pPendingMorphPayload, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pOwnerMesh), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(pOwnerMesh), nullptr);

    ASSERT_TRUE(WaitForMeshDrawableStatus(pDuplicateMesh, RADIENT_STATUS_OK, pUploadManager, pContext));
    ASSERT_TRUE(WaitForMeshLoadStatus(pOwnerMesh, RADIENT_STATUS_OK));
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pDuplicateMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(pDuplicateMesh), pPendingMeshPayload);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshMorphTargetDataPayload(pDuplicateMesh, 0), pPendingMorphPayload);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshMorphTargetDataPayload(pOwnerMesh, 0), pPendingMorphPayload);
    ASSERT_EQ(pDuplicateMesh->GetDesc().MorphTargetCount, 1u);
    EXPECT_STREQ(pDuplicateMesh->GetDesc().pMorphTargets[0].Name, "Pending morph target");
    EXPECT_EQ(pResourceManager->GetVertexPoolUsageStats().AllocationCount, 1u);
    EXPECT_EQ(pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount, 1u);
}

TEST(RadientMeshAssetManagerGPUTest, UploadsMorphTargetsToGPUBuffer)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreadPool{pThreadPool};

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);
    EXPECT_NE(pResourceManager->GetMorphTargetBuffer(), nullptr);
    EXPECT_EQ(pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount, 0u);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    const RadientFloat3 Positions[] =
        {
            {0.f, 0.f, 0.f},
            {1.f, 0.f, 0.f},
            {0.f, 1.f, 0.f},
        };
    const Uint32  Indices[] = {0, 1, 2};
    const Float32 PositionDeltas[] =
        {
            0.f,
            0.f,
            0.f,
            0.1f,
            0.2f,
            0.3f,
            -0.1f,
            -0.2f,
            -0.3f,
        };

    RadientMorphTargetAttributeDesc AttributeDesc{};
    AttributeDesc.Semantic       = RadientMorphTargetPositionSemantic;
    AttributeDesc.ComponentCount = 3;

    RadientMorphTargetAttributeCreateInfo AttributeCI{};

    RadientMorphTargetCreateInfo MorphTargetCI{};
    MorphTargetCI.Desc.Name           = "Position offset";
    MorphTargetCI.Desc.pAttributes    = &AttributeDesc;
    MorphTargetCI.Desc.AttributeCount = 1;
    MorphTargetCI.pAttributeData      = &AttributeCI;

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.ElementCount = 3;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name = "Radient morph target GPU upload";
    RadientVertexLayoutDescX VertexLayout;
    VertexLayout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = sizeof(Positions);
    BlobCI.pData = Positions;
    RefCntAutoPtr<IRadientDataBlob> pPositionsBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pPositionsBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(Indices);
    BlobCI.pData = Indices;
    RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pIndexBlob), RADIENT_STATUS_OK);
    BlobCI.Size  = sizeof(PositionDeltas);
    BlobCI.pData = PositionDeltas;
    RefCntAutoPtr<IRadientDataBlob> pDeltaBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pDeltaBlob), RADIENT_STATUS_OK);
    AttributeCI.pDeltaBuffer = pDeltaBlob;
    IRadientDataBlob* const VertexData[]{pPositionsBlob};

    MeshCI.VertexData.VertexLayout          = VertexLayout;
    MeshCI.VertexData.ppVertexBuffers       = VertexData;
    MeshCI.VertexData.VertexCount           = 3;
    MeshCI.MorphTargetData.pMorphTargets    = &MorphTargetCI;
    MeshCI.MorphTargetData.MorphTargetCount = 1;
    MeshCI.IndexData.pIndexBuffer           = pIndexBlob;
    MeshCI.IndexData.IndexCount             = 3;
    MeshCI.IndexData.IndexType              = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives                      = &PrimitiveCI;
    MeshCI.PrimitiveCount                   = 1;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh)));
    ASSERT_NE(pMesh, nullptr);
    ASSERT_TRUE(WaitForMeshLoadStatus(pMesh, RADIENT_STATUS_OK));
    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_OK, pUploadManager, pContext));

    const RadientDrawableMeshResolveResult DrawableResult =
        RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
    ASSERT_EQ(DrawableResult.Status, RADIENT_STATUS_OK);
    ASSERT_NE(DrawableResult.pMesh, nullptr);
    ASSERT_EQ(DrawableResult.pMesh->Geometries.size(), 1u);

    const RadientDrawableMeshGeometry&  Geometry         = DrawableResult.pMesh->Geometries[0];
    const RadientMorphTargetData* const pMorphTargetData = Geometry.pMorphTargetData;
    ASSERT_NE(pMorphTargetData, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(pMorphTargetData->GetDataSize(), sizeof(PositionDeltas));
    EXPECT_EQ(pMorphTargetData->GetAttributeDataOffset(0, 0), 0u);

    IBuffer* const pMorphTargetBuffer = pResourceManager->GetMorphTargetBuffer();
    ASSERT_NE(pMorphTargetBuffer, nullptr);
    EXPECT_LE(Uint64{Geometry.MorphTargetDataOffset} + pMorphTargetData->GetDataSize(),
              pMorphTargetBuffer->GetDesc().Size);
    EXPECT_EQ(pResourceManager->GetMorphTargetBufferUsageStats().AllocationCount, 1u);

    const BufferDesc& BufferDesc = pMorphTargetBuffer->GetDesc();
    EXPECT_EQ(BufferDesc.Usage, USAGE_DEFAULT);
    EXPECT_NE(BufferDesc.BindFlags & BIND_SHADER_RESOURCE, BIND_NONE);
    EXPECT_EQ(BufferDesc.Mode, BUFFER_MODE_STRUCTURED);
    EXPECT_EQ(BufferDesc.ElementByteStride, sizeof(Float32));

    pThreadPool->StopThreads();
    StopThreadPool.pThreadPool = nullptr;
}

} // namespace
