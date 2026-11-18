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

#include "AggregateConstants.h"

#include <cmath>
#include <cstring>

namespace Diligent
{

Bool CheckCharacterConstant(const MarshalCharacterConstantDesc& Value)
{
    const MarshalCharacterConstantDesc& Expected = MarshalCharacterConstant;
    return std::memcmp(Value.Name, Expected.Name, sizeof(Value.Name)) == 0 &&
        std::memcmp(Value.Utf8, Expected.Utf8, sizeof(Value.Utf8)) == 0 &&
        std::memcmp(Value.Embedded, Expected.Embedded, sizeof(Value.Embedded)) == 0 &&
        std::memcmp(Value.Rows, Expected.Rows, sizeof(Value.Rows)) == 0 &&
        std::memcmp(Value.Signed, Expected.Signed, sizeof(Value.Signed)) == 0 &&
        std::memcmp(Value.Unsigned, Expected.Unsigned, sizeof(Value.Unsigned)) == 0 &&
        std::memcmp(Value.SignedAlias, Expected.SignedAlias, sizeof(Value.SignedAlias)) == 0 &&
        std::memcmp(Value.UnsignedAlias, Expected.UnsignedAlias, sizeof(Value.UnsignedAlias)) == 0 &&
        std::memcmp(Value.Empty, Expected.Empty, sizeof(Value.Empty)) == 0;
}

Bool CheckLargeConstant(const MarshalLargeConstantDesc& Value)
{
    for (Uint32 i = 0; i < 16384; ++i)
    {
        if (Value.Values[i] != MarshalPatternLookup[i])
            return False;
    }
    return Value.SignedZeros[0] == 0.0f && !std::signbit(Value.SignedZeros[0]) &&
        Value.SignedZeros[1] == 0.0f && std::signbit(Value.SignedZeros[1]);
}

} // namespace Diligent
