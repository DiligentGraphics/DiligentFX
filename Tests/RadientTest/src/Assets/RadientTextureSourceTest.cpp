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

#include "Assets/RadientTextureFormat.hpp"
#include "Assets/RadientTextureSource.hpp"

#include "RadientTestAssetHelpers.hpp"
#include "TextureLoader.h"

#include "gtest/gtest.h"

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Ensures texture loading captures storage metadata only after BeginRead.
class ScopeCheckedDataBlob final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;
    ScopeCheckedDataBlob(IReferenceCounters* pRefCounters, const void* pData, Uint64 Size) :
        TBase{pRefCounters}, m_pData{pData}, m_Size{Size} {}
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);

    Uint64 DILIGENT_CALL_TYPE GetSize() const override
    {
        EXPECT_NE(ReaderCount, 0u);
        return m_Size;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE BeginRead(const void** ppData) override
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        ++ReaderCount;
        *ppData = m_pData;
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE EndRead() override
    {
        if (ReaderCount == 0)
            return RADIENT_STATUS_INVALID_OPERATION;
        --ReaderCount;
        return RADIENT_STATUS_OK;
    }

    Uint32 ReaderCount = 0;

private:
    const void* const m_pData;
    const Uint64      m_Size;
};

struct ReentrantReleaseState
{
    RadientTextureSource* pSource       = nullptr;
    Uint32                Count         = 0;
    bool                  SourceHasData = true;
    bool                  CacheKeyEmpty = false;
};

RefCntAutoPtr<IRadientDataBlob> MakeReferencedDataBlob(const void* pData, Uint64 Size, RadientDataBlobReadReleaseCallbackType Callback = nullptr, void* pUserData = nullptr)
{
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Size;
    CI.pData                = pData;
    CI.OnLastReaderReleased = Callback;
    CI.pUserData            = pUserData;
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
    return pBlob;
}

std::vector<Uint8> ReadSourceBytes(const RadientTextureSource& Source)
{
    const Uint8* pBytes = static_cast<const Uint8*>(Source.GetData());
    return std::vector<Uint8>{pBytes, pBytes + Source.GetDataSize()};
}

} // namespace

TEST(RadientTextureSourceTest, BuildsCanonicalURITextureCacheKey)
{
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI    = "Textures/Albedo.png";
    LoadInfo.IsSRGB = True;

    RadientTextureSource Source{LoadInfo};
    EXPECT_TRUE(Source.MakeCacheKey().empty());

    RefCntAutoPtr<TestRadientAssetLocation> pLocation{
        MakeNewRCObj<TestRadientAssetLocation>()("file:///textures/albedo.png")};
    RefCntAutoPtr<TestRadientAssetLocation> pSameLocation{
        MakeNewRCObj<TestRadientAssetLocation>()("file:///textures/albedo.png")};
    RefCntAutoPtr<TestRadientAssetLocation> pDifferentLocation{
        MakeNewRCObj<TestRadientAssetLocation>()("file:///other/albedo.png")};

    EXPECT_EQ(Source.MakeCacheKey(pLocation), Source.MakeCacheKey(pSameLocation));
    EXPECT_NE(Source.MakeCacheKey(pLocation), Source.MakeCacheKey(pDifferentLocation));

    RadientTextureLoadInfo BaseLoadInfo = LoadInfo;
    BaseLoadInfo.BaseURI                = "Scenes/SceneA.gltf";
    RadientTextureSource BaseSource{BaseLoadInfo};
    EXPECT_EQ(Source.MakeCacheKey(pLocation), BaseSource.MakeCacheKey(pLocation));

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = False;
    RadientTextureSource LinearSource{LinearLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(pLocation), LinearSource.MakeCacheKey(pLocation));
}

