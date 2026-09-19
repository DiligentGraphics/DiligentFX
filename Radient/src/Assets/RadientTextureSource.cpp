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

#include "Assets/RadientTextureSource.hpp"

#include "Assets/RadientAssetResolver.hpp"
#include "Assets/RadientCacheKeyBuilder.hpp"
#include "Assets/RadientTextureFormat.hpp"
#include "Core/RadientValidation.hpp"
#include "DebugUtilities.hpp"
#include "GraphicsAccessories.hpp"
#include "ProxyDataBlob.hpp"
#include "RefCntAutoPtr.hpp"
#include "TextureLoader.h"
#include "XXH128Hasher.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace Diligent
{

bool GetRadientTextureDataSpan(const RadientTextureData& TextureData,
                               Uint32                    MipLevel,
                               RadientTextureDataSpan&   Span)
{
    Span = {};

    const TEXTURE_FORMAT TextureFormat = RadientToTextureFormat(TextureData.Format);
    if (TextureFormat == TEX_FORMAT_UNKNOWN || TextureData.Width == 0 || TextureData.Height == 0 ||
        TextureData.pMipLevels == nullptr || MipLevel >= TextureData.MipLevelCount ||
        MipLevel >= ComputeMipLevelsCount(TextureData.Width, TextureData.Height))
        return false;

    TextureDesc Desc;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = TextureData.Width;
    Desc.Height    = TextureData.Height;
    Desc.MipLevels = 1;
    Desc.Format    = TextureFormat;

    const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(TextureFormat);
    const MipLevelProperties    MipProps   = GetMipLevelProperties(Desc, MipLevel);
    if (MipProps.RowSize == 0 || MipProps.StorageHeight == 0)
        return false;

    if (MipProps.RowSize > static_cast<Uint64>((std::numeric_limits<Uint32>::max)()))
        return false;

    Uint32 RowCount = MipProps.StorageHeight;
    if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED)
    {
        if (FmtAttribs.BlockHeight == 0 || (MipProps.StorageHeight % FmtAttribs.BlockHeight) != 0)
            return false;

        RowCount = MipProps.StorageHeight / FmtAttribs.BlockHeight;
    }

    if (RowCount == 0)
        return false;

    const Uint32 MipStride = TextureData.pMipLevels[MipLevel].Stride;
    const Uint32 Stride    = MipStride != 0 ? MipStride : static_cast<Uint32>(MipProps.RowSize);
    if (Stride < MipProps.RowSize)
        return false;
    if (FmtAttribs.ComponentType != COMPONENT_TYPE_COMPRESSED &&
        RowCount > 1 && (Stride % FmtAttribs.ComponentSize) != 0)
        return false;

    Uint64 DataSize = MipProps.RowSize;
    if (RowCount > 1)
    {
        const Uint64 RowStrideCount = Uint64{RowCount - 1};
        if (!RadientValidation::IsProductRepresentable<Uint64>(Stride, RowStrideCount))
            return false;

        const Uint64 PrefixSize = Uint64{Stride} * RowStrideCount;
        if (!RadientValidation::IsSumRepresentable<Uint64>(PrefixSize, MipProps.RowSize))
            return false;

        DataSize = PrefixSize + MipProps.RowSize;
    }

    Span.ActiveRowSize = MipProps.RowSize;
    Span.RowCount      = RowCount;
    Span.DataSize      = DataSize;
    return true;
}

namespace
{

constexpr Uint32 TextureSourceCacheKeyVersion = 2;

XXH128Hash ComputeDataHash(const void* pData, Uint64 DataSize)
{
    if (pData == nullptr || DataSize == 0)
        return {};

    XXH128State Hasher;
    Hasher.UpdateRaw(pData, DataSize);
    return Hasher.Digest();
}

XXH128Hash ComputeTextureDataHash(const void* pData,
                                  Uint64      ActiveRowSize,
                                  Uint32      RowCount,
                                  Uint32      Stride)
{
    if (pData == nullptr || ActiveRowSize == 0 || RowCount == 0 ||
        !RadientValidation::IsAddressableSize(ActiveRowSize))
    {
        return {};
    }

    XXH128State  Hasher;
    const Uint8* pRow0       = static_cast<const Uint8*>(pData);
    const size_t RowDataSize = static_cast<size_t>(ActiveRowSize);
    for (Uint32 Row = 0; Row < RowCount; ++Row)
    {
        const size_t RowOffset = static_cast<size_t>(Uint64{Row} * Stride);
        Hasher.UpdateRaw(pRow0 + RowOffset, RowDataSize);
    }

    return Hasher.Digest();
}

} // namespace

