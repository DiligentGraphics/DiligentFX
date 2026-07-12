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
#include <string>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

struct ReleaseState
{
    Uint32      Count    = 0;
    const void* pData    = nullptr;
    Uint64      DataSize = 0;
};

void ReleaseTextureData(const void* pData, Uint64 DataSize, void* pUserData)
{
    auto& State = *static_cast<ReleaseState*>(pUserData);
    ++State.Count;
    State.pData    = pData;
    State.DataSize = DataSize;
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

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data0.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data0.size());

    RadientTextureLoadInfo SameDataLoadInfo = LoadInfo;
    SameDataLoadInfo.pData                  = Data1.data();

    RadientTextureSource Source{LoadInfo};
    RadientTextureSource SameDataSource{SameDataLoadInfo};
    Source.MakeMemoryCopy();
    SameDataSource.MakeMemoryCopy();
    EXPECT_EQ(Source.MakeCacheKey(), SameDataSource.MakeCacheKey());

    RadientTextureLoadInfo DifferentDataLoadInfo = LoadInfo;
    DifferentDataLoadInfo.pData                  = Data2.data();
    RadientTextureSource DifferentDataSource{DifferentDataLoadInfo};
    DifferentDataSource.MakeMemoryCopy();
    EXPECT_NE(Source.MakeCacheKey(), DifferentDataSource.MakeCacheKey());

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = True;
    RadientTextureSource LinearSource{LinearLoadInfo};
    LinearSource.MakeMemoryCopy();
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

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pData  = Data0.data();

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureData SameTextureData = TextureData;
    SameTextureData.pData              = Data1.data();
    RadientTextureLoadInfo SameDataLoadInfo{};
    SameDataLoadInfo.pTextureData = &SameTextureData;

    RadientTextureSource Source{LoadInfo};
    RadientTextureSource SameDataSource{SameDataLoadInfo};
    Source.MakeMemoryCopy();
    SameDataSource.MakeMemoryCopy();
    EXPECT_EQ(Source.MakeCacheKey(), SameDataSource.MakeCacheKey());

    RadientTextureData DifferentTextureData = TextureData;
    DifferentTextureData.pData              = Data2.data();
    RadientTextureLoadInfo DifferentDataLoadInfo{};
    DifferentDataLoadInfo.pTextureData = &DifferentTextureData;
    RadientTextureSource DifferentDataSource{DifferentDataLoadInfo};
    DifferentDataSource.MakeMemoryCopy();
    EXPECT_NE(Source.MakeCacheKey(), DifferentDataSource.MakeCacheKey());

    RadientTextureData DifferentFormatTextureData = TextureData;
    DifferentFormatTextureData.Format             = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    DifferentFormatTextureData.pData              = Data3.data();
    RadientTextureLoadInfo DifferentFormatLoadInfo{};
    DifferentFormatLoadInfo.pTextureData = &DifferentFormatTextureData;
    RadientTextureSource DifferentFormatSource{DifferentFormatLoadInfo};
    DifferentFormatSource.MakeMemoryCopy();
    EXPECT_NE(Source.MakeCacheKey(), DifferentFormatSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, TextureDataCacheKeyIgnoresRowPadding)
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
    std::array<Uint8, DataSize1> Data1{
        // Same row 0 pixels, followed by different 8-byte padding.
        1, 2, 3, 4,     5, 6, 7, 8,     9, 10, 11, 12,
        190, 191, 192, 193, 194, 195, 196, 197,
        // Same row 1 pixels.
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    // clang-format on

    RadientTextureData TextureData{};
    TextureData.Width  = Width;
    TextureData.Height = Height;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.Stride = Stride0;
    TextureData.pData  = Data0.data();

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();

    RadientTextureData SameTextureData = TextureData;
    SameTextureData.pData              = Data1.data();
    SameTextureData.Stride             = Stride1;
    RadientTextureLoadInfo SameLoadInfo{};
    SameLoadInfo.pTextureData = &SameTextureData;

    RadientTextureSource SameSource{SameLoadInfo};
    SameSource.MakeMemoryCopy();

    EXPECT_EQ(Source.MakeCacheKey(), SameSource.MakeCacheKey());

    std::array<Uint8, DataSize0> Data2 = Data0;
    Data2[4]                           = 200;

    RadientTextureData DifferentTextureData = TextureData;
    DifferentTextureData.pData              = Data2.data();
    RadientTextureLoadInfo DifferentLoadInfo{};
    DifferentLoadInfo.pTextureData = &DifferentTextureData;

    RadientTextureSource DifferentSource{DifferentLoadInfo};
    DifferentSource.MakeMemoryCopy();

    EXPECT_NE(Source.MakeCacheKey(), DifferentSource.MakeCacheKey());
}

