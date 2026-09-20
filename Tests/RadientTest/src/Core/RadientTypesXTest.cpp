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

#include "RadientTypesX.hpp"

#include "Assets/RadientVertexLayout.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "ThreadPool.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <type_traits>

using namespace Diligent;

namespace
{

struct BlobEvents
{
    Uint32 Destroyed       = 0;
    Uint32 ReadersReleased = 0;
};

void OnBlobDestroyed(void* pUserData)
{
    ++static_cast<BlobEvents*>(pUserData)->Destroyed;
}

void OnBlobReadReleased(IRadientDataBlob*, void* pUserData)
{
    ++static_cast<BlobEvents*>(pUserData)->ReadersReleased;
}

RefCntAutoPtr<IRadientMutableDataBlob> MakeBlob(BlobEvents& Events, Uint64 Size = 64)
{
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Size;
    CI.pUserData            = &Events;
    CI.OnDestroy            = OnBlobDestroyed;
    CI.OnLastReaderReleased = OnBlobReadReleased;
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    return Blob;
}

static_assert(std::is_same<decltype(std::declval<RadientVertexLayoutDescX&>().Get()), const RadientVertexLayoutDesc&>::value,
              "Layout metadata must not expose mutable pointers.");
static_assert(std::is_same<decltype(std::declval<RadientTextureDataX&>().Get()), const RadientTextureData&>::value,
              "Mip metadata must not expose mutable blob pointers.");
static_assert(std::is_same<decltype(std::declval<RadientTextureLoadInfoX&>().Get()), const RadientTextureLoadInfo&>::value,
              "Load metadata must not expose mutable owned pointers.");
static_assert(std::is_nothrow_move_constructible<RadientVertexLayoutDescX>::value &&
                  std::is_nothrow_move_assignable<RadientVertexLayoutDescX>::value &&
                  std::is_nothrow_move_constructible<RadientTextureDataX>::value &&
                  std::is_nothrow_move_assignable<RadientTextureDataX>::value &&
                  std::is_nothrow_move_constructible<RadientTextureLoadInfoX>::value &&
                  std::is_nothrow_move_assignable<RadientTextureLoadInfoX>::value,
              "Moving descriptors must not allocate.");

} // namespace

TEST(RadientTypesXTest, LayoutOwnsArraysAndSemanticStrings)
{
    std::string                Semantic     = "POSITION";
    RadientVertexAttributeDesc Attributes[] = {
        {Semantic.c_str(), 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False}};
    RadientVertexBufferLayoutDesc Buffers[] = {{RADIENT_VERTEX_AUTO_STRIDE}};
    const RadientVertexLayoutDesc Desc{Attributes, 1, Buffers, 1};
    RadientVertexLayoutDescX      Layout{Desc};
    EXPECT_NE(Layout.Get().pAttributes, Attributes);
    EXPECT_NE(Layout.Get().pBuffers, Buffers);
    EXPECT_NE(Layout.GetAttribute(0).Semantic, Semantic.c_str());
    Semantic.assign(200, 'x');
    Attributes[0].ComponentCount = 1;
    Buffers[0].ByteStride        = 1;
    EXPECT_STREQ(Layout.GetAttribute(0).Semantic, "POSITION");
    EXPECT_EQ(Layout.GetAttribute(0).ComponentCount, 3u);
    EXPECT_EQ(Layout.GetBuffer(0).ByteStride, RADIENT_VERTEX_AUTO_STRIDE);

    const Char* PositionSemantic = Layout.GetAttribute(0).Semantic;
    Layout.AddAttribute("NORMAL", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
    for (Uint32 Index = 0; Index < 64; ++Index)
    {
        const std::string Name = "CUSTOM_" + std::to_string(Index);
        Layout.AddAttribute(Name.c_str(), RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1);
    }
    EXPECT_EQ(Layout.GetAttribute(0).Semantic, PositionSemantic);
    EXPECT_STREQ(PositionSemantic, "POSITION");
    EXPECT_STREQ(Layout.GetAttribute(65).Semantic, "CUSTOM_63");
    EXPECT_EQ(Layout.GetAttributeCount(), 66u);
    EXPECT_EQ(Layout.GetBufferCount(), 1u);
    EXPECT_EQ(Layout.GetAttribute(65).ByteOffset, RADIENT_VERTEX_AUTO_OFFSET);
    ResolvedVertexLayout Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.BufferStrides[0], 88u);
    EXPECT_EQ(Layout.GetBuffer(0).ByteStride, RADIENT_VERTEX_AUTO_STRIDE);
}

