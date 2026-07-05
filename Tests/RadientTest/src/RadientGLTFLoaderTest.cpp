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
 *  for loss of goodwill, work stoppage, computer failure or malfunction, and any
 *  and all other commercial damages or losses), even if such Contributor has been
 *  advised of the possibility of such damages.
 */

#include "TempDirectory.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include "Assets/RadientGLTFLoader.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "GLTFDocument.hpp"
#include "ThreadPool.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <string>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr std::array<Uint8, 67> TransparentPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};

static constexpr const char* TransparentPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==";

std::string WriteGLTFFile(const TempDirectory& TempDir, const char* FileName, const char* Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

std::string WriteGLTFDataURITextureFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "DataURITexture", "uri": "data:image/png;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF("}],
    "textures": [{"source": 0}]
})GLTF";

    return WriteGLTFFile(TempDir, "data_uri_texture.gltf", Contents.c_str());
}

std::string WriteGLTFExternalTextureFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "external_texture.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [{"source": 0}]
})GLTF");
}

std::string WriteGLTFMaterialWithoutTexturesFile(const TempDirectory& TempDir)
{
    return WriteGLTFFile(TempDir, "material_without_textures.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "materials": [{
        "name": "PlainMaterial",
        "doubleSided": true,
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.25, 0.5, 0.75, 1.0],
            "metallicFactor": 0.125,
            "roughnessFactor": 0.875
        }
    }]
})GLTF");
}

std::string WriteGLTFMaterialWithTextureDependencyFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "material_with_texture.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [{"source": 0}],
    "materials": [{
        "name": "TexturedMaterial",
        "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0, "texCoord": 1}
        }
    }]
})GLTF");
}

std::string WriteGLTFWithMissingAndValidTextureSourcesFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "missing_and_valid_texture_sources.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [
        {"source": 99},
        {"source": 0}
    ]
})GLTF");
}

std::string WriteGLTFBufferViewTextureFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "buffers": [{
        "uri": "data:application/octet-stream;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF(",
        "byteLength": 67
    }],
    "bufferViews": [{
        "buffer": 0,
        "byteOffset": 0,
        "byteLength": 67
    }],
    "images": [{
        "name": "BufferViewTexture",
        "bufferView": 0,
        "mimeType": "image/png"
    }],
    "textures": [{"source": 0}]
})GLTF";

    return WriteGLTFFile(TempDir, "buffer_view_texture.gltf", Contents.c_str());
}

std::string WriteGLTFDataURIAndBufferViewTexturesFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "buffers": [{
        "uri": "data:application/octet-stream;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF(",
        "byteLength": 67
    }],
    "bufferViews": [{
        "buffer": 0,
        "byteOffset": 0,
        "byteLength": 67
    }],
    "images": [
        {
            "name": "BufferViewTexture",
            "bufferView": 0,
            "mimeType": "image/png"
        },
        {
            "name": "DataURITexture",
            "uri": "data:image/png;base64,)GLTF" +
        TransparentPngBase64 +
        R"GLTF("
        }
    ],
    "textures": [
        {"source": 0},
        {"source": 1}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "data_uri_and_buffer_view_textures.gltf", Contents.c_str());
}

std::shared_ptr<GLTF::Document> LoadMetadataOnlyDocument(const std::string& GLTFPath)
{
    GLTF::DocumentLoadInfo LoadInfo;
    LoadInfo.FileName     = GLTFPath.c_str();
    LoadInfo.DecodeImages = false;
    return std::make_shared<GLTF::Document>(LoadInfo);
}

void ProcessQueuedTasks(IThreadPool& ThreadPool)
{
    while (ThreadPool.GetQueueSize() != 0)
        ThreadPool.ProcessTask(0, false);
}

void WaitForAllTasksAndStop(IThreadPool& ThreadPool)
{
    ThreadPool.WaitForAllTasks();
    ThreadPool.StopThreads();
}

RadientGLTFLoader::TextureAssetList LoadTextures(IThreadPool&                ThreadPool,
                                                 RadientTextureAssetManager& TextureManager,
                                                 const std::string&          GLTFPath)
{
    return RadientGLTFLoader::LoadTextures(ThreadPool,
                                           TextureManager,
                                           GLTFPath,
                                           LoadMetadataOnlyDocument(GLTFPath));
}