TEST(RadientTextureSourceTest, SupportsRGBA8_UNORM_SRGBTextureDataFormat)
{
    std::array<Uint8, 16> Data{};

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB;
    TextureData.pData  = Data.data();

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

    RadientTextureData TextureData{};
    TextureData.Width  = 1;
    TextureData.Height = 1;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_R16_UINT;
    TextureData.pData  = Data.data();

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI          = "r16-uint-data";
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource          Source{LoadInfo};
    RefCntAutoPtr<ITextureLoader> pLoader;
    ASSERT_EQ(Source.CreateLoader(nullptr, nullptr, pLoader.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pLoader, nullptr);
    EXPECT_EQ(pLoader->GetTextureDesc().Format, TEX_FORMAT_R16_UINT);
}

TEST(RadientTextureSourceTest, BorrowsMemoryWhenCopyIsDisabled)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    EXPECT_TRUE(Source.IsMemory());
    EXPECT_FALSE(Source.OwnsMemory());
    EXPECT_EQ(Source.GetData(), Data.data());
    EXPECT_EQ(Source.GetDataSize(), Data.size());
}

TEST(RadientTextureSourceTest, CopiesMemoryWhenRequested)
{
    std::array<Uint8, 4>     Data{1, 2, 3, 4};
    const std::vector<Uint8> Expected{Data.begin(), Data.end()};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    ASSERT_TRUE(Source.IsMemory());
    EXPECT_TRUE(Source.OwnsMemory());
    EXPECT_NE(Source.GetData(), Data.data());

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Source), Expected);
}

TEST(RadientTextureSourceTest, CopiesTextureDataMemoryWhenRequested)
{
    // clang-format off
    std::array<Uint8, 8> Data{
        // Row 0 pixels, followed by 2 bytes of padding.
        1, 2, 3, 4,
        // Row 1 pixels, followed by unused final-row padding.
        5, 6, 7, 8};
    const std::vector<Uint8> Expected{
        1, 2,
        5, 6};
    // clang-format on

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pData  = Data.data();
    TextureData.Stride = 4;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    ASSERT_TRUE(Source.IsMemory());
    EXPECT_TRUE(Source.IsTextureData());
    EXPECT_TRUE(Source.OwnsMemory());
    EXPECT_NE(Source.GetData(), Data.data());
    EXPECT_EQ(Source.GetDataSize(), Expected.size());

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Source), Expected);
}

TEST(RadientTextureSourceTest, PacksTextureDataRowsWhenCopying)
{
    constexpr Uint32 Width         = 3;
    constexpr Uint32 Height        = 2;
    constexpr Uint32 ActiveRowSize = Width * 4;
    constexpr Uint32 Stride        = 16;
    constexpr Uint32 DataSize      = (Height - 1) * Stride + ActiveRowSize;

    // clang-format off
    std::array<Uint8, DataSize> Data{
        // Row 0 pixels, followed by 4 bytes of padding.
        1, 2, 3, 4,     5, 6, 7, 8,     9, 10, 11, 12,
        90, 91, 92, 93,
        // Row 1 pixels. Final-row padding is not part of the valid source span.
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    const std::vector<Uint8> Expected{
        1, 2, 3, 4,     5, 6, 7, 8,     9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    // clang-format on

    RadientTextureData TextureData{};
    TextureData.Width  = Width;
    TextureData.Height = Height;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.Stride = Stride;
    TextureData.pData  = Data.data();

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    ASSERT_TRUE(Source.IsMemory());
    EXPECT_TRUE(Source.IsTextureData());
    EXPECT_TRUE(Source.OwnsMemory());
    EXPECT_NE(Source.GetData(), Data.data());
    EXPECT_EQ(Source.GetDataSize(), Expected.size());

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Source), Expected);
}

