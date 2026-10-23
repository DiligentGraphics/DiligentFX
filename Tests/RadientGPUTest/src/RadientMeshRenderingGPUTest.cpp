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

#include "RadientQuadTestScene.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "BufferSuballocator.h"
#include "GLTFResourceManager.hpp"
#include "GPUTestingEnvironment.hpp"

#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

// Positions, UVs, and vertex colors occupy renderer slots 0, 2, and 4.
// The pool stores these as consecutive elements 0, 1, and 2. Compare two such meshes
// against quads with the same colors supplied entirely through their materials.
TEST(RadientMeshRenderingGPUTest, RendersSparseVertexStreams)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    const RadientFloat4                VertexColors[] = {DefaultQuadColor, {0.75f, 0.25f, 0.5f, 1.f}};
    for (Uint32 Pair = 0; Pair < 2; ++Pair)
    {
        QuadSetup& ColoredQuad     = Setups[2 * Pair];
        ColoredQuad.Color          = {1.f, 1.f, 1.f, 1.f};
        ColoredQuad.UseVertexColor = true;
        ColoredQuad.VertexColor    = VertexColors[Pair];
        Setups[2 * Pair + 1].Color = VertexColors[Pair];
    }

    RadientQuadTestScene Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));
    SampledColors Colors;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ASSERT_GT(Colors[1][0], 20);
    ASSERT_GT(Colors[3][1], 20);
    ASSERT_GT(Colors[3][0], Colors[1][0] + 20);
    ASSERT_GT(Colors[1][1], Colors[3][1] + 20);
    ExpectSameColor(Colors[0], Colors[1]);
    ExpectSameColor(Colors[2], Colors[3]);
}

// UV0 and UV1 have identical storage sizes but different renderer slots. Their
// meshes share the same physical pool and must still bind the correct UV slot.
// Nonzero UV scale makes sampling depend on the bound vertex stream.
TEST(RadientMeshRenderingGPUTest, RendersDifferentVertexSlotMappingsFromTheSamePool)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    Setups[0].UVScale     = {1.f, 0.f};
    Setups[1].UVScale     = {1.f, 0.f};
    Setups[1].TexCoordSet = 1;
    // Fixed-UV reference and control do not depend on vertex attribute sampling.
    Setups[2].UVBias = {0.75f, 0.5f};
    Setups[3].UVBias = {0.25f, 0.5f};

    RadientQuadTestScene Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));
    const RadientDrawableMeshResolveResult First = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[0], true);
    ASSERT_EQ(First.Status, RADIENT_STATUS_OK);
    ASSERT_NE(First.pMesh, nullptr);
    ASSERT_EQ(First.pMesh->Geometries.size(), 1u);
    for (Uint32 Index = 0; Index < Scene.Meshes.size(); ++Index)
    {
        SCOPED_TRACE(Index);
        const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[Index], true);
        ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
        ASSERT_NE(Result.pMesh, nullptr);
        ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
        EXPECT_EQ(Result.pMesh->Geometries[0].pVertexPool, First.pMesh->Geometries[0].pVertexPool);
        EXPECT_EQ(Result.pMesh->Geometries[0].VertexBufferMask, (1u << 0) | (1u << (2 + Setups[Index].TexCoordSet)));
    }

    SampledColors Colors;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ASSERT_GT(Colors[2][1], 20);
    ASSERT_GT(Colors[3][0], Colors[2][0] + 20);
    ExpectSameColor(Colors[0], Colors[2]);
    ExpectSameColor(Colors[1], Colors[2]);
}

