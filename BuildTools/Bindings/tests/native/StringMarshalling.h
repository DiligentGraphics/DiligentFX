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

struct MarshalStringDesc
{
    const Char* Name  DEFAULT_INITIALIZER("native default");
    const char* Label DEFAULT_INITIALIZER(nullptr);
    Bool Enabled      DEFAULT_INITIALIZER(True);
};
typedef struct MarshalStringDesc MarshalStringDesc;
typedef struct IMarshalStrings   IMarshalStrings;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalStrings =
    {0xa1234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalStrings
#include "DefineInterfaceHelperMacros.h"
#define IMarshalStringsInclusiveMethods \
    IObjectInclusiveMethods;            \
    IMarshalStringsMethods MarshalStrings

// clang-format off
DILIGENT_BEGIN_INTERFACE(IMarshalStrings, IObject)
{
    VIRTUAL void METHOD(SetText)(THIS_ const Char* Text) PURE;
    VIRTUAL const Char* METHOD(GetText)(THIS) CONST PURE;
    VIRTUAL MARSHAL_STATUS METHOD(ReadText)(THIS_ Bool Fail, const Char* REF Text) CONST PURE;
    VIRTUAL MARSHAL_STATUS METHOD(ReadTextPointer)(THIS_ Bool Fail, const Char** ppText) CONST PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalStrings_SetText(This, ...) CALL_IFACE_METHOD(MarshalStrings, SetText, This, __VA_ARGS__)
#    define IMarshalStrings_GetText(This) CALL_IFACE_METHOD(MarshalStrings, GetText, This)
#    define IMarshalStrings_ReadText(This, ...) CALL_IFACE_METHOD(MarshalStrings, ReadText, This, __VA_ARGS__)
#    define IMarshalStrings_ReadTextPointer(This, ...) CALL_IFACE_METHOD(MarshalStrings, ReadTextPointer, This, __VA_ARGS__)
#endif

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateMarshalStrings)(const Char* Text, IMarshalStrings** ppObject);
const Char* DILIGENT_GLOBAL_FUNCTION(GetStringDescName)(const MarshalStringDesc REF Desc);
const char* DILIGENT_GLOBAL_FUNCTION(GetStringDescLabel)(const MarshalStringDesc REF Desc);
const Char* DILIGENT_GLOBAL_FUNCTION(EchoString)(const Char* Text, Bool GrowHeap);
const char* DILIGENT_GLOBAL_FUNCTION(EchoNullableString)(const char* Text);
Bool DILIGENT_GLOBAL_FUNCTION(CheckStringBytes)(const Char* Text);
Bool DILIGENT_GLOBAL_FUNCTION(CheckCharacterBytes)(Uint32 ByteCount, const Char* Bytes);
const Char* DILIGENT_GLOBAL_FUNCTION(GetStaticString)(void);
const Char* DILIGENT_GLOBAL_FUNCTION(GetInvalidUTF8)(void);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetStringCallCount)(void);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveStringCount)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
