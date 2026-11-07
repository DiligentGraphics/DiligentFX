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
#include "FlagEnum.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Status returned by the test API.
DILIGENT_TYPED_ENUM(BINDING_TEST_STATUS, Int32)
{
    /// The operation failed.
    BINDING_TEST_STATUS_FAILED  = -1,
    /// The operation succeeded.
    BINDING_TEST_STATUS_OK      = 0,
    /// The operation is pending.
    BINDING_TEST_STATUS_PENDING = 3
};

/// Storage mode with an eight-bit underlying type.
DILIGENT_TYPED_ENUM(BINDING_TEST_MODE, Uint8)
{
    BINDING_TEST_MODE_COPY = 0,
    BINDING_TEST_MODE_REFERENCE,
    BINDING_TEST_MODE_HIGH = 241
};

/// Combinable access flags.
DILIGENT_TYPED_ENUM(BINDING_TEST_FLAGS, Uint32)
{
    BINDING_TEST_FLAG_NONE  = 0,
    BINDING_TEST_FLAG_READ  = 1u << 0,
    BINDING_TEST_FLAG_WRITE = 1u << 1,
    BINDING_TEST_FLAG_ALL   = BINDING_TEST_FLAG_READ | BINDING_TEST_FLAG_WRITE,
    BINDING_TEST_FLAG_HIGH  = 1u << 31
};
DEFINE_FLAG_ENUM_OPERATORS(BINDING_TEST_FLAGS)

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
