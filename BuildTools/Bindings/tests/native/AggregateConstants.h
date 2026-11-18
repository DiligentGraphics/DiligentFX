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

struct MarshalCharacterConstantDesc
{
    Char Name[8];
    Char Utf8[8];
    Char Embedded[8];
    Char Rows[2][8];
    signed char Signed[8];
    unsigned char Unsigned[8];
    Int8 SignedAlias[8];
    Uint8 UnsignedAlias[8];
    Uint8 Empty[8];
};
typedef struct MarshalCharacterConstantDesc MarshalCharacterConstantDesc;
static DILIGENT_CONSTEXPR MarshalCharacterConstantDesc MarshalCharacterConstant = {
    "foo", "\xc3\xa9", "a\0b", {"left", "right"},
    "\xc3\xa9", "\xc3\xa9", "a\0b", "a\0b", ""
};

// Explicit byte arrays remain numeric arrays even when initialized with text.
static DILIGENT_CONSTEXPR signed char MarshalSignedByteConstant[8] = "\xc3\xa9";
static DILIGENT_CONSTEXPR unsigned char MarshalUnsignedByteConstant[8] = "\xc3\xa9";
static DILIGENT_CONSTEXPR Int8 MarshalSignedAliasConstant[8] = "a\0b";
static DILIGENT_CONSTEXPR Uint8 MarshalUnsignedAliasConstant[8] = "a\0b";

// Exercise both compact zero initialization and nonzero data beyond Java's
// per-method bytecode limit. Macros keep the C/C++ fixture small and identical.
#define MARSHAL_CONSTANT_4 17, 65535, 0, 32768
#define MARSHAL_CONSTANT_8 MARSHAL_CONSTANT_4, MARSHAL_CONSTANT_4
#define MARSHAL_CONSTANT_16 MARSHAL_CONSTANT_8, MARSHAL_CONSTANT_8
#define MARSHAL_CONSTANT_32 MARSHAL_CONSTANT_16, MARSHAL_CONSTANT_16
#define MARSHAL_CONSTANT_64 MARSHAL_CONSTANT_32, MARSHAL_CONSTANT_32
#define MARSHAL_CONSTANT_128 MARSHAL_CONSTANT_64, MARSHAL_CONSTANT_64
#define MARSHAL_CONSTANT_256 MARSHAL_CONSTANT_128, MARSHAL_CONSTANT_128
#define MARSHAL_CONSTANT_512 MARSHAL_CONSTANT_256, MARSHAL_CONSTANT_256
#define MARSHAL_CONSTANT_1024 MARSHAL_CONSTANT_512, MARSHAL_CONSTANT_512
#define MARSHAL_CONSTANT_2048 MARSHAL_CONSTANT_1024, MARSHAL_CONSTANT_1024
#define MARSHAL_CONSTANT_4096 MARSHAL_CONSTANT_2048, MARSHAL_CONSTANT_2048
#define MARSHAL_CONSTANT_8192 MARSHAL_CONSTANT_4096, MARSHAL_CONSTANT_4096
#define MARSHAL_CONSTANT_16384 MARSHAL_CONSTANT_8192, MARSHAL_CONSTANT_8192
static DILIGENT_CONSTEXPR Uint16 MarshalZeroLookup[16384] = {0};
static DILIGENT_CONSTEXPR Uint16 MarshalPatternLookup[16384] = {MARSHAL_CONSTANT_16384};
struct MarshalLargeConstantDesc
{
    Uint16 Values[16384];
    Float32 SignedZeros[2];
};
typedef struct MarshalLargeConstantDesc MarshalLargeConstantDesc;
static DILIGENT_CONSTEXPR MarshalLargeConstantDesc MarshalLargeConstant = {
    {MARSHAL_CONSTANT_16384}, {0.0f, -0.0f}
};
#undef MARSHAL_CONSTANT_16384
#undef MARSHAL_CONSTANT_8192
#undef MARSHAL_CONSTANT_4096
#undef MARSHAL_CONSTANT_2048
#undef MARSHAL_CONSTANT_1024
#undef MARSHAL_CONSTANT_512
#undef MARSHAL_CONSTANT_256
#undef MARSHAL_CONSTANT_128
#undef MARSHAL_CONSTANT_64
#undef MARSHAL_CONSTANT_32
#undef MARSHAL_CONSTANT_16
#undef MARSHAL_CONSTANT_8
#undef MARSHAL_CONSTANT_4

#include "DefineRefMacro.h"
Bool DILIGENT_GLOBAL_FUNCTION(CheckCharacterConstant)(const MarshalCharacterConstantDesc REF Value);
Bool DILIGENT_GLOBAL_FUNCTION(CheckLargeConstant)(const MarshalLargeConstantDesc REF Value);
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
