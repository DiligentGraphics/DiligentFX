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

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off
DILIGENT_TYPED_ENUM(TEST_STATUS, Int32)
{
    TEST_STATUS_FAILED = -1,
    TEST_STATUS_OK = 0,
    TEST_STATUS_PENDING = 3
};
DILIGENT_TYPED_ENUM(TEST_MODE, Uint8)
{
    TEST_MODE_FIRST = 0,
    TEST_MODE_LAST = 241
};

Int8 DILIGENT_GLOBAL_FUNCTION(EchoInt8)(Int8 Value);
Uint8 DILIGENT_GLOBAL_FUNCTION(EchoUint8)(Uint8 Value);
Int16 DILIGENT_GLOBAL_FUNCTION(EchoInt16)(Int16 Value);
Uint16 DILIGENT_GLOBAL_FUNCTION(EchoUint16)(Uint16 Value);
Int32 DILIGENT_GLOBAL_FUNCTION(EchoInt32)(Int32 Value);
Uint32 DILIGENT_GLOBAL_FUNCTION(EchoUint32)(Uint32 Value);
Int64 DILIGENT_GLOBAL_FUNCTION(EchoInt64)(Int64 Value);
Uint64 DILIGENT_GLOBAL_FUNCTION(EchoUint64)(Uint64 Value);
Float32 DILIGENT_GLOBAL_FUNCTION(EchoFloat)(Float32 Value);
Float64 DILIGENT_GLOBAL_FUNCTION(EchoDouble)(Float64 Value);
Bool DILIGENT_GLOBAL_FUNCTION(InvertBool)(Bool Value);
TEST_MODE DILIGENT_GLOBAL_FUNCTION(EchoMode)(TEST_MODE Value);
TEST_STATUS DILIGENT_GLOBAL_FUNCTION(EchoStatus)(TEST_STATUS Value);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetCallCount)();
void DILIGENT_GLOBAL_FUNCTION(ThrowNative)();
// clang-format on
DILIGENT_END_NAMESPACE
