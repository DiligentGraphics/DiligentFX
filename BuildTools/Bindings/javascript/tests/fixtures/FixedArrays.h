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
typedef Float32 MarshalFixedRow[4];

struct MarshalFixedMatrix
{
    Float32 Data[16] DEFAULT_INITIALIZER(
        {1.f, 0.f, 0.f, 0.f,
         0.f, 1.f, 0.f, 0.f,
         0.f, 0.f, 1.f, 0.f,
         0.f, 0.f, 0.f, 1.f});
};
typedef struct MarshalFixedMatrix MarshalFixedMatrix;

struct MarshalFixedChild
{
    Uint32 Id DEFAULT_INITIALIZER(7);
    const Char* Label DEFAULT_INITIALIZER("child default");
    Float32 Samples[2] DEFAULT_INITIALIZER({1.f, 2.f});
    const Int16* pExtra DEFAULT_INITIALIZER(nullptr);
    Uint32 ExtraCount DEFAULT_INITIALIZER(0);
};
typedef struct MarshalFixedChild MarshalFixedChild;

struct MarshalFixedArrays
{
    Int8 ValuesInt8[3] DEFAULT_INITIALIZER({-128, 17, 127});
    Uint8 ValuesUint8[3] DEFAULT_INITIALIZER({0, 128, 255});
    Int16 ValuesInt16[3] DEFAULT_INITIALIZER({-32768, 17, 32767});
    Uint16 ValuesUint16[3] DEFAULT_INITIALIZER({0, 32768, 65535});
    Int32 ValuesInt32[3] DEFAULT_INITIALIZER({(-2147483647 - 1), 17, 2147483647});
    Uint32 ValuesUint32[3] DEFAULT_INITIALIZER({0, 0x80000000u, 0xffffffffu});
    Int64 ValuesInt64[3] DEFAULT_INITIALIZER({-0x7000000000000001ll, 17, 0x7123456789abcdefll});
    Uint64 ValuesUint64[3] DEFAULT_INITIALIZER({0, 0x8000000000000000ull, 0xfedcba9876543210ull});
    Float32 ValuesFloat32[3] DEFAULT_INITIALIZER({-1.5f, 0.25f, 123.5f});
    Float64 ValuesFloat64[3] DEFAULT_INITIALIZER({-1.25, 0.1, 123456.789});
    Bool ValuesBool[3] DEFAULT_INITIALIZER({True, False, True});
    MARSHAL_MODE ValuesMARSHAL_MODE[3] DEFAULT_INITIALIZER({MARSHAL_MODE_FIRST, MARSHAL_MODE_LAST, MARSHAL_MODE_FIRST});
    Char ValuesChar[3] DEFAULT_INITIALIZER({'A', 0, static_cast<Char>(0xff)});
    MarshalFixedRow Rows[2] DEFAULT_INITIALIZER({{1.f, 2.f, 3.f, 4.f}, {5.f, 6.f, 7.f, 8.f}});
    Float32 Cube[2][2][2] DEFAULT_INITIALIZER({{{1.f, 2.f}, {3.f, 4.f}}, {{5.f, 6.f}, {7.f, 8.f}}});
    MarshalFixedChild Children[2] DEFAULT_INITIALIZER({{11, "first", {3.f, 4.f}}, {22, "second", {5.f, 6.f}}});
    MarshalFixedChild Grid[2][2] DEFAULT_INITIALIZER({});
    MarshalFixedMatrix Matrices[2] DEFAULT_INITIALIZER({});
};
typedef struct MarshalFixedArrays MarshalFixedArrays;

struct MarshalFixedReadOnlyElement
{
    const Uint32 Value DEFAULT_INITIALIZER(13);
};
typedef struct MarshalFixedReadOnlyElement MarshalFixedReadOnlyElement;

struct MarshalFixedReadOnly
{
    const Uint16 Values[3] DEFAULT_INITIALIZER({10, 20, 30});
    MarshalFixedReadOnlyElement Items[2] DEFAULT_INITIALIZER({{5}, {9}});
};
typedef struct MarshalFixedReadOnly MarshalFixedReadOnly;

#include "DefineRefMacro.h"
Bool DILIGENT_GLOBAL_FUNCTION(CheckFixedInputs)(const MarshalFixedArrays REF Desc, Uint32 Type);
Float32 DILIGENT_GLOBAL_FUNCTION(ReadFixedMatrix)(const MarshalFixedMatrix REF Matrix, Uint32 Row, Uint32 Column);
Float32 DILIGENT_GLOBAL_FUNCTION(ReadFixedRow)(const MarshalFixedArrays REF Desc, Uint32 Row, Uint32 Column);
Float32 DILIGENT_GLOBAL_FUNCTION(ReadFixedCube)(const MarshalFixedArrays REF Desc, Uint32 Plane, Uint32 Row, Uint32 Column);
Uint32 DILIGENT_GLOBAL_FUNCTION(ReadFixedChild)(const MarshalFixedArrays REF Desc, Uint32 Index);
Float32 DILIGENT_GLOBAL_FUNCTION(ReadFixedSample)(const MarshalFixedArrays REF Desc, Uint32 Index, Uint32 Sample);
const MarshalFixedArrays REF DILIGENT_GLOBAL_FUNCTION(GetFixedArrays)(Bool GrowMemory);
const MarshalFixedReadOnly REF DILIGENT_GLOBAL_FUNCTION(GetFixedReadOnlyArrays)(void);
const MarshalFixedArrays REF DILIGENT_GLOBAL_FUNCTION(EchoFixedArrays)(const MarshalFixedArrays REF Desc, Bool InvalidUTF8);
const MarshalFixedMatrix REF DILIGENT_GLOBAL_FUNCTION(EchoFixedMatrix)(const MarshalFixedMatrix REF Matrix);
void DILIGENT_GLOBAL_FUNCTION(UpdateFixedArrays)(void);
void DILIGENT_GLOBAL_FUNCTION(ResetFixedArrays)(void);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetFixedCallCount)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
