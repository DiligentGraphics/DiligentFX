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
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off
struct MarshalOutputArrayItem
{
    Uint32 Id DEFAULT_INITIALIZER(7);
    const Char* Label DEFAULT_INITIALIZER("item default");
    MarshalNestedTransform Transform DEFAULT_INITIALIZER({});
    const Float32* pSamples DEFAULT_INITIALIZER(nullptr);
    Uint32 SampleCount DEFAULT_INITIALIZER(0);
};
typedef struct MarshalOutputArrayItem MarshalOutputArrayItem;
typedef struct MarshalOutputArrayItem MarshalOutputArrayItemAlias;

struct MarshalOutputArrayGroup
{
    const MarshalOutputArrayItemAlias* pItems DEFAULT_INITIALIZER(nullptr);
    Uint32 ItemCount DEFAULT_INITIALIZER(0);
};
typedef struct MarshalOutputArrayGroup MarshalOutputArrayGroup;

struct MarshalOutputArrayDesc
{
    const Int8* pInt8 DEFAULT_INITIALIZER(nullptr);
    const Uint8* pUint8 DEFAULT_INITIALIZER(nullptr);
    const Int16* pInt16 DEFAULT_INITIALIZER(nullptr);
    const Uint16* pUint16 DEFAULT_INITIALIZER(nullptr);
    const Int32* pInt32 DEFAULT_INITIALIZER(nullptr);
    const Uint32* pUint32 DEFAULT_INITIALIZER(nullptr);
    const Int64* pInt64 DEFAULT_INITIALIZER(nullptr);
    const Uint64* pUint64 DEFAULT_INITIALIZER(nullptr);
    const Float32* pFloat32 DEFAULT_INITIALIZER(nullptr);
    const Float64* pFloat64 DEFAULT_INITIALIZER(nullptr);
    const Bool* pBool DEFAULT_INITIALIZER(nullptr);
    const MARSHAL_MODE* pMARSHAL_MODE DEFAULT_INITIALIZER(nullptr);
    const Char* pChar DEFAULT_INITIALIZER(nullptr);
    Uint32 Count DEFAULT_INITIALIZER(3);
    Uint64 WideCount DEFAULT_INITIALIZER(3);
    const MarshalOutputArrayGroup* pGroups DEFAULT_INITIALIZER(nullptr);
    Uint32 GroupCount DEFAULT_INITIALIZER(0);
};
typedef struct MarshalOutputArrayDesc MarshalOutputArrayDesc;

struct MarshalArrayRoundTrip
{
    const Char* Name DEFAULT_INITIALIZER("round trip");
    const MarshalOutputArrayItem* pItems DEFAULT_INITIALIZER(nullptr);
    Uint32 ItemCount DEFAULT_INITIALIZER(0);
    const Float64* pValues DEFAULT_INITIALIZER(nullptr);
    Uint64 ValueCount DEFAULT_INITIALIZER(0);
};
typedef struct MarshalArrayRoundTrip MarshalArrayRoundTrip;

typedef struct IMarshalArraySnapshots IMarshalArraySnapshots;
static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalArraySnapshots =
    {0xd1234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalArraySnapshots
#include "DefineInterfaceHelperMacros.h"
#define IMarshalArraySnapshotsInclusiveMethods \
    IObjectInclusiveMethods; \
    IMarshalArraySnapshotsMethods MarshalArraySnapshots

DILIGENT_BEGIN_INTERFACE(IMarshalArraySnapshots, IObject)
{
    VIRTUAL const MarshalOutputArrayDesc REF METHOD(GetDesc)(THIS) CONST PURE;
    VIRTUAL const MarshalOutputArrayDesc REF METHOD(GetDescAfterGrowingMemory)(THIS) CONST PURE;
    VIRTUAL const Float32* METHOD(GetValues)(THIS_ Uint32 Count) CONST PURE;
    VIRTUAL const MarshalOutputArrayItem* METHOD(GetItems)(THIS_ Uint32 Count) CONST PURE;
    VIRTUAL Uint64 METHOD(GetValueCount)(THIS) CONST PURE;
    VIRTUAL Uint32 METHOD(GetItemCount)(THIS) CONST PURE;
    VIRTUAL const Float32* METHOD(GetAllValues)(THIS) CONST PURE;
    VIRTUAL const MarshalOutputArrayItem* METHOD(GetAllItems)(THIS) CONST PURE;
    VIRTUAL void METHOD(SetValueCount)(THIS_ Uint64 Count) PURE;
    VIRTUAL void METHOD(Update)(THIS) PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalArraySnapshots_GetDesc(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetDesc, This)
#    define IMarshalArraySnapshots_GetDescAfterGrowingMemory(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetDescAfterGrowingMemory, This)
#    define IMarshalArraySnapshots_GetValues(This, ...) CALL_IFACE_METHOD(MarshalArraySnapshots, GetValues, This, __VA_ARGS__)
#    define IMarshalArraySnapshots_GetItems(This, ...) CALL_IFACE_METHOD(MarshalArraySnapshots, GetItems, This, __VA_ARGS__)
#    define IMarshalArraySnapshots_GetValueCount(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetValueCount, This)
#    define IMarshalArraySnapshots_GetItemCount(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetItemCount, This)
#    define IMarshalArraySnapshots_GetAllValues(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetAllValues, This)
#    define IMarshalArraySnapshots_GetAllItems(This) CALL_IFACE_METHOD(MarshalArraySnapshots, GetAllItems, This)
#    define IMarshalArraySnapshots_SetValueCount(This, ...) CALL_IFACE_METHOD(MarshalArraySnapshots, SetValueCount, This, __VA_ARGS__)
#    define IMarshalArraySnapshots_Update(This) CALL_IFACE_METHOD(MarshalArraySnapshots, Update, This)
#endif

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateMarshalArraySnapshots)(IMarshalArraySnapshots** ppObject);
const MarshalArrayRoundTrip REF DILIGENT_GLOBAL_FUNCTION(EchoArrayDescriptor)(const MarshalArrayRoundTrip REF Desc, Bool InvalidUTF8);
const MarshalArrayRoundTrip REF DILIGENT_GLOBAL_FUNCTION(GetInvalidOutputArray)(Uint32 Case);
const Float64* DILIGENT_GLOBAL_FUNCTION(GetStaticOutputArray)(Uint64 Count);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveArraySnapshotCount)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
