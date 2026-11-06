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

#include "Typedefs.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Invalid identifier, using every bit of the unsigned 64-bit type.
static DILIGENT_CONSTEXPR BindingTestEntityID BindingTestInvalidEntity = (Uint64)~0ull;

/// Signed error value.
static DILIGENT_CONSTEXPR Int32 BindingTestError = -7;

static DILIGENT_CONSTEXPR Uint32 BindingTestMask = (1u << 3) | (1u << 7);
static DILIGENT_CONSTEXPR Bool BindingTestEnabled = True;
static DILIGENT_CONSTEXPR Float32 BindingTestScale = 1.5f;

// Like RADIENT_VERTEX_AUTO_OFFSET, a public constant can use a C macro.
#define DILIGENT_BINDING_TEST_AUTO_OFFSET 0xFFFFFFFFU
static DILIGENT_CONSTEXPR Uint32 BindingTestAutoOffset = DILIGENT_BINDING_TEST_AUTO_OFFSET;

/// Canonical property name.
static DILIGENT_CONSTEXPR Char BindingTestPropertyName[] = "Translation";

/// A schema identifier containing nested scalar and array values.
static DILIGENT_CONSTEXPR BindingTestSchemaID BindingTestSchema =
    {0xe4add320, 0xeeca, 0x439f, {0xa6, 0xc3, 0x1d, 0x8a, 0x25, 0xae, 0xfb, 0xc3}};

static DILIGENT_CONSTEXPR BindingTestSchemaID BindingTestZeroSchema = {0};

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
