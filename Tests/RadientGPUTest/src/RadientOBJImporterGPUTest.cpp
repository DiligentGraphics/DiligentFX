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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "RadientEngine.h"
#include "RadientImportedDocument.hpp"
#include "RadientStandardMaterialParameters.h"

#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "TempDirectory.hpp"
#include "VertexPool.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

struct AssetManagerStopGuard
{
    IRadientAssetManager& AssetManager;
    IDeviceContext*       pContext = nullptr;

    ~AssetManagerStopGuard()
    {
        AssetManager.Stop(pContext);
    }
};

RADIENT_STATUS WaitForSceneGPUResources(RadientAssetManagerImpl& AssetManager,
                                        IRadientSceneAsset*      pScene,
                                        IRenderDevice*           pDevice,
                                        IDeviceContext*          pContext)
{
    const std::chrono::steady_clock::time_point Deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    do
    {
        const RADIENT_STATUS UpdateStatus = AssetManager.UpdateGPUResources(pDevice, pContext);
        if (RADIENT_FAILED(UpdateStatus))
            return UpdateStatus;
        pContext->Flush();
        pContext->FinishFrame();

        const RADIENT_STATUS Status = RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < Deadline);
    return RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene);
}

std::vector<Uint8> ReadBufferRange(IRenderDevice&  Device,
                                   IDeviceContext& Context,
                                   IBuffer&        Buffer,
                                   Uint64          Offset,
                                   Uint64          Size)
{
    BufferDesc StagingDesc;
    StagingDesc.Name           = "OBJ imported geometry readback";
    StagingDesc.Size           = Size;
    StagingDesc.Usage          = USAGE_STAGING;
    StagingDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pStagingBuffer;
    Device.CreateBuffer(StagingDesc, nullptr, pStagingBuffer.GetAddressOfEmpty());
    EXPECT_NE(pStagingBuffer, nullptr);
    if (pStagingBuffer == nullptr)
        return {};

    Context.CopyBuffer(&Buffer, Offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                       pStagingBuffer, 0, Size, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.WaitForIdle();
    void* pMappedData = nullptr;
    Context.MapBuffer(pStagingBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT, pMappedData);
    EXPECT_NE(pMappedData, nullptr);
    if (pMappedData == nullptr)
        return {};

    std::vector<Uint8> Data(static_cast<size_t>(Size));
    std::memcpy(Data.data(), pMappedData, Data.size());
    Context.UnmapBuffer(pStagingBuffer, MAP_READ);
    return Data;
}

void VerifyImportedTexturePixels(IRenderDevice&        Device,
                                 IDeviceContext&       Context,
                                 IRadientTextureAsset& Texture)
{
    ASSERT_EQ(RadientTextureAssetManager::GetLoadStatus(&Texture), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(&Texture), RADIENT_STATUS_OK);
    ITextureView* const pSRV = RadientTextureAssetManager::GetTextureSRV(&Texture, RadientTextureViewType::SRGB);
    ASSERT_NE(pSRV, nullptr);
    EXPECT_EQ(pSRV->GetDesc().Format, TEX_FORMAT_RGBA8_UNORM_SRGB);
    ITexture* const pUploadedTexture = pSRV->GetTexture();
    ASSERT_NE(pUploadedTexture, nullptr);
    RadientTextureSamplingInfo Sampling;
    ASSERT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(&Texture, Sampling));
    ASSERT_EQ(Sampling.Width, 2u);
    ASSERT_EQ(Sampling.Height, 2u);

    TextureDesc StagingDesc;
    StagingDesc.Name           = "OBJ imported texture readback";
    StagingDesc.Type           = RESOURCE_DIM_TEX_2D;
    StagingDesc.Width          = 2;
    StagingDesc.Height         = 2;
    StagingDesc.Format         = TEX_FORMAT_RGBA8_UNORM;
    StagingDesc.MipLevels      = 1;
    StagingDesc.Usage          = USAGE_STAGING;
    StagingDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> pReadback;
    Device.CreateTexture(StagingDesc, nullptr, pReadback.GetAddressOfEmpty());
    ASSERT_NE(pReadback, nullptr);

    const TextureDesc& UploadedDesc = pUploadedTexture->GetDesc();
    const Uint32       SrcX         = static_cast<Uint32>(Sampling.UVScaleBias.z * static_cast<float>(UploadedDesc.Width) + 0.5f);
    const Uint32       SrcY         = static_cast<Uint32>(Sampling.UVScaleBias.w * static_cast<float>(UploadedDesc.Height) + 0.5f);
    const Box          SrcBox{SrcX, SrcX + 2, SrcY, SrcY + 2};
    CopyTextureAttribs Copy;
    Copy.pSrcTexture              = pUploadedTexture;
    Copy.SrcSlice                 = static_cast<Uint32>(Sampling.TextureSlice + 0.5f);
    Copy.pSrcBox                  = &SrcBox;
    Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Copy.pDstTexture              = pReadback;
    Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Context.CopyTexture(Copy);
    Context.WaitForIdle();

    MappedTextureSubresource Mapped;
    Context.MapTextureSubresource(pReadback, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, Mapped);
    ASSERT_NE(Mapped.pData, nullptr);
    for (Uint32 Row = 0; Row < 2; ++Row)
    {
        const Uint8* pPixels = static_cast<const Uint8*>(Mapped.pData) + Row * Mapped.Stride;
        for (Uint32 Column = 0; Column < 2; ++Column)
        {
            EXPECT_EQ(pPixels[Column * 4 + 0], 32u);
            EXPECT_EQ(pPixels[Column * 4 + 1], 64u);
            EXPECT_EQ(pPixels[Column * 4 + 2], 128u);
            EXPECT_EQ(pPixels[Column * 4 + 3], 255u);
        }
    }
    Context.UnmapTextureSubresource(pReadback, 0, 0);
}

