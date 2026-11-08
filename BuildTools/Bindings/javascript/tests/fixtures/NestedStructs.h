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

#include "BasicTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

struct MarshalNestedVector
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
};
typedef struct MarshalNestedVector MarshalNestedVector;
typedef struct MarshalNestedVector MarshalNestedVectorAlias;

struct MarshalNestedTransform
{
    MarshalNestedVector Position DEFAULT_INITIALIZER({});
    MarshalNestedVectorAlias Scale DEFAULT_INITIALIZER((MarshalNestedVector{1.f, 1.f, 1.f}));
};
typedef struct MarshalNestedTransform MarshalNestedTransform;

struct MarshalNestedData
{
    const Char* Name DEFAULT_INITIALIZER("child default");
    Uint32 ByteCount DEFAULT_INITIALIZER(2);
    const Uint8* pBytes DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalNestedData MarshalNestedData;

struct MarshalNestedDesc
{
    Uint32 Tag DEFAULT_INITIALIZER(17);
    MarshalNestedTransform Transform DEFAULT_INITIALIZER({});
    // The same child types have different defaults in this parent field.
    MarshalNestedTransform Alternate DEFAULT_INITIALIZER(
        (MarshalNestedTransform{MarshalNestedVector{4.f, 5.f, 6.f}, MarshalNestedVector{2.f, 3.f, 4.f}}));
    MarshalNestedData Data DEFAULT_INITIALIZER((MarshalNestedData{"parent default", 3, nullptr}));
};
typedef struct MarshalNestedDesc MarshalNestedDesc;

#include "DefineRefMacro.h"

Float32 DILIGENT_GLOBAL_FUNCTION(ReadNestedValue)(const MarshalNestedDesc REF Desc, Uint32 Index);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedTag)(const MarshalNestedDesc REF Desc);
const Char* DILIGENT_GLOBAL_FUNCTION(GetNestedName)(const MarshalNestedDesc REF Desc);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedChecksum)(const MarshalNestedDesc REF Desc);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedCallCount)(void);

#include "UndefRefMacro.h"

// clang-format on

DILIGENT_END_NAMESPACE
