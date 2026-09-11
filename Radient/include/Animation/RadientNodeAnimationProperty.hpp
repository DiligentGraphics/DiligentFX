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

namespace Diligent
{

enum class RadientNodeTransformField : Uint8
{
    Translation = 1u << 0,
    Rotation    = 1u << 1,
    Scale       = 1u << 2,
};

enum class RadientNodeAnimationPropertyKind : Uint8
{
    Unknown    = 0,
    Transform  = 1u << 0,
    Visibility = 1u << 1,
};

using RadientNodeAnimationPropertyMask = Uint8;

static constexpr RadientNodeAnimationPropertyMask RadientNodeAnimationTransformPropertyMask =
    static_cast<RadientNodeAnimationPropertyMask>(RadientNodeAnimationPropertyKind::Transform);

static constexpr RadientNodeAnimationPropertyMask RadientNodeAnimationVisibilityPropertyMask =
    static_cast<RadientNodeAnimationPropertyMask>(RadientNodeAnimationPropertyKind::Visibility);

static constexpr RadientNodeAnimationPropertyMask RadientNodeAnimationAllPropertyMask =
    RadientNodeAnimationTransformPropertyMask |
    RadientNodeAnimationVisibilityPropertyMask;

struct RadientNodeAnimationPropertyResolution
{
    RadientNodeAnimationPropertyKind Kind     = RadientNodeAnimationPropertyKind::Unknown;
    RadientNodeTransformField        Field    = RadientNodeTransformField::Translation;
    RADIENT_ANIMATION_VALUE_SEMANTIC Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
};

inline RADIENT_STATUS ResolveRadientNodeAnimationProperty(const RadientAnimationPropertyBindingDesc& Property,
                                                          RadientNodeAnimationPropertyMask           SupportedProperties,
                                                          RadientNodeAnimationPropertyResolution&    Resolution) noexcept
{
    Resolution = {};

    if (Property.Schema == InvalidRadientAnimationSchemaID ||
        Property.DestinationElement == InvalidRadientAnimationDestinationElement ||
        Property.Property == InvalidRadientAnimationPropertyID ||
        Property.Value.Type <= RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN ||
        Property.Value.Type >= RADIENT_ANIMATION_VALUE_TYPE_COUNT ||
        Property.Value.ArraySize == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (Property.Schema != RadientNodeAnimationSchemaID)
        return RADIENT_STATUS_UNSUPPORTED;

    RADIENT_ANIMATION_VALUE_TYPE ExpectedType = RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN;
    switch (Property.Property)
    {
        case RadientNodeTranslationProperty:
            Resolution.Kind     = RadientNodeAnimationPropertyKind::Transform;
            Resolution.Field    = RadientNodeTransformField::Translation;
            Resolution.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
            ExpectedType        = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
            break;

        case RadientNodeRotationProperty:
            Resolution.Kind     = RadientNodeAnimationPropertyKind::Transform;
            Resolution.Field    = RadientNodeTransformField::Rotation;
            Resolution.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION;
            ExpectedType        = RADIENT_ANIMATION_VALUE_TYPE_FLOAT4;
            break;

        case RadientNodeScaleProperty:
            Resolution.Kind     = RadientNodeAnimationPropertyKind::Transform;
            Resolution.Field    = RadientNodeTransformField::Scale;
            Resolution.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
            ExpectedType        = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
            break;

        case RadientNodeVisibilityProperty:
            Resolution.Kind     = RadientNodeAnimationPropertyKind::Visibility;
            Resolution.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
            ExpectedType        = RADIENT_ANIMATION_VALUE_TYPE_BOOL;
            break;

        default:
            return RADIENT_STATUS_UNSUPPORTED;
    }

    if ((SupportedProperties & static_cast<RadientNodeAnimationPropertyMask>(Resolution.Kind)) == 0)
    {
        Resolution = {};
        return RADIENT_STATUS_UNSUPPORTED;
    }

    if (Property.Value.Type != ExpectedType ||
        Property.Value.ArraySize != 1 ||
        Property.FirstArrayElement != 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

inline void* GetRadientNodeTransformFieldAddress(RadientTransform&         Transform,
                                                 RadientNodeTransformField Field) noexcept
{
    switch (Field)
    {
        case RadientNodeTransformField::Translation:
            return &Transform.Position;

        case RadientNodeTransformField::Rotation:
            return &Transform.Rotation;

        case RadientNodeTransformField::Scale:
            return &Transform.Scale;

        default:
            return nullptr;
    }
}

} // namespace Diligent
