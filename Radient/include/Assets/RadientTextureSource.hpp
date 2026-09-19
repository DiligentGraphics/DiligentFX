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

#pragma once

#include "RadientAssets.h"
#include "Core/RadientDataBlobReadAccess.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace Diligent
{

struct ITextureLoader;
struct IRadientAssetLocation;
struct IRadientAssetResolver;

/// Describes the valid readable span of one RadientTextureMipData.
struct RadientTextureDataSpan
{
    /// Number of bytes in each row that contain texture data.
    /// Row padding, if any, is not included.
    Uint64 ActiveRowSize = 0;

    /// Number of stored source rows. For block-compressed formats, this is the number of block rows.
    Uint32 RowCount = 0;

    /// Minimum number of bytes read starting at the mip's ByteOffset:
    /// (RowCount - 1) * Stride + ActiveRowSize.
    /// The final row does not need padding bytes beyond ActiveRowSize.
    Uint64 DataSize = 0;
};

/// Computes the format-aware source span for one supplied mip level, excluding ByteOffset.
///
/// \returns    true if the format, dimensions, stride, and computed span are valid; false otherwise.
///
/// \remarks    If RadientTextureMipData::Stride is zero, tightly packed rows are assumed.
///             Validates that non-zero stride is at least ActiveRowSize and that DataSize
///             does not overflow Uint64. Uncompressed rows must start at component-aligned
///             offsets; compressed block rows have no alignment requirement.
///             Does not acquire blob access or validate its data pointer or size.
bool GetRadientTextureDataSpan(const RadientTextureData& TextureData,
                               Uint32                    MipLevel,
                               RadientTextureDataSpan&   Span);

class RadientTextureSource final
{
public:
    enum class SourceType
    {
        Invalid,
        URI,
        EncodedMemory,
        TextureData
    };

    explicit RadientTextureSource(const RadientTextureLoadInfo& LoadInfo);
    ~RadientTextureSource();

    RadientTextureSource(RadientTextureSource&& Rhs) noexcept;
    RadientTextureSource& operator=(RadientTextureSource&& Rhs) noexcept;

    RadientTextureSource(const RadientTextureSource&)            = delete;
    RadientTextureSource& operator=(const RadientTextureSource&) = delete;

    bool IsMemory() const
    {
        return m_pData != nullptr;
    }

    bool IsTextureData() const
    {
        return m_SourceType == SourceType::TextureData;
    }

    Bool IsSRGB() const
    {
        return m_IsSRGB;
    }

    const std::string& GetURI() const
    {
        return m_URI;
    }

    const std::string& GetBaseURI() const
    {
        return m_BaseURI;
    }

    const void* GetData() const
    {
        return m_pData;
    }

    size_t GetDataSize() const
    {
        return m_DataSize;
    }

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    // For memory input, this source must outlive the returned loader.
    RADIENT_STATUS CreateLoader(IRadientAssetResolver* pAssetResolver,
                                IRadientAssetLocation* pAssetLocation,
                                ITextureLoader**       ppLoader) const;

    static std::string GetURI(const RadientTextureLoadInfo& LoadInfo);

    std::string MakeCacheKey(IRadientAssetLocation* pAssetLocation = nullptr) const;

private:
    void ReleaseMemory();
    void MoveFrom(RadientTextureSource&& Rhs) noexcept;

    SourceType  m_SourceType = SourceType::Invalid;
    std::string m_URI;
    std::string m_BaseURI;
    Bool        m_IsSRGB = False;

    RADIENT_STATUS m_Status = RADIENT_STATUS_OK;

    // Retains the blob and read access while loaders use the source bytes.
    RadientDataBlobReadAccess m_ReadAccess;
    const void*               m_pData    = nullptr;
    size_t                    m_DataSize = 0;

    struct MipData
    {
        RadientDataBlobReadAccess ReadAccess;
        const void*               pData  = nullptr;
        Uint32                    Stride = 0;
        RadientTextureDataSpan    Span;
    };
    RadientTextureData   m_TextureData;
    std::vector<MipData> m_Mips;
};

} // namespace Diligent
