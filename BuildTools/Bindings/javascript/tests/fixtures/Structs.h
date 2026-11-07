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

struct MarshalDesc
{
    Uint64 Size       DEFAULT_INITIALIZER(0xFEDCBA9876543210ull);
    Int32 Count       DEFAULT_INITIALIZER(-13);
    Float32 Scale     DEFAULT_INITIALIZER(1.25f);
    Float64 Weight    DEFAULT_INITIALIZER(-2.5);
    Bool Enabled      DEFAULT_INITIALIZER(True);
    MARSHAL_MODE Mode DEFAULT_INITIALIZER(MARSHAL_MODE_LAST);
};
typedef struct MarshalDesc MarshalDesc;

#include "DefineRefMacro.h"

Bool DILIGENT_GLOBAL_FUNCTION(CheckDefaultDesc)(const MarshalDesc REF Desc);
Bool DILIGENT_GLOBAL_FUNCTION(CheckCustomDesc)(const MarshalDesc REF Desc);

#include "UndefRefMacro.h"

// clang-format on

DILIGENT_END_NAMESPACE
