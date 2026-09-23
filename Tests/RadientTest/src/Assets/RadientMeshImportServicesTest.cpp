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

#include "RadientMeshImportServices.h"
#include "RadientTypesX.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <string>

using namespace Diligent;

namespace
{

class RadientMeshImportServicesTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // No workers run until WaitForAssetLoad processes the queue. This makes
        // source ownership and views of pending data deterministic.
        pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
        RadientAssetManagerImpl::CreateInfo CI;
        CI.pThreadPool                                        = pThreadPool;
        const RefCntAutoPtr<RadientAssetManagerImpl> pManager = RadientAssetManagerImpl::Create(CI);
        ASSERT_NE(pManager, nullptr);
        pAssetManager       = pManager;
        pMeshImportServices = pManager->CreateMeshImportServices();
        ASSERT_NE(pMeshImportServices, nullptr);
    }

    void TearDown() override
    {
        for (Uint32 Iteration = 0; pThreadPool->GetQueueSize() != 0 && Iteration < 64; ++Iteration)
        {
            EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
        }
        EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
        pThreadPool->StopThreads();
    }

    RefCntAutoPtr<IRadientMeshVertexData> CreateVertices(Float32 Offset = 0.f)
    {
        const std::array<RadientFloat3, 3> Positions{{{Offset, 0.f, 0.f}, {Offset + 1.f, 0.f, 0.f}, {Offset, 1.f, 0.f}}};
        RadientVertexLayoutDescX           Layout;
        Layout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
        const RefCntAutoPtr<IRadientDataBlob> pBlob  = Testing::MakeTestDataBlob(Positions.data(), sizeof(Positions));
        IRadientDataBlob* const               Buffer = pBlob;
        RadientMeshVertexDataCreateInfo       CI;
        CI.VertexLayout    = Layout;
        CI.ppVertexBuffers = &Buffer;
        CI.VertexCount     = static_cast<Uint32>(Positions.size());
        RefCntAutoPtr<IRadientMeshVertexData> pData;
        EXPECT_EQ(pMeshImportServices->CreateMeshVertexData(CI, &pData), RADIENT_STATUS_PENDING);
        return pData;
    }

    RefCntAutoPtr<IRadientMeshIndexData> CreateIndices(bool Reverse = false)
    {
        const std::array<Uint8, 3>            Indices = Reverse ? std::array<Uint8, 3>{0, 2, 1} : std::array<Uint8, 3>{0, 1, 2};
        const RefCntAutoPtr<IRadientDataBlob> pBlob   = Testing::MakeTestDataBlob(Indices.data(), sizeof(Indices));
        RadientMeshIndexDataCreateInfo        CI;
        CI.pIndexBuffer = pBlob;
        CI.IndexCount   = static_cast<Uint32>(Indices.size());
        CI.IndexType    = RADIENT_INDEX_TYPE_UINT8;
        RefCntAutoPtr<IRadientMeshIndexData> pData;
        EXPECT_EQ(pMeshImportServices->CreateMeshIndexData(CI, &pData), RADIENT_STATUS_PENDING);
        return pData;
    }

    RefCntAutoPtr<IRadientMeshAsset> CreateView(const RadientMeshGeometryData& Geometry)
    {
        RadientMeshPrimitiveCreateInfo Primitive;
        Primitive.IndexCount = 3;
        RadientMeshViewCreateInfo CI;
        CI.pPrimitives    = &Primitive;
        CI.PrimitiveCount = 1;
        CI.pGeometryData  = &Geometry;
        CI.GeometryCount  = 1;
        RefCntAutoPtr<IRadientMeshAsset> pMesh;
        EXPECT_EQ(pMeshImportServices->CreateMeshView(CI, &pMesh), RADIENT_STATUS_PENDING);
        return pMesh;
    }

    RefCntAutoPtr<IThreadPool>                pThreadPool;
    RefCntAutoPtr<IRadientAssetManager>       pAssetManager;
    RefCntAutoPtr<IRadientMeshImportServices> pMeshImportServices;
};