TEST(RadientTextureSourceTest, BuildsStableMemoryTextureCacheKeys)
{
    std::array<Uint8, 4> Data0{1, 2, 3, 4};
    std::array<Uint8, 4> Data1{1, 2, 3, 4};
    std::array<Uint8, 4> Data2{1, 2, 3, 5};

    auto                   pBlob0 = MakeTestDataBlob(Data0.data(), Data0.size());
    auto                   pBlob1 = MakeTestDataBlob(Data1.data(), Data1.size());
    auto                   pBlob2 = MakeTestDataBlob(Data2.data(), Data2.size());
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob0;

    RadientTextureLoadInfo SameDataLoadInfo = LoadInfo;
    SameDataLoadInfo.pDataBlob              = pBlob1;

    RadientTextureSource Source{LoadInfo};
    RadientTextureSource SameDataSource{SameDataLoadInfo};
    EXPECT_EQ(Source.MakeCacheKey(), SameDataSource.MakeCacheKey());

    RadientTextureLoadInfo DifferentDataLoadInfo = LoadInfo;
    DifferentDataLoadInfo.pDataBlob              = pBlob2;
    RadientTextureSource DifferentDataSource{DifferentDataLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(), DifferentDataSource.MakeCacheKey());

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = True;
    RadientTextureSource LinearSource{LinearLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(), LinearSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, MapsRadientTextureFormats)
{
    const std::array<std::pair<RADIENT_TEXTURE_FORMAT, TEXTURE_FORMAT>, 28> Formats{
        std::pair{RADIENT_TEXTURE_FORMAT_R8_UNORM, TEX_FORMAT_R8_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_RG8_UNORM, TEX_FORMAT_RG8_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, TEX_FORMAT_RGBA8_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB, TEX_FORMAT_RGBA8_UNORM_SRGB},
        std::pair{RADIENT_TEXTURE_FORMAT_R8_UINT, TEX_FORMAT_R8_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG8_UINT, TEX_FORMAT_RG8_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA8_UINT, TEX_FORMAT_RGBA8_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R8_SINT, TEX_FORMAT_R8_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG8_SINT, TEX_FORMAT_RG8_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA8_SINT, TEX_FORMAT_RGBA8_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R16_UNORM, TEX_FORMAT_R16_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_RG16_UNORM, TEX_FORMAT_RG16_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA16_UNORM, TEX_FORMAT_RGBA16_UNORM},
        std::pair{RADIENT_TEXTURE_FORMAT_R16_UINT, TEX_FORMAT_R16_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG16_UINT, TEX_FORMAT_RG16_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA16_UINT, TEX_FORMAT_RGBA16_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R16_SINT, TEX_FORMAT_R16_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG16_SINT, TEX_FORMAT_RG16_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA16_SINT, TEX_FORMAT_RGBA16_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R32_UINT, TEX_FORMAT_R32_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG32_UINT, TEX_FORMAT_RG32_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA32_UINT, TEX_FORMAT_RGBA32_UINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R32_SINT, TEX_FORMAT_R32_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG32_SINT, TEX_FORMAT_RG32_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA32_SINT, TEX_FORMAT_RGBA32_SINT},
        std::pair{RADIENT_TEXTURE_FORMAT_R32_FLOAT, TEX_FORMAT_R32_FLOAT},
        std::pair{RADIENT_TEXTURE_FORMAT_RG32_FLOAT, TEX_FORMAT_RG32_FLOAT},
        std::pair{RADIENT_TEXTURE_FORMAT_RGBA32_FLOAT, TEX_FORMAT_RGBA32_FLOAT}};

    EXPECT_EQ(RadientToTextureFormat(RADIENT_TEXTURE_FORMAT_UNKNOWN), TEX_FORMAT_UNKNOWN);
    for (const auto& [RadientFormat, TextureFormat] : Formats)
        EXPECT_EQ(RadientToTextureFormat(RadientFormat), TextureFormat);
}

TEST(RadientTextureSourceTest, BuildsStableTextureDataCacheKeys)
{
    std::array<Uint8, 4>  Data0{1, 2, 3, 4};
    std::array<Uint8, 4>  Data1{1, 2, 3, 4};
    std::array<Uint8, 4>  Data2{1, 2, 3, 5};
    std::array<Uint8, 16> Data3{1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4};

    auto               pTextureDataBlob = MakeTestDataBlob(Data0.data(), Data0.size() * sizeof(Data0[0]));
    RadientTextureData TextureData{};
    TextureData.Width     = 2;
    TextureData.Height    = 2;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pDataBlob = pTextureDataBlob;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    auto               pSameTextureDataBlob = MakeTestDataBlob(Data1.data(), Data1.size() * sizeof(Data1[0]));
    RadientTextureData SameTextureData      = TextureData;
    SameTextureData.pDataBlob               = pSameTextureDataBlob;
    RadientTextureLoadInfo SameDataLoadInfo{};
    SameDataLoadInfo.pTextureData = &SameTextureData;

    RadientTextureSource Source{LoadInfo};
    RadientTextureSource SameDataSource{SameDataLoadInfo};
    EXPECT_EQ(Source.MakeCacheKey(), SameDataSource.MakeCacheKey());

    auto               pDifferentTextureDataBlob = MakeTestDataBlob(Data2.data(), Data2.size() * sizeof(Data2[0]));
    RadientTextureData DifferentTextureData      = TextureData;
    DifferentTextureData.pDataBlob               = pDifferentTextureDataBlob;
    RadientTextureLoadInfo DifferentDataLoadInfo{};
    DifferentDataLoadInfo.pTextureData = &DifferentTextureData;
    RadientTextureSource DifferentDataSource{DifferentDataLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(), DifferentDataSource.MakeCacheKey());

    auto               pDifferentFormatTextureDataBlob = MakeTestDataBlob(Data3.data(), Data3.size() * sizeof(Data3[0]));
    RadientTextureData DifferentFormatTextureData      = TextureData;
    DifferentFormatTextureData.Format                  = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    DifferentFormatTextureData.pDataBlob               = pDifferentFormatTextureDataBlob;
    RadientTextureLoadInfo DifferentFormatLoadInfo{};
    DifferentFormatLoadInfo.pTextureData = &DifferentFormatTextureData;
    RadientTextureSource DifferentFormatSource{DifferentFormatLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(), DifferentFormatSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, TextureDataCacheKeyIgnoresRowAndTrailingPadding)
{
    constexpr Uint32 Width         = 3;
    constexpr Uint32 Height        = 2;
    constexpr Uint32 ActiveRowSize = Width * 4;
    constexpr Uint32 Stride0       = 16;
    constexpr Uint32 Stride1       = 20;
    constexpr Uint32 DataSize0     = (Height - 1) * Stride0 + ActiveRowSize;
    constexpr Uint32 DataSize1     = (Height - 1) * Stride1 + ActiveRowSize;

    // clang-format off
    std::array<Uint8, DataSize0> Data0{
        // Row 0 pixels, followed by 4 bytes of padding.
        1, 2, 3, 4,     5, 6, 7, 8,     9, 10, 11, 12,
        90, 91, 92, 93,
        // Row 1 pixels. Final-row padding is not part of the valid source span.
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    std::array<Uint8, DataSize1 + 3> Data1{
        // Same row 0 pixels, followed by different 8-byte padding.
        1, 2, 3, 4,     5, 6, 7, 8,     9, 10, 11, 12,
        190, 191, 192, 193, 194, 195, 196, 197,
        // Same row 1 pixels, followed by unrelated trailing storage.
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 201, 202, 203};
    // clang-format on

    auto               pTextureDataBlob = MakeTestDataBlob(Data0.data(), Data0.size() * sizeof(Data0[0]));
    RadientTextureData TextureData{};
    TextureData.Width     = Width;
    TextureData.Height    = Height;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.Stride    = Stride0;
    TextureData.pDataBlob = pTextureDataBlob;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};

    auto               pSameTextureDataBlob = MakeTestDataBlob(Data1.data(), Data1.size() * sizeof(Data1[0]));
    RadientTextureData SameTextureData      = TextureData;
    SameTextureData.pDataBlob               = pSameTextureDataBlob;
    SameTextureData.Stride                  = Stride1;
    RadientTextureLoadInfo SameLoadInfo{};
    SameLoadInfo.pTextureData = &SameTextureData;

    RadientTextureSource SameSource{SameLoadInfo};

    EXPECT_EQ(Source.MakeCacheKey(), SameSource.MakeCacheKey());

    std::array<Uint8, DataSize0> Data2 = Data0;
    Data2[4]                           = 200;

    auto               pDifferentTextureDataBlob = MakeTestDataBlob(Data2.data(), Data2.size() * sizeof(Data2[0]));
    RadientTextureData DifferentTextureData      = TextureData;
    DifferentTextureData.pDataBlob               = pDifferentTextureDataBlob;
    RadientTextureLoadInfo DifferentLoadInfo{};
    DifferentLoadInfo.pTextureData = &DifferentTextureData;

    RadientTextureSource DifferentSource{DifferentLoadInfo};

    EXPECT_NE(Source.MakeCacheKey(), DifferentSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, SupportsRGBA8_UNORM_SRGBTextureDataFormat)
{
    std::array<Uint8, 16> Data{};

    auto               pTextureDataBlob = MakeTestDataBlob(Data.data(), Data.size() * sizeof(Data[0]));
    RadientTextureData TextureData{};
    TextureData.Width     = 2;
    TextureData.Height    = 2;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB;
    TextureData.pDataBlob = pTextureDataBlob;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "rgba8-srgb-data";
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};
    EXPECT_EQ(Source.GetDataSize(), Data.size());

    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);
    EXPECT_EQ(pLoader->GetTextureDesc().Format, TEX_FORMAT_RGBA8_UNORM_SRGB);

    RadientTextureData LinearTextureData = TextureData;
    LinearTextureData.Format             = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;

    RadientTextureLoadInfo LinearLoadInfo{};
    LinearLoadInfo.URI          = LoadInfo.URI;
    LinearLoadInfo.pTextureData = &LinearTextureData;

    RadientTextureSource LinearSource{LinearLoadInfo};
    EXPECT_NE(Source.MakeCacheKey(), LinearSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, CreatesLoaderFromIntegerTextureData)
{
    const std::array<Uint16, 1> Data{42};

    auto               pTextureDataBlob = MakeTestDataBlob(Data.data(), Data.size() * sizeof(Data[0]));
    RadientTextureData TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R16_UINT;
    TextureData.pDataBlob = pTextureDataBlob;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "r16-uint-data";
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource          Source{LoadInfo};
    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);
    EXPECT_EQ(pLoader->GetTextureDesc().Format, TEX_FORMAT_R16_UINT);
}

TEST(RadientTextureSourceTest, RetainsBlobWithoutCopyingEncodedBytes)
{
    const std::array<Uint8, 4> Data{1, 2, 3, 4};
    Uint32                     ReadReleases = 0;
    auto                       pBlob        = MakeTestDataBlob(Data.data(), Data.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RefCntWeakPtr<IRadientDataBlob> WeakBlob{pBlob};

    const void* pBytes = nullptr;
    ASSERT_EQ(pBlob->BeginRead(&pBytes), RADIENT_STATUS_OK);
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob;
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_EQ(Source.GetData(), pBytes);
        EXPECT_EQ(Source.GetDataSize(), Data.size());
        EXPECT_EQ(Source.GetData(), pBytes);
        EXPECT_EQ(pBlob->EndRead(), RADIENT_STATUS_OK);
        EXPECT_EQ(ReadReleases, 0u);
        pBlob.Release();
        EXPECT_NE(WeakBlob.Lock(), nullptr);
        EXPECT_EQ(ReadSourceBytes(Source), (std::vector<Uint8>{Data.begin(), Data.end()}));
    }
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
}

TEST(RadientTextureSourceTest, RetainsDecodedBlobWithoutCopyingOrPackingRows)
{
    const std::array<Uint8, 8>      Data{1, 2, 90, 91, 5, 6, 92, 93};
    auto                            pBlob = MakeReferencedDataBlob(Data.data(), Data.size());
    RefCntWeakPtr<IRadientDataBlob> WeakBlob{pBlob};

    RadientTextureData TextureData{};
    TextureData.Width     = 2;
    TextureData.Height    = 2;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.Stride    = 4;
    TextureData.pDataBlob = pBlob;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource Source{LoadInfo};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_TRUE(Source.IsTextureData());
        EXPECT_EQ(Source.GetData(), Data.data());
        // The texture starts at byte zero and excludes unused final-row padding.
        EXPECT_EQ(Source.GetDataSize(), 6u);
        pBlob.Release();
        EXPECT_NE(WeakBlob.Lock(), nullptr);
        EXPECT_EQ(ReadSourceBytes(Source), (std::vector<Uint8>{1, 2, 90, 91, 5, 6}));
    }
    EXPECT_EQ(WeakBlob.Lock(), nullptr);
}