// Both index formats use one index buffer and one material. Reserve vertices
// beyond the 16-bit range, and prefix each index allocation with degenerate
// triangles so correct rendering requires both BaseVertex and FirstElement.
// Exercise individual draws and multi-draw batches where the backend supports them.
TEST(RadientMeshRenderingGPUTest, RendersMixedIndexTypesWithLargeBaseVertex)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (Uint32 Index = 0; Index < Setups.size(); ++Index)
    {
        Setups[Index].IndexType    = (Index & 1) == 0 ? RADIENT_INDEX_TYPE_UINT16 : RADIENT_INDEX_TYPE_UINT32;
        Setups[Index].FirstElement = 3 * (Index + 1);
    }

    for (Uint32 BatchSize : {1u, 16u})
    {
        SCOPED_TRACE(BatchSize);
        RadientQuadTestScene Scene;
        Scene.ReservedVertexCount = 65536;
        Scene.MultiDrawBatchSize  = BatchSize;
        Scene.ShareMaterial       = true;
        ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));
        RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(Scene.pAssets.RawPtr());
        ASSERT_EQ(AssetManager.GetResourceManager()->GetIndexBufferCount(), 1u);
        for (Uint32 Index = 0; Index < Scene.Meshes.size(); ++Index)
        {
            SCOPED_TRACE(Index);
            const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[Index], true);
            ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
            ASSERT_NE(Result.pMesh, nullptr);
            ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
            ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
            EXPECT_GE(Result.pMesh->Geometries[0].BaseVertex, 65536u);
            EXPECT_EQ(Result.pMesh->Geometries[0].IndexType, (Index & 1) == 0 ? VT_UINT16 : VT_UINT32);
            EXPECT_EQ(Result.pMesh->Primitives[0].FirstElement, Setups[Index].FirstElement);
            EXPECT_EQ(Result.pMesh->Primitives[0].pMaterialAsset, Scene.Materials[0]);
            EXPECT_EQ(Scene.Meshes[Index]->GetDesc().pGeometries[0].IndexType, Setups[Index].IndexType);
        }

        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
        ASSERT_GT(Colors[0][0], 20);
        ASSERT_GT(Colors[0][1], Colors[0][0] + 20);
        ASSERT_GT(Colors[0][2], Colors[0][1] + 20);
        for (Uint32 Index = 1; Index < Colors.size(); ++Index)
        {
            ExpectSameColor(Colors[Index], Colors[0]);
        }
    }
}

// Each allocator holds two distinct six-index quads. All four quads use the
// same material, vertex pool, and index type, so correct single-draw and
// multi-draw rendering requires splitting batches by index allocator. Prefixes
// make another allocator's index data select degenerate vertices.
TEST(RadientMeshRenderingGPUTest, RendersMeshesFromMultipleIndexBuffers)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (Uint32 Index = 0; Index < Setups.size(); ++Index)
    {
        Setups[Index].VertexPrefixCount = 4 * Index;
    }

    for (Uint32 BatchSize : {1u, 16u})
    {
        SCOPED_TRACE(BatchSize);
        RadientQuadTestScene Scene;
        Scene.IndexBufferSize    = 24;
        Scene.MaxIndexBufferSize = 24;
        Scene.MultiDrawBatchSize = BatchSize;
        Scene.ShareMaterial      = true;
        ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));

        RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(Scene.pAssets.RawPtr());
        GLTF::ResourceManager&   Resources    = *AssetManager.GetResourceManager();
        ASSERT_EQ(Resources.GetIndexBufferCount(), 2u);
        std::array<Uint32, 2> AllocatorMeshCounts{};
        IVertexPool*          pVertexPool = nullptr;
        for (Uint32 Index = 0; Index < Scene.Meshes.size(); ++Index)
        {
            SCOPED_TRACE(Index);
            const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[Index], true);
            ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
            ASSERT_NE(Result.pMesh, nullptr);
            ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
            ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
            const RadientDrawableMeshGeometry& Geometry = Result.pMesh->Geometries[0];
            ASSERT_NE(Geometry.pIndexAllocator, nullptr);
            const Uint32 AllocatorIndex = Resources.GetIndexAllocatorIndex(Geometry.pIndexAllocator);
            ASSERT_LT(AllocatorIndex, AllocatorMeshCounts.size());
            ++AllocatorMeshCounts[AllocatorIndex];
            EXPECT_EQ(Geometry.IndexType, VT_UINT16);
            EXPECT_EQ(Result.pMesh->Primitives[0].pMaterialAsset, Scene.Materials[0]);
            if (Index == 0)
                pVertexPool = Geometry.pVertexPool;
            EXPECT_EQ(Geometry.pVertexPool, pVertexPool);
        }
        EXPECT_EQ(AllocatorMeshCounts[0], 2u);
        EXPECT_EQ(AllocatorMeshCounts[1], 2u);

        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
        ASSERT_GT(Colors[0][0], 20);
        ASSERT_GT(Colors[0][1], Colors[0][0] + 20);
        ASSERT_GT(Colors[0][2], Colors[0][1] + 20);
        for (Uint32 Index = 1; Index < Colors.size(); ++Index)
        {
            ExpectSameColor(Colors[Index], Colors[0]);
        }
    }
}

