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

#include "Assets/RadientMaterialStorage.hpp"

#include <vector>

namespace Diligent
{

namespace RadientMaterialDetail
{

class MaterialParameterChanges final
{
public:
    MaterialParameterChanges() = default;

    // clang-format off
    MaterialParameterChanges           (const MaterialParameterChanges&) = delete;
    MaterialParameterChanges& operator=(const MaterialParameterChanges&) = delete;
    MaterialParameterChanges           (MaterialParameterChanges&&) noexcept = default;
    MaterialParameterChanges& operator=(MaterialParameterChanges&&) noexcept = default;
    // clang-format on

    // Layout is used only to validate the handle and the supplied value.
    // Explicit assignments are retained regardless of the material's live value.
    RADIENT_STATUS SetParameter(const MaterialStorage&         Layout,
                                RadientMaterialParameterHandle Handle,
                                const void*                    pData,
                                Uint32                         DataSize);

    RADIENT_STATUS SetTexture(const MaterialStorage&         Layout,
                              RadientMaterialParameterHandle Handle,
                              Uint32                         ArrayIndex,
                              IRadientTextureAsset*          pTexture);

    bool HasEffectiveTextureChanges(const PackedMaterialData& Target) const noexcept;
    bool ApplyTo(PackedMaterialData& Target) const noexcept;

private:
    static constexpr Uint32 ValueArrayIndex = ~Uint32{0};

    struct ParameterChange
    {
        Uint32 ParameterIndex = 0;
        Uint32 ArrayIndex     = ValueArrayIndex;
        Uint32 DataOffset     = 0;
    };

    using ChangeList     = std::vector<ParameterChange>;
    using ChangeIterator = ChangeList::iterator;

    ChangeIterator FindChange(Uint32 ParameterIndex, Uint32 ArrayIndex) noexcept;

    ChangeList                                  m_Changes;
    std::vector<Uint8>                          m_ValueData;
    std::vector<PackedMaterialData::TexturePtr> m_TextureData;
};

struct EmptyMaterialState
{};

struct EmptyMaterialChanges
{
    bool ApplyTo(EmptyMaterialState&) const noexcept
    {
        return false;
    }
};

template <typename SpecializedChanges>
struct MaterialChangeSet
{
    using SpecializedChangesType = SpecializedChanges;

    MaterialParameterChanges Parameters;
    SpecializedChanges       Specialized;
};

} // namespace RadientMaterialDetail

} // namespace Diligent
