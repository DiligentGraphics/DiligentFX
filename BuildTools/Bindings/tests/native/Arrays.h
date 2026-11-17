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

DILIGENT_BEGIN_NAMESPACE(Diligent)
// clang-format off
Int64 DILIGENT_GLOBAL_FUNCTION(SumValues)(const Int32* pValues, Uint32 Count);
Uint64 DILIGENT_GLOBAL_FUNCTION(SumBytes)(const Uint8* pValues, Uint32 Count);
const Int32* DILIGENT_GLOBAL_FUNCTION(EchoValues)(const Int32* pValues, Uint32 Count);
const Bool* DILIGENT_GLOBAL_FUNCTION(EchoBools)(const Bool* pValues, Uint32 Count);
const Float64* DILIGENT_GLOBAL_FUNCTION(EchoDoubles)(const Float64* pValues, Uint32 Count);
const Uint64* DILIGENT_GLOBAL_FUNCTION(EchoWideValues)(const Uint64* pValues, Uint32 Count);
const Int8* DILIGENT_GLOBAL_FUNCTION(EchoSignedBytes)(const Int8* pValues, Uint32 Count);
const Float32* DILIGENT_GLOBAL_FUNCTION(EchoFloats)(const Float32* pValues, Uint32 Count);
const TEST_MODE* DILIGENT_GLOBAL_FUNCTION(EchoModes)(const TEST_MODE* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(HasRequiredArrayStorage)(const Int32* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(IsNullArray)(const Int32* pValues, Uint32 Count);
const Int32* DILIGENT_GLOBAL_FUNCTION(EchoOptionalValues)(const Int32* pValues, Uint32 Count);
const Int32* DILIGENT_GLOBAL_FUNCTION(GetNullArray)(Uint32 Count);
TEST_STATUS DILIGENT_GLOBAL_FUNCTION(FillCapacityValues)(Uint32 Capacity, Bool Partial, Uint32* pValues);
#include "DefineRefMacro.h"
TEST_STATUS DILIGENT_GLOBAL_FUNCTION(FillValues)(Uint32 Capacity, Uint16* pValues, Uint32 REF Written);
void DILIGENT_GLOBAL_FUNCTION(InvalidWrittenCount)(Uint32 Capacity, Uint16* pValues, Uint32 REF Written);
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
