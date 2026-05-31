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

#include "RadientDrawList.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

void RadientDrawList::Clear()
{
    m_Items.clear();
}

size_t RadientDrawList::Add(RadientDrawableID DrawableID)
{
    const size_t Index = m_Items.size();
    m_Items.emplace_back(DrawableID);
    return Index;
}

RadientDrawableID RadientDrawList::RemoveAt(size_t Index)
{
    VERIFY(Index < m_Items.size(), "Invalid draw list item index");
    if (Index >= m_Items.size())
        return InvalidRadientDrawableID;

    const RadientDrawableID MovedDrawableID = m_Items.back().DrawableID;
    m_Items[Index]                          = m_Items.back();
    m_Items.pop_back();

    return Index < m_Items.size() ? MovedDrawableID : InvalidRadientDrawableID;
}

void RadientDrawLists::Clear()
{
    for (RadientDrawList& DrawList : m_DrawLists)
        DrawList.Clear();
}

size_t RadientDrawLists::Add(GLTF::Material::ALPHA_MODE AlphaMode,
                             RadientDrawableID          DrawableID)
{
    return m_DrawLists[AlphaMode].Add(DrawableID);
}

RadientDrawableID RadientDrawLists::RemoveAt(GLTF::Material::ALPHA_MODE AlphaMode,
                                             size_t                     Index)
{
    return m_DrawLists[AlphaMode].RemoveAt(Index);
}

bool RadientDrawLists::IsEmpty() const
{
    for (const RadientDrawList& DrawList : m_DrawLists)
    {
        if (!DrawList.IsEmpty())
            return false;
    }

    return true;
}

} // namespace Diligent