TEST(RadientOBJImporterGPUTest, ImportsTexturedSceneAndUploadsSharedGeometry)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment*             pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*                     pDevice  = pEnv->GetDevice();
    IDeviceContext*                    pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    TempDirectory     TempDir{"RadientOBJImporterGPUTest"};
    const std::string MaterialDirectory = TempDir.Get() + "/materials";
    ASSERT_TRUE(std::filesystem::create_directory(MaterialDirectory));
    const std::string OBJPath = TempDir.Get() + "/model.OBJ";
    {
        std::ofstream File{OBJPath, std::ios::binary};
        ASSERT_TRUE(File.is_open());
        File << "mtllib materials/surface.mtl\n"
                "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
                "vn 0 0 1\nusemtl Paint\no Lower\n"
                "f 1/1/1 2/2/1 3/3/1\no Upper\n"
                "f 2/2/1 4/4/1 3/3/1\n";
        ASSERT_TRUE(File.good());
    }
    {
        std::ofstream File{MaterialDirectory + "/surface.mtl", std::ios::binary};
        ASSERT_TRUE(File.is_open());
        File << "newmtl Paint\nillum 2\nKd 0.8 0.6 0.4\nKs 0.2 0.3 0.4\nNs 30\n"
                "map_Kd -s 2 3 -o 0.25 0.5 -clamp on paint.tga\n";
        ASSERT_TRUE(File.good());
    }
    {
        // A tiny uncompressed true-color TGA keeps this fixture self-contained.
        std::array<Uint8, 18> Header{};
        Header[2]  = 2;
        Header[12] = 2;
        Header[14] = 2;
        Header[16] = 24;
        Header[17] = 32;
        const Uint8   Pixels[]{128, 64, 32, 128, 64, 32, 128, 64, 32, 128, 64, 32};
        std::ofstream File{MaterialDirectory + "/paint.tga", std::ios::binary};
        ASSERT_TRUE(File.is_open());
        File.write(reinterpret_cast<const char*>(Header.data()), static_cast<std::streamsize>(Header.size()));
        File.write(reinterpret_cast<const char*>(Pixels), static_cast<std::streamsize>(sizeof(Pixels)));
        ASSERT_TRUE(File.good());
    }

    RadientEngineCreateInfo EngineCI;
    EngineCI.Backend.pDevice           = pDevice;
    EngineCI.Backend.pImmediateContext = pContext;
    // A single worker also exercises progress while texture copy callbacks need
    // the render thread. Waiting synchronously before pumping uploads can stall.
    EngineCI.WorkerThreadCount                   = 1;
    EngineCI.Resources.IndexBufferSize           = 4096;
    EngineCI.Resources.VertexPoolSize            = 64;
    EngineCI.Resources.TextureAtlasSize          = 64;
    EngineCI.Resources.TextureAtlasMipLevel0Size = 64 * 64 * 4;
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetManager> pAssets;
    ASSERT_EQ(pEngine->GetAssetManager(pAssets.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    AssetManagerStopGuard    StopGuard{*pAssets, pContext};
    RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());

    RefCntAutoPtr<IRadientScene>         pScene;
    RefCntAutoPtr<IRadientSceneWriter>   pWriter;
    RefCntAutoPtr<IRadientSceneImporter> pImporter;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pEngine->CreateSceneImporter(pWriter, pImporter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RadientSceneLoadInfo LoadInfo;
    LoadInfo.URI = OBJPath.c_str();
    RefCntAutoPtr<IRadientSceneAsset> pSceneAsset;
    RadientEntityID                   RootEntity = InvalidRadientEntityID;
    ASSERT_TRUE(IsPendingOrOK(pImporter->ImportScene(LoadInfo, {}, pSceneAsset.GetAddressOfEmpty(), RootEntity)));
    ASSERT_NE(pSceneAsset, nullptr);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);
    ASSERT_EQ(WaitForSceneGPUResources(AssetManager, pSceneAsset, pDevice, pContext), RADIENT_STATUS_OK);
    ASSERT_EQ(pAssets->WaitForAssetLoad(pSceneAsset), RADIENT_STATUS_OK);
    ASSERT_TRUE(RADIENT_SUCCEEDED(pImporter->ProcessPendingImports()));
    ASSERT_TRUE(RADIENT_SUCCEEDED(pWriter->CommitChanges()));

    Uint32 ChildCount = 0;
    ASSERT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildCount, 2u);
    RadientEntityID Children[2]{};
    Uint32          ChildrenWritten = 0;
    ASSERT_EQ(pScene->GetChildren(RootEntity, 0, 2, Children, ChildrenWritten), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildrenWritten, 2u);
    for (RadientEntityID Child : Children)
    {
        Bool HasMesh = False;
        ASSERT_EQ(pScene->HasComponent(Child, RADIENT_COMPONENT_TYPE_MESH, HasMesh), RADIENT_STATUS_OK);
        EXPECT_TRUE(HasMesh);
        RadientTransform Transform;
        ASSERT_EQ(pScene->GetLocalTransform(Child, Transform), RADIENT_STATUS_OK);
        EXPECT_EQ(Transform, RadientTransform{});
    }

    const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pSceneAsset);
    ASSERT_NE(pDocument, nullptr);
    ASSERT_EQ(pDocument->Meshes.size(), 2u);
    ASSERT_EQ(pDocument->Materials.size(), 1u);
    ASSERT_EQ(pDocument->Textures.size(), 1u);
    ASSERT_EQ(pDocument->Nodes.size(), 2u);
    ASSERT_NE(pDocument->Meshes[0], nullptr);
    ASSERT_NE(pDocument->Meshes[1], nullptr);
    ASSERT_NE(pDocument->Materials[0], nullptr);
    EXPECT_EQ(pDocument->Nodes[0].Name, "Lower");
    EXPECT_EQ(pDocument->Nodes[1].Name, "Upper");
    ASSERT_NE(pDocument->Textures[0], nullptr);
    ASSERT_NO_FATAL_FAILURE(VerifyImportedTexturePixels(*pDevice, *pContext, *pDocument->Textures[0]));

    IRadientMaterialAsset&         Material = *pDocument->Materials[0];
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(Material.GetDefinition()->FindParameter(RadientStandardMaterialDiffuseTextureName, &TextureHandle), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientTextureAsset> pBoundTexture;
    ASSERT_EQ(Material.GetTexture(TextureHandle, 0, pBoundTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_EQ(pBoundTexture, pDocument->Textures[0]);
    EXPECT_EQ(GetMaterialParameter<Int32>(Material, RadientStandardMaterialDiffuseTextureUVSelectorName), 0);
    EXPECT_EQ(GetMaterialParameter<Uint32>(Material, RadientStandardMaterialDiffuseTextureWrapUName),
              static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP));
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(Material, RadientStandardMaterialGlossinessFactorName), 0.75f);

    const RadientDrawableMeshResolveResult Lower = RadientMeshAssetManager::GetDrawableMesh(pDocument->Meshes[0], true);
    const RadientDrawableMeshResolveResult Upper = RadientMeshAssetManager::GetDrawableMesh(pDocument->Meshes[1], true);
    ASSERT_EQ(Lower.Status, RADIENT_STATUS_OK);
    ASSERT_EQ(Upper.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Lower.pMesh, nullptr);
    ASSERT_NE(Upper.pMesh, nullptr);
    ASSERT_EQ(Lower.pMesh->Geometries.size(), 1u);
    ASSERT_EQ(Upper.pMesh->Geometries.size(), 1u);
    ASSERT_EQ(Lower.pMesh->Primitives.size(), 1u);
    ASSERT_EQ(Upper.pMesh->Primitives.size(), 1u);
    const RadientDrawableMeshGeometry& Geometry = Lower.pMesh->Geometries[0];
    EXPECT_EQ(Geometry.pVertexPool, Upper.pMesh->Geometries[0].pVertexPool);
    EXPECT_EQ(Geometry.BaseVertex, Upper.pMesh->Geometries[0].BaseVertex);
    EXPECT_EQ(Geometry.FirstIndexLocation, Upper.pMesh->Geometries[0].FirstIndexLocation);
    EXPECT_EQ(Lower.pMesh->Primitives[0].FirstElement, 0u);
    EXPECT_EQ(Upper.pMesh->Primitives[0].FirstElement, 3u);
    EXPECT_EQ(Lower.pMesh->Primitives[0].ElementCount, 3u);
    EXPECT_EQ(Upper.pMesh->Primitives[0].ElementCount, 3u);
    EXPECT_EQ(Lower.pMesh->Primitives[0].pMaterialAsset, &Material);
    EXPECT_EQ(Upper.pMesh->Primitives[0].pMaterialAsset, &Material);

    const RadientMeshAssetDesc& MeshDesc = pDocument->Meshes[0]->GetDesc();
    ASSERT_EQ(MeshDesc.GeometryCount, 1u);
    const RadientMeshGeometryDesc& GeometryDesc = MeshDesc.pGeometries[0];
    ASSERT_EQ(GeometryDesc.VertexCount, 4u);
    ASSERT_EQ(GeometryDesc.IndexCount, 6u);
    ASSERT_EQ(GeometryDesc.IndexType, RADIENT_INDEX_TYPE_UINT32);
    const RadientVertexAttributeDesc* pPosition = nullptr;
    for (Uint32 Index = 0; Index < GeometryDesc.VertexLayout.AttributeCount; ++Index)
    {
        const RadientVertexAttributeDesc& Attribute = GeometryDesc.VertexLayout.pAttributes[Index];
        if (std::strcmp(Attribute.Semantic, "POSITION") == 0)
            pPosition = &Attribute;
    }
    ASSERT_NE(pPosition, nullptr);
    ASSERT_EQ(pPosition->ComponentType, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32);
    ASSERT_EQ(pPosition->ComponentCount, 3u);
    // POSITION is in the renderer's first physical stream as well as logical
    // stream zero. Read its declared stride rather than assuming packed XYZ.
    ASSERT_EQ(pPosition->BufferIndex, 0u);
    ASSERT_NE(Geometry.pVertexPool, nullptr);
    IBuffer* const pVertexBuffer = Geometry.pVertexPool->GetBuffer(0);
    ASSERT_NE(pVertexBuffer, nullptr);
    const Uint32             Stride      = GeometryDesc.VertexLayout.pBuffers[0].ByteStride;
    const std::vector<Uint8> VertexBytes = ReadBufferRange(*pDevice, *pContext, *pVertexBuffer,
                                                           static_cast<Uint64>(Geometry.BaseVertex) * Stride, static_cast<Uint64>(Stride) * GeometryDesc.VertexCount);
    ASSERT_EQ(VertexBytes.size(), static_cast<size_t>(Stride) * 4);
    const RadientFloat3 ExpectedPositions[]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    for (Uint32 Vertex = 0; Vertex < 4; ++Vertex)
    {
        RadientFloat3 Position;
        std::memcpy(&Position, VertexBytes.data() + Vertex * Stride + pPosition->ByteOffset, sizeof(Position));
        EXPECT_EQ(Position, ExpectedPositions[Vertex]);
    }

    GLTF::ResourceManager* pResources = AssetManager.GetResourceManager();
    ASSERT_NE(pResources, nullptr);
    ASSERT_EQ(pResources->GetIndexBufferCount(), 1u);
    IBuffer* const pIndexBuffer = pResources->GetIndexBuffer();
    ASSERT_NE(pIndexBuffer, nullptr);
    const std::vector<Uint8> IndexBytes = ReadBufferRange(*pDevice, *pContext, *pIndexBuffer,
                                                          static_cast<Uint64>(Geometry.FirstIndexLocation) * sizeof(Uint32), 6 * sizeof(Uint32));
    const Uint32             ExpectedIndices[]{0, 1, 2, 1, 3, 2};
    ASSERT_EQ(IndexBytes.size(), sizeof(ExpectedIndices));
    EXPECT_EQ(std::memcmp(IndexBytes.data(), ExpectedIndices, sizeof(ExpectedIndices)), 0);
}

} // namespace