TEST(RadientTypesXTest, LayoutCopiesMovesAndSwapsIndependentMetadata)
{
    RadientVertexLayoutDescX Original{
        {{"POSITION", 0, 4, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False}}, {{20}}};
    RadientVertexLayoutDescX Copy{Original};
    EXPECT_NE(Copy.Get().pAttributes, Original.Get().pAttributes);
    EXPECT_NE(Copy.GetAttribute(0).Semantic, Original.GetAttribute(0).Semantic);
    Copy.SetAttribute(0, {"NORMAL", 0, 0, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False})
        .SetBuffer(0, 12);
    EXPECT_STREQ(Original.GetAttribute(0).Semantic, "POSITION");
    EXPECT_EQ(Original.GetAttribute(0).ByteOffset, 4u);
    EXPECT_EQ(Original.GetBuffer(0).ByteStride, 20u);

    RadientVertexLayoutDescX Assigned;
    Assigned = Original;
    Original.Clear();
    RadientVertexLayoutDescX Moved{std::move(Assigned)};
    EXPECT_EQ(Assigned.Get().pAttributes, nullptr);
    EXPECT_EQ(Assigned.Get().pBuffers, nullptr);
    EXPECT_EQ(Assigned.GetAttributeCount(), 0u);
    EXPECT_EQ(Assigned.GetBufferCount(), 0u);
    Copy = std::move(Moved);
    EXPECT_EQ(Moved.GetAttributeCount(), 0u);
    EXPECT_STREQ(Copy.GetAttribute(0).Semantic, "POSITION");

    Original.AddBuffer().AddAttribute("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, 0, 0, True);
    Copy.Swap(Original);
    Original.Clear();
    EXPECT_STREQ(Copy.GetAttribute(0).Semantic, "COLOR_0");
    EXPECT_TRUE(Copy.GetAttribute(0).Normalized);
    RadientVertexLayoutDescX& Alias = Copy;
    Copy                            = Alias;
    Copy                            = std::move(Alias);
    Copy.SetAttribute(0, Copy.GetAttribute(0));
    EXPECT_STREQ(Copy.GetAttribute(0).Semantic, "COLOR_0");
}

TEST(RadientTypesXTest, TextureDataRetainsBlobsWithoutAcquiringReadAccess)
{
    BlobEvents                             Events;
    RefCntAutoPtr<IRadientMutableDataBlob> Blob = MakeBlob(Events);
    ASSERT_NE(Blob, nullptr);
    void* Pixels = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&Pixels), RADIENT_STATUS_OK);

    RadientTextureMipData Mips[] = {{Blob, 0, 16}, {Blob, 32, 8}};
    RadientTextureData    Desc;
    Desc.Width = Desc.Height = 4;
    Desc.Format              = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    Desc.pMipLevels          = Mips;
    Desc.MipLevelCount       = 2;
    RadientTextureDataX Data{Desc};
    Mips[0].ByteOffset = 12;
    EXPECT_NE(Data.Get().pMipLevels, Mips);
    EXPECT_EQ(Data.GetMip(0).ByteOffset, 0u);
    EXPECT_EQ(Data.GetMip(1).pDataBlob, Blob.RawPtr());

    RadientTextureDataX Copy{Data};
    EXPECT_NE(Copy.Get().pMipLevels, Data.Get().pMipLevels);
    EXPECT_EQ(Copy.GetMip(0).pDataBlob, Data.GetMip(0).pDataBlob);
    ASSERT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize(128), RADIENT_STATUS_OK);
    EXPECT_EQ(Events.ReadersReleased, 0u);
    Blob.Release();
    Data.Clear();
    EXPECT_EQ(Events.Destroyed, 0u);
    Copy.ClearMips();
    EXPECT_EQ(Events.Destroyed, 1u);
    EXPECT_EQ(Copy.GetMipLevelCount(), 0u);
    EXPECT_EQ(Copy.Get().pMipLevels, nullptr);
    EXPECT_EQ(Copy.Get().Width, 4u);
    EXPECT_EQ(Copy.Get().Format, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM);
    EXPECT_TRUE(Copy.Get().GenerateMips);
}