template <typename InterfaceType>
void ExpectAssetInterface(InterfaceType* pData, const INTERFACE_ID& IID, RADIENT_ASSET_TYPE Type)
{
    ASSERT_NE(pData, nullptr);
    EXPECT_EQ(pData->GetType(), Type);
    EXPECT_NE(pData->GetReference().URI, nullptr);
    EXPECT_NE(pData->GetReference().Version, 0u);
    RefCntAutoPtr<InterfaceType> pTyped{pData, IID};
    RefCntAutoPtr<IRadientAsset> pAsset{pData, IID_RadientAsset};
    EXPECT_EQ(pTyped.RawPtr(), pData);
    EXPECT_EQ(pAsset.RawPtr(), static_cast<IRadientAsset*>(pData));
}

TEST_F(RadientMeshImportServicesTest, ServicesAreSeparateFromAndRetainTheAssetManager)
{
    RefCntAutoPtr<IRadientMeshImportServices> pQueried{pMeshImportServices, IID_RadientMeshImportServices};
    EXPECT_EQ(pQueried, pMeshImportServices);
    const RefCntAutoPtr<IRadientMeshImportServices> pManagerServices{pAssetManager, IID_RadientMeshImportServices};
    EXPECT_EQ(pManagerServices, nullptr);
    pQueried.Release();

    RefCntWeakPtr<IRadientAssetManager> WeakManager{pAssetManager.RawPtr()};
    pAssetManager.Release();
    RefCntAutoPtr<IRadientMeshVertexData> pVertices        = CreateVertices();
    RefCntAutoPtr<IRadientAssetManager>   pRetainedManager = WeakManager.Lock();
    ASSERT_NE(pRetainedManager, nullptr);
    ASSERT_EQ(pRetainedManager->WaitForAssetLoad(pVertices), RADIENT_STATUS_OK);
    pRetainedManager.Release();
    EXPECT_NE(WeakManager.Lock(), nullptr);

    // The service owns the manager; opaque data handles do not keep it alive.
    pMeshImportServices.Release();
    EXPECT_EQ(WeakManager.Lock(), nullptr);
}

