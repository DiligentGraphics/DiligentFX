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

typedef struct IMarshalObject IMarshalObject;
struct MarshalInterfaceDesc
{
    IMarshalObject* pObject DEFAULT_INITIALIZER(nullptr);
    Uint32 Count DEFAULT_INITIALIZER(0);
    IMarshalObject* const* ppObjects DEFAULT_INITIALIZER(nullptr);
    const Char* Label DEFAULT_INITIALIZER("interfaces");
};
typedef struct MarshalInterfaceDesc MarshalInterfaceDesc;
struct MarshalInterfaceGroup
{
    MarshalInterfaceDesc Primary;
    MarshalInterfaceDesc Entries[2];
    const Char* Label DEFAULT_INITIALIZER("interface group");
};
typedef struct MarshalInterfaceGroup MarshalInterfaceGroup;

struct MarshalRequiredInterfaceDesc
{
    IMarshalObject* pObject DEFAULT_INITIALIZER(nullptr);
    // The pointer intentionally precedes its count.
    IMarshalObject* const* ppObjects DEFAULT_INITIALIZER(nullptr);
    Uint32 Count DEFAULT_INITIALIZER(0);
};
typedef struct MarshalRequiredInterfaceDesc MarshalRequiredInterfaceDesc;

struct MarshalInterfaceArrayDesc
{
    const MarshalInterfaceDesc* pItems DEFAULT_INITIALIZER(nullptr);
    Uint32 Count DEFAULT_INITIALIZER(0);
    const Char* Label DEFAULT_INITIALIZER("interface array");
};
typedef struct MarshalInterfaceArrayDesc MarshalInterfaceArrayDesc;

#include "DefineRefMacro.h"
Uint64 DILIGENT_GLOBAL_FUNCTION(SumInterfaces)(const MarshalInterfaceDesc REF Desc);
const MarshalInterfaceDesc REF DILIGENT_GLOBAL_FUNCTION(EchoInterfaces)(const MarshalInterfaceDesc REF Desc);
const MarshalInterfaceDesc REF DILIGENT_GLOBAL_FUNCTION(GetInvalidInterfaceSnapshot)(const MarshalInterfaceDesc REF Desc);
Uint64 DILIGENT_GLOBAL_FUNCTION(SumInterfaceArray)(IMarshalObject* const* ppObjects, Uint32 Count);
IMarshalObject* const* DILIGENT_GLOBAL_FUNCTION(EchoInterfaceArray)(IMarshalObject* const* ppObjects, Uint32 Count);
IMarshalObject* const* DILIGENT_GLOBAL_FUNCTION(GetRequiredInterfaceArray)(IMarshalObject* const* ppObjects, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(InterfaceArrayHasStorage)(IMarshalObject* const* ppObjects, Uint32 Count);
const MarshalInterfaceGroup REF DILIGENT_GLOBAL_FUNCTION(EchoInterfaceGroup)(const MarshalInterfaceGroup REF Group, Bool InvalidLabel);
const MarshalRequiredInterfaceDesc REF DILIGENT_GLOBAL_FUNCTION(EchoRequiredInterfaces)(const MarshalRequiredInterfaceDesc REF Desc);
const MarshalRequiredInterfaceDesc REF DILIGENT_GLOBAL_FUNCTION(GetRequiredInterfaceSnapshot)(const MarshalInterfaceDesc REF Desc);
void DILIGENT_GLOBAL_FUNCTION(CopyInterfaceArray)(IMarshalObject* const* ppObjects, Uint32 Count, IMarshalObject** ppOutput, Uint32 Capacity, Uint32 REF Written);
Uint64 DILIGENT_GLOBAL_FUNCTION(SumInterfaceDescriptors)(const MarshalInterfaceDesc* pItems, Uint32 Count);
const MarshalInterfaceArrayDesc REF DILIGENT_GLOBAL_FUNCTION(EchoInterfaceDescriptors)(const MarshalInterfaceArrayDesc REF Desc, Bool InvalidLabel);
void DILIGENT_GLOBAL_FUNCTION(FillInterfaceDescriptors)(const MarshalInterfaceDesc REF Source, Uint32 Capacity, MarshalInterfaceDesc* pItems, Uint32 REF Written, Bool InvalidCount);
#include "UndefRefMacro.h"

// clang-format on
DILIGENT_END_NAMESPACE