TEST(RadientTextureSourceTest, LoaderReferencesPaddedTextureRowsWithoutCopying)
{
    constexpr Uint32 Width  = 3;
    constexpr Uint32 Height = 2;
    constexpr Uint32 Stride = 16;
    // There is no final-row padding: only the minimum readable span is present.
    const std::array<Uint8, 28> Data{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        90, 91, 92, 93,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    auto               pBlob = MakeReferencedDataBlob(Data.data(), Data.size());
    RadientTextureData TextureData{};
    TextureData.Width     = Width;
    TextureData.Height    = Height;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.Stride    = Stride;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);
    const auto& Subres = pLoader->GetSubresourceData(0);
    EXPECT_EQ(Subres.pData, Data.data());
    EXPECT_EQ(Subres.Stride, Stride);
    EXPECT_EQ(Source.GetDataSize(), Data.size());
    const auto* pLastRow = static_cast<const Uint8*>(Subres.pData) + Subres.Stride;
    EXPECT_EQ(pLastRow[0], 13u);
    EXPECT_EQ(pLastRow[11], 24u);
}

TEST(RadientTextureSourceTest, MovePreservesBlobMemory)
{
    std::array<Uint8, 4>     Data{1, 2, 3, 4};
    const std::vector<Uint8> Expected{Data.begin(), Data.end()};

    auto                   pBlob = MakeTestDataBlob(Data.data(), Data.size());
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob;

    RadientTextureSource Source{LoadInfo};
    RadientTextureSource Moved{std::move(Source)};

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Moved), Expected);
    EXPECT_NE(Moved.GetData(), Data.data());
}

