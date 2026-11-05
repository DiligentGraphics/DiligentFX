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

typedef Uint64 BindingTestSize;

/// Descriptor documentation.
struct BindingTestDesc
{
    /// Number of input bytes.
    BindingTestSize Size DEFAULT_INITIALIZER(0);
    const void* pData    DEFAULT_INITIALIZER(nullptr);
    Float32 Offset[2]   DEFAULT_INITIALIZER({1.f, 2.f});
    Bool Enabled        DEFAULT_INITIALIZER(False);
};
typedef struct BindingTestDesc BindingTestDesc;

/// A descriptor derived through Diligent's C-compatible macro.
struct BindingTestExtendedDesc DILIGENT_DERIVE(BindingTestDesc)
    Uint32 Extra DEFAULT_INITIALIZER(7);
};
typedef struct BindingTestExtendedDesc BindingTestExtendedDesc;

/// Vector with C++ convenience helpers, like the public Radient math types.
struct BindingTestVector
{
    Float32 Values[2] DEFAULT_INITIALIZER({0.f, 0.f});

#if DILIGENT_CPP_INTERFACE
    constexpr BindingTestVector() noexcept = default;
    constexpr BindingTestVector(Float32 X, Float32 Y) noexcept : Values{X, Y} {}

    constexpr bool operator==(const BindingTestVector& Rhs) const
    {
        return Values[0] == Rhs.Values[0] && Values[1] == Rhs.Values[1];
    }

    Float32& operator[](Uint32 Index) { return Values[Index]; }
    const Float32& operator[](Uint32 Index) const { return Values[Index]; }
#endif
};
typedef struct BindingTestVector BindingTestVector;

typedef void (*BindingTestCallbackType)(Uint64 Value, void* pUserData);

/// Descriptor containing a callback and its opaque context.
struct BindingTestCallbackDesc
{
    BindingTestCallbackType OnEvent DEFAULT_INITIALIZER(nullptr);
    void* pUserData                 DEFAULT_INITIALIZER(nullptr);
};
typedef struct BindingTestCallbackDesc BindingTestCallbackDesc;

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