TEST_F(RadientMeshImportServicesTest, PendingViewsRetainSourcesAndCopyDescriptors)
{
    Uint32                                 VertexReadReleases = 0;
    Uint32                                 IndexReadReleases  = 0;
    const std::array<RadientFloat3, 3>     Positions{{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}};
    const std::array<Uint8, 3>             Indices{0, 1, 2};
    RefCntAutoPtr<IRadientMutableDataBlob> pVertexBlob = Testing::MakeTestMutableDataBlob(
        Positions.data(), sizeof(Positions), Testing::CountBlobReadReleases, &VertexReadReleases);
    RefCntAutoPtr<IRadientMutableDataBlob> pIndexBlob = Testing::MakeTestMutableDataBlob(
        Indices.data(), sizeof(Indices), Testing::CountBlobReadReleases, &IndexReadReleases);
    RefCntWeakPtr<IRadientDataBlob> WeakVertexBlob{pVertexBlob.RawPtr()};
    RefCntWeakPtr<IRadientDataBlob> WeakIndexBlob{pIndexBlob.RawPtr()};

    RefCntAutoPtr<IRadientMeshVertexData> pVertices;
    RefCntAutoPtr<IRadientMeshIndexData>  pIndices;
    RefCntAutoPtr<IRadientMeshAsset>      pMesh;
    {
        std::string              Semantic{"POSITION"};
        RadientVertexLayoutDescX Layout;
        Layout.AddBuffer().AddAttribute(Semantic.c_str(), RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
        IRadientDataBlob*               Buffer = pVertexBlob;
        RadientMeshVertexDataCreateInfo VertexCI;
        VertexCI.VertexLayout    = Layout;
        VertexCI.ppVertexBuffers = &Buffer;
        VertexCI.VertexCount     = static_cast<Uint32>(Positions.size());
        ASSERT_EQ(pMeshImportServices->CreateMeshVertexData(VertexCI, &pVertices), RADIENT_STATUS_PENDING);

        RadientMeshIndexDataCreateInfo IndexCI;
        IndexCI.pIndexBuffer = pIndexBlob;
        IndexCI.IndexCount   = static_cast<Uint32>(Indices.size());
        IndexCI.IndexType    = RADIENT_INDEX_TYPE_UINT8;
        ASSERT_EQ(pMeshImportServices->CreateMeshIndexData(IndexCI, &pIndices), RADIENT_STATUS_PENDING);

        std::string                    PrimitiveName{"imported triangle"};
        RadientMeshPrimitiveCreateInfo Primitive;
        Primitive.Name       = PrimitiveName.c_str();
        Primitive.IndexCount = 3;
        RadientMeshGeometryData   Geometry{pVertices, pIndices};
        Uint32                    GeometryIndex = 0;
        RadientMeshViewCreateInfo ViewCI;
        ViewCI.pPrimitives      = &Primitive;
        ViewCI.PrimitiveCount   = 1;
        ViewCI.pGeometryIndices = &GeometryIndex;
        ViewCI.pGeometryData    = &Geometry;
        ViewCI.GeometryCount    = 1;
        ASSERT_EQ(pMeshImportServices->CreateMeshView(ViewCI, &pMesh), RADIENT_STATUS_PENDING);

        // All descriptors can be overwritten immediately, including strings,
        // the buffer-pointer array, geometry references, and primitive ranges.
        Semantic[0]          = 'X';
        Buffer               = nullptr;
        PrimitiveName[0]     = 'X';
        Primitive.IndexCount = 0;
        GeometryIndex        = 99;
        Geometry             = {};
    }
    ExpectAssetInterface(pVertices.RawPtr(), IID_RadientMeshVertexData, RADIENT_ASSET_TYPE_MESH_VERTEX_DATA);
    ExpectAssetInterface(pIndices.RawPtr(), IID_RadientMeshIndexData, RADIENT_ASSET_TYPE_MESH_INDEX_DATA);

    void* pWriteData = nullptr;
    EXPECT_EQ(pVertexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pIndexBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    pVertexBlob.Release();
    pIndexBlob.Release();
    EXPECT_NE(WeakVertexBlob.Lock(), nullptr);
    EXPECT_NE(WeakIndexBlob.Lock(), nullptr);
    EXPECT_EQ(VertexReadReleases, 0u);
    EXPECT_EQ(IndexReadReleases, 0u);

    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pVertices), RADIENT_STATUS_OK);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pIndices), RADIENT_STATUS_OK);
    EXPECT_EQ(VertexReadReleases, 1u);
    EXPECT_EQ(IndexReadReleases, 1u);
    EXPECT_EQ(WeakVertexBlob.Lock(), nullptr);
    EXPECT_EQ(WeakIndexBlob.Lock(), nullptr);

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    EXPECT_STREQ(Desc.pPrimitives[0].Name, "imported triangle");
    EXPECT_EQ(Desc.pPrimitives[0].GeometryIndex, 0u);
    EXPECT_EQ(Desc.pPrimitives[0].ElementCount, 3u);
    EXPECT_EQ(Desc.pGeometries[0].VertexCount, 3u);
    EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
    // Reflection describes the renderer's packed data, not the UINT8 source.
    EXPECT_EQ(Desc.pGeometries[0].IndexType, RADIENT_INDEX_TYPE_UINT32);
    ASSERT_GT(Desc.pGeometries[0].VertexLayout.AttributeCount, 0u);
    EXPECT_STREQ(Desc.pGeometries[0].VertexLayout.pAttributes[0].Semantic, "POSITION");
}