TEST(RadientTextureSourceTest, DecodedSourceHoldsMutableBlobReadAccess)
{
    const std::array<Uint8, 4> Data{1, 2, 3, 4};
    Uint32                     ReadReleases = 0;
    auto                       pBlob        = MakeTestMutableDataBlob(Data.data(), Data.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureData TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    void* pWriteData      = nullptr;
    {
        RadientTextureSource Source{LoadInfo};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(pBlob->Resize(Data.size() + 1), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(ReadReleases, 0u);
    }
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(pBlob->Resize(Data.size() + 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientTextureSourceTest, DetachesDecodedBlobStateBeforeReadReleaseCallback)
{
    const std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReentrantReleaseState      State;

    auto pBlob = MakeTestMutableDataBlob(
        Data.data(), Data.size(),
        [](IRadientDataBlob* pReleasedBlob, void* pUserData) {
        auto& CallbackState = *static_cast<ReentrantReleaseState*>(pUserData);
        ++CallbackState.Count;
        CallbackState.SourceHasData = CallbackState.pSource->IsMemory();
        CallbackState.CacheKeyEmpty = CallbackState.pSource->MakeCacheKey().empty();
        RefCntAutoPtr<IRadientMutableDataBlob> pMutableBlob{pReleasedBlob, IID_RadientMutableDataBlob};
        ASSERT_NE(pMutableBlob, nullptr);
        EXPECT_EQ(pMutableBlob->Resize(8), RADIENT_STATUS_OK);
        void* pWriteData = nullptr;
        EXPECT_EQ(pMutableBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
        EXPECT_EQ(pMutableBlob->EndWrite(), RADIENT_STATUS_OK); }, &State);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureData TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource Source{LoadInfo};
        State.pSource = &Source;
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    }
    EXPECT_EQ(State.Count, 1u);
    EXPECT_FALSE(State.SourceHasData);
    EXPECT_TRUE(State.CacheKeyEmpty);
}

TEST(RadientTextureSourceTest, InvalidTextureDataDoesNotFallbackToOtherSourceTypes)
{
    const std::array<Uint8, 8> Data{1, 2, 3, 4, 5, 6, 7, 8};
    auto                       pBlob        = MakeTestDataBlob(Data.data(), Data.size());
    auto                       pEncodedBlob = MakeTestDataBlob(TransparentPng.data(), TransparentPng.size());
    auto                       pShortBlob   = MakeTestDataBlob(Data.data(), Uint64{5});
    auto                       pEmptyBlob   = MakeTestDataBlob(nullptr, 0);
    RadientTextureData         ValidData{};
    ValidData.Width     = 2;
    ValidData.Height    = 2;
    ValidData.Format    = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    ValidData.Stride    = 4;
    ValidData.pDataBlob = pBlob;

    std::array<RadientTextureData, 7> InvalidData;
    InvalidData.fill(ValidData);
    InvalidData[0].Format    = RADIENT_TEXTURE_FORMAT_UNKNOWN;
    InvalidData[1].Width     = 0;
    InvalidData[2].Height    = 0;
    InvalidData[3].Stride    = 1;
    InvalidData[4].pDataBlob = nullptr;
    InvalidData[5].pDataBlob = pShortBlob;
    InvalidData[6].pDataBlob = pEmptyBlob;
    for (size_t Index = 0; Index < InvalidData.size(); ++Index)
    {
        SCOPED_TRACE(Index);
        RadientTextureLoadInfo LoadInfo{};
        LoadInfo.URI          = "textures/albedo.png";
        LoadInfo.pTextureData = &InvalidData[Index];
        LoadInfo.pDataBlob    = pEncodedBlob;
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_FALSE(Source.IsMemory());
        EXPECT_FALSE(Source.IsTextureData());
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        RefCntAutoPtr<ITextureLoader> pLoader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pLoader, nullptr);
    }
}

TEST(RadientTextureSourceTest, DecodedBlobReadAccessSurvivesMovesAndMultipleSources)
{
    const std::array<Uint8, 4> Data{1, 2, 3, 4};
    Uint32                     ReadReleases = 0;
    auto                       pBlob        = MakeTestMutableDataBlob(Data.data(), Data.size(), CountBlobReadReleases, &ReadReleases);
    RadientTextureData         TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource First{LoadInfo};
        RadientTextureSource Second{LoadInfo};
        RadientTextureSource Moved{std::move(First)};
        EXPECT_FALSE(First.IsMemory());
        const void* pData = Moved.GetData();
        Second            = std::move(Moved);
        EXPECT_FALSE(Moved.IsMemory());
        EXPECT_TRUE(Second.IsTextureData());
        EXPECT_EQ(Second.GetData(), pData);
        EXPECT_EQ(ReadReleases, 0u);
        EXPECT_EQ(pBlob->Resize(Data.size() + 1), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(ReadReleases, 1u);
}

TEST(RadientTextureSourceTest, CreatesLoaderFromTextureData)
{
    const std::array<Uint8, 16> Data{
        1, 3, 20, 24,
        5, 7, 28, 32,
        40, 44, 80, 88,
        52, 56, 94, 98};

    auto               pTextureDataBlob = MakeTestDataBlob(Data.data(), Data.size() * sizeof(Data[0]));
    RadientTextureData TextureData{};
    TextureData.Width     = 4;
    TextureData.Height    = 4;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pDataBlob = pTextureDataBlob;
    TextureData.Stride    = 4;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "r8-data";
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource          Source{LoadInfo};
    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);

    const TextureDesc& Desc = pLoader->GetTextureDesc();
    EXPECT_EQ(Desc.Type, RESOURCE_DIM_TEX_2D);
    EXPECT_EQ(Desc.Width, 4u);
    EXPECT_EQ(Desc.Height, 4u);
    EXPECT_EQ(Desc.Format, TEX_FORMAT_R8_UNORM);
    EXPECT_EQ(Desc.MipLevels, 3u);

    const std::array<Uint8, 4> ExpectedMip1{
        4, 26,
        48, 90};
    const std::array<Uint8, 1> ExpectedMip2{42};

    const auto CheckMip = [&](Uint32 Mip, Uint32 Width, Uint32 Height, const Uint8* pExpected) {
        const TextureSubResData& Subres = pLoader->GetSubresourceData(Mip);
        ASSERT_NE(Subres.pData, nullptr);

        const Uint8* pData = static_cast<const Uint8*>(Subres.pData);
        for (Uint32 y = 0; y < Height; ++y)
        {
            const Uint8* pRow = pData + Subres.Stride * y;
            for (Uint32 x = 0; x < Width; ++x)
                EXPECT_EQ(pRow[x], pExpected[y * Width + x]);
        }
    };

    CheckMip(0, 4, 4, Data.data());
    CheckMip(1, 2, 2, ExpectedMip1.data());
    CheckMip(2, 1, 1, ExpectedMip2.data());
}

TEST(RadientTextureSourceTest, CreatesLoaderFromURIAssetResolver)
{
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI     = "textures/albedo.png";
    LoadInfo.BaseURI = "memory://scenes/scene.gltf";

    RadientTextureSource Source{LoadInfo};

    RefCntAutoPtr<TestRadientAssetResolver> pResolver{MakeNewRCObj<TestRadientAssetResolver>()()};
    pResolver->AddAsset(LoadInfo.URI,
                        "memory://resolved/albedo.png",
                        std::vector<Uint8>{TransparentPng.begin(), TransparentPng.end()});

    const TestRadientAssetResolverStats& Stats = pResolver->GetStats();

    RefCntAutoPtr<IRadientAssetLocation> pLocation;
    ASSERT_EQ(pResolver->ResolveAssetLocation(
                  {LoadInfo.URI, LoadInfo.BaseURI},
                  pLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pLocation, nullptr);
    EXPECT_EQ(Stats.ResolveLocationCount, 1u);
    EXPECT_EQ(Stats.OpenCount, 0u);

    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(pResolver, pLocation, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    ASSERT_NE(pLoader, nullptr);
    EXPECT_EQ(Stats.ResolveLocationCount, 1u);
    EXPECT_EQ(Stats.OpenCount, 1u);
    EXPECT_EQ(Stats.LastURI, LoadInfo.URI);
    EXPECT_EQ(Stats.LastBaseURI, LoadInfo.BaseURI);
    EXPECT_EQ(Stats.LastResolvedURI, pLocation->GetLocation());

    const TextureDesc& Desc = pLoader->GetTextureDesc();
    EXPECT_EQ(Desc.Type, RESOURCE_DIM_TEX_2D);
    EXPECT_EQ(Desc.Width, 1u);
    EXPECT_EQ(Desc.Height, 1u);
    EXPECT_EQ(Desc.Format, TEX_FORMAT_RGBA8_UNORM);

    EXPECT_EQ(Stats.AssetDataDestroyCount, 0u);
    pLoader.Release();
    EXPECT_EQ(Stats.AssetDataDestroyCount, 1u);
}

TEST(RadientTextureSourceTest, RequiresOKAssetOpenStatus)
{
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI = "textures/albedo.png";

    RadientTextureSource Source{LoadInfo};

    RefCntAutoPtr<TestRadientAssetResolver> pResolver{MakeNewRCObj<TestRadientAssetResolver>()()};
    pResolver->AddAsset(LoadInfo.URI,
                        "memory://resolved/albedo.png",
                        std::vector<Uint8>{TransparentPng.begin(), TransparentPng.end()});

    RefCntAutoPtr<IRadientAssetLocation> pLocation;
    ASSERT_EQ(pResolver->ResolveAssetLocation(
                  {LoadInfo.URI, nullptr},
                  pLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pLocation, nullptr);

    pResolver->SetOpenAssetStatus(RADIENT_STATUS_OUT_OF_DATE);

    RefCntAutoPtr<ITextureLoader> pLoader;
    EXPECT_EQ(Source.CreateLoader(pResolver, pLocation, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(pLoader, nullptr);
}

TEST(RadientTextureSourceTest, BlobReadAccessSurvivesMovesAndMultipleSources)
{
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob;
    {
        RadientTextureSource First{LoadInfo};
        RadientTextureSource Second{LoadInfo};
        RadientTextureSource Moved{std::move(First)};
        EXPECT_FALSE(First.IsMemory());
        // Assignment releases Second's read scope; Moved still holds another one.
        Second = std::move(Moved);
        EXPECT_FALSE(Moved.IsMemory());
        EXPECT_TRUE(Second.IsMemory());
        EXPECT_EQ(ReadReleases, 0u);
        void* pWriteData = nullptr;
        EXPECT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_EQ(ReadReleases, 1u);
}

TEST(RadientTextureSourceTest, BusyBlobDoesNotFallBackToURI)
{
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(TransparentPng.data(), TransparentPng.size(), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    void* pWriteData = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob;
    LoadInfo.URI       = "textures/albedo.png";
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        RefCntAutoPtr<ITextureLoader> pLoader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(pLoader, nullptr);
    }
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadReleases, 0u);
}

TEST(RadientTextureSourceTest, DetachesBlobStateBeforeReadReleaseCallback)
{
    ReentrantReleaseState State;

    auto pBlob = MakeTestMutableDataBlob(
        TransparentPng.data(), TransparentPng.size(),
        [](IRadientDataBlob* pReleasedBlob, void* pUserData) {
            auto& CallbackState = *static_cast<ReentrantReleaseState*>(pUserData);
            ++CallbackState.Count;
            CallbackState.SourceHasData = CallbackState.pSource->IsMemory();
            CallbackState.CacheKeyEmpty = CallbackState.pSource->MakeCacheKey().empty();
            RefCntAutoPtr<IRadientMutableDataBlob> pMutableBlob{pReleasedBlob, IID_RadientMutableDataBlob};
            ASSERT_NE(pMutableBlob, nullptr);
            void* pWriteData = nullptr;
            EXPECT_EQ(pMutableBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
            EXPECT_EQ(pMutableBlob->EndWrite(), RADIENT_STATUS_OK); }, &State);
    ASSERT_NE(pBlob, nullptr);
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pDataBlob = pBlob;
    {
        RadientTextureSource Source{LoadInfo};
        State.pSource = &Source;
    }
    EXPECT_EQ(State.Count, 1u);
    EXPECT_FALSE(State.SourceHasData);
    EXPECT_TRUE(State.CacheKeyEmpty);
}

TEST(RadientTextureSourceTest, CapturesBlobSizeUnderReadAccess)
{
    RefCntAutoPtr<ScopeCheckedDataBlob> Blob{MakeNewRCObj<ScopeCheckedDataBlob>()(
        TransparentPng.data(), TransparentPng.size())};
    RadientTextureLoadInfo              LoadInfo;
    LoadInfo.pDataBlob = Blob;
    {
        RadientTextureSource Source{LoadInfo};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(Source.GetData(), TransparentPng.data());
        EXPECT_EQ(Source.GetDataSize(), TransparentPng.size());
        EXPECT_EQ(Blob->ReaderCount, 1u);
        RefCntAutoPtr<ITextureLoader> Loader;
        ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &Loader), RADIENT_STATUS_OK);
        EXPECT_EQ(Blob->ReaderCount, 1u);
        Loader.Release();
        EXPECT_EQ(Blob->ReaderCount, 1u);
        ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &Loader), RADIENT_STATUS_OK);
        EXPECT_EQ(Blob->ReaderCount, 1u);
    }
    EXPECT_EQ(Blob->ReaderCount, 0u);
}

TEST(RadientTextureSourceTest, RejectsInvalidBlobStorageUnderReadAccess)
{
    RefCntAutoPtr<ScopeCheckedDataBlob> Blob{MakeNewRCObj<ScopeCheckedDataBlob>()(nullptr, Uint64{4})};
    RadientTextureLoadInfo              LoadInfo;
    LoadInfo.pDataBlob = Blob;
    LoadInfo.URI       = "textures/albedo.png";
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_DATA);
        EXPECT_FALSE(Source.IsMemory());
        EXPECT_EQ(Source.GetData(), nullptr);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        RefCntAutoPtr<ITextureLoader> Loader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &Loader), RADIENT_STATUS_INVALID_DATA);
        EXPECT_EQ(Loader, nullptr);
    }
    EXPECT_EQ(Blob->ReaderCount, 0u);

    if ((std::numeric_limits<size_t>::max)() < (std::numeric_limits<Uint64>::max)())
    {
        RefCntAutoPtr<ScopeCheckedDataBlob> LargeBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(
            TransparentPng.data(), static_cast<Uint64>((std::numeric_limits<size_t>::max)()) + Uint64{1})};
        LoadInfo.pDataBlob = LargeBlob;
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(LargeBlob->ReaderCount, 0u);
    }
}

