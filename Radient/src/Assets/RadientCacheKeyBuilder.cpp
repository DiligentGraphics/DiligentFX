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

#include "Assets/RadientCacheKeyBuilder.hpp"

namespace Diligent
{

namespace
{

void Append(std::string& Key, std::string_view Value)
{
    if (!Value.empty())
        Key.append(Value.data(), Value.size());
}

} // namespace

RadientCacheKeyBuilder::RadientCacheKeyBuilder(std::string_view Type, Uint32 Version)
{
    Append(m_Key, Type);
    m_Key += ":v";
    m_Key += std::to_string(Version);
}

RadientCacheKeyBuilder& RadientCacheKeyBuilder::AddString(std::string_view Name, std::string_view Value)
{
    AddFieldName(Name);
    m_Key += std::to_string(Value.size());
    m_Key += ':';
    Append(m_Key, Value);
    return *this;
}

RadientCacheKeyBuilder& RadientCacheKeyBuilder::AddBool(std::string_view Name, bool Value)
{
    AddFieldName(Name);
    m_Key += Value ? '1' : '0';
    return *this;
}

void RadientCacheKeyBuilder::AddFieldName(std::string_view Name)
{
    m_Key += ':';
    Append(m_Key, Name);
    m_Key += '=';
}

} // namespace Diligent