TEST_F(RadientMeshImportServicesTest, ReusesVertexAndIndexDataIndependentlyAndDeduplicatesPayloads)
{
    RefCntAutoPtr<IRadientMeshVertexData>         pVertices          = CreateVertices();
    RefCntAutoPtr<IRadientMeshVertexData>         pOtherVertices     = CreateVertices(2.f);
    RefCntAutoPtr<IRadientMeshVertexData>         pDuplicateVertices = CreateVertices();
    RefCntAutoPtr<IRadientMeshIndexData>          pIndices           = CreateIndices();
    RefCntAutoPtr<IRadientMeshIndexData>          pOtherIndices      = CreateIndices(true);
    RefCntAutoPtr<IRadientMeshIndexData>          pDuplicateIndices  = CreateIndices();
    const std::array<RadientMeshGeometryData, 3>  Geometries{{{pVertices, pIndices}, {pVertices, pOtherIndices}, {pOtherVertices, pIndices}}};
    const std::array<Uint32, 4>                   GeometryIndices{2, 0, 1, 2};
    std::array<RadientMeshPrimitiveCreateInfo, 4> Primitives{};
    for (RadientMeshPrimitiveCreateInfo& Primitive : Primitives)
        Primitive.IndexCount = 3;
    RadientMeshViewCreateInfo CI;
    CI.pPrimitives      = Primitives.data();
    CI.PrimitiveCount   = static_cast<Uint32>(Primitives.size());
    CI.pGeometryIndices = GeometryIndices.data();
    CI.pGeometryData    = Geometries.data();
    CI.GeometryCount    = static_cast<Uint32>(Geometries.size());
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    ASSERT_EQ(pMeshImportServices->CreateMeshView(CI, &pMesh), RADIENT_STATUS_PENDING);
    RefCntAutoPtr<IRadientMeshAsset> pDuplicate = CreateView({pDuplicateVertices, pDuplicateIndices});
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pMesh), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pDuplicate), RADIENT_STATUS_OK);

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 3u);
    ASSERT_EQ(Desc.PrimitiveCount, 4u);
    const std::array<Uint32, 4> CanonicalIndices{0, 1, 2, 0};
    for (Uint32 Index = 0; Index < Desc.PrimitiveCount; ++Index)
        EXPECT_EQ(Desc.pPrimitives[Index].GeometryIndex, CanonicalIndices[Index]);

    // Inspect only payload identity: public reflection alone cannot establish
    // that independently created handles share the same data allocation.
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 1), RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 2));
    EXPECT_NE(RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 0), RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 1));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 0), RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 1));
    EXPECT_NE(RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 1), RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 2));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(pMesh, 1), RadientMeshAssetManager::GetMeshVertexDataPayload(pDuplicate, 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(pMesh, 1), RadientMeshAssetManager::GetMeshIndexDataPayload(pDuplicate, 0));
}