TEST(RadientTextureSourceTest, RejectsEmptyBlobAndActiveEmptyBlobWriter)
{
    Uint32 ReadReleases = 0;
    auto   Blob         = MakeTestMutableDataBlob(nullptr, 0, CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(Blob, nullptr);
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pDataBlob = Blob;
    LoadInfo.URI       = "textures/albedo.png";
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
    }
    EXPECT_EQ(ReadReleases, 1u);

    void* Data = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
    }
    EXPECT_EQ(ReadReleases, 1u);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientTextureSourceTest, CapturesDecodedBlobSizeUnderReadAccess)
{
    const std::array<Uint8, 8>          Data{1, 2, 90, 91, 3, 4, 92, 93};
    RefCntAutoPtr<ScopeCheckedDataBlob> pBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(Data.data(), Data.size())};
    RadientTextureData                  TextureData{};
    TextureData.Width     = 2;
    TextureData.Height    = 2;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.Stride    = 4;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource Source{LoadInfo};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(Source.GetData(), Data.data());
        EXPECT_EQ(Source.GetDataSize(), 6u);
        EXPECT_EQ(pBlob->ReaderCount, 1u);
        RefCntAutoPtr<ITextureLoader> pLoader;
        ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->ReaderCount, 1u);
        pLoader.Release();
        EXPECT_EQ(pBlob->ReaderCount, 1u);
        ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->ReaderCount, 1u);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
}

