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
#include "Interfaces.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

struct BindingTestLink
{
    const struct BindingTestLink* pNext DEFAULT_INITIALIZER(nullptr);
    BindingTestVector Value DEFAULT_INITIALIZER({});
};
typedef struct BindingTestLink BindingTestLink;

struct BindingTestEnvelope DILIGENT_DERIVE(BindingTestExtendedDesc)
    BindingTestVector Basis[2];
    const BindingTestLink* pLink DEFAULT_INITIALIZER(nullptr);
};
typedef struct BindingTestEnvelope BindingTestEnvelope;
typedef BindingTestEnvelope BindingTestEnvelopeAlias;

#include "DefineRefMacro.h"

void DILIGENT_GLOBAL_FUNCTION(UseBindingTestEnvelope)(const BindingTestEnvelopeAlias REF Desc);
const BindingTestVector REF DILIGENT_GLOBAL_FUNCTION(GetBindingTestVector)(void);
void DILIGENT_GLOBAL_FUNCTION(EditBindingTestVector)(BindingTestVector REF Value);

typedef const Float32 BindingTestConstFloat;
typedef const Char* BindingTestString;
typedef IBindingTestObject BindingTestObjectAlias;

// Direction depends on the immediate target, not the variable name or constness
// of the pointer itself. Interfaces represent existing input objects.
void DILIGENT_GLOBAL_FUNCTION(InferBindingTestDirections)(
    const Float32* pInput,
    Float32* pOutput,
    Float32* const pConstPointer,
    const Char** ppText,
    const Char* REF Text,
    const Char* const* ppInputText,
    BindingTestConstFloat* pAliasedInput,
    BindingTestString* pAliasedText,
    BindingTestObjectAlias* pObject,
    BindingTestObjectAlias REF Object,
    const BindingTestObjectAlias REF ConstObject,
    BindingTestObjectAlias** ppObject,
    IObject* pExternal,
    IObject REF External,
    void* pContext);

// Pointer return ownership is supplied by API conventions or explicit overrides.
IBindingTestObject* DILIGENT_GLOBAL_FUNCTION(GetBindingTestObject)(void);

// An unrelated signature must not need extraction support or metadata.
void DILIGENT_GLOBAL_FUNCTION(UnselectedLongDouble)(long double Value);

#include "UndefRefMacro.h"

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
