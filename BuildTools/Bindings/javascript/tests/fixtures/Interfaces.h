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
#include "Buffers.h"
#include "StringMarshalling.h"
#include "StructResults.h"
#include "InputArrays.h"
#include "OutputArrays.h"
#include "FixedArrays.h"
#include "DefaultArguments.h"
#include "Exports.h"
#include "InheritedDescriptors.h"
#include "StructPointers.h"
#include "InterfaceFields.h"
#include "BorrowedInterfaces.h"
#include "InterfaceReferences.h"
#include "ComputedOutputs.h"
#include "OutputBuffers.h"
#include "OutputParameters.h"
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IMarshalObject        IMarshalObject;
typedef struct IMarshalMutableObject IMarshalMutableObject;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalObject =
    {0x71234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};
static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalMutableObject =
    {0x81234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalObject
#include "DefineInterfaceHelperMacros.h"
#define IMarshalObjectInclusiveMethods \
    IObjectInclusiveMethods;           \
    IMarshalObjectMethods MarshalObject

// clang-format off
DILIGENT_BEGIN_INTERFACE(IMarshalObject, IObject)
{
    VIRTUAL Uint64 METHOD(GetSize)(THIS) CONST PURE;
    VIRTUAL Uint32 METHOD(GetRefCount)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalObject_GetSize(This) CALL_IFACE_METHOD(MarshalObject, GetSize, This)
#    define IMarshalObject_GetRefCount(This) CALL_IFACE_METHOD(MarshalObject, GetRefCount, This)
#endif
// clang-format on

#define DILIGENT_INTERFACE_NAME IMarshalMutableObject
#include "DefineInterfaceHelperMacros.h"
#define IMarshalMutableObjectInclusiveMethods \
    IMarshalObjectInclusiveMethods;           \
    IMarshalMutableObjectMethods MarshalMutableObject

// clang-format off
DILIGENT_BEGIN_INTERFACE(IMarshalMutableObject, IMarshalObject)
{
    VIRTUAL MARSHAL_STATUS METHOD(SetSize)(THIS_ Uint64 Size) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(CopySizeFrom)(THIS_ const IMarshalObject REF Source) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(CopySizeTo)(THIS_ IMarshalMutableObject REF Destination) CONST PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalMutableObject_SetSize(This, ...) CALL_IFACE_METHOD(MarshalMutableObject, SetSize, This, __VA_ARGS__)
#    define IMarshalMutableObject_CopySizeFrom(This, ...) CALL_IFACE_METHOD(MarshalMutableObject, CopySizeFrom, This, __VA_ARGS__)
#    define IMarshalMutableObject_CopySizeTo(This, ...) CALL_IFACE_METHOD(MarshalMutableObject, CopySizeTo, This, __VA_ARGS__)
#endif

#include "DefineRefMacro.h"

MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateMarshalObject)(
    const MarshalDesc REF Desc, MARSHAL_STATUS Status, Bool Mutable, IMarshalObject** ppObject);
Uint64 DILIGENT_GLOBAL_FUNCTION(GetObjectSize)(IMarshalObject* pObject);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveObjectCount)(void);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetDestructionCount)(void);

#include "UndefRefMacro.h"

// clang-format on
DILIGENT_END_NAMESPACE