RadientGLTFLoader::MaterialAssetList LoadMaterials(RadientMaterialAssetManager&               MaterialManager,
                                                   const std::shared_ptr<GLTF::Document>&     pDocument,
                                                   const RadientGLTFLoader::TextureAssetList& Textures)
{
    return RadientGLTFLoader::LoadMaterials(MaterialManager, pDocument, Textures);
}

} // namespace

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromExternalImageURI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFExternalTextureFile(TempDir);

    RadientGLTFLoader::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);

    const std::string TextureURI = Textures[0]->GetReference().URI;
    EXPECT_NE(TextureURI.find("external.png"), std::string::npos);
    EXPECT_EQ(TextureURI.find("#texture:"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesContinuesAfterUnresolvedTextureSource)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFWithMissingAndValidTextureSourcesFile(TempDir);

    RadientGLTFLoader::TextureAssetList Textures;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Failed to resolve GLTF texture source 0"};
        Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);
    }

    // The first GLTF texture points to a missing image source. Keep its slot
    // empty so later material binding can substitute a renderer stub.
    ASSERT_EQ(Textures.size(), 2u);
    EXPECT_EQ(Textures[0], nullptr);

    // Later texture slots must preserve their original GLTF indices and still
    // load normally.
    ASSERT_NE(Textures[1], nullptr);
    ASSERT_NE(Textures[1]->GetReference().URI, nullptr);

    const std::string TextureURI = Textures[1]->GetReference().URI;
    EXPECT_NE(TextureURI.find("external.png"), std::string::npos);
    EXPECT_EQ(TextureURI.find("#texture:"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[1]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromDataURIImage)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFDataURITextureFile(TempDir);

    RadientGLTFLoader::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromBufferViewImage)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFBufferViewTextureFile(TempDir);

    RadientGLTFLoader::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, IdenticalDataURIAndBufferViewTexturesSharePayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFDataURIAndBufferViewTexturesFile(TempDir);

    RadientGLTFLoader::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 2u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[1], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    ASSERT_NE(Textures[1]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_STREQ(Textures[1]->GetReference().URI, (GLTFPath + "#texture:1").c_str());

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pBufferViewPayload = RadientTextureAssetManager::GetTexturePayload(Textures[0]);
    ASSERT_NE(pBufferViewPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(Textures[1]), pBufferViewPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[1]), RADIENT_STATUS_NO_GPU_DATA);
}

TEST(RadientGLTFLoaderTest, IdenticalTexturesFromDifferentGLTFsSharePayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string BufferViewGLTFPath = WriteGLTFBufferViewTextureFile(TempDir);
    const std::string DataURIGLTFPath    = WriteGLTFDataURITextureFile(TempDir);

    RadientGLTFLoader::TextureAssetList BufferViewTextures = LoadTextures(*pThreadPool, *pTextureManager, BufferViewGLTFPath);
    RadientGLTFLoader::TextureAssetList DataURITextures    = LoadTextures(*pThreadPool, *pTextureManager, DataURIGLTFPath);

    ASSERT_EQ(BufferViewTextures.size(), 1u);
    ASSERT_EQ(DataURITextures.size(), 1u);
    ASSERT_NE(BufferViewTextures[0], nullptr);
    ASSERT_NE(DataURITextures[0], nullptr);
    ASSERT_NE(BufferViewTextures[0]->GetReference().URI, nullptr);
    ASSERT_NE(DataURITextures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(BufferViewTextures[0]->GetReference().URI, (BufferViewGLTFPath + "#texture:0").c_str());
    EXPECT_STREQ(DataURITextures[0]->GetReference().URI, (DataURIGLTFPath + "#texture:0").c_str());

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pBufferViewPayload = RadientTextureAssetManager::GetTexturePayload(BufferViewTextures[0]);
    ASSERT_NE(pBufferViewPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(DataURITextures[0]), pBufferViewPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(BufferViewTextures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(BufferViewTextures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(DataURITextures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(DataURITextures[0]), RADIENT_STATUS_NO_GPU_DATA);
}

TEST(RadientGLTFLoaderTest, LoadMaterialsCreatesMaterialAssetWithoutTextures)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMaterialWithoutTexturesFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientGLTFLoader::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, {});

    ASSERT_EQ(Materials.size(), 1u);
    ASSERT_NE(Materials[0], nullptr);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_OK);

    const GLTF::Material* pMaterial = RadientMaterialAssetManager::GetMaterial(Materials[0]);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_TRUE(pMaterial->DoubleSided);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.x, 0.25f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.y, 0.5f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.z, 0.75f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.w, 1.0f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.MetallicFactor, 0.125f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.RoughnessFactor, 0.875f);
}

TEST(RadientGLTFLoaderTest, LoadMaterialsTracksTextureDependencies)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMaterialWithTextureDependencyFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientGLTFLoader::TextureAssetList Textures =
        RadientGLTFLoader::LoadTextures(*pThreadPool, *pTextureManager, GLTFPath, pDocument);
    RadientGLTFLoader::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, Textures);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_EQ(Materials.size(), 1u);
    ASSERT_NE(Materials[0], nullptr);

    // The material keeps the texture asset dependency and remains pending
    // while the texture source load task is still queued.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);

    // With no render device, CPU/source loading succeeds but no GPU texture
    // storage is created. The material source status follows the texture source,
    // while GPU status reports the null-device condition.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMaterialAssetManager::GetMaterial(Materials[0]), nullptr);
    pThreadPool->StopThreads();
}