TEST_F(RadientMeshImportServicesTest, CopiesMorphDataBeforeReturningAndPreservesWeights)
{
    RefCntAutoPtr<IRadientMeshVertexData>      pVertices = CreateVertices();
    RefCntAutoPtr<IRadientMeshIndexData>       pIndices  = CreateIndices();
    RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphData;
    RefCntAutoPtr<IRadientMeshMorphTargetData> pDuplicateMorphData;
    {
        std::string                             Name{"Smile"};
        std::string                             Semantic{"POSITION"};
        static constexpr std::array<Float32, 9> OriginalDeltas{0.f, 0.f, 0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
        std::array<Float32, 9>                  Deltas = OriginalDeltas;
        RadientMorphTargetAttributeDesc         Attribute;
        Attribute.Semantic       = Semantic.c_str();
        Attribute.ComponentCount = 3;
        RadientMorphTargetAttributeCreateInfo AttributeData;
        AttributeData.pDeltas = Deltas.data();
        RadientMorphTargetCreateInfo Target;
        Target.Desc.Name           = Name.c_str();
        Target.Desc.pAttributes    = &Attribute;
        Target.Desc.AttributeCount = 1;
        Target.Desc.DefaultWeight  = 0.25f;
        Target.pAttributeData      = &AttributeData;
        RadientMeshMorphTargetDataCreateInfo CI;
        CI.pMorphTargets    = &Target;
        CI.MorphTargetCount = 1;
        CI.VertexCount      = 3;
        ASSERT_EQ(pMeshImportServices->CreateMeshMorphTargetData(CI, &pMorphData), RADIENT_STATUS_PENDING);
        // An independent source with unchanged bytes must deduplicate with the
        // first call even after the first call's delta array is overwritten.
        RadientMorphTargetAttributeCreateInfo DuplicateAttributeData;
        DuplicateAttributeData.pDeltas               = OriginalDeltas.data();
        RadientMorphTargetCreateInfo DuplicateTarget = Target;
        DuplicateTarget.pAttributeData               = &DuplicateAttributeData;
        CI.pMorphTargets                             = &DuplicateTarget;
        ASSERT_EQ(pMeshImportServices->CreateMeshMorphTargetData(CI, &pDuplicateMorphData), RADIENT_STATUS_PENDING);
        Deltas.fill(999.f);
        Name[0]                   = 'X';
        Semantic[0]               = 'X';
        Attribute.ComponentCount  = 1;
        Target.Desc.DefaultWeight = 1.f;
        Target.pAttributeData     = nullptr;
    }
    ExpectAssetInterface(pMorphData.RawPtr(), IID_RadientMeshMorphTargetData, RADIENT_ASSET_TYPE_MESH_MORPH_TARGET_DATA);
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pMorphData), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pDuplicateMorphData), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = CreateView({pVertices, pIndices, pMorphData});
    RefCntAutoPtr<IRadientMeshAsset> pDuplicate = CreateView({pVertices, pIndices, pDuplicateMorphData});
    pMorphData.Release();
    pDuplicateMorphData.Release();
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pMesh), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssetManager->WaitForAssetLoad(pDuplicate), RADIENT_STATUS_OK);

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.MorphTargetCount, 1u);
    EXPECT_STREQ(Desc.pMorphTargets[0].Name, "Smile");
    ASSERT_EQ(Desc.pMorphTargets[0].AttributeCount, 1u);
    EXPECT_STREQ(Desc.pMorphTargets[0].pAttributes[0].Semantic, "POSITION");
    EXPECT_EQ(Desc.pMorphTargets[0].pAttributes[0].ComponentCount, 3u);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshMorphTargetDataPayload(pMesh, 0), RadientMeshAssetManager::GetMeshMorphTargetDataPayload(pDuplicate, 0));
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(&pWeights), RADIENT_STATUS_OK);
    ASSERT_EQ(pWeights->GetWeightCount(), 1u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
}

