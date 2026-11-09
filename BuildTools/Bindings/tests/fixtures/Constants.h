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

/// A standalone pointer to a named, immutable string.
static const Char* const BindingTestPropertyNamePointer = (BindingTestPropertyName);

static DILIGENT_CONSTEXPR Char BindingTestEmptyName[] = "";
static DILIGENT_CONSTEXPR Char BindingTestEmbeddedNullName[] = "First\0Second";

// Exercise Clang's normalized string spelling, including concatenation and UTF-8.
static DILIGENT_CONSTEXPR Char BindingTestEscapedName[] = "Line\n\"Quoted\"\\Path";
static DILIGENT_CONSTEXPR Char BindingTestUnicodeName[] = u8"\u03c0";
static DILIGENT_CONSTEXPR Char BindingTestJoinedName[] = "Base" "Color";
static DILIGENT_CONSTEXPR Char BindingTestPaddedName[32] = "Padded";

/// Like RadientStandardMaterialTextureParameterNames, this groups existing names.
struct BindingTestParameterNames
{
    const Char* Property;
    const Char* Repeated;
    const Char* Empty;
    const Char* EmbeddedNull;
    const Char* Literal;
};
typedef struct BindingTestParameterNames BindingTestParameterNames;

static DILIGENT_CONSTEXPR BindingTestParameterNames BindingTestNames =
    {BindingTestPropertyName, BindingTestPropertyName, BindingTestEmptyName, BindingTestEmbeddedNullName, "Literal"};

static const Char* const BindingTestNameArray[] = {BindingTestPropertyName, BindingTestEmptyName};

// Unsupported address expressions must not silently become string values.
static const Char* const BindingTestNameOffset = BindingTestPropertyName + 1;
static const Char* const BindingTestAddress = (const Char*)1234;
static Char BindingTestMutableName[] = "Mutable";
static const Char* const BindingTestMutableNamePointer = BindingTestMutableName;

/// A schema identifier containing nested scalar and array values.
static DILIGENT_CONSTEXPR BindingTestSchemaID BindingTestSchema =
    {0xe4add320, 0xeeca, 0x439f, {0xa6, 0xc3, 0x1d, 0x8a, 0x25, 0xae, 0xfb, 0xc3}};

static DILIGENT_CONSTEXPR BindingTestSchemaID BindingTestZeroSchema = {0};

// Aggregate bases precede direct fields, including when braces are omitted.
struct BindingTestAggregateBase
{
    Uint32 A DEFAULT_INITIALIZER(5);
};
typedef struct BindingTestAggregateBase BindingTestAggregateBase;
struct BindingTestAggregateChild DILIGENT_DERIVE(BindingTestAggregateBase)
    Uint32 B DEFAULT_INITIALIZER(7);
};
typedef struct BindingTestAggregateChild BindingTestAggregateChild;
struct BindingTestAggregateLeaf DILIGENT_DERIVE(BindingTestAggregateChild)
    Uint32 C;
};
typedef struct BindingTestAggregateLeaf BindingTestAggregateLeaf;
struct BindingTestAggregateHolder
{
    BindingTestAggregateChild Child;
};
typedef struct BindingTestAggregateHolder BindingTestAggregateHolder;

static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInherited = {{11}, 13};
static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInheritedFlat = {11, 13};
static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInheritedDefault = {};
static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInheritedPartial = {{17}};
static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInheritedEmptyBase = {{}, 19};
static DILIGENT_CONSTEXPR BindingTestAggregateLeaf BindingTestInheritedLeaf = {{{23}, 29}, 31};
static DILIGENT_CONSTEXPR BindingTestAggregateLeaf BindingTestInheritedLeafDefault = {};
static DILIGENT_CONSTEXPR BindingTestAggregateHolder BindingTestInheritedNestedDefault = {};
static DILIGENT_CONSTEXPR BindingTestAggregateChild BindingTestInheritedArray[2] = {{{11}, 13}};

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
