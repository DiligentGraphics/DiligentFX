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

typedef Uint64 MarshalEntityID;
typedef MarshalEntityID MarshalEntityAlias;
typedef MarshalDesc MarshalDescriptorAlias;
typedef const Char* MarshalPropertyName;
static DILIGENT_CONSTEXPR MarshalEntityID MarshalInvalidID = ~0ull;
static DILIGENT_CONSTEXPR Uint32 MarshalAutoOffset = ~0u;
static DILIGENT_CONSTEXPR Int64 MarshalNegativeID = -0x7123456789abcdefll;
static DILIGENT_CONSTEXPR Float32 MarshalDefaultScale = 1.25f;
static DILIGENT_CONSTEXPR Bool MarshalDefaultEnabled = True;
static DILIGENT_CONSTEXPR Char MarshalAnimationName[] = "Translation";
static const Char* const MarshalAnimationNamePointer = MarshalAnimationName;
static DILIGENT_CONSTEXPR Uint64 MarshalLookup[] = {0, 0xfedcba9876543210ull};
struct MarshalExportNames
{
    const Char* Name;
    Uint64 Invalid;
    Uint32 Parts[2];
};
typedef struct MarshalExportNames MarshalExportNames;
static DILIGENT_CONSTEXPR MarshalExportNames MarshalNames = {MarshalAnimationName, ~0ull, {3, 5}};
MarshalEntityAlias DILIGENT_GLOBAL_FUNCTION(EchoEntityID)(MarshalEntityAlias Value);
MarshalEntityID DILIGENT_GLOBAL_FUNCTION(GetInvalidEntityID)(void);

// clang-format on
DILIGENT_END_NAMESPACE
