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
#include "Structs.h"
#include "InterfaceFields.h"
#include "OutputArrays.h"
#include "FixedDescriptorArrays.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

struct MarshalPointerDesc
{
    const MarshalDesc* pData DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalPointerDesc MarshalPointerDesc;

// This native helper supplies a non-null default without exposing storage to a binding.
#if DILIGENT_CPP_INTERFACE
const MarshalDesc* GetDefaultPointerData();
#endif

struct MarshalDefaultPointerDesc
{
    const MarshalDesc* pData DEFAULT_INITIALIZER(GetDefaultPointerData());
};
typedef struct MarshalDefaultPointerDesc MarshalDefaultPointerDesc;

struct MarshalRequiredPointerDesc
{
    const MarshalDesc* pData DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalRequiredPointerDesc MarshalRequiredPointerDesc;

struct MarshalNestedPointerDesc
{
    const MarshalPointerDesc* pNested DEFAULT_INITIALIZER(nullptr);
    const MarshalInterfaceArrayDesc* pInterfaces DEFAULT_INITIALIZER(nullptr);
    const MarshalArrayRoundTrip* pArrays DEFAULT_INITIALIZER(nullptr);
    const TestLargeFixedArray* pImage DEFAULT_INITIALIZER(nullptr);
    const Char* Label DEFAULT_INITIALIZER("pointer snapshot");
};
typedef struct MarshalNestedPointerDesc MarshalNestedPointerDesc;

#include "DefineRefMacro.h"
Uint64 DILIGENT_GLOBAL_FUNCTION(ReadStructPointer)(const MarshalDesc* pData);
Uint64 DILIGENT_GLOBAL_FUNCTION(ReadRequiredPointer)(const MarshalDesc* pData);
const MarshalDesc* DILIGENT_GLOBAL_FUNCTION(EchoStructPointer)(const MarshalDesc* pData);
const MarshalPointerDesc REF DILIGENT_GLOBAL_FUNCTION(EchoPointerDesc)(const MarshalPointerDesc REF Desc);
const MarshalDefaultPointerDesc REF DILIGENT_GLOBAL_FUNCTION(EchoDefaultPointerDesc)(const MarshalDefaultPointerDesc REF Desc);
const MarshalRequiredPointerDesc REF DILIGENT_GLOBAL_FUNCTION(EchoRequiredPointerDesc)(const MarshalRequiredPointerDesc REF Desc);
const MarshalRequiredPointerDesc REF DILIGENT_GLOBAL_FUNCTION(GetRequiredPointerSnapshot)(const MarshalPointerDesc REF Desc);
Float64 DILIGENT_GLOBAL_FUNCTION(ReadNestedPointerValue)(const MarshalNestedPointerDesc REF Desc, Uint32 Field);
const MarshalNestedPointerDesc REF DILIGENT_GLOBAL_FUNCTION(EchoNestedPointerDesc)(const MarshalNestedPointerDesc REF Desc, Bool InvalidLabel);
const MarshalNestedPointerDesc* DILIGENT_GLOBAL_FUNCTION(EchoNestedPointerArray)(const MarshalNestedPointerDesc* pItems, Uint32 Count);
#include "UndefRefMacro.h"

// clang-format on
DILIGENT_END_NAMESPACE
