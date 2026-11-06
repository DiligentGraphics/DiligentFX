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

#include "Enums.h"
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IBindingTestObject        IBindingTestObject;
typedef struct IBindingTestMutableObject IBindingTestMutableObject;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_BindingTestObject =
    {0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IBindingTestObject
#include "DefineInterfaceHelperMacros.h"

#define IBindingTestObjectInclusiveMethods \
    IObjectInclusiveMethods;               \
    IBindingTestObjectMethods BindingTestObject

// clang-format off

/// Reference-counted test interface.
DILIGENT_BEGIN_INTERFACE(IBindingTestObject, IObject)
{
    /// Returns the byte count.
    VIRTUAL Uint64 METHOD(GetSize)(THIS) CONST PURE;

    /// Returns borrowed storage through an output pointer.
    VIRTUAL BINDING_TEST_STATUS METHOD(Read)(THIS_
                                               const void** ppData) PURE;
};
DILIGENT_END_INTERFACE

#include "UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IBindingTestObject_GetSize(This)   CALL_IFACE_METHOD(BindingTestObject, GetSize, This)
#    define IBindingTestObject_Read(This, ...) CALL_IFACE_METHOD(BindingTestObject, Read, This, __VA_ARGS__)
#endif

// clang-format on

#define DILIGENT_INTERFACE_NAME IBindingTestMutableObject
#include "DefineInterfaceHelperMacros.h"

#define IBindingTestMutableObjectInclusiveMethods \
    IBindingTestObjectInclusiveMethods;           \
    IBindingTestMutableObjectMethods BindingTestMutableObject

// clang-format off

/// Test interface derived from another public interface.
DILIGENT_BEGIN_INTERFACE(IBindingTestMutableObject, IBindingTestObject)
{
    VIRTUAL BINDING_TEST_STATUS METHOD(Resize)(THIS_
                                                 Uint64 Size,
                                                 Bool PreserveData DEFAULT_VALUE(True),
                                                 Bool UpdateState DEFAULT_VALUE(False)) PURE;
};
DILIGENT_END_INTERFACE

#include "UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IBindingTestMutableObject_Resize(This, ...) CALL_IFACE_METHOD(BindingTestMutableObject, Resize, This, __VA_ARGS__)
#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