TEST(RadientTextureSourceTest, BusyDecodedBlobDoesNotFallbackToOtherSources)
{
    const std::array<Uint8, 4> Data{1, 2, 3, 4};
    auto                       pBlob        = MakeTestMutableDataBlob(Data.data(), Data.size());
    auto                       pEncodedBlob = MakeTestDataBlob(TransparentPng.data(), TransparentPng.size());
    void*                      pWriteData   = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWriteData), RADIENT_STATUS_OK);
    RadientTextureData TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "textures/albedo.png";
    LoadInfo.pDataBlob    = pEncodedBlob;
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
    }
    LoadInfo.pDataBlob = nullptr;
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        RefCntAutoPtr<ITextureLoader> pLoader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(pLoader, nullptr);
    }
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientTextureSourceTest, RejectsInvalidDecodedBlobStorageUnderReadAccess)
{
    RefCntAutoPtr<ScopeCheckedDataBlob> pBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(nullptr, Uint64{4})};
    RadientTextureData                  TextureData{};
    TextureData.Width     = 1;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pDataBlob = pBlob;
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "textures/albedo.png";
    LoadInfo.pTextureData = &TextureData;
    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_DATA);
        EXPECT_FALSE(Source.IsMemory());
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        RefCntAutoPtr<ITextureLoader> pLoader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_INVALID_DATA);
        EXPECT_EQ(pLoader, nullptr);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
}

