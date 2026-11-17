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

#include "FixedDescriptorArrays.h"
#include <cstring>

namespace Diligent
{
namespace
{
Uint32 FixedCallCount = 0;
}

Bool CheckFixedDescriptorInputs(const TestFixedArrays& Desc)
{
    ++FixedCallCount;
    return Desc.Matrix[13] == 42.25f && Desc.Rows[1][3] == -7.5f && Desc.Cube[1][0][1] == 27 &&
        Desc.SignedBytes[0] == -128 && Desc.SignedBytes[1] == 127 &&
        Desc.Bytes[0] == 0 && Desc.Bytes[1] == 255 &&
        Desc.Shorts[0] == -32768 && Desc.Shorts[1] == 32767 &&
        Desc.UnsignedShorts[0] == 0 && Desc.UnsignedShorts[1] == 65535 &&
        Desc.Ints[0] == (-2147483647 - 1) && Desc.Ints[1] == 2147483647 &&
        Desc.UnsignedInts[0] == 0 && Desc.UnsignedInts[1] == 0xffffffffu &&
        Desc.Longs[0] == (-9223372036854775807ll - 1) && Desc.Longs[1] == 9223372036854775807ll &&
        Desc.WideValues[0] == 0 && Desc.WideValues[1] == 0xfedcba9876543210ull &&
        Desc.Flags[0] && !Desc.Flags[1] && Desc.Modes[0] == TEST_MODE_FIRST && Desc.Modes[1] == TEST_MODE_LAST &&
        Desc.Children[0].Id == 11 && Desc.Children[1].Id == 22 &&
        std::strcmp(Desc.Children[0].Label, "first") == 0 && std::strcmp(Desc.Children[1].Label, "second") == 0 &&
        Desc.Children[1].Samples[0] == -11.5 && Desc.Children[1].Samples[1] == 9.75 &&
        std::strcmp(Desc.Grid[0][0].Label, "grid zero") == 0 && std::strcmp(Desc.Grid[1][0].Label, "grid one") == 0;
}

void CopyFixedArrays(const TestFixedArrays& Desc, TestFixedArrays& Result)
{
    ++FixedCallCount;
    Result = Desc;
}

const TestFixedArrays& EchoFixedDescriptorArrays(const TestFixedArrays& Desc)
{
    ++FixedCallCount;
    return Desc;
}

const TestFixedArrays& GetIncompleteFixedArrays()
{
    static const TestFixedArrays Desc;
    return Desc;
}

Bool CheckLargeFixedInput(const TestLargeFixedArray& Desc)
{
    ++FixedCallCount;
    for (size_t Index = 0; Index < sizeof(Desc.Pixels); ++Index)
    {
        if (Desc.Pixels[Index] != static_cast<Uint8>((Index * 37 + 11) & 255))
            return false;
    }
    return true;
}

const TestLargeFixedArray& EchoLargeFixedArray(const TestLargeFixedArray& Desc)
{
    ++FixedCallCount;
    return Desc;
}

Bool CheckLargeDescriptorArray(const TestLargeDescriptorArray& Desc)
{
    ++FixedCallCount;
    for (size_t Index = 0; Index < sizeof(Desc.Pixels) / sizeof(Desc.Pixels[0]); ++Index)
    {
        if (Desc.Pixels[Index].Value != static_cast<Uint32>(Index * 37 + 11))
            return false;
    }
    return true;
}

const TestLargeDescriptorArray& EchoLargeDescriptorArray(const TestLargeDescriptorArray& Desc)
{
    ++FixedCallCount;
    return Desc;
}

Bool CheckLargeArrayPacket(const TestLargeArrayPacket& Desc)
{
    return CheckLargeDescriptorArray(Desc.Image);
}

const TestLargeArrayPacket& EchoLargeArrayPacket(const TestLargeArrayPacket& Desc)
{
    ++FixedCallCount;
    return Desc;
}

Uint32 GetFixedDescriptorCallCount() { return FixedCallCount; }

void CopyLargeDescriptorArray(const TestLargeDescriptorArray& Desc, TestLargeDescriptorArray& Result)
{
    Result = Desc;
}

} // namespace Diligent