// Render first so drawable and batch caches retain the index allocator, then
// grow its backing buffer without changing the scene. Cached draws must bind
// the replacement buffer and preserve the original quad pixels. Clearing the
// retired buffer makes a stale raw-buffer binding produce degenerate triangles.
TEST(RadientMeshRenderingGPUTest, RebindsIndexBufferAfterGrowth)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    RadientQuadTestScene               Scene;
    Scene.IndexBufferSize    = 12;
    Scene.MaxIndexBufferSize = 96;
    Scene.ShareMaterial      = true;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(QuadSetups{}));

    RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(Scene.pAssets.RawPtr());
    GLTF::ResourceManager&   Resources    = *AssetManager.GetResourceManager();
    ASSERT_EQ(Resources.GetIndexBufferCount(), 1u);
    const RadientDrawableMeshResolveResult InitialMesh = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[0], true);
    ASSERT_EQ(InitialMesh.Status, RADIENT_STATUS_OK);
    ASSERT_NE(InitialMesh.pMesh, nullptr);
    ASSERT_EQ(InitialMesh.pMesh->Geometries.size(), 1u);
    IBufferSuballocator* const pAllocator = InitialMesh.pMesh->Geometries[0].pIndexAllocator;
    ASSERT_NE(pAllocator, nullptr);
    const RefCntAutoPtr<IBuffer> pOriginalBuffer{pAllocator->GetBuffer()};
    ASSERT_NE(pOriginalBuffer, nullptr);
    const Uint32  OriginalVersion = pAllocator->GetVersion();
    SampledColors Before;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Before));
    for (const std::array<Uint8, 4>& Color : Before)
    {
        ASSERT_GT(Color[0], 20);
        ASSERT_GT(Color[1], Color[0] + 20);
        ASSERT_GT(Color[2], Color[1] + 20);
    }

    const RefCntAutoPtr<IBufferSuballocation> pExtraIndices = Resources.AllocateIndices(48);
    ASSERT_NE(pExtraIndices, nullptr);
    ASSERT_EQ(pExtraIndices->GetAllocator(), pAllocator);
    ASSERT_EQ(Resources.GetIndexBufferCount(), 1u);
    ASSERT_NO_FATAL_FAILURE(Scene.RenderUntilReady());
    ASSERT_NE(pAllocator->GetBuffer(), nullptr);
    EXPECT_NE(pAllocator->GetBuffer(), pOriginalBuffer.RawPtr());
    EXPECT_GT(pAllocator->GetVersion(), OriginalVersion);
    EXPECT_GE(pAllocator->GetBuffer()->GetDesc().Size, Uint64{60});
    const RadientDrawableMeshResolveResult UpdatedMesh = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[0], true);
    ASSERT_EQ(UpdatedMesh.Status, RADIENT_STATUS_OK);
    ASSERT_NE(UpdatedMesh.pMesh, nullptr);
    ASSERT_EQ(UpdatedMesh.pMesh->Geometries.size(), 1u);
    EXPECT_EQ(UpdatedMesh.pMesh->Geometries[0].pIndexAllocator, pAllocator);

    // Growth has finished copying the old contents. Poison only the retired
    // buffer: a batch retaining its raw pointer must not keep rendering from it.
    const std::array<Uint16, 6> EmptyIndices{};
    Scene.pContext->UpdateBuffer(pOriginalBuffer, 0, sizeof(EmptyIndices), EmptyIndices.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ASSERT_NO_FATAL_FAILURE(Scene.RenderUntilReady());

    SampledColors After;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(After));
    for (Uint32 Index = 0; Index < After.size(); ++Index)
    {
        ExpectSameColor(After[Index], Before[Index]);
    }
}

