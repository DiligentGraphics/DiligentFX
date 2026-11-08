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
#include "NestedStructs.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off
struct MarshalArrayItem
{
    Int32 Id DEFAULT_INITIALIZER(-7);
    const Char* Name DEFAULT_INITIALIZER("item default");
    MarshalNestedTransform Transform DEFAULT_INITIALIZER({});
    Uint32 ValueCount DEFAULT_INITIALIZER(2);
    const Float32* pValues DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalArrayItem MarshalArrayItem;
typedef struct MarshalArrayItem MarshalArrayItemAlias;

struct MarshalArrayDesc
{
    Uint32 ItemCount DEFAULT_INITIALIZER(2);
    const MarshalArrayItemAlias* pItems DEFAULT_INITIALIZER(nullptr);
    Uint64 SampleCount DEFAULT_INITIALIZER(3);
    const Uint16* pSamples DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalArrayDesc MarshalArrayDesc;

struct MarshalNestedCountDesc
{
    const Uint32* pValues DEFAULT_INITIALIZER(nullptr);
    MarshalArrayDesc Counts DEFAULT_INITIALIZER({});
    const Uint32* pPair DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalNestedCountDesc MarshalNestedCountDesc;

#include "DefineRefMacro.h"
Uint32 DILIGENT_GLOBAL_FUNCTION(SumNestedCount)(const MarshalNestedCountDesc REF Desc);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayInt8)(const Int8* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayUint8)(const Uint8* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayInt16)(const Int16* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayUint16)(const Uint16* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayInt32)(const Int32* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayUint32)(const Uint32* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayInt64)(const Int64* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayUint64)(const Uint64* pValues, Uint64 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayFloat32)(const Float32* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayFloat64)(const Float64* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayBool)(const Bool* pValues, Uint32 Count);
Bool DILIGENT_GLOBAL_FUNCTION(CheckArrayMode)(const MARSHAL_MODE* pValues, Uint32 Count);
Float64 DILIGENT_GLOBAL_FUNCTION(ReadArrayItem)(const MarshalArrayItem* pItems, Uint32 Count, Uint32 Index, Uint32 Field);
const Char* DILIGENT_GLOBAL_FUNCTION(GetArrayItemName)(const MarshalArrayItem* pItems, Uint32 Count, Uint32 Index);
Float64 DILIGENT_GLOBAL_FUNCTION(ReadArrayDesc)(const MarshalArrayDesc REF Desc, Uint32 Index, Uint32 Field);
Uint64 DILIGENT_GLOBAL_FUNCTION(SumArraySamples)(const MarshalArrayDesc REF Desc);
Int32 DILIGENT_GLOBAL_FUNCTION(ReadArrayGroup)(const MarshalArrayDesc* pGroups, Uint32 Count, Uint32 Group, Uint32 Item);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetArrayCallCount)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
