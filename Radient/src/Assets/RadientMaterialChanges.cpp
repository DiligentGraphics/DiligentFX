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

#include "RadientMaterialChanges.hpp"

#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "DebugUtilities.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>

namespace Diligent
{

namespace RadientMaterialDetail
{

RADIENT_STATUS MaterialParameterChanges::SetParameter(const MaterialStorage&         Layout,
                                                      RadientMaterialParameterHandle Handle,
                                                      const void*                    pData,
                                                      Uint32                         DataSize)
{
    if (!Layout.IsValidHandle(Handle) || IsTextureParameter(Layout.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const Uint32 ValueSize = Layout.m_Data.GetValueSize(Handle.Index);
    if (pData == nullptr || DataSize != ValueSize)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator ChangeIt = FindChange(Handle.Index, ValueArrayIndex);
    if (ChangeIt != m_Changes.end())
    {
        if (std::memcmp(m_ValueData.data() + ChangeIt->DataOffset, pData, ValueSize) == 0)
            return RADIENT_STATUS_NO_CHANGE;

        std::memcpy(m_ValueData.data() + ChangeIt->DataOffset, pData, ValueSize);
        return RADIENT_STATUS_OK;
    }

    if (m_ValueData.size() > std::numeric_limits<Uint32>::max() - ValueSize)
    {
        LOG_ERROR_MESSAGE("Material writer value storage exceeds the supported size");
        return RADIENT_STATUS_FAILED;
    }

    try
    {
        const Uint32 DataOffset = static_cast<Uint32>(m_ValueData.size());
        m_Changes.reserve(m_Changes.size() + 1);
        m_ValueData.resize(m_ValueData.size() + ValueSize);
        std::memcpy(m_ValueData.data() + DataOffset, pData, ValueSize);
        m_Changes.push_back(ParameterChange{Handle.Index, ValueArrayIndex, DataOffset});
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to store a material parameter change: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS MaterialParameterChanges::SetTexture(const MaterialStorage&         Layout,
                                                    RadientMaterialParameterHandle Handle,
                                                    Uint32                         ArrayIndex,
                                                    IRadientTextureAsset*          pTexture)
{
    if (!Layout.IsValidHandle(Handle) || !IsTextureParameter(Layout.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (ArrayIndex >= Layout.m_Data.GetValueSize(Handle.Index))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator ChangeIt = FindChange(Handle.Index, ArrayIndex);
    if (ChangeIt != m_Changes.end())
    {
        if (m_TextureData[ChangeIt->DataOffset].RawPtr() == pTexture)
            return RADIENT_STATUS_NO_CHANGE;

        m_TextureData[ChangeIt->DataOffset] = pTexture;
        return RADIENT_STATUS_OK;
    }

    if (m_TextureData.size() >= std::numeric_limits<Uint32>::max())
    {
        LOG_ERROR_MESSAGE("Material writer texture storage exceeds the supported size");
        return RADIENT_STATUS_FAILED;
    }

    try
    {
        const Uint32 DataOffset = static_cast<Uint32>(m_TextureData.size());
        m_Changes.reserve(m_Changes.size() + 1);
        m_TextureData.emplace_back(pTexture);
        m_Changes.push_back(ParameterChange{Handle.Index, ArrayIndex, DataOffset});
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to store a material texture change: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

bool MaterialParameterChanges::HasEffectiveTextureChanges(const PackedMaterialData& Target) const noexcept
{
    for (const ParameterChange& Change : m_Changes)
    {
        if (!IsTextureParameter(Target.GetValueType(Change.ParameterIndex)))
            continue;

        VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
        IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
        if (Target.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
        {
            return true;
        }
    }
    return false;
}

MATERIAL_CHANGE_FLAGS MaterialParameterChanges::ApplyTo(PackedMaterialData& Target) const noexcept
{
    MATERIAL_CHANGE_FLAGS Flags = MATERIAL_CHANGE_FLAG_NONE;
    for (const ParameterChange& Change : m_Changes)
    {
        if (IsTextureParameter(Target.GetValueType(Change.ParameterIndex)))
        {
            VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
            IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
            if (Target.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
            {
                Target.SetTexture(Change.ParameterIndex, Change.ArrayIndex, pTexture);
                Flags |= MATERIAL_CHANGE_FLAG_SHADER_DATA |
                    MATERIAL_CHANGE_FLAG_TEXTURE_BINDINGS;
            }
        }
        else
        {
            VERIFY_EXPR(Change.ArrayIndex == ValueArrayIndex);
            const void* const pChangedData = m_ValueData.data() + Change.DataOffset;
            if (!Target.HasSameValue(Change.ParameterIndex, pChangedData))
            {
                Target.CopyValue(Change.ParameterIndex, pChangedData);
                Flags |= MATERIAL_CHANGE_FLAG_SHADER_DATA;
            }
        }
    }
    return Flags;
}

MaterialParameterChanges::ChangeIterator MaterialParameterChanges::FindChange(Uint32 ParameterIndex,
                                                                              Uint32 ArrayIndex) noexcept
{
    return std::find_if(
        m_Changes.begin(), m_Changes.end(),
        [ParameterIndex, ArrayIndex](const ParameterChange& Change) {
            return Change.ParameterIndex == ParameterIndex && Change.ArrayIndex == ArrayIndex;
        });
}

} // namespace RadientMaterialDetail

} // namespace Diligent
