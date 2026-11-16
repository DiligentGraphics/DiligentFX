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
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)
typedef struct ITestCounter ITestCounter;
#define DILIGENT_INTERFACE_NAME ITestCounter
#include "DefineInterfaceHelperMacros.h"
#define ITestCounterInclusiveMethods \
    IObjectInclusiveMethods;         \
    ITestCounterMethods TestCounter

// clang-format off
DILIGENT_BEGIN_INTERFACE(ITestCounter, IObject)
{
    VIRTUAL Uint64 METHOD(GetValue)(THIS) CONST PURE;
    VIRTUAL void METHOD(SetValue)(THIS_ Uint64 Value) PURE;
    VIRTUAL Uint32 METHOD(GetRefCount)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define ITestCounter_GetValue(This) CALL_IFACE_METHOD(TestCounter, GetValue, This)
#    define ITestCounter_SetValue(This, ...) CALL_IFACE_METHOD(TestCounter, SetValue, This, __VA_ARGS__)
#    define ITestCounter_GetRefCount(This) CALL_IFACE_METHOD(TestCounter, GetRefCount, This)
#endif
TEST_STATUS DILIGENT_GLOBAL_FUNCTION(CreateCounter)(Uint64 Value, TEST_STATUS Status, ITestCounter** ppCounter);
ITestCounter* DILIGENT_GLOBAL_FUNCTION(BorrowCounter)(ITestCounter* pCounter);
Uint64 DILIGENT_GLOBAL_FUNCTION(ReadCounter)(ITestCounter* pCounter);
#include "DefineRefMacro.h"
Uint64 DILIGENT_GLOBAL_FUNCTION(ReadCounterReference)(const ITestCounter REF Counter);
void DILIGENT_GLOBAL_FUNCTION(BorrowCounterOutput)(ITestCounter* pCounter, ITestCounter** ppCounter);
#include "UndefRefMacro.h"
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveCounterCount)();
void DILIGENT_GLOBAL_FUNCTION(FailingCounterOutputs)(ITestCounter** ppCounter, const Char** ppText);
// clang-format on
DILIGENT_END_NAMESPACE
