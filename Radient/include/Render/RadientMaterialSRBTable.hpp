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

#include "Render/RadientMaterialTextureBinding.hpp"

#include "RefCntAutoPtr.hpp"
#include "ShaderResourceBinding.h"

#include <array>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Diligent
{

using RadientMaterialSRBIndex = Uint32;

static constexpr RadientMaterialSRBIndex InvalidRadientMaterialSRBIndex = ~RadientMaterialSRBIndex{0};

/// Identifies an SRB exclusively by its ordered material texture slots.
/// Shader texture indexing intentionally does not participate in this key.
struct RadientMaterialSRBKey
{
    Uint32 SlotCount = 0;

    std::array<ITextureView*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> SlotSRVs{};

    bool operator==(const RadientMaterialSRBKey& Rhs) const noexcept;

    struct Hasher
    {
        size_t operator()(const RadientMaterialSRBKey& Key) const noexcept;
    };
};

/// Builds an SRB key by filling all slots with their semantic defaults and then
/// applying the active bindings selected by the material binding plan.
bool BuildRadientMaterialSRBKey(const RadientMaterialTextureBindingPlan& Plan,
                                const RadientMaterialTextureSRVArray&    TextureSRVs,
                                const RadientMaterialTextureSRVArray&    DefaultTextureSRVs,
                                RadientMaterialSRBKey&                   Key) noexcept;

/// Renderer-owned table that gives material SRBs stable integer indices.
/// The table is render-thread-only and retains representative texture assets so
/// entries can be rebuilt after an atlas resize without changing their indices.
class RadientMaterialSRBTable final
{
public:
    using CreateSRBCallbackType = std::function<RefCntAutoPtr<IShaderResourceBinding>(ITextureView* const*, Uint32)>;

    RadientMaterialSRBIndex GetOrCreate(const RadientMaterialTextureBindingPlan& Plan,
                                        const RadientMaterialTextureSRVArray&    TextureSRVs,
                                        const RadientMaterialTextureSRVArray&    DefaultTextureSRVs,
                                        const CreateSRBCallbackType&             CreateSRB);

    IShaderResourceBinding* Get(RadientMaterialSRBIndex Index) const noexcept;

    size_t GetSize() const noexcept
    {
        return m_Entries.size();
    }

    void Clear();

private:
    struct Entry
    {
        RefCntAutoPtr<IShaderResourceBinding> pSRB;
        Uint32                                SlotCount = 0;

        std::array<RefCntAutoPtr<IRadientTextureAsset>, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> SlotTextures;
        std::array<PBR_Renderer::TEXTURE_ATTRIB_ID, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>     SlotTextureAttribIds{};
    };

    std::vector<Entry> m_Entries;

    std::unordered_map<RadientMaterialSRBKey,
                       RadientMaterialSRBIndex,
                       RadientMaterialSRBKey::Hasher>
        m_Lookup;
};

} // namespace Diligent
