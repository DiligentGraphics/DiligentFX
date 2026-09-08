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

#include "BasicTypes.h"

#include <cstddef>
#include <limits>

namespace Diligent
{

namespace RadientValidation
{

/// Returns true if the sum of two 64-bit unsigned values can be represented by ResultType.
template <typename ResultType>
inline bool IsSumRepresentable(Uint64 Lhs, Uint64 Rhs) noexcept
{
    static_assert(std::numeric_limits<ResultType>::is_integer && !std::numeric_limits<ResultType>::is_signed,
                  "ResultType must be an unsigned integer type");
    static_assert(std::numeric_limits<ResultType>::digits <= std::numeric_limits<Uint64>::digits,
                  "ResultType must not be wider than Uint64");

    constexpr Uint64 Maximum = static_cast<Uint64>((std::numeric_limits<ResultType>::max)());
    return Lhs <= Maximum && Rhs <= Maximum - Lhs;
}

/// Returns true if the product of two 64-bit unsigned values can be represented by ResultType.
template <typename ResultType>
inline bool IsProductRepresentable(Uint64 Lhs, Uint64 Rhs) noexcept
{
    static_assert(std::numeric_limits<ResultType>::is_integer && !std::numeric_limits<ResultType>::is_signed,
                  "ResultType must be an unsigned integer type");
    static_assert(std::numeric_limits<ResultType>::digits <= std::numeric_limits<Uint64>::digits,
                  "ResultType must not be wider than Uint64");

    constexpr Uint64 Maximum = static_cast<Uint64>((std::numeric_limits<ResultType>::max)());
    return Lhs == 0 || Rhs <= Maximum / Lhs;
}

/// Returns true if Size can be represented by size_t.
inline bool IsAddressableSize(Uint64 Size) noexcept
{
    return Size <= (std::numeric_limits<size_t>::max)();
}

/// Returns true if the half-open range [First, First + Count) is contained in [0, Total).
inline bool IsValidSubrange(Uint64 First, Uint64 Count, Uint64 Total) noexcept
{
    return First <= Total && Count <= Total - First;
}

/// Multiplies two 64-bit unsigned values, returning false on overflow.
/// On success, Result receives the product.
inline bool CheckedMultiply(Uint64 Lhs, Uint64 Rhs, Uint64& Result) noexcept
{
    if (!IsProductRepresentable<Uint64>(Lhs, Rhs))
        return false;

    Result = Lhs * Rhs;
    return true;
}

/// Returns true if an array's byte size can be represented by size_t.
inline bool IsAddressableArray(Uint64 Count, Uint64 ElementSize) noexcept
{
    return IsProductRepresentable<size_t>(Count, ElementSize);
}

} // namespace RadientValidation

} // namespace Diligent
