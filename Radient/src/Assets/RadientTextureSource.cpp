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

#include "HashUtils.hpp"
#include "TextureLoader.h"

#include <utility>

namespace Diligent
{

RadientTextureSource::RadientTextureSource(const RadientTextureLoadInfo& LoadInfo) :
    m_URI{GetURI(LoadInfo)},
    m_IsSRGB{LoadInfo.IsSRGB}
{
    if (LoadInfo.pData != nullptr)
    {
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
}

RefCntAutoPtr<ITextureLoader> RadientTextureSource::CreateLoader() const
{
    TextureLoadInfo LoadInfo{m_URI.empty() ? nullptr : m_URI.c_str()};
    LoadInfo.Usage     = USAGE_DEFAULT;
    LoadInfo.BindFlags = BIND_SHADER_RESOURCE;
    LoadInfo.IsSRGB    = m_IsSRGB;

    RefCntAutoPtr<ITextureLoader> pLoader;
    if (IsMemory())
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

std::string RadientTextureSource::MakeCacheKey(const RadientTextureLoadInfo& LoadInfo)
{
    std::string Key{"texture:"};
    if (LoadInfo.pData != nullptr)
    {
        Key += "memory:";
        Key += std::to_string(LoadInfo.DataSize);
        Key += ':';
        Key += std::to_string(ComputeHashRaw(LoadInfo.pData, static_cast<size_t>(LoadInfo.DataSize)));
    }
    else if (LoadInfo.URI != nullptr && *LoadInfo.URI != 0)
    {
        Key += LoadInfo.URI;
    }
    Key += LoadInfo.IsSRGB ? ":srgb=1" : ":srgb=0";
    return Key;
}

void RadientTextureSource::ReleaseMemory()
{
    if (m_pData != nullptr && m_ReleaseData != nullptr)
        m_ReleaseData(m_pData, static_cast<Uint64>(m_DataSize), m_pReleaseDataUserData);

    m_pData                = nullptr;
    m_DataSize             = 0;
    m_ReleaseData          = nullptr;
    m_pReleaseDataUserData = nullptr;
}

void RadientTextureSource::MoveFrom(RadientTextureSource&& Rhs) noexcept
{
    m_URI                  = std::move(Rhs.m_URI);
    m_IsSRGB               = Rhs.m_IsSRGB;
    m_Data                 = std::move(Rhs.m_Data);
    m_DataSize             = Rhs.m_DataSize;
    m_ReleaseData          = Rhs.m_ReleaseData;
    m_pReleaseDataUserData = Rhs.m_pReleaseDataUserData;

    if (!m_Data.empty())
        m_pData = m_Data.data();
    else
        m_pData = Rhs.m_pData;

    Rhs.m_pData                = nullptr;
    Rhs.m_DataSize             = 0;
    Rhs.m_ReleaseData          = nullptr;
    Rhs.m_pReleaseDataUserData = nullptr;
}

} // namespace Diligent
