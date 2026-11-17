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

#include "Structs.h"
#include "NestedStructs.h"
#include "StringMarshalling.h"
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off
struct MarshalSnapshotChild
{
    Uint32 Sequence DEFAULT_INITIALIZER(0xfedcba98u);
    const Char* Label DEFAULT_INITIALIZER(nullptr);
    MarshalNestedVector Offset DEFAULT_INITIALIZER((MarshalNestedVector{1.5f, -2.5f, 3.25f}));
};
typedef struct MarshalSnapshotChild MarshalSnapshotChild;

struct MarshalSnapshotDesc
{
    const Uint32 Tag DEFAULT_INITIALIZER(41);
    const Char* Name DEFAULT_INITIALIZER(nullptr);
    MarshalDesc Values DEFAULT_INITIALIZER({});
    MarshalSnapshotChild Child DEFAULT_INITIALIZER({});
};
typedef struct MarshalSnapshotDesc MarshalSnapshotDesc;
typedef struct IMarshalSnapshots IMarshalSnapshots;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalSnapshots =
    {0xb1234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalSnapshots
#include "DefineInterfaceHelperMacros.h"
#define IMarshalSnapshotsInclusiveMethods \
    IObjectInclusiveMethods; \
    IMarshalSnapshotsMethods MarshalSnapshots

DILIGENT_BEGIN_INTERFACE(IMarshalSnapshots, IObject)
{
    VIRTUAL const MarshalSnapshotDesc REF METHOD(GetDesc)(THIS) CONST PURE;
    VIRTUAL const MarshalSnapshotDesc REF METHOD(GetDescAfterGrowingMemory)(THIS) CONST PURE;
    VIRTUAL void METHOD(Update)(THIS_ Int32 Count, const Char* Name, const Char* Label) PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalSnapshots_GetDesc(This) CALL_IFACE_METHOD(MarshalSnapshots, GetDesc, This)
#    define IMarshalSnapshots_GetDescAfterGrowingMemory(This) CALL_IFACE_METHOD(MarshalSnapshots, GetDescAfterGrowingMemory, This)
#    define IMarshalSnapshots_Update(This, ...) CALL_IFACE_METHOD(MarshalSnapshots, Update, This, __VA_ARGS__)
#endif

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateMarshalSnapshots)(IMarshalSnapshots** ppObject);
const MarshalDesc REF DILIGENT_GLOBAL_FUNCTION(GetStaticDescriptor)(void);
const MarshalStringDesc REF DILIGENT_GLOBAL_FUNCTION(EchoStringDescriptor)(const MarshalStringDesc REF Desc, Bool InvalidUTF8);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveSnapshotCount)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