TEST_F(RadientMeshImportServicesTest, RejectsMissingOutputAndInvalidViewReferences)
{
    EXPECT_EQ(pMeshImportServices->CreateMeshVertexData({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMeshImportServices->CreateMeshIndexData({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMeshImportServices->CreateMeshMorphTargetData({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMeshImportServices->CreateMeshView({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientMeshVertexData> pVertices = CreateVertices();
    RefCntAutoPtr<IRadientMeshIndexData>  pIndices  = CreateIndices();
    RadientMeshGeometryData               Geometry{pVertices, pIndices};
    RadientMeshPrimitiveCreateInfo        Primitive;
    Primitive.IndexCount                    = 3;
    Uint32                    GeometryIndex = 1;
    RadientMeshViewCreateInfo CI;
    CI.pPrimitives      = &Primitive;
    CI.PrimitiveCount   = 1;
    CI.pGeometryIndices = &GeometryIndex;
    CI.pGeometryData    = &Geometry;
    CI.GeometryCount    = 1;
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(pMeshImportServices->CreateMeshView(CI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
    GeometryIndex    = 0;
    CI.pGeometryData = nullptr;
    EXPECT_EQ(pMeshImportServices->CreateMeshView(CI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
}

TEST_F(RadientMeshImportServicesTest, RejectsInvalidIndexAndMorphSourcesWithoutQueuingWork)
{
    const std::array<Uint8, 3>            Indices{0, 1, 2};
    const RefCntAutoPtr<IRadientDataBlob> pBlob = Testing::MakeTestDataBlob(Indices.data(), sizeof(Indices));
    RadientMeshIndexDataCreateInfo        IndexCI;
    IndexCI.pIndexBuffer = pBlob;
    IndexCI.IndexCount   = 3;
    IndexCI.IndexType    = RADIENT_INDEX_TYPE_NONE;
    RefCntAutoPtr<IRadientMeshIndexData> pIndices;
    EXPECT_EQ(pMeshImportServices->CreateMeshIndexData(IndexCI, &pIndices), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pIndices, nullptr);
    // UINT16 input needs six bytes; the source contains only three.
    IndexCI.IndexType = RADIENT_INDEX_TYPE_UINT16;
    EXPECT_EQ(pMeshImportServices->CreateMeshIndexData(IndexCI, &pIndices), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pIndices, nullptr);

    RadientMeshMorphTargetDataCreateInfo MorphCI;
    MorphCI.MorphTargetCount = 1;
    MorphCI.VertexCount      = 3;
    RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphData;
    EXPECT_EQ(pMeshImportServices->CreateMeshMorphTargetData(MorphCI, &pMorphData), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMorphData, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
}

TEST_F(RadientMeshImportServicesTest, RejectsActiveWritersBeforeQueuingWork)
{
    RefCntAutoPtr<IRadientMutableDataBlob> pBlob = Testing::MakeTestMutableDataBlob(nullptr, 3 * sizeof(RadientFloat3));
    void*                                  pData = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pData), RADIENT_STATUS_OK);
    RadientVertexLayoutDescX Layout;
    Layout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    IRadientDataBlob* const         Buffer = pBlob;
    RadientMeshVertexDataCreateInfo VertexCI;
    VertexCI.VertexLayout    = Layout;
    VertexCI.ppVertexBuffers = &Buffer;
    VertexCI.VertexCount     = 3;
    RadientMeshIndexDataCreateInfo IndexCI;
    IndexCI.pIndexBuffer = pBlob;
    IndexCI.IndexCount   = 3;
    IndexCI.IndexType    = RADIENT_INDEX_TYPE_UINT8;
    RefCntAutoPtr<IRadientMeshVertexData> pVertices;
    RefCntAutoPtr<IRadientMeshIndexData>  pIndices;
    EXPECT_EQ(pMeshImportServices->CreateMeshVertexData(VertexCI, &pVertices), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMeshImportServices->CreateMeshIndexData(IndexCI, &pIndices), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pVertices, nullptr);
    EXPECT_EQ(pIndices, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
}

TEST_F(RadientMeshImportServicesTest, RejectsGeometryDataFromAnotherManager)
{
    RadientAssetManagerImpl::CreateInfo OtherCI;
    OtherCI.pThreadPool                                            = pThreadPool;
    const RefCntAutoPtr<RadientAssetManagerImpl>    pOtherManager  = RadientAssetManagerImpl::Create(OtherCI);
    const RefCntAutoPtr<IRadientMeshImportServices> pOtherServices = pOtherManager->CreateMeshImportServices();
    RefCntAutoPtr<IRadientMeshVertexData>           pVertices      = CreateVertices();
    RefCntAutoPtr<IRadientMeshIndexData>            pIndices       = CreateIndices();
    const RadientMeshGeometryData                   Geometry{pVertices, pIndices};
    RadientMeshPrimitiveCreateInfo                  Primitive;
    Primitive.IndexCount = 3;
    RadientMeshViewCreateInfo CI;
    CI.pPrimitives    = &Primitive;
    CI.PrimitiveCount = 1;
    CI.pGeometryData  = &Geometry;
    CI.GeometryCount  = 1;
    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(pOtherServices->CreateMeshView(CI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
}

TEST_F(RadientMeshImportServicesTest, RejectsCreationAfterManagerStops)
{
    ASSERT_EQ(pAssetManager->Stop(nullptr), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMeshVertexData>      pVertices;
    RefCntAutoPtr<IRadientMeshIndexData>       pIndices;
    RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphData;
    RefCntAutoPtr<IRadientMeshAsset>           pMesh;
    EXPECT_EQ(pMeshImportServices->CreateMeshVertexData({}, &pVertices), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMeshImportServices->CreateMeshIndexData({}, &pIndices), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMeshImportServices->CreateMeshMorphTargetData({}, &pMorphData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMeshImportServices->CreateMeshView({}, &pMesh), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pVertices, nullptr);
    EXPECT_EQ(pIndices, nullptr);
    EXPECT_EQ(pMorphData, nullptr);
    EXPECT_EQ(pMesh, nullptr);
}

} // namespace
