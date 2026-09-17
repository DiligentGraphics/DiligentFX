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
                               RadientTextureDataSpan&   Span)
{
    Span = {};

    const TEXTURE_FORMAT TextureFormat = RadientToTextureFormat(TextureData.Format);
    if (TextureFormat == TEX_FORMAT_UNKNOWN || TextureData.Width == 0 || TextureData.Height == 0)
        return false;

    TextureDesc Desc;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = TextureData.Width;
    Desc.Height    = TextureData.Height;
    Desc.MipLevels = 1;
    Desc.Format    = TextureFormat;

    const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(TextureFormat);
    const MipLevelProperties    MipProps   = GetMipLevelProperties(Desc, 0);
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

    const Uint32 Stride = TextureData.Stride != 0 ? TextureData.Stride : static_cast<Uint32>(MipProps.RowSize);
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

constexpr Uint32 TextureSourceCacheKeyVersion = 1;

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
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    RadientTextureDataSpan Span;
    IRadientDataBlob*      pBlob = LoadInfo.pDataBlob;
    if (LoadInfo.pTextureData != nullptr)
    {
        m_TextureData = *LoadInfo.pTextureData;
        pBlob         = m_TextureData.pDataBlob;
        if (pBlob == nullptr || !GetRadientTextureDataSpan(m_TextureData, Span) ||
            !RadientValidation::IsAddressableSize(Span.DataSize))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
    }

    if (pBlob != nullptr)
    {
        RadientDataBlobReadAccess ReadAccess{pBlob};
        if (!ReadAccess)
        {
            m_Status = RADIENT_STATUS_INVALID_OPERATION;
            return;
        }

        // Both the pointer and size stay stable throughout this shared read scope.
        const Uint64 Size = ReadAccess.GetSize();
        if (Size == 0 || !RadientValidation::IsAddressableSize(Size) || Size < Span.DataSize)
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
        if (ReadAccess.GetData() == nullptr)
        {
            m_Status = RADIENT_STATUS_INVALID_DATA;
            return;
        }

        const bool IsDecoded = LoadInfo.pTextureData != nullptr;
        if (IsDecoded)
        {
            // Mip generation reads typed components directly from each source row.
            const auto& FmtAttribs = GetTextureFormatAttribs(RadientToTextureFormat(m_TextureData.Format));
            if (FmtAttribs.ComponentType != COMPONENT_TYPE_COMPRESSED &&
                (reinterpret_cast<std::uintptr_t>(ReadAccess.GetData()) % FmtAttribs.ComponentSize) != 0)
            {
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return;
            }
        }
        m_DataSize   = static_cast<size_t>(IsDecoded ? Span.DataSize : Size);
        m_ReadAccess = std::move(ReadAccess);
        m_pData      = m_ReadAccess.GetData();
        m_SourceType = IsDecoded ? SourceType::TextureData : SourceType::EncodedMemory;
        if (IsDecoded)
        {
            if (m_TextureData.Stride == 0)
                m_TextureData.Stride = static_cast<Uint32>(Span.ActiveRowSize);
            m_TextureDataActiveRowSize = Span.ActiveRowSize;
            m_TextureDataRowCount      = Span.RowCount;
        }
    }
    else if (!m_URI.empty())
    {
        m_SourceType = SourceType::URI;
    }
    else
    {
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
        return RADIENT_STATUS_INVALID_ARGUMENT;
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
        Desc.MipLevels = 1;
        Desc.Format    = RadientToTextureFormat(m_TextureData.Format);
        Desc.Usage     = USAGE_DEFAULT;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        TextureSubResData Subres;
        Subres.pData  = GetData();
        Subres.Stride = m_TextureData.Stride;

        TextureData TexData{&Subres, 1};

        LoadInfo.GenerateMips = True;
        LoadInfo.MipLevels    = 0;
        LoadInfo.Format       = Desc.Format;

        // Compressed blocks are consumed as supplied. The image-processing path
        // generates mips for uncompressed formats and does not accept compressed data.
        const bool     IsCompressed = GetTextureFormatAttribs(Desc.Format).ComponentType == COMPONENT_TYPE_COMPRESSED;
        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromTextureData(Desc, TexData, MakeDataCopy, IsCompressed ? nullptr : &LoadInfo, &pLoader);
    }
    else if (m_SourceType == SourceType::EncodedMemory)
    {
        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromMemory(GetData(), GetDataSize(), MakeDataCopy, LoadInfo, &pLoader);
    }
    else if (m_SourceType == SourceType::URI)
    {
        if (pAssetResolver == nullptr || pAssetLocation == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        RefCntAutoPtr<IRadientAssetData> pAssetData;
        const RADIENT_STATUS             Status =
            pAssetResolver->OpenAsset(pAssetLocation, pAssetData.GetAddressOfEmpty());
        if (Status != RADIENT_STATUS_OK)
            return Status;

        if (pAssetData == nullptr ||
            pAssetData->GetData() == nullptr ||
            pAssetData->GetSize() == 0)
        {
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
        const XXH128Hash Hash = ComputeTextureDataHash(m_pData,
                                                       m_TextureDataActiveRowSize,
                                                       m_TextureDataRowCount,
                                                       m_TextureData.Stride);
        Builder.AddString("type", "data")
            .AddInteger("width", m_TextureData.Width)
            .AddInteger("height", m_TextureData.Height)
            .AddInteger("format", m_TextureData.Format)
            .AddInteger("row", m_TextureDataActiveRowSize)
            .AddInteger("rows", m_TextureDataRowCount)
            .AddString("hash", Hash.ToString());
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
    m_pData                    = nullptr;
    m_DataSize                 = 0;
    m_SourceType               = SourceType::Invalid;
    m_TextureData              = {};
    m_TextureDataActiveRowSize = 0;
    m_TextureDataRowCount      = 0;

    // Clear observable state before EndRead can invoke a reentrant callback.
    m_ReadAccess.Reset();
}

void RadientTextureSource::MoveFrom(RadientTextureSource&& Rhs) noexcept
{
    m_Status                   = Rhs.m_Status;
    m_ReadAccess               = std::move(Rhs.m_ReadAccess);
    m_SourceType               = Rhs.m_SourceType;
    m_URI                      = std::move(Rhs.m_URI);
    m_BaseURI                  = std::move(Rhs.m_BaseURI);
    m_IsSRGB                   = Rhs.m_IsSRGB;
    m_DataSize                 = Rhs.m_DataSize;
    m_TextureData              = Rhs.m_TextureData;
    m_TextureDataActiveRowSize = Rhs.m_TextureDataActiveRowSize;
    m_TextureDataRowCount      = Rhs.m_TextureDataRowCount;
    m_pData                    = Rhs.m_pData;

    Rhs.m_Status     = RADIENT_STATUS_INVALID_ARGUMENT;
    Rhs.m_SourceType = SourceType::Invalid;
    Rhs.m_URI.clear();
    Rhs.m_BaseURI.clear();
    Rhs.m_pData                    = nullptr;
    Rhs.m_DataSize                 = 0;
    Rhs.m_TextureData              = {};
    Rhs.m_TextureDataActiveRowSize = 0;
    Rhs.m_TextureDataRowCount      = 0;
}

} // namespace Diligent
