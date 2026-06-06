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

#include <array>
#include <vector>

namespace Diligent
{

/// Renderer-facing light-list entry.
///
/// Light items keep references to scene-state component data instead of copying it. The
/// references remain valid while the scene is not mutated and the drawable cache stays synced.
/// Render passes should check pEffectiveVisible before using the light.
struct RadientLightItem
{
    RadientLightItem(RadientEntityID              _Entity,
                     const RadientLightComponent& _Light,
                     const RadientMatrix4x4&      _WorldMatrix,
                     const Bool&                  _EffectiveVisible) :
        Entity{_Entity},
        pLight{&_Light},
        pWorldMatrix{&_WorldMatrix},
        pEffectiveVisible{&_EffectiveVisible}
    {}

    RadientEntityID              Entity            = InvalidRadientEntityID;
    const RadientLightComponent* pLight            = nullptr;
    const RadientMatrix4x4*      pWorldMatrix      = nullptr;
    const Bool*                  pEffectiveVisible = nullptr;
};


/// Compact list of scene lights of one or more compatible render paths.
///
/// The list does not own light data. Removal uses swap-erase; RemoveAt returns the moved
/// entity ID so RadientSceneDrawableCache can repair the moved LightRecord::ListIndex.
class RadientLightList
{
public:
    using ItemListType = std::vector<RadientLightItem>;

    void Clear()
    {
        m_Items.clear();
    }

    size_t Add(RadientEntityID              Entity,
               const RadientLightComponent& Light,
               const RadientMatrix4x4&      WorldMatrix,
               const Bool&                  EffectiveVisible)
    {
        const size_t Index = m_Items.size();
        m_Items.emplace_back(Entity, Light, WorldMatrix, EffectiveVisible);
        return Index;
    }

    RadientEntityID RemoveAt(size_t Index);

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

private:
    ItemListType m_Items;
};


/// Light lists split by Radient light type.
///
/// Keeping directional, point, and spot lights in separate arrays lets render code consume
/// only the categories it supports without filtering the whole light set every frame.
class RadientLightLists
{
public:
    static constexpr size_t LightTypeCount = static_cast<size_t>(RADIENT_LIGHT_TYPE_COUNT);

    void Clear();

    size_t Add(RADIENT_LIGHT_TYPE           Type,
               RadientEntityID              Entity,
               const RadientLightComponent& Light,
               const RadientMatrix4x4&      WorldMatrix,
               const Bool&                  EffectiveVisible)
    {
        return m_LightLists[Type].Add(Entity, Light, WorldMatrix, EffectiveVisible);
    }

    RadientEntityID RemoveAt(RADIENT_LIGHT_TYPE Type,
                             size_t             Index)
    {
        return m_LightLists[Type].RemoveAt(Index);
    }

    bool IsEmpty() const;

    size_t GetItemCount() const;

    const RadientLightList& GetLightList(RADIENT_LIGHT_TYPE Type) const
    {
        return m_LightLists[Type];
    }

    template <typename CallbackType>
    void Enumerate(CallbackType&& Callback) const
    {
        for (const RadientLightList& LightList : m_LightLists)
        {
            for (const RadientLightItem& Item : LightList.GetItems())
                Callback(Item);
        }
    }

private:
    std::array<RadientLightList, LightTypeCount> m_LightLists;
};

} // namespace Diligent
