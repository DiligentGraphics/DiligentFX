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
#include "RefCntAutoPtr.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace Diligent
{

struct ITextureLoader;

/// Describes the valid readable span of RadientTextureData::pData.
struct RadientTextureDataSpan
{
    /// Number of bytes in each row that contain texture data.
    /// Row padding, if any, is not included.
    Uint64 ActiveRowSize = 0;

    /// Number of stored source rows. For block-compressed formats, this is the number of block rows.
    Uint32 RowCount = 0;

    /// Minimum number of bytes that may be read from RadientTextureData::pData:
    /// (RowCount - 1) * Stride + ActiveRowSize.
    /// The final row does not need padding bytes beyond ActiveRowSize.
    Uint64 DataSize = 0;
};

/// Computes the format-aware source data span for RadientTextureData.
///
/// \returns    true if the format, dimensions, stride, and computed span are valid; false otherwise.
///
/// \remarks    If RadientTextureData::Stride is zero, tightly packed rows are assumed.
///             The function validates that non-zero stride is at least ActiveRowSize and that
///             the computed DataSize does not overflow Uint64.
bool GetRadientTextureDataSpan(const RadientTextureData& TextureData,
                               RadientTextureDataSpan&   Span);

class RadientTextureSource final
{
public:
    enum class SourceType
    {
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

    const void* GetData() const
    {
        return m_pData;
    }

    size_t GetDataSize() const
    {
        return m_DataSize;
    }

    bool OwnsMemory() const
    {
        return !m_Data.empty() || m_ReleaseData != nullptr;
    }

    void MakeMemoryCopy();

    RefCntAutoPtr<ITextureLoader> CreateLoader() const;

    static std::string GetURI(const RadientTextureLoadInfo& LoadInfo);

    std::string MakeCacheKey() const;

private:
    void ReleaseMemory();
    void MoveFrom(RadientTextureSource&& Rhs) noexcept;

    SourceType  m_SourceType = SourceType::URI;
    std::string m_URI;
    Bool        m_IsSRGB = False;

    std::vector<Uint8> m_Data;
    const void*        m_pData    = nullptr;
    size_t             m_DataSize = 0;

    RadientTextureData m_TextureData;
    Uint64             m_TextureDataActiveRowSize = 0;
    Uint32             m_TextureDataRowCount      = 0;

    RadientTextureReleaseDataCallbackType m_ReleaseData          = nullptr;
    void*                                 m_pReleaseDataUserData = nullptr;
};

} // namespace Diligent