TEST(RadientTypesXTest, TextureDataReplacementsAndMovesReleaseReferences)
{
    BlobEvents                             FirstEvents;
    BlobEvents                             SecondEvents;
    RefCntAutoPtr<IRadientMutableDataBlob> First  = MakeBlob(FirstEvents);
    RefCntAutoPtr<IRadientMutableDataBlob> Second = MakeBlob(SecondEvents);
    ASSERT_NE(First, nullptr);
    ASSERT_NE(Second, nullptr);
    RadientTextureDataX Data{4, 4, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    Data.SetGenerateMips(False).AddMip(First).AddMip(First, 32, 8);
    First.Release();
    Data.SetMip(0, Second, 16, 16);
    EXPECT_EQ(FirstEvents.Destroyed, 0u);
    Data.SetMip(1, Data.GetMip(0));
    EXPECT_EQ(FirstEvents.Destroyed, 1u);
    EXPECT_EQ(Data.GetMip(1).ByteOffset, 16u);
    Second.Release();

    RadientTextureDataX Copy;
    Copy = Data;
    RadientTextureDataX Moved{std::move(Copy)};
    EXPECT_EQ(Copy.GetMipLevelCount(), 0u);
    EXPECT_TRUE(Copy.Get().GenerateMips);
    EXPECT_FALSE(Moved.Get().GenerateMips);
    Data = std::move(Moved);
    EXPECT_EQ(Moved.Get().pMipLevels, nullptr);
    EXPECT_EQ(Moved.Get().Width, 0u);
    RadientTextureDataX& Alias = Data;
    Data                       = Alias;
    Data                       = std::move(Alias);
    EXPECT_EQ(Data.GetMipLevelCount(), 2u);
    EXPECT_EQ(SecondEvents.Destroyed, 0u);
    Data.Clear();
    EXPECT_EQ(SecondEvents.Destroyed, 1u);
    EXPECT_EQ(Data.Get().Format, RADIENT_TEXTURE_FORMAT_UNKNOWN);
    EXPECT_TRUE(Data.Get().GenerateMips);
}

TEST(RadientTypesXTest, LoadInfoCopiesNestedDescriptorsAndURIStrings)
{
    BlobEvents                             Events;
    RefCntAutoPtr<IRadientMutableDataBlob> Blob = MakeBlob(Events);
    ASSERT_NE(Blob, nullptr);
    std::string           URI     = "memory:texture";
    std::string           BaseURI = "assets/";
    RadientTextureMipData Mip{Blob, 0, 8};
    RadientTextureData    Data;
    Data.Width = Data.Height = 2;
    Data.Format              = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    Data.pMipLevels          = &Mip;
    Data.MipLevelCount       = 1;
    RadientTextureLoadInfo Desc;
    Desc.URI          = URI.c_str();
    Desc.BaseURI      = BaseURI.c_str();
    Desc.pTextureData = &Data;
    Desc.IsSRGB       = True;
    RadientTextureLoadInfoX Info{Desc};
    EXPECT_NE(Info.Get().URI, URI.c_str());
    EXPECT_NE(Info.Get().BaseURI, BaseURI.c_str());
    EXPECT_NE(Info.Get().pTextureData, &Data);
    EXPECT_NE(Info.Get().pTextureData->pMipLevels, &Mip);
    URI.assign(200, 'x');
    BaseURI.clear();
    Mip.pDataBlob = nullptr;
    Data.Width    = 100;
    Blob.Release();
    EXPECT_STREQ(Info.Get().URI, "memory:texture");
    EXPECT_STREQ(Info.Get().BaseURI, "assets/");
    EXPECT_EQ(Info.Get().pTextureData->Width, 2u);
    EXPECT_NE(Info.Get().pTextureData->pMipLevels[0].pDataBlob, nullptr);
    EXPECT_TRUE(Info.Get().IsSRGB);

    RadientTextureLoadInfoX Copy{Info};
    EXPECT_NE(Copy.Get().pTextureData, Info.Get().pTextureData);
    EXPECT_NE(Copy.Get().pTextureData->pMipLevels, Info.Get().pTextureData->pMipLevels);
    Copy.SetTextureData(*Copy.Get().pTextureData);
    Info.Clear();
    EXPECT_EQ(Events.Destroyed, 0u);
    Copy.Clear();
    EXPECT_EQ(Events.Destroyed, 1u);
}

TEST(RadientTypesXTest, LoadInfoMoveAndSwapRebindShortAndLongStrings)
{
    const std::array<std::string, 2> URIs{{"x", std::string(200, 'y')}};
    for (const std::string& URI : URIs)
    {
        BlobEvents                             Events;
        RefCntAutoPtr<IRadientMutableDataBlob> Blob = MakeBlob(Events);
        ASSERT_NE(Blob, nullptr);
        RadientTextureDataX Data{2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
        Data.AddMip(Blob);
        RadientTextureLoadInfoX Original;
        Original.SetURI(URI).SetBaseURI(URI).SetTextureData(std::move(Data));
        EXPECT_EQ(Data.GetMipLevelCount(), 0u);
        RadientTextureLoadInfoX Assigned;
        Assigned = Original;
        Original.Clear();
        RadientTextureLoadInfoX Moved{std::move(Assigned)};
        EXPECT_EQ(Assigned.Get().URI, nullptr);
        EXPECT_EQ(Assigned.Get().BaseURI, nullptr);
        EXPECT_EQ(Assigned.Get().pTextureData, nullptr);
        Assigned.SetURI("reuse");
        EXPECT_STREQ(Moved.Get().URI, URI.c_str());
        Original = std::move(Moved);
        EXPECT_EQ(Moved.Get().URI, nullptr);
        EXPECT_EQ(Moved.Get().pTextureData, nullptr);
        EXPECT_STREQ(Original.Get().BaseURI, URI.c_str());
        EXPECT_EQ(Original.Get().pTextureData->pMipLevels[0].pDataBlob, Blob.RawPtr());

        RadientTextureLoadInfoX EmptyURI;
        EmptyURI.SetURI("").SetDataBlob(Blob);
        Original.Swap(EmptyURI);
        EXPECT_STREQ(Original.Get().URI, "");
        EXPECT_EQ(Original.Get().BaseURI, nullptr);
        EXPECT_EQ(Original.Get().pDataBlob, Blob.RawPtr());
        EXPECT_EQ(Original.Get().pTextureData, nullptr);
        EXPECT_STREQ(EmptyURI.Get().URI, URI.c_str());
        EXPECT_STREQ(EmptyURI.Get().BaseURI, URI.c_str());
        EXPECT_EQ(EmptyURI.Get().pTextureData->Width, 2u);
        RadientTextureLoadInfoX& Alias = EmptyURI;
        EmptyURI                       = Alias;
        EmptyURI                       = std::move(Alias);
        EXPECT_STREQ(EmptyURI.Get().URI, URI.c_str());
        EXPECT_EQ(EmptyURI.Get().pTextureData->Width, 2u);
        EmptyURI.SetURI(EmptyURI.Get().URI);
        EmptyURI.SetBaseURI(EmptyURI.Get().BaseURI);
        EXPECT_STREQ(EmptyURI.Get().BaseURI, URI.c_str());
    }
}

TEST(RadientTypesXTest, SourceSettersPreserveIdentityAndReleasePreviousBlobs)
{
    BlobEvents                             EncodedEvents;
    BlobEvents                             MipEvents;
    RefCntAutoPtr<IRadientMutableDataBlob> Encoded = MakeBlob(EncodedEvents);
    RefCntAutoPtr<IRadientMutableDataBlob> MipBlob = MakeBlob(MipEvents);
    ASSERT_NE(Encoded, nullptr);
    ASSERT_NE(MipBlob, nullptr);
    RadientTextureLoadInfoX Info;
    Info.SetURI("id").SetBaseURI("base/").SetSRGB(True).SetDataBlob(Encoded);
    Encoded.Release();
    RadientTextureDataX Data{4, 4, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    Data.AddMip(MipBlob).SetGenerateMips(False);
    Info.SetTextureData(Data);
    Data.Clear();
    MipBlob.Release();
    EXPECT_EQ(EncodedEvents.Destroyed, 1u);
    EXPECT_EQ(MipEvents.Destroyed, 0u);
    EXPECT_EQ(Info.Get().pDataBlob, nullptr);
    EXPECT_FALSE(Info.Get().pTextureData->GenerateMips);
    Info.SetURI("new-id").SetBaseURI("");
    ASSERT_NE(Info.Get().pTextureData, nullptr);
    // The new encoded reference must be retained before dropping the sole mip owner.
    Info.SetDataBlob(Info.Get().pTextureData->pMipLevels[0].pDataBlob);
    EXPECT_EQ(MipEvents.Destroyed, 0u);
    EXPECT_EQ(Info.Get().pTextureData, nullptr);
    EXPECT_NE(Info.Get().pDataBlob, nullptr);
    Info.ClearSourceData();
    EXPECT_EQ(MipEvents.Destroyed, 1u);
    EXPECT_STREQ(Info.Get().URI, "new-id");
    EXPECT_STREQ(Info.Get().BaseURI, "");
    EXPECT_TRUE(Info.Get().IsSRGB);
    EXPECT_EQ(Info.Get().pDataBlob, nullptr);
    Info.SetURI(nullptr).SetBaseURI(nullptr);
    EXPECT_EQ(Info.Get().URI, nullptr);
    EXPECT_EQ(Info.Get().BaseURI, nullptr);
    EXPECT_EQ(EncodedEvents.ReadersReleased, 0u);
    EXPECT_EQ(MipEvents.ReadersReleased, 0u);
    Info.Clear();
    EXPECT_FALSE(Info.Get().IsSRGB);
}

TEST(RadientTypesXTest, TextureLoadCapturesWrapperBeforeAsyncProcessing)
{
    BlobEvents                             Events;
    RefCntAutoPtr<IRadientMutableDataBlob> Blob = MakeBlob(Events, 16);
    ASSERT_NE(Blob, nullptr);
    void* Pixels = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&Pixels), RADIENT_STATUS_OK);
    std::memset(Pixels, 255, 16);
    ASSERT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    RadientTextureDataX Data{2, 2, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    Data.AddMip(Blob);
    RadientTextureLoadInfoX Info;
    Info.SetTextureData(std::move(Data)).SetURI("memory:wrapper-test");
    RefCntAutoPtr<IThreadPool>          ThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    RadientTextureAssetManagerSharedPtr Manager    = RadientTextureAssetManager::Create(RadientTextureAssetManager::CreateInfo{});
    ASSERT_NE(ThreadPool, nullptr);
    ASSERT_NE(Manager, nullptr);
    RefCntAutoPtr<IRadientTextureAsset> Texture;
    ASSERT_EQ(Manager->LoadTexture(*ThreadPool, Info, &Texture), RADIENT_STATUS_PENDING);
    Info.Clear();
    Blob.Release();
    EXPECT_EQ(Events.Destroyed, 0u);
    ASSERT_TRUE(ThreadPool->ProcessTask(0, false));
    ASSERT_EQ(RadientTextureAssetManager::GetLoadStatus(Texture), RADIENT_STATUS_OK);
    EXPECT_EQ(Texture->GetDesc().Width, 2u);
    EXPECT_EQ(Texture->GetDesc().Height, 2u);
    EXPECT_EQ(Texture->GetDesc().MipLevels, 2u);
    EXPECT_EQ(Texture->GetDesc().Format, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM);
    ThreadPool->StopThreads();
}
