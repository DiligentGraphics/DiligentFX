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

#include "RadientTypes.h"

#include <string>
#include <string_view>
#include <type_traits>

namespace Diligent
{

/// Builds human-readable cache keys with unambiguous field boundaries.
class RadientCacheKeyBuilder final
{
public:
    RadientCacheKeyBuilder(std::string_view Type, Uint32 Version);

    /// Appends a length-prefixed string value.
    RadientCacheKeyBuilder& AddString(std::string_view Name, std::string_view Value);

    /// Appends an integral or enum value using its decimal representation.
    template <typename IntegerType>
    RadientCacheKeyBuilder& AddInteger(std::string_view Name, IntegerType Value);

    RadientCacheKeyBuilder& AddBool(std::string_view Name, bool Value);

    const std::string& GetKey() const noexcept
    {
        return m_Key;
    }

private:
    void AddFieldName(std::string_view Name);

    std::string m_Key;
};

template <typename IntegerType>
RadientCacheKeyBuilder& RadientCacheKeyBuilder::AddInteger(std::string_view Name, IntegerType Value)
{
    using ValueType = std::remove_cv_t<std::remove_reference_t<IntegerType>>;
    if constexpr (std::is_enum_v<ValueType>)
    {
        return AddInteger(Name, static_cast<std::underlying_type_t<ValueType>>(Value));
    }
    else
    {
        static_assert(std::is_integral_v<ValueType>, "Cache key value must be an integer or enum type.");
        static_assert(!std::is_same_v<ValueType, bool>, "Use AddBool() to add boolean values.");
        static_assert(sizeof(ValueType) <= sizeof(Uint64), "Cache key integers must not exceed 64 bits.");

        AddFieldName(Name);
        m_Key += std::to_string(Value);
        return *this;
    }
}

} // namespace Diligent
