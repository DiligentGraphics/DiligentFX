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
#include "Structs.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

Uint32 DILIGENT_GLOBAL_FUNCTION(NextDefaultValue)(void);
Uint64 DILIGENT_GLOBAL_FUNCTION(WithDefaults)(Uint64 Value, Bool Add DEFAULT_VALUE(True), Uint32 Amount DEFAULT_VALUE(NextDefaultValue()));
Uint64 DILIGENT_GLOBAL_FUNCTION(DefaultPointer)(const MarshalDesc* pDesc DEFAULT_VALUE(nullptr));
#include "DefineRefMacro.h"
Uint64 DILIGENT_GLOBAL_FUNCTION(DefaultDescriptor)(const MarshalDesc REF Desc DEFAULT_VALUE(MarshalDesc{}));
void DILIGENT_GLOBAL_FUNCTION(DefaultOutput)(Uint64 REF Value, Bool Large DEFAULT_VALUE(True));
const MarshalDesc REF DILIGENT_GLOBAL_FUNCTION(DefaultBorrowedDescriptor)(const MarshalDesc REF Desc DEFAULT_VALUE(MarshalDesc{}));
#include "UndefRefMacro.h"
const Float32* DILIGENT_GLOBAL_FUNCTION(DefaultArrayCount)(Uint32 Count DEFAULT_VALUE(3));
Uint32 DILIGENT_GLOBAL_FUNCTION(DefaultStringLength)(const Char* Text DEFAULT_VALUE("native"));

// clang-format on
DILIGENT_END_NAMESPACE