TEST(RadientTextureSourceTest, MovePreservesCopiedMemory)
{
    std::array<Uint8, 4>     Data{1, 2, 3, 4};
    const std::vector<Uint8> Expected{Data.begin(), Data.end()};

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());

    RadientTextureSource Source{LoadInfo};
    Source.MakeMemoryCopy();
    RadientTextureSource Moved{std::move(Source)};

    Data.fill(0);
    EXPECT_EQ(ReadSourceBytes(Moved), Expected);
    EXPECT_NE(Moved.GetData(), Data.data());
}

TEST(RadientTextureSourceTest, ReleasesCallbackOwnedMemory)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReleaseState         State;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData                = Data.data();
    LoadInfo.DataSize             = static_cast<Uint64>(Data.size());
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_TRUE(Source.OwnsMemory());
        EXPECT_EQ(Source.GetData(), Data.data());
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, Data.size());
}

TEST(RadientTextureSourceTest, ReleasesCallbackOwnedTextureDataMemory)
{
    std::array<Uint8, 8> Data{1, 2, 3, 4, 5, 6, 7, 8};
    ReleaseState         State;

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pData  = Data.data();
    TextureData.Stride = 4;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData         = &TextureData;
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource Source{LoadInfo};
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_TRUE(Source.IsTextureData());
        EXPECT_TRUE(Source.OwnsMemory());
        EXPECT_EQ(Source.GetData(), Data.data());
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, 6u);
}

TEST(RadientTextureSourceTest, InvalidTextureDataDoesNotFallbackToOtherSourceTypes)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReleaseState         State;

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_UNKNOWN;
    TextureData.pData  = Data.data();

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData         = &TextureData;
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource                    Source{LoadInfo};
        RefCntAutoPtr<TestRadientAssetLocation> pLocation{
            MakeNewRCObj<TestRadientAssetLocation>()("file:///invalid-texture-data")};

        Source.MakeMemoryCopy();
        EXPECT_TRUE(Source.IsMemory());
        EXPECT_FALSE(Source.IsTextureData());
        EXPECT_TRUE(Source.OwnsMemory());
        EXPECT_TRUE(Source.MakeCacheKey(pLocation).empty());

        RefCntAutoPtr<ITextureLoader> pLoader;
        EXPECT_EQ(Source.CreateLoader(nullptr, nullptr, pLoader.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pLoader, nullptr);
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, 0u);
}

TEST(RadientTextureSourceTest, MoveTransfersReleaseCallbackOwnership)
{
    std::array<Uint8, 4> Data{1, 2, 3, 4};
    ReleaseState         State;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData                = Data.data();
    LoadInfo.DataSize             = static_cast<Uint64>(Data.size());
    LoadInfo.ReleaseData          = ReleaseTextureData;
    LoadInfo.pReleaseDataUserData = &State;

    {
        RadientTextureSource Source{LoadInfo};
        RadientTextureSource Moved{std::move(Source)};
        EXPECT_TRUE(Moved.OwnsMemory());
    }

    EXPECT_EQ(State.Count, 1u);
    EXPECT_EQ(State.pData, Data.data());
    EXPECT_EQ(State.DataSize, Data.size());
}

TEST(RadientTextureSourceTest, CreatesLoaderFromTextureData)
{
    const std::array<Uint8, 16> Data{
        1, 3, 20, 24,
        5, 7, 28, 32,
        40, 44, 80, 88,
        52, 56, 94, 98};

    RadientTextureData TextureData{};
    TextureData.Width  = 4;
    TextureData.Height = 4;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    TextureData.pData  = Data.data();
    TextureData.Stride = 4;

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
