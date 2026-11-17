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

struct MarshalOutputRecord
{
    Uint64 Value DEFAULT_INITIALIZER(17);
    Float32 Data[4] DEFAULT_INITIALIZER({1, 0, 0, 1});
    const Char* Label DEFAULT_INITIALIZER("default");
};
typedef struct MarshalOutputRecord MarshalOutputRecord;
#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(ReadReferenceOutput)(MARSHAL_STATUS Status, Uint64 REF Value);
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(ReadRecordOutput)(Bool Write, Bool Grow, Bool InvalidText, MarshalOutputRecord REF Value);
void DILIGENT_GLOBAL_FUNCTION(MakeRecordOutput)(MarshalOutputRecord* pValue);
#include "UndefRefMacro.h"

// clang-format on
DILIGENT_END_NAMESPACE
