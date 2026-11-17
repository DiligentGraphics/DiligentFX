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

// clang-format off
struct MarshalObjectAttribs
{
    Uint32 Value DEFAULT_INITIALIZER(7);
    const Char* Name DEFAULT_INITIALIZER("base default");
};
typedef struct MarshalObjectAttribs MarshalObjectAttribs;

struct MarshalObjectDesc DILIGENT_DERIVE(MarshalObjectAttribs)
    Uint64 Size DEFAULT_INITIALIZER(0xfedcba9876543210ull);
#if DILIGENT_CPP_INTERFACE
    MarshalObjectDesc()
    {
        Value = 29;
        Name = "derived default";
    }
#endif
};
typedef struct MarshalObjectDesc MarshalObjectDesc;

typedef struct IMarshalBaseOverrides IMarshalBaseOverrides;
typedef struct IMarshalDerivedOverrides IMarshalDerivedOverrides;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalBaseOverrides =
    {0xa1234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};
static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalDerivedOverrides =
    {0xb1234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalBaseOverrides
#include "DefineInterfaceHelperMacros.h"
#define IMarshalBaseOverridesInclusiveMethods \
    IObjectInclusiveMethods; \
    IMarshalBaseOverridesMethods MarshalBaseOverrides

DILIGENT_BEGIN_INTERFACE(IMarshalBaseOverrides, IObject)
{
    VIRTUAL const MarshalObjectAttribs REF METHOD(GetDesc)(THIS) CONST PURE;
    VIRTUAL void METHOD(GetValue)(THIS_ Uint32 REF Value) CONST PURE;
    VIRTUAL MARSHAL_STATUS METHOD(GetValues)(THIS_ Uint32 REF Value, Uint64 REF Size) CONST PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalBaseOverrides_GetDesc(This) CALL_IFACE_METHOD(MarshalBaseOverrides, GetDesc, This)
#    define IMarshalBaseOverrides_GetValue(This, ...) CALL_IFACE_METHOD(MarshalBaseOverrides, GetValue, This, __VA_ARGS__)
#    define IMarshalBaseOverrides_GetValues(This, ...) CALL_IFACE_METHOD(MarshalBaseOverrides, GetValues, This, __VA_ARGS__)
#endif

#define DILIGENT_INTERFACE_NAME IMarshalDerivedOverrides
#include "DefineInterfaceHelperMacros.h"
#define IMarshalDerivedOverridesInclusiveMethods \
    IMarshalBaseOverridesInclusiveMethods; \
    IMarshalDerivedOverridesMethods MarshalDerivedOverrides

DILIGENT_BEGIN_INTERFACE(IMarshalDerivedOverrides, IMarshalBaseOverrides)
{
#if DILIGENT_CPP_INTERFACE
    // Match Core interfaces such as IBuffer: covariant overrides reuse the C vtable slot.
    virtual const MarshalObjectDesc& METHOD(GetDesc)() const override = 0;
    virtual void METHOD(GetValue)(Uint32& Value) const override = 0;
    virtual MARSHAL_STATUS METHOD(GetValues)(Uint32& Value, Uint64& Size) const override = 0;
#endif
    VIRTUAL void METHOD(SetValue)(THIS_ Uint32 Value) PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalDerivedOverrides_GetDesc(This) ((const MarshalObjectDesc*)IMarshalBaseOverrides_GetDesc(This))
#    define IMarshalDerivedOverrides_GetValue(This, ...) IMarshalBaseOverrides_GetValue(This, __VA_ARGS__)
#    define IMarshalDerivedOverrides_GetValues(This, ...) IMarshalBaseOverrides_GetValues(This, __VA_ARGS__)
#    define IMarshalDerivedOverrides_SetValue(This, ...) CALL_IFACE_METHOD(MarshalDerivedOverrides, SetValue, This, __VA_ARGS__)
#endif

#include "DefineRefMacro.h"
void DILIGENT_GLOBAL_FUNCTION(CreateMarshalOverrides)(IMarshalBaseOverrides** ppObject);
const MarshalObjectDesc REF DILIGENT_GLOBAL_FUNCTION(EchoOverrideDesc)(const MarshalObjectDesc REF Desc);
Uint32 DILIGENT_GLOBAL_FUNCTION(ReadOverrideBase)(const MarshalObjectAttribs REF Desc);
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
