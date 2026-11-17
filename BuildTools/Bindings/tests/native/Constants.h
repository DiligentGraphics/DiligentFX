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
#include "TestScalars.h"
#include <float.h>
#include <math.h>

DILIGENT_BEGIN_NAMESPACE(Diligent)
// clang-format off
typedef Uint64 TestEntityID;
typedef TestEntityID TestEntityAlias;

static DILIGENT_CONSTEXPR Int8 TestConstantSignedByte = -128;
static DILIGENT_CONSTEXPR Uint8 TestConstantByte = 255;
static DILIGENT_CONSTEXPR Int16 TestConstantShort = -32768;
static DILIGENT_CONSTEXPR Uint16 TestConstantUnsignedShort = 65535;
static DILIGENT_CONSTEXPR Int32 TestConstantInt = (-2147483647 - 1);
static DILIGENT_CONSTEXPR Uint32 TestConstantUnsignedInt = 0xffffffffu;
static DILIGENT_CONSTEXPR Int64 TestConstantLong = (-9223372036854775807ll - 1);
static DILIGENT_CONSTEXPR TestEntityAlias TestConstantInvalidID = ~0ull;
static DILIGENT_CONSTEXPR TestEntityAlias TestConstantWideID = 0xfedcba9876543210ull;
static DILIGENT_CONSTEXPR Uint32 TestConstantMask = (1u << 28) | 0x55u;
static DILIGENT_CONSTEXPR Bool TestConstantEnabled = True;
static DILIGENT_CONSTEXPR Bool TestConstantDisabled = False;
static DILIGENT_CONSTEXPR TEST_MODE TestConstantMode = TEST_MODE_LAST;
static DILIGENT_CONSTEXPR Float32 TestConstantFloat = 0.1f;
static DILIGENT_CONSTEXPR Float64 TestConstantDouble = 0.1;
static DILIGENT_CONSTEXPR Float32 TestConstantNegativeZero = -0.0f;
static DILIGENT_CONSTEXPR Float64 TestConstantNegativeDoubleZero = -0.0;
static DILIGENT_CONSTEXPR Float32 TestConstantFloatMax = FLT_MAX;
static DILIGENT_CONSTEXPR Float64 TestConstantDoubleMax = DBL_MAX;
static DILIGENT_CONSTEXPR Float32 TestConstantFloatSubnormal = FLT_MIN * 0.5f;
static DILIGENT_CONSTEXPR Float64 TestConstantDoubleSubnormal = DBL_MIN * 0.5;
static DILIGENT_CONSTEXPR Float32 TestConstantInfinity = INFINITY;
static DILIGENT_CONSTEXPR Float64 TestConstantNegativeInfinity = -INFINITY;
static DILIGENT_CONSTEXPR Float32 TestConstantNotANumber = NAN;

// Constructing this snapshot reads the constants through the native compiler,
// independently of the binding literals emitted from Clang's extracted values.
struct TestConstantSnapshot
{
    Int8 SignedByte DEFAULT_INITIALIZER(TestConstantSignedByte);
    Uint8 Byte DEFAULT_INITIALIZER(TestConstantByte);
    Int16 Short DEFAULT_INITIALIZER(TestConstantShort);
    Uint16 UnsignedShort DEFAULT_INITIALIZER(TestConstantUnsignedShort);
    Int32 Int DEFAULT_INITIALIZER(TestConstantInt);
    Uint32 UnsignedInt DEFAULT_INITIALIZER(TestConstantUnsignedInt);
    Int64 Long DEFAULT_INITIALIZER(TestConstantLong);
    TestEntityAlias InvalidID DEFAULT_INITIALIZER(TestConstantInvalidID);
    TestEntityAlias WideID DEFAULT_INITIALIZER(TestConstantWideID);
    Uint32 Mask DEFAULT_INITIALIZER(TestConstantMask);
    Bool Enabled DEFAULT_INITIALIZER(TestConstantEnabled);
    Bool Disabled DEFAULT_INITIALIZER(TestConstantDisabled);
    TEST_MODE Mode DEFAULT_INITIALIZER(TestConstantMode);
    Float32 Float DEFAULT_INITIALIZER(TestConstantFloat);
    Float64 Double DEFAULT_INITIALIZER(TestConstantDouble);
    Float32 NegativeZero DEFAULT_INITIALIZER(TestConstantNegativeZero);
    Float64 NegativeDoubleZero DEFAULT_INITIALIZER(TestConstantNegativeDoubleZero);
    Float32 FloatMax DEFAULT_INITIALIZER(TestConstantFloatMax);
    Float64 DoubleMax DEFAULT_INITIALIZER(TestConstantDoubleMax);
    Float32 FloatSubnormal DEFAULT_INITIALIZER(TestConstantFloatSubnormal);
    Float64 DoubleSubnormal DEFAULT_INITIALIZER(TestConstantDoubleSubnormal);
    Float32 Infinity DEFAULT_INITIALIZER(TestConstantInfinity);
    Float64 NegativeInfinity DEFAULT_INITIALIZER(TestConstantNegativeInfinity);
    Float32 NotANumber DEFAULT_INITIALIZER(TestConstantNotANumber);
};
typedef struct TestConstantSnapshot TestConstantSnapshot;
#include "DefineRefMacro.h"
const TestConstantSnapshot REF DILIGENT_GLOBAL_FUNCTION(GetConstantSnapshot)();
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