RadientTextureSource::RadientTextureSource(const RadientTextureLoadInfo& LoadInfo) :
    m_URI{GetURI(LoadInfo)},
    m_BaseURI{LoadInfo.BaseURI != nullptr ? LoadInfo.BaseURI : ""},
    m_IsSRGB{LoadInfo.IsSRGB}
{
    if (LoadInfo.pDataBlob != nullptr && LoadInfo.pTextureData != nullptr)
    {
        LOG_ERROR_MESSAGE("Invalid texture source: pDataBlob and pTextureData must not both be specified.");
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    if (LoadInfo.pTextureData != nullptr)
    {
        const RadientTextureData& Data   = *LoadInfo.pTextureData;
        const TEXTURE_FORMAT      Format = RadientToTextureFormat(Data.Format);
        if (Format == TEX_FORMAT_UNKNOWN)
        {
            LOG_ERROR_MESSAGE("Invalid texture source: unsupported texture data format (", static_cast<Uint32>(Data.Format), ").");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        if (Data.Width == 0 || Data.Height == 0)
        {
            LOG_ERROR_MESSAGE("Invalid texture source: width and height must be nonzero; got ", Data.Width, " x ", Data.Height, ".");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        if (Data.pMipLevels == nullptr)
        {
            LOG_ERROR_MESSAGE("Invalid texture source: pMipLevels must not be null.");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        const Uint32 FullMipLevelCount = ComputeMipLevelsCount(Data.Width, Data.Height);
        if (Data.MipLevelCount == 0 || Data.MipLevelCount > FullMipLevelCount)
        {
            LOG_ERROR_MESSAGE("Invalid texture source: MipLevelCount (", Data.MipLevelCount,
                              ") must be between 1 and ", FullMipLevelCount, " for a ", Data.Width, " x ", Data.Height, " texture.");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        const auto& FmtAttribs = GetTextureFormatAttribs(Format);
        if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED &&
            (Data.Width % FmtAttribs.BlockWidth != 0 || Data.Height % FmtAttribs.BlockHeight != 0))
        {
            LOG_ERROR_MESSAGE("Invalid texture source: compressed texture dimensions (", Data.Width, " x ", Data.Height,
                              ") must be multiples of the ", Uint32{FmtAttribs.BlockWidth}, " x ", Uint32{FmtAttribs.BlockHeight},
                              " block dimensions of ", FmtAttribs.Name, ".");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }

        // Build locally so a failure in a later mip releases every earlier read scope.
        std::vector<MipData> Mips;
        Mips.reserve(Data.MipLevelCount);
        for (Uint32 Level = 0; Level < Data.MipLevelCount; ++Level)
        {
            const RadientTextureMipData& Src = Data.pMipLevels[Level];
            MipData                      Mip;
            if (Src.pDataBlob == nullptr)
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": pDataBlob must not be null.");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            if (!GetRadientTextureDataSpan(Data, Level, Mip.Span))
            {
                const MipLevelProperties MipProps = GetMipLevelProperties(Data.Width, Data.Height, 1, Format, Level);
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": cannot compute a valid data span for ",
                                  MipProps.LogicalWidth, " x ", MipProps.LogicalHeight, " ", FmtAttribs.Name,
                                  " with Stride (", Src.Stride, ") and active row size (", MipProps.RowSize,
                                  "). The active row size must fit in Uint32, Stride must be zero or at least the active row size, ",
                                  "uncompressed rows must be component-aligned, and the data span must not overflow.");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            if (!RadientValidation::IsAddressableSize(Src.ByteOffset) ||
                !RadientValidation::IsAddressableSize(Mip.Span.DataSize))
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": ByteOffset (", Src.ByteOffset,
                                  ") or required data size (", Mip.Span.DataSize,
                                  ") exceeds the maximum supported size_t value (", (std::numeric_limits<size_t>::max)(), ").");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            Mip.ReadAccess = RadientDataBlobReadAccess{Src.pDataBlob};
            if (!Mip.ReadAccess)
            {
                LOG_ERROR_MESSAGE("Unable to acquire read access to texture mip ", Level,
                                  " data blob. The blob must allow read access while the texture source uses it.");
                m_Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }
            const Uint64 Size = Mip.ReadAccess.GetSize();
            if (!RadientValidation::IsAddressableSize(Size))
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": data blob size (", Size,
                                  ") exceeds the maximum supported size_t value (", (std::numeric_limits<size_t>::max)(), ").");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            if (Src.ByteOffset > Size || Mip.Span.DataSize > Size - Src.ByteOffset)
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": ByteOffset (", Src.ByteOffset,
                                  ") and required data size (", Mip.Span.DataSize, ") exceed data blob size (", Size, ").");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            if (Mip.ReadAccess.GetData() == nullptr)
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": data blob returned a null data pointer for ", Size, " bytes.");
                m_Status = RADIENT_STATUS_INVALID_DATA;
                return;
            }
            Mip.pData = static_cast<const Uint8*>(Mip.ReadAccess.GetData()) + static_cast<size_t>(Src.ByteOffset);
            if (FmtAttribs.ComponentType != COMPONENT_TYPE_COMPRESSED &&
                reinterpret_cast<std::uintptr_t>(Mip.pData) % FmtAttribs.ComponentSize != 0)
            {
                LOG_ERROR_MESSAGE("Invalid texture mip ", Level, ": data pointer at ByteOffset (", Src.ByteOffset,
                                  ") is not aligned to the ", Uint32{FmtAttribs.ComponentSize}, "-byte components of ", FmtAttribs.Name, ".");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
            Mip.Stride = Src.Stride != 0 ? Src.Stride : static_cast<Uint32>(Mip.Span.ActiveRowSize);
            Mips.emplace_back(std::move(Mip));
        }
        m_TextureData = Data;
        // A complete supplied chain needs no generation and shares the same cache entry.
        m_TextureData.GenerateMips = Data.GenerateMips &&
            Data.MipLevelCount < ComputeMipLevelsCount(Data.Width, Data.Height);
        if (m_TextureData.GenerateMips && FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED)
        {
            LOG_WARNING_MESSAGE("Mip generation is unsupported for compressed textures; only the supplied mip levels will be loaded.");
            m_TextureData.GenerateMips = False;
        }
        // Mips retains the resolved descriptors; never keep the caller's array pointer.
        m_TextureData.pMipLevels = nullptr;
        m_Mips                   = std::move(Mips);
        m_pData                  = m_Mips.front().pData;
        m_DataSize               = static_cast<size_t>(m_Mips.front().Span.DataSize);
        m_SourceType             = SourceType::TextureData;
    }
    else if (LoadInfo.pDataBlob != nullptr)
    {
        RadientDataBlobReadAccess ReadAccess{LoadInfo.pDataBlob};
        if (!ReadAccess)
        {
            LOG_ERROR_MESSAGE("Unable to acquire read access to encoded texture data blob. The blob must allow read access while the texture source uses it.");
            m_Status = RADIENT_STATUS_INVALID_OPERATION;
            return;
        }
        const Uint64 Size = ReadAccess.GetSize();
        if (Size == 0)
        {
            LOG_ERROR_MESSAGE("Encoded texture data blob must not be empty.");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        if (!RadientValidation::IsAddressableSize(Size))
        {
            LOG_ERROR_MESSAGE("Encoded texture data blob size (", Size,
                              ") exceeds the maximum supported size_t value (", (std::numeric_limits<size_t>::max)(), ").");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        if (ReadAccess.GetData() == nullptr)
        {
            LOG_ERROR_MESSAGE("Encoded texture data blob returned a null data pointer for ", Size, " bytes.");
            m_Status = RADIENT_STATUS_INVALID_DATA;
            return;
        }
        m_DataSize   = static_cast<size_t>(Size);
        m_ReadAccess = std::move(ReadAccess);
        m_pData      = m_ReadAccess.GetData();
        m_SourceType = SourceType::EncodedMemory;
    }
    else if (!m_URI.empty())
    {
        m_SourceType = SourceType::URI;
    }
    else
    {
        LOG_ERROR_MESSAGE("Invalid texture source: a nonempty URI, pDataBlob, or pTextureData must be specified.");
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
    }
}

RadientTextureSource::~RadientTextureSource()
{
    ReleaseMemory();
}

RadientTextureSource::RadientTextureSource(RadientTextureSource&& Rhs) noexcept
{
    MoveFrom(std::move(Rhs));
}

RadientTextureSource& RadientTextureSource::operator=(RadientTextureSource&& Rhs) noexcept
{
    if (this != &Rhs)
    {
        ReleaseMemory();
        MoveFrom(std::move(Rhs));
    }
    return *this;
}

RADIENT_STATUS RadientTextureSource::CreateLoader(IRadientAssetResolver* pAssetResolver,
                                                  IRadientAssetLocation* pAssetLocation,
                                                  ITextureLoader**       ppLoader) const
{
    if (ppLoader == nullptr)
    {
        LOG_ERROR_MESSAGE("Cannot create texture loader: ppLoader must not be null.");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    *ppLoader = nullptr;

    if (m_Status != RADIENT_STATUS_OK)
        return m_Status;

    TextureLoadInfo LoadInfo{m_URI.empty() ? nullptr : m_URI.c_str()};
    LoadInfo.Usage     = USAGE_DEFAULT;
    LoadInfo.BindFlags = BIND_SHADER_RESOURCE;
    LoadInfo.IsSRGB    = m_IsSRGB;

    RefCntAutoPtr<ITextureLoader> pLoader;
    if (m_SourceType == SourceType::TextureData)
    {
        TextureDesc Desc;
        Desc.Name      = m_URI.empty() ? nullptr : m_URI.c_str();
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_TextureData.Width;
        Desc.Height    = m_TextureData.Height;
        Desc.MipLevels = m_TextureData.MipLevelCount;
        Desc.Format    = RadientToTextureFormat(m_TextureData.Format);
        Desc.Usage     = USAGE_DEFAULT;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        std::vector<TextureSubResData> Subresources(m_Mips.size());
        for (size_t Level = 0; Level < m_Mips.size(); ++Level)
        {
            Subresources[Level].pData  = m_Mips[Level].pData;
            Subresources[Level].Stride = m_Mips[Level].Stride;
        }
        TextureData TexData{Subresources.data(), static_cast<Uint32>(Subresources.size())};

        LoadInfo.GenerateMips = True;
        LoadInfo.MipLevels    = 0;
        LoadInfo.Format       = Desc.Format;

        // Prepared levels are consumed as supplied. Only uncompressed input can
        // request generation of the missing tail from the last provided level.
        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromTextureData(Desc, TexData, MakeDataCopy,
                                           m_TextureData.GenerateMips ? &LoadInfo : nullptr, &pLoader);
    }
    else if (m_SourceType == SourceType::EncodedMemory)
    {
        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromMemory(GetData(), GetDataSize(), MakeDataCopy, LoadInfo, &pLoader);
    }
    else if (m_SourceType == SourceType::URI)
    {
        if (pAssetResolver == nullptr)
        {
            LOG_ERROR_MESSAGE("Cannot load texture '", m_URI, "': pAssetResolver must not be null for URI input.");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (pAssetLocation == nullptr)
        {
            LOG_ERROR_MESSAGE("Cannot load texture '", m_URI, "': pAssetLocation must not be null for URI input.");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        RefCntAutoPtr<IRadientAssetData> pAssetData;
        const RADIENT_STATUS             Status =
            pAssetResolver->OpenAsset(pAssetLocation, pAssetData.GetAddressOfEmpty());
        if (Status != RADIENT_STATUS_OK)
            return Status;

        if (pAssetData == nullptr)
        {
            LOG_ERROR_MESSAGE("Cannot load texture '", m_URI, "': asset resolver returned no asset data.");
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (pAssetData->GetData() == nullptr)
        {
            LOG_ERROR_MESSAGE("Cannot load texture '", m_URI, "': asset resolver returned a null data pointer.");
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (pAssetData->GetSize() == 0)
        {
            LOG_ERROR_MESSAGE("Cannot load texture '", m_URI, "': asset resolver returned empty asset data.");
            return RADIENT_STATUS_INVALID_DATA;
        }

        LoadInfo.Name = pAssetData->GetResolvedURI();

        RefCntAutoPtr<IDataBlob> pDataBlob =
            ProxyDataBlob::Create(pAssetData->GetData(),
                                  pAssetData->GetSize(),
                                  pAssetData);
        CreateTextureLoaderFromDataBlob(std::move(pDataBlob), LoadInfo, &pLoader);
    }
    else
    {
        LOG_ERROR_MESSAGE("Cannot create texture loader: texture source has no valid input.");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (pLoader == nullptr)
        return RADIENT_STATUS_FAILED;

    // Require whole blocks at mip 0 for every source, including encoded images.
    // Smaller dimensions at the end of a compressed mip chain remain valid.
    const TextureDesc&          Desc       = pLoader->GetTextureDesc();
    const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(Desc.Format);
    if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED &&
        (Desc.Width % FmtAttribs.BlockWidth != 0 || Desc.Height % FmtAttribs.BlockHeight != 0))
    {
        LOG_ERROR_MESSAGE("Cannot load compressed texture '", m_URI, "': mip 0 dimensions (", Desc.Width, " x ", Desc.Height,
                          ") must be multiples of the ", Uint32{FmtAttribs.BlockWidth}, " x ", Uint32{FmtAttribs.BlockHeight},
                          " block dimensions of ", FmtAttribs.Name, ".");
        return RADIENT_STATUS_UNSUPPORTED;
    }

    *ppLoader = pLoader.Detach();
    return RADIENT_STATUS_OK;
}

std::string RadientTextureSource::GetURI(const RadientTextureLoadInfo& LoadInfo)
{
    return LoadInfo.URI != nullptr ? LoadInfo.URI : "";
}

std::string RadientTextureSource::MakeCacheKey(IRadientAssetLocation* pAssetLocation) const
{
    RadientCacheKeyBuilder Builder{"texture", TextureSourceCacheKeyVersion};
    if (m_SourceType == SourceType::TextureData)
    {
        Builder.AddString("type", "data")
            .AddInteger("width", m_TextureData.Width)
            .AddInteger("height", m_TextureData.Height)
            .AddInteger("format", m_TextureData.Format)
            .AddInteger("mips", m_TextureData.MipLevelCount)
            .AddBool("generate", m_TextureData.GenerateMips);
        for (const MipData& Mip : m_Mips)
        {
            const XXH128Hash Hash = ComputeTextureDataHash(Mip.pData, Mip.Span.ActiveRowSize, Mip.Span.RowCount, Mip.Stride);
            Builder.AddInteger("row", Mip.Span.ActiveRowSize)
                .AddInteger("rows", Mip.Span.RowCount)
                .AddString("hash", Hash.ToString());
        }
    }
    else if (m_SourceType == SourceType::EncodedMemory)
    {
        const XXH128Hash Hash = ComputeDataHash(m_pData, m_DataSize);
        Builder.AddString("type", "memory")
            .AddInteger("size", m_DataSize)
            .AddString("hash", Hash.ToString());
    }
    else if (m_SourceType == SourceType::URI)
    {
        if (pAssetLocation == nullptr ||
            pAssetLocation->GetLocation() == nullptr ||
            pAssetLocation->GetLocation()[0] == '\0')
        {
            return {};
        }

        Builder.AddString("type", "uri")
            .AddString("location", pAssetLocation->GetLocation());
    }
    else
    {
        return {};
    }
    Builder.AddBool("srgb", m_IsSRGB);
    return Builder.GetKey();
}

void RadientTextureSource::ReleaseMemory()
{
    m_pData       = nullptr;
    m_DataSize    = 0;
    m_SourceType  = SourceType::Invalid;
    m_TextureData = {};

    // Clear observable state before EndRead can invoke a reentrant callback.
    std::vector<MipData> Mips{std::move(m_Mips)};
    m_ReadAccess.Reset();
}

void RadientTextureSource::MoveFrom(RadientTextureSource&& Rhs) noexcept
{
    m_Status      = Rhs.m_Status;
    m_ReadAccess  = std::move(Rhs.m_ReadAccess);
    m_SourceType  = Rhs.m_SourceType;
    m_URI         = std::move(Rhs.m_URI);
    m_BaseURI     = std::move(Rhs.m_BaseURI);
    m_IsSRGB      = Rhs.m_IsSRGB;
    m_DataSize    = Rhs.m_DataSize;
    m_TextureData = Rhs.m_TextureData;
    m_Mips        = std::move(Rhs.m_Mips);
    m_pData       = Rhs.m_pData;

    Rhs.m_Status     = RADIENT_STATUS_INVALID_ARGUMENT;
    Rhs.m_SourceType = SourceType::Invalid;
    Rhs.m_URI.clear();
    Rhs.m_BaseURI.clear();
    Rhs.m_pData       = nullptr;
    Rhs.m_DataSize    = 0;
    Rhs.m_TextureData = {};
}

} // namespace Diligent
