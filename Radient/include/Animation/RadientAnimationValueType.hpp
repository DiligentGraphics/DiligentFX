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

#include "RadientAnimation.h"

#include <type_traits>

namespace Diligent
{

struct AnimationValueTypeInfo
{
    Uint32 NativeSize      = 0;
    Uint32 NativeAlignment = 0;
    bool   IsDiscrete      = false;
    bool   IsFloatingPoint = false;
};

template <typename ValueType>
constexpr AnimationValueTypeInfo MakeAnimationValueTypeInfo() noexcept
{
    using ComponentType = typename std::remove_all_extents<ValueType>::type;
    static_assert(std::is_integral<ComponentType>::value || std::is_floating_point<ComponentType>::value,
                  "Animation values must have integral or floating-point components");

    return {
        static_cast<Uint32>(sizeof(ValueType)),
        static_cast<Uint32>(alignof(ValueType)),
        std::is_integral<ComponentType>::value,
        std::is_floating_point<ComponentType>::value,
    };
}

inline AnimationValueTypeInfo GetAnimationValueTypeInfo(RADIENT_ANIMATION_VALUE_TYPE Type) noexcept
{
    switch (Type)
    {
        case RADIENT_ANIMATION_VALUE_TYPE_BOOL:
            return MakeAnimationValueTypeInfo<Uint8>();

        case RADIENT_ANIMATION_VALUE_TYPE_INT:
            return MakeAnimationValueTypeInfo<Int32>();
        case RADIENT_ANIMATION_VALUE_TYPE_INT2:
            return MakeAnimationValueTypeInfo<Int32[2]>();
        case RADIENT_ANIMATION_VALUE_TYPE_INT3:
            return MakeAnimationValueTypeInfo<Int32[3]>();
        case RADIENT_ANIMATION_VALUE_TYPE_INT4:
            return MakeAnimationValueTypeInfo<Int32[4]>();

        case RADIENT_ANIMATION_VALUE_TYPE_UINT:
            return MakeAnimationValueTypeInfo<Uint32>();
        case RADIENT_ANIMATION_VALUE_TYPE_UINT2:
            return MakeAnimationValueTypeInfo<Uint32[2]>();
        case RADIENT_ANIMATION_VALUE_TYPE_UINT3:
            return MakeAnimationValueTypeInfo<Uint32[3]>();
        case RADIENT_ANIMATION_VALUE_TYPE_UINT4:
            return MakeAnimationValueTypeInfo<Uint32[4]>();

        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT:
            return MakeAnimationValueTypeInfo<Float32>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT2:
            return MakeAnimationValueTypeInfo<Float32[2]>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3:
            return MakeAnimationValueTypeInfo<Float32[3]>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4:
            return MakeAnimationValueTypeInfo<Float32[4]>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2:
            return MakeAnimationValueTypeInfo<Float32[2][2]>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3:
            return MakeAnimationValueTypeInfo<Float32[3][3]>();
        case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4:
            return MakeAnimationValueTypeInfo<Float32[4][4]>();

        default:
            return {};
    }
}

} // namespace Diligent
