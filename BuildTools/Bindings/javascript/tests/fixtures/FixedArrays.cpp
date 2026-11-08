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

#include "FixedArrays.h"

#include <cstddef>
#include <emscripten/heap.h>

namespace Diligent
{
namespace
{
MarshalFixedArrays Arrays;
Uint32             FixedCallCount = 0;

template <typename T, size_t N>
Bool EqualArray(const T (&Actual)[N], const T (&Expected)[N])
{
    for (size_t i = 0; i < N; ++i)
    {
        if (Actual[i] != Expected[i])
            return False;
    }
    return True;
}
} // namespace

// These checks read native array members independently of snapshot conversion.
Bool CheckFixedInputs(const MarshalFixedArrays& Desc, Uint32 Type)
{
    ++FixedCallCount;
    switch (Type)
    {
        case 0:
        {
            static constexpr Int8 Expected[] = {-100, -2, 111};
            return EqualArray(Desc.ValuesInt8, Expected);
        }
        case 1:
        {
            static constexpr Uint8 Expected[] = {42, 200, 250};
            return EqualArray(Desc.ValuesUint8, Expected);
        }
        case 2:
        {
            static constexpr Int16 Expected[] = {-30000, -20, 30000};
            return EqualArray(Desc.ValuesInt16, Expected);
        }
        case 3:
        {
            static constexpr Uint16 Expected[] = {12, 40000, 60000};
            return EqualArray(Desc.ValuesUint16, Expected);
        }
        case 4:
        {
            static constexpr Int32 Expected[] = {-2000000000, -20, 2000000000};
            return EqualArray(Desc.ValuesInt32, Expected);
        }
        case 5:
        {
            static constexpr Uint32 Expected[] = {13, 0x81234567u, 0xfedcba98u};
            return EqualArray(Desc.ValuesUint32, Expected);
        }
        case 6:
        {
            static constexpr Int64 Expected[] = {-0x6123456789abcdefll, -30, 0x6123456789abcdefll};
            return EqualArray(Desc.ValuesInt64, Expected);
        }
        case 7:
        {
            static constexpr Uint64 Expected[] = {25, 0x8123456789abcdefull, 0xeeeeeeeeeeeeeeeeull};
            return EqualArray(Desc.ValuesUint64, Expected);
        }
        case 8:
        {
            static constexpr Float32 Expected[] = {-2.5f, 1.75f, 456.25f};
            return EqualArray(Desc.ValuesFloat32, Expected);
        }
        case 9:
        {
            static constexpr Float64 Expected[] = {-2.25, 0.2, 987654.321};
            return EqualArray(Desc.ValuesFloat64, Expected);
        }
        case 10:
        {
            static constexpr Bool Expected[] = {False, True, False};
            return EqualArray(Desc.ValuesBool, Expected);
        }
        case 11:
        {
            static constexpr MARSHAL_MODE Expected[] = {MARSHAL_MODE_LAST, MARSHAL_MODE_FIRST, MARSHAL_MODE_LAST};
            return EqualArray(Desc.ValuesMARSHAL_MODE, Expected);
        }
        case 12:
        {
            static constexpr Char Expected[] = {'B', static_cast<Char>(0x80), 0};
            return EqualArray(Desc.ValuesChar, Expected);
        }
        default: return False;
    }
}

Float32 ReadFixedMatrix(const MarshalFixedMatrix& Matrix, Uint32 Row, Uint32 Column)
{
    ++FixedCallCount;
    return Row < 4 && Column < 4 ? Matrix.Data[Row * 4 + Column] : -1000.f;
}
Float32 ReadFixedRow(const MarshalFixedArrays& Desc, Uint32 Row, Uint32 Column)
{
    ++FixedCallCount;
    return Row < 2 && Column < 4 ? Desc.Rows[Row][Column] : -1000.f;
}
Float32 ReadFixedCube(const MarshalFixedArrays& Desc, Uint32 Plane, Uint32 Row, Uint32 Column)
{
    ++FixedCallCount;
    return Plane < 2 && Row < 2 && Column < 2 ? Desc.Cube[Plane][Row][Column] : -1000.f;
}
Uint32 ReadFixedChild(const MarshalFixedArrays& Desc, Uint32 Index)
{
    ++FixedCallCount;
    return Index < 2 ? Desc.Children[Index].Id : 0;
}
Float32 ReadFixedSample(const MarshalFixedArrays& Desc, Uint32 Index, Uint32 Sample)
{
    ++FixedCallCount;
    return Index < 2 && Sample < 2 ? Desc.Children[Index].Samples[Sample] : -1000.f;
}
const MarshalFixedArrays& GetFixedArrays(Bool GrowMemory)
{
    ++FixedCallCount;
    if (GrowMemory)
        emscripten_resize_heap(emscripten_get_heap_size() + 65536);
    return Arrays;
}
const MarshalFixedReadOnly& GetFixedReadOnlyArrays()
{
    static const MarshalFixedReadOnly ReadOnly;
    return ReadOnly;
}
const MarshalFixedArrays& EchoFixedArrays(const MarshalFixedArrays& Desc, Bool InvalidUTF8)
{
    ++FixedCallCount;
    if (InvalidUTF8)
    {
        static constexpr Char     Invalid[] = {static_cast<Char>(0xc3), 'x', 0};
        static MarshalFixedArrays InvalidDesc;
        InvalidDesc                   = Desc;
        InvalidDesc.Children[1].Label = Invalid;
        return InvalidDesc;
    }
    return Desc;
}
const MarshalFixedMatrix& EchoFixedMatrix(const MarshalFixedMatrix& Matrix)
{
    ++FixedCallCount;
    return Matrix;
}
void UpdateFixedArrays()
{
    Arrays.ValuesFloat32[0]       = 10.f;
    Arrays.Rows[1][2]             = 20.f;
    Arrays.Children[0].Id         = 30;
    Arrays.Children[0].Samples[1] = 40.f;
    Arrays.Children[0].Label      = "changed";
    Arrays.Grid[1][0].Id          = 50;
    Arrays.Matrices[1].Data[15]   = 60.f;
}
void   ResetFixedArrays() { Arrays = {}; }
Uint32 GetFixedCallCount() { return FixedCallCount; }
} // namespace Diligent
