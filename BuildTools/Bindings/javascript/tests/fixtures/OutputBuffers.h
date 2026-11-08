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
#include "OutputParameters.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(FillIds)(Uint32 Capacity, Uint64* pIds, Uint32 REF Written, Bool InvalidCount, Bool Grow);
void DILIGENT_GLOBAL_FUNCTION(FillRecords)(Uint32 Capacity, MarshalOutputRecord* pRecords);
void DILIGENT_GLOBAL_FUNCTION(FillMultiple)(Uint32 REF Count, const Char** ppLabel);
#include "UndefRefMacro.h"

// clang-format on
DILIGENT_END_NAMESPACE