// Indexed and nonindexed ranges share the material and vertex pool. Nonindexed
// ranges start after dummy vertices and at a nonzero pool base, and must never
// allocate index data. Compare the complete rendering path with indexed quads.
TEST(RadientMeshRenderingGPUTest, RendersIndexedAndNonIndexedVertexRanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (Uint32 Index = 0; Index < Setups.size(); ++Index)
    {
        Setups[Index].IndexType    = (Index & 1) == 0 ? RADIENT_INDEX_TYPE_NONE : RADIENT_INDEX_TYPE_UINT16;
        Setups[Index].FirstElement = 3 * (Index + 1);
    }
    Setups[3].IndexType = RADIENT_INDEX_TYPE_UINT32;
    for (Uint32 BatchSize : {1u, 16u})
    {
        SCOPED_TRACE(BatchSize);
        RadientQuadTestScene Scene;
        Scene.ReservedVertexCount = 32;
        Scene.MultiDrawBatchSize  = BatchSize;
        Scene.ShareMaterial       = true;
        ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));
        RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(Scene.pAssets.RawPtr());
        EXPECT_EQ(AssetManager.GetResourceManager()->GetIndexBufferUsageStats().AllocationCount, 2u);
        for (Uint32 Index = 0; Index < Scene.Meshes.size(); ++Index)
        {
            SCOPED_TRACE(Index);
            const bool                             IsIndexed = Setups[Index].IndexType != RADIENT_INDEX_TYPE_NONE;
            const RadientDrawableMeshResolveResult Result    = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[Index], true);
            ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
            ASSERT_NE(Result.pMesh, nullptr);
            ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
            ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
            EXPECT_GE(Result.pMesh->Geometries[0].BaseVertex, Scene.ReservedVertexCount);
            EXPECT_EQ(Result.pMesh->Primitives[0].IsIndexed, IsIndexed);
            EXPECT_EQ(Result.pMesh->Primitives[0].FirstElement, Setups[Index].FirstElement);
            const RadientMeshGeometryDesc& Geometry = Scene.Meshes[Index]->GetDesc().pGeometries[0];
            EXPECT_EQ(Geometry.IndexType, Setups[Index].IndexType);
            if (!IsIndexed)
            {
                EXPECT_EQ(Geometry.IndexCount, 0u);
                EXPECT_EQ(Geometry.VertexCount, Setups[Index].FirstElement + 6);
                EXPECT_EQ(Result.pMesh->Geometries[0].IndexType, VT_UNDEFINED);
            }
        }
        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
        ASSERT_GT(Colors[1][0], 20);
        ASSERT_GT(Colors[3][1], 20);
        ExpectSameColor(Colors[0], Colors[1]);
        ExpectSameColor(Colors[2], Colors[3]);
    }
}

// The morph target expands a small quad to the size of the plain indexed
// reference. Off-center pixels require nonzero deltas. Nonindexed draws must
// address geometry-local deltas despite backend differences in vertex IDs.
TEST(RadientMeshRenderingGPUTest, MorphsNonIndexedVertexRangesAtNonzeroPoolOffsets)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (Uint32 Index = 0; Index < Setups.size(); ++Index)
    {
        Setups[Index].IndexType      = (Index & 1) == 0 ? RADIENT_INDEX_TYPE_NONE : RADIENT_INDEX_TYPE_UINT16;
        Setups[Index].FirstElement   = 3 * (Index + 1);
        Setups[Index].UseMorphTarget = Index != 3;
    }
    for (Uint32 BatchSize : {1u, 16u})
    {
        SCOPED_TRACE(BatchSize);
        RadientQuadTestScene Scene;
        Scene.ReservedVertexCount = 32;
        Scene.MultiDrawBatchSize  = BatchSize;
        Scene.ShareMaterial       = true;
        ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups));
        for (Uint32 Index = 0; Index < 3; ++Index)
        {
            const RadientDrawableMeshResolveResult Result = RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[Index], true);
            ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
            ASSERT_NE(Result.pMesh, nullptr);
            ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
            EXPECT_GE(Result.pMesh->Geometries[0].BaseVertex, Scene.ReservedVertexCount);
            EXPECT_NE(Result.pMesh->Geometries[0].pMorphTargetData, nullptr);
        }
        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors, 16));
        ASSERT_GT(Colors[3][0], 20);
        for (Uint32 Index = 0; Index < 3; ++Index)
        {
            SCOPED_TRACE(Index);
            ExpectSameColor(Colors[Index], Colors[3]);
        }
    }
}

} // namespace
