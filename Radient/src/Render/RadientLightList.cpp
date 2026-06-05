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

#include "Render/RadientLightList.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace
{

size_t GetLightTypeIndex(RADIENT_LIGHT_TYPE Type)
{
    const size_t Index = static_cast<size_t>(Type);
    VERIFY(Index < RadientLightLists::LightTypeCount, "Invalid Radient light type");
    return Index < RadientLightLists::LightTypeCount ? Index : 0;
}

} // namespace

RadientEntityID RadientLightList::RemoveAt(size_t Index)
{
    if (Index >= m_Items.size())
    {
        UNEXPECTED("Light list item index (", Index, ") exceeds the number of items in the list (", m_Items.size(), ")");
        return InvalidRadientEntityID;
    }

    const RadientEntityID MovedEntity = m_Items.back().Entity;

    m_Items[Index] = m_Items.back();
    m_Items.pop_back();

    return Index < m_Items.size() ? MovedEntity : InvalidRadientEntityID;
}

void RadientLightLists::Clear()
{
    for (RadientLightList& LightList : m_LightLists)
        LightList.Clear();
}

bool RadientLightLists::IsEmpty() const
{
    for (const RadientLightList& LightList : m_LightLists)
    {
        if (!LightList.IsEmpty())
            return false;
    }

    return true;
}

size_t RadientLightLists::GetItemCount() const
{
    size_t ItemCount = 0;
    for (const RadientLightList& LightList : m_LightLists)
        ItemCount += LightList.GetItemCount();

    return ItemCount;
}

const RadientLightList& RadientLightLists::GetLightList(RADIENT_LIGHT_TYPE Type) const
{
    return m_LightLists[GetLightTypeIndex(Type)];
}

RadientLightList& RadientLightLists::GetMutableLightList(RADIENT_LIGHT_TYPE Type)
{
    return m_LightLists[GetLightTypeIndex(Type)];
}

} // namespace Diligent
