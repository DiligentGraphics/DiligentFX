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
#include "Scalars.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)
// clang-format off
typedef Float32 TestFixedRow[4];

struct TestFixedChild
{
    Int32 Id DEFAULT_INITIALIZER(7);
    const Char* Label DEFAULT_INITIALIZER(nullptr);
    Float64 Samples[2] DEFAULT_INITIALIZER({1.25, -2.5});
};
typedef struct TestFixedChild TestFixedChild;

struct TestFixedArrays
{
    Float32 Matrix[16] DEFAULT_INITIALIZER(
        {1.f, 0.f, 0.f, 0.f,
         0.f, 1.f, 0.f, 0.f,
         0.f, 0.f, 1.f, 0.f,
         0.f, 0.f, 0.f, 1.f});
    Int8 SignedBytes[2] DEFAULT_INITIALIZER({-128, 127});
    Uint8 Bytes[2] DEFAULT_INITIALIZER({0, 255});
    Int16 Shorts[2] DEFAULT_INITIALIZER({-32768, 32767});
    Uint16 UnsignedShorts[2] DEFAULT_INITIALIZER({0, 65535});
    Int32 Ints[2] DEFAULT_INITIALIZER({(-2147483647 - 1), 2147483647});
    Uint32 UnsignedInts[2] DEFAULT_INITIALIZER({0, 0xffffffffu});
    Int64 Longs[2] DEFAULT_INITIALIZER({(-9223372036854775807ll - 1), 9223372036854775807ll});
    Uint64 WideValues[2] DEFAULT_INITIALIZER({0, 0xfedcba9876543210ull});
    Bool Flags[2] DEFAULT_INITIALIZER({True, False});
    TEST_MODE Modes[2] DEFAULT_INITIALIZER({TEST_MODE_FIRST, TEST_MODE_LAST});
    TestFixedRow Rows[2] DEFAULT_INITIALIZER({{1.f, 2.f, 3.f, 4.f}, {5.f, 6.f, 7.f, 8.f}});
    Int32 Cube[2][2][2] DEFAULT_INITIALIZER({{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}});
    TestFixedChild Children[2];
    TestFixedChild Grid[2][1];
};
typedef struct TestFixedArrays TestFixedArrays;

// Large enough to expose conversion buffers that exceed a JVM's native stack.
// The extra elements also exercise the final partial conversion chunk.
struct TestLargeFixedArray
{
    Uint8 Pixels[512 * 256 * 3 + 13] DEFAULT_INITIALIZER({});
};
typedef struct TestLargeFixedArray TestLargeFixedArray;

struct TestFixedPixel
{
    Uint32 Value DEFAULT_INITIALIZER(17);
};
typedef struct TestFixedPixel TestFixedPixel;

// The native descriptor fits the default JVM stack, but a second inline copy
// for each element's input storage would exhaust it.
struct TestLargeDescriptorArray
{
    TestFixedPixel Pixels[400 * 400];
};
typedef struct TestLargeDescriptorArray TestLargeDescriptorArray;

// Ordinary descriptor nesting must not add an inline copy of the image
// to the conversion storage retained for the native call.
struct TestLargeArrayPacket
{
    TestLargeDescriptorArray Image;
};
typedef struct TestLargeArrayPacket TestLargeArrayPacket;

#include "DefineRefMacro.h"
Bool DILIGENT_GLOBAL_FUNCTION(CheckLargeArrayPacket)(const TestLargeArrayPacket REF Desc);
const TestLargeArrayPacket REF DILIGENT_GLOBAL_FUNCTION(EchoLargeArrayPacket)(const TestLargeArrayPacket REF Desc);
Bool DILIGENT_GLOBAL_FUNCTION(CheckLargeDescriptorArray)(const TestLargeDescriptorArray REF Desc);
const TestLargeDescriptorArray REF DILIGENT_GLOBAL_FUNCTION(EchoLargeDescriptorArray)(const TestLargeDescriptorArray REF Desc);
Bool DILIGENT_GLOBAL_FUNCTION(CheckLargeFixedInput)(const TestLargeFixedArray REF Desc);
const TestLargeFixedArray REF DILIGENT_GLOBAL_FUNCTION(EchoLargeFixedArray)(const TestLargeFixedArray REF Desc);
Bool DILIGENT_GLOBAL_FUNCTION(CheckFixedInputs)(const TestFixedArrays REF Desc);
void DILIGENT_GLOBAL_FUNCTION(CopyFixedArrays)(const TestFixedArrays REF Desc, TestFixedArrays REF Result);
const TestFixedArrays REF DILIGENT_GLOBAL_FUNCTION(EchoFixedArrays)(const TestFixedArrays REF Desc);
const TestFixedArrays REF DILIGENT_GLOBAL_FUNCTION(GetIncompleteFixedArrays)();
Uint32 DILIGENT_GLOBAL_FUNCTION(GetFixedCallCount)();
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