TEST(RadientTextureSourceTest, RejectsMisalignedDecodedBlobPointers)
{
    alignas(Uint32) const std::array<Uint8, 32> Data{};
    const std::array<RADIENT_TEXTURE_FORMAT, 2> Formats{RADIENT_TEXTURE_FORMAT_R16_UNORM, RADIENT_TEXTURE_FORMAT_R32_FLOAT};
    for (const auto Format : Formats)
    {
        SCOPED_TRACE(Format);
        Uint32             ReadReleases = 0;
        auto               pBlob        = MakeReferencedDataBlob(Data.data() + 1, Data.size() - 1, CountBlobReadReleases, &ReadReleases);
        RadientTextureData TextureData{};
        TextureData.Width     = 2;
        TextureData.Height    = 2;
        TextureData.Format    = Format;
        TextureData.pDataBlob = pBlob;
        RadientTextureLoadInfo LoadInfo{};
        LoadInfo.pTextureData = &TextureData;
        {
            RadientTextureSource Source{LoadInfo};
            EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_FALSE(Source.IsMemory());
            EXPECT_TRUE(Source.MakeCacheKey().empty());
            // Alignment is checked after BeginRead; failure releases that scope.
            EXPECT_EQ(ReadReleases, 1u);
            RefCntAutoPtr<ITextureLoader> pLoader;
            EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_EQ(pLoader, nullptr);
        }
        EXPECT_EQ(ReadReleases, 1u);
    }
}

