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

#include "RadientScene.h"

#include "GLTFLoader.hpp"

#include <array>
#include <vector>

namespace Diligent
{

using RadientDrawableID = Uint32;

/// Invalid stable drawable slot identifier.
static constexpr RadientDrawableID InvalidRadientDrawableID = ~RadientDrawableID{0};

/// Lightweight draw-list entry.
///
/// Draw items deliberately store only a stable drawable ID. Heavy primitive data, material
/// state, scene transform/visibility references, and renderer-derived pass data live in
/// RadientSceneDrawableCache/RadientDrawableSlot or in the render pass cache.
struct RadientDrawItem
{
    explicit RadientDrawItem(RadientDrawableID _DrawableID) :
        DrawableID{_DrawableID}
    {}

    RadientDrawableID DrawableID = InvalidRadientDrawableID;
};


/// Compact list of drawable IDs selected for a render pass subset.
///
/// A draw list does not own drawable data and does not preserve semantic order. Removal uses
/// swap-erase; RemoveAt returns the moved drawable ID so RadientSceneDrawableCache can repair
/// the moved slot's DrawListIndex.
class RadientDrawList
{
public:
    using ItemListType = std::vector<RadientDrawItem>;

    size_t Add(RadientDrawableID DrawableID)
    {
        const size_t Index = m_Items.size();
        m_Items.emplace_back(DrawableID);
        return Index;
    }

    /// Removes the item at the specified index using swap-erase and returns the moved drawable ID,
    /// or InvalidRadientDrawableID if no item was moved (i.e. the removed item was the last one).
    RadientDrawableID RemoveAt(size_t Index);

    size_t GetItemCount() const
    {
        return m_Items.size();
    }

    bool IsEmpty() const
    {
        return m_Items.empty();
    }

    const ItemListType& GetItems() const
    {
        return m_Items;
    }

    void Clear()
    {
        m_Items.clear();
    }

private:
    ItemListType m_Items;
};


/// Draw lists split by material alpha mode.
///
/// Render passes can select one or more alpha-mode lists, then build their own pass-local
/// sorted index without moving heavyweight drawable data.
class RadientDrawLists
{
public:
    void Clear();

    size_t Add(GLTF::Material::ALPHA_MODE AlphaMode,
               RadientDrawableID          DrawableID)
    {
        return m_DrawLists[AlphaMode].Add(DrawableID);
    }

    RadientDrawableID RemoveAt(GLTF::Material::ALPHA_MODE AlphaMode,
                               size_t                     Index)
    {
        return m_DrawLists[AlphaMode].RemoveAt(Index);
    }

    bool IsEmpty() const;

    const RadientDrawList& GetDrawList(GLTF::Material::ALPHA_MODE AlphaMode) const
    {
        return m_DrawLists[AlphaMode];
    }

private:
    std::array<RadientDrawList, GLTF::Material::ALPHA_MODE_NUM_MODES> m_DrawLists;
};

} // namespace Diligent
