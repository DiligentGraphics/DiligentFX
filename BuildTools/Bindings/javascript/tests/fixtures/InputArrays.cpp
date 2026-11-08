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

#include "InputArrays.h"

#include <cstddef>

namespace Diligent
{
namespace
{
Uint32 ArrayCallCount = 0;

template <typename T, size_t N>
Bool MatchesPrefix(const T* pValues, Uint64 Count, const T (&Expected)[N])
{
    ++ArrayCallCount;
    if (Count == 0)
        return pValues == nullptr;
    if (pValues == nullptr || Count > N)
        return False;
    for (Uint64 i = 0; i < Count; ++i)
    {
        if (pValues[i] != Expected[i])
            return False;
    }
    return True;
}
} // namespace

Bool CheckArrayInt8(const Int8* pValues, Uint32 Count)
{
    static constexpr Int8 Expected[] = {-128, 17, 127};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayUint8(const Uint8* pValues, Uint32 Count)
{
    static constexpr Uint8 Expected[] = {0, 128, 255};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayInt16(const Int16* pValues, Uint32 Count)
{
    static constexpr Int16 Expected[] = {-32768, 17, 32767};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayUint16(const Uint16* pValues, Uint32 Count)
{
    static constexpr Uint16 Expected[] = {0, 32768, 65535};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayInt32(const Int32* pValues, Uint32 Count)
{
    static constexpr Int32 Expected[] = {(-2147483647 - 1), 17, 2147483647};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayUint32(const Uint32* pValues, Uint32 Count)
{
    static constexpr Uint32 Expected[] = {0, 0x80000000u, 0xffffffffu};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayInt64(const Int64* pValues, Uint32 Count)
{
    static constexpr Int64 Expected[] = {-0x7000000000000001ll, 17, 0x7123456789abcdefll};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayUint64(const Uint64* pValues, Uint64 Count)
{
    static constexpr Uint64 Expected[] = {0, 0x8000000000000000ull, 0xfedcba9876543210ull};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayFloat32(const Float32* pValues, Uint32 Count)
{
    static constexpr Float32 Expected[] = {-1.5f, 0.25f, 123.5f};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayFloat64(const Float64* pValues, Uint32 Count)
{
    static constexpr Float64 Expected[] = {-1.25, 0.1, 123456.789};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayBool(const Bool* pValues, Uint32 Count)
{
    static constexpr Bool Expected[] = {True, False, True};
    return MatchesPrefix(pValues, Count, Expected);
}

Bool CheckArrayMode(const MARSHAL_MODE* pValues, Uint32 Count)
{
    static constexpr MARSHAL_MODE Expected[] = {MARSHAL_MODE_FIRST, MARSHAL_MODE_LAST, MARSHAL_MODE_FIRST};
    return MatchesPrefix(pValues, Count, Expected);
}

// Read selected fields independently instead of using array output marshalling
// as the test oracle. Indexing here also checks the contiguous native layout.
Float64 ReadArrayItem(const MarshalArrayItem* pItems, Uint32 Count, Uint32 Index, Uint32 Field)
{
    ++ArrayCallCount;
    if (pItems == nullptr || Index >= Count)
        return -1000;
    const MarshalArrayItem& Item = pItems[Index];
    switch (Field)
    {
        case 0: return Item.Id;
        case 1: return Item.Transform.Scale.x;
        case 2: return Item.Transform.Scale.y;
        case 3: return Item.Transform.Position.y;
        case 4: return Item.ValueCount;
        case 5: return Item.pValues != nullptr && Item.ValueCount > 0 ? Item.pValues[0] : -1000;
        case 6: return Item.pValues != nullptr && Item.ValueCount > 1 ? Item.pValues[1] : -1000;
        default: return -1001;
    }
}

const Char* GetArrayItemName(const MarshalArrayItem* pItems, Uint32 Count, Uint32 Index)
{
    ++ArrayCallCount;
    return pItems != nullptr && Index < Count ? pItems[Index].Name : nullptr;
}

Float64 ReadArrayDesc(const MarshalArrayDesc& Desc, Uint32 Index, Uint32 Field)
{
    return ReadArrayItem(Desc.pItems, Desc.ItemCount, Index, Field);
}

Uint64 SumArraySamples(const MarshalArrayDesc& Desc)
{
    ++ArrayCallCount;
    if (Desc.pSamples == nullptr)
        return ~Uint64{0};
    Uint64 Sum = 0;
    for (Uint64 i = 0; i < Desc.SampleCount; ++i)
    {
        Sum += (i + 1) * Desc.pSamples[i];
    }
    return Sum;
}

Int32 ReadArrayGroup(const MarshalArrayDesc* pGroups, Uint32 Count, Uint32 Group, Uint32 Item)
{
    ++ArrayCallCount;
    if (pGroups == nullptr || Group >= Count || pGroups[Group].pItems == nullptr || Item >= pGroups[Group].ItemCount)
        return -1000;
    return pGroups[Group].pItems[Item].Id;
}

Uint32 GetArrayCallCount() { return ArrayCallCount; }
} // namespace Diligent