TEST(RadientTextureSourceTest, RejectsMisalignedDecodedMultirowStridesBeforeReading)
{
    alignas(Uint32) const std::array<Uint8, 32>                    Data{};
    const std::array<std::pair<RADIENT_TEXTURE_FORMAT, Uint32>, 2> Formats{
        std::pair{RADIENT_TEXTURE_FORMAT_R16_UNORM, 2u},
        std::pair{RADIENT_TEXTURE_FORMAT_R32_FLOAT, 4u}};
    for (const auto& [Format, ComponentSize] : Formats)
    {
        SCOPED_TRACE(Format);
        Uint32             ReadReleases = 0;
        auto               pBlob        = MakeReferencedDataBlob(Data.data(), Data.size(), CountBlobReadReleases, &ReadReleases);
        RadientTextureData TextureData{};
        TextureData.Width     = 2;
        TextureData.Height    = 2;
        TextureData.Format    = Format;
        TextureData.Stride    = TextureData.Width * ComponentSize + 1;
        TextureData.pDataBlob = pBlob;
        RadientTextureDataSpan Span;
        EXPECT_FALSE(GetRadientTextureDataSpan(TextureData, Span));
        RadientTextureLoadInfo LoadInfo{};
        LoadInfo.pTextureData = &TextureData;
        {
            RadientTextureSource Source{LoadInfo};
            EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_FALSE(Source.IsMemory());
            EXPECT_TRUE(Source.MakeCacheKey().empty());
        }
        EXPECT_EQ(ReadReleases, 0u);
    }
}

TEST(RadientTextureSourceTest, AlignedDecodedComponentsAndRowPaddingGenerateMips)
{
    alignas(Uint32) const std::array<Uint8, 32>                    Data{};
    const std::array<std::pair<RADIENT_TEXTURE_FORMAT, Uint32>, 2> Formats{
        std::pair{RADIENT_TEXTURE_FORMAT_R16_UNORM, 2u},
        std::pair{RADIENT_TEXTURE_FORMAT_R32_FLOAT, 4u}};
    for (const auto& [Format, ComponentSize] : Formats)
    {
        SCOPED_TRACE(Format);
        auto               pBlob = MakeReferencedDataBlob(Data.data(), Data.size());
        RadientTextureData TextureData{};
        TextureData.Width     = 2;
        TextureData.Height    = 2;
        TextureData.Format    = Format;
        TextureData.Stride    = (TextureData.Width + 1) * ComponentSize;
        TextureData.pDataBlob = pBlob;
        RadientTextureLoadInfo LoadInfo{};
        LoadInfo.pTextureData = &TextureData;
        RadientTextureSource Source{LoadInfo};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        RefCntAutoPtr<ITextureLoader> pLoader;
        ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_OK);
        ASSERT_NE(pLoader, nullptr);
        ASSERT_EQ(pLoader->GetTextureDesc().MipLevels, 2u);
        const auto& Subres = pLoader->GetSubresourceData(0);
        EXPECT_EQ(Subres.pData, Data.data());
        EXPECT_EQ(Subres.Stride, TextureData.Stride);
        const auto* pMip = static_cast<const Uint8*>(pLoader->GetSubresourceData(1).pData);
        ASSERT_NE(pMip, nullptr);
        for (Uint32 Byte = 0; Byte < ComponentSize; ++Byte)
            EXPECT_EQ(pMip[Byte], 0u);
    }
}

TEST(RadientTextureSourceTest, SingleRowDecodedTextureAllowsUnalignedUnusedStride)
{
    alignas(Uint32) const std::array<Uint8, 8> Data{};
    auto                                       pBlob = MakeReferencedDataBlob(Data.data(), Data.size());
    RadientTextureData                         TextureData{};
    TextureData.Width     = 2;
    TextureData.Height    = 1;
    TextureData.Format    = RADIENT_TEXTURE_FORMAT_R32_FLOAT;
    TextureData.Stride    = 9;
    TextureData.pDataBlob = pBlob;
    RadientTextureDataSpan Span;
    EXPECT_TRUE(GetRadientTextureDataSpan(TextureData, Span));
    EXPECT_EQ(Span.DataSize, Data.size());
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    RadientTextureSource Source{LoadInfo};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, &pLoader), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);
    EXPECT_EQ(pLoader->GetTextureDesc().MipLevels, 2u);
    EXPECT_EQ(pLoader->GetSubresourceData(0).pData, Data.data());
    EXPECT_EQ(pLoader->GetSubresourceData(0).Stride, TextureData.Stride);
}
