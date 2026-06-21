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

#include "Assets/RadientTextureFormat.hpp"
#include "GraphicsAccessories.hpp"
#include "HashUtils.hpp"
#include "TextureLoader.h"

#include <utility>

namespace Diligent
{

RadientTextureSource::RadientTextureSource(const RadientTextureLoadInfo& LoadInfo) :
    m_URI{GetURI(LoadInfo)},
    m_IsSRGB{LoadInfo.IsSRGB}
{
    if (LoadInfo.pTextureData != nullptr)
    {
        m_SourceType  = SourceType::TextureData;
        m_TextureData = *LoadInfo.pTextureData;
        m_pData       = m_TextureData.pData;
        if (m_TextureData.Stride == 0)
        {
            const TEXTURE_FORMAT        TextureFormat = RadientToTextureFormat(m_TextureData.Format);
            const TextureFormatAttribs& FmtAttribs    = GetTextureFormatAttribs(TextureFormat);

            m_TextureData.Stride = Uint64{m_TextureData.Width} *
                Uint64{FmtAttribs.ComponentSize} *
                Uint64{FmtAttribs.NumComponents};
        }
        m_DataSize             = static_cast<size_t>(m_TextureData.Stride * Uint64{m_TextureData.Height});
        m_ReleaseData          = LoadInfo.ReleaseData;
        m_pReleaseDataUserData = LoadInfo.pReleaseDataUserData;
    }
    else if (LoadInfo.pData != nullptr)
    {
        m_SourceType           = SourceType::EncodedMemory;
        m_pData                = LoadInfo.pData;
        m_DataSize             = static_cast<size_t>(LoadInfo.DataSize);
        m_ReleaseData          = LoadInfo.ReleaseData;
        m_pReleaseDataUserData = LoadInfo.pReleaseDataUserData;
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

void RadientTextureSource::MakeMemoryCopy()
{
    if (!IsMemory() || OwnsMemory())
        return;

    const Uint8* pBytes = static_cast<const Uint8*>(m_pData);
    m_Data.assign(pBytes, pBytes + m_DataSize);
    m_pData = m_Data.data();
    if (m_SourceType == SourceType::TextureData)
        m_TextureData.pData = m_pData;
}

RefCntAutoPtr<ITextureLoader> RadientTextureSource::CreateLoader() const
{
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

        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromTextureData(Desc, TexData, MakeDataCopy, &LoadInfo, &pLoader);
    }
    else if (m_SourceType == SourceType::EncodedMemory)
    {
        constexpr bool MakeDataCopy = false;
        CreateTextureLoaderFromMemory(GetData(), GetDataSize(), MakeDataCopy, LoadInfo, &pLoader);
    }
    else
    {
        CreateTextureLoaderFromFile(m_URI.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, LoadInfo, &pLoader);
    }

    return pLoader;
}

std::string RadientTextureSource::GetURI(const RadientTextureLoadInfo& LoadInfo)
{
    return LoadInfo.URI != nullptr ? LoadInfo.URI : "";
}

std::string RadientTextureSource::MakeCacheKey() const
{
    std::string Key{"texture:"};
    if (m_SourceType == SourceType::TextureData)
    {
        Key += "data:";
        Key += std::to_string(m_TextureData.Width);
        Key += 'x';
        Key += std::to_string(m_TextureData.Height);
        Key += ":format=";
        Key += std::to_string(static_cast<Uint32>(m_TextureData.Format));
        Key += ":stride=";
        Key += std::to_string(m_TextureData.Stride);
        Key += ":size=";
        Key += std::to_string(m_DataSize);
        Key += ":hash=";
        Key += std::to_string(ComputeHashRaw(m_pData, m_DataSize));
    }
    else if (m_SourceType == SourceType::EncodedMemory)
    {
        Key += "memory:";
        Key += std::to_string(m_DataSize);
        Key += ':';
        Key += std::to_string(ComputeHashRaw(m_pData, m_DataSize));
    }
    else if (!m_URI.empty())
    {
        Key += m_URI;
    }
    Key += m_IsSRGB ? ":srgb=1" : ":srgb=0";
    return Key;
}

void RadientTextureSource::ReleaseMemory()
{
    if (m_pData != nullptr && m_ReleaseData != nullptr)
        m_ReleaseData(m_pData, static_cast<Uint64>(m_DataSize), m_pReleaseDataUserData);

    m_SourceType           = SourceType::URI;
    m_pData                = nullptr;
    m_DataSize             = 0;
    m_TextureData          = {};
    m_ReleaseData          = nullptr;
    m_pReleaseDataUserData = nullptr;
}

void RadientTextureSource::MoveFrom(RadientTextureSource&& Rhs) noexcept
{
    m_SourceType           = Rhs.m_SourceType;
    m_URI                  = std::move(Rhs.m_URI);
    m_IsSRGB               = Rhs.m_IsSRGB;
    m_Data                 = std::move(Rhs.m_Data);
    m_DataSize             = Rhs.m_DataSize;
    m_TextureData          = Rhs.m_TextureData;
    m_ReleaseData          = Rhs.m_ReleaseData;
    m_pReleaseDataUserData = Rhs.m_pReleaseDataUserData;

    if (!m_Data.empty())
        m_pData = m_Data.data();
    else
        m_pData = Rhs.m_pData;

    if (m_SourceType == SourceType::TextureData)
        m_TextureData.pData = m_pData;

    Rhs.m_SourceType           = SourceType::URI;
    Rhs.m_pData                = nullptr;
    Rhs.m_DataSize             = 0;
    Rhs.m_TextureData          = {};
    Rhs.m_ReleaseData          = nullptr;
    Rhs.m_pReleaseDataUserData = nullptr;
}

} // namespace Diligent
