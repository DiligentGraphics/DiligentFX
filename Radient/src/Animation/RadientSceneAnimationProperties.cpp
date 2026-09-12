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

#include "Animation/RadientSceneAnimationDestinationBindingImpl.hpp"
#include "Animation/RadientNodeAnimationProperty.hpp"

#include "DebugUtilities.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace Diligent
{

namespace
{

static constexpr Uint32 NodeFinalizeTransform  = 1u << 0;
static constexpr Uint32 NodeFinalizeVisibility = 1u << 1;

static constexpr Uint32 LightFinalizeNormalize              = 1u << 0;
static constexpr Uint32 LightFinalizeEnableColorTemperature = 1u << 1;

static constexpr size_t LightNormalizeStagedOffset              = 0;
static constexpr size_t LightEnableColorTemperatureStagedOffset = 1;

template <typename ValueType>
struct AnimationValueType;

template <>
struct AnimationValueType<Float32>
{
    static constexpr RADIENT_ANIMATION_VALUE_TYPE Value = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
};

template <>
struct AnimationValueType<Bool>
{
    static constexpr RADIENT_ANIMATION_VALUE_TYPE Value = RADIENT_ANIMATION_VALUE_TYPE_BOOL;
};

template <>
struct AnimationValueType<RadientFloat3>
{
    static constexpr RADIENT_ANIMATION_VALUE_TYPE Value = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
};

template <typename ValueType>
constexpr RadientAnimationValueDesc MakeAnimationValueDesc() noexcept
{
    return {AnimationValueType<ValueType>::Value, 1};
}

} // namespace

const RadientSceneAnimationDestinationBindingImpl::PropertyDesc* RadientSceneAnimationDestinationBindingImpl::FindProperty(
    RadientAnimationSchemaID   Schema,
    RadientAnimationPropertyID Property) noexcept
{
    static_assert(std::is_standard_layout<RadientSceneState::LocalTransformComponent>::value,
                  "Animated component storage must have a standard layout");
    static_assert(std::is_standard_layout<RadientTransform>::value,
                  "Animated component storage must have a standard layout");
    static_assert(std::is_standard_layout<RadientLightComponent>::value,
                  "Animated component storage must have a standard layout");
    static_assert(std::is_standard_layout<RadientCameraComponent>::value,
                  "Animated component storage must have a standard layout");
    static_assert(std::is_standard_layout<RadientFloat2>::value,
                  "Animated component storage must have a standard layout");
    static constexpr StorageDesc NodeStorage{
        &AcquireCoreStorage<RadientSceneState::LocalTransformComponent>,
        &FinalizeNodeStorage,
        sizeof(RadientSceneState::LocalTransformComponent),
        alignof(RadientSceneState::LocalTransformComponent),
    };
    static constexpr StorageDesc LightStorage{
        &AcquireOptionalStorage<RadientLightComponent>,
        &FinalizeLightStorage,
        sizeof(RadientLightComponent),
        alignof(RadientLightComponent),
    };
    static constexpr StorageDesc CameraStorage{
        &AcquireOptionalStorage<RadientCameraComponent>,
        nullptr,
        sizeof(RadientCameraComponent),
        alignof(RadientCameraComponent),
    };

    static constexpr auto MakeLightProperty =
        [](RadientAnimationPropertyID Property,
           RadientAnimationValueDesc  Value,
           size_t                     Offset) constexpr noexcept {
            return PropertyDesc{
                {RadientLightAnimationSchemaID, Property},
                Value,
                RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
                LightStorage,
                OutputStorageKind::Direct,
                Offset,
                RadientSceneState::CHANGE_FLAG_LIGHTS,
            };
        };

    static constexpr auto MakeLightBooleanProperty =
        [](RadientAnimationPropertyID Property,
           RadientAnimationValueDesc  Value,
           size_t                     StagedOffset,
           Uint32                     FinalizeFlags) constexpr noexcept {
            return PropertyDesc{
                {RadientLightAnimationSchemaID, Property},
                Value,
                RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
                LightStorage,
                OutputStorageKind::Staged,
                StagedOffset,
                RadientSceneState::CHANGE_FLAG_LIGHTS,
                FinalizeFlags,
            };
        };

    static constexpr auto MakeNodeTransformProperty =
        [](const RadientNodeAnimationPropertyInfo& Info,
           size_t                                  Offset) constexpr noexcept {
            return PropertyDesc{
                {RadientNodeAnimationSchemaID, Info.Property},
                Info.Value,
                Info.Semantic,
                NodeStorage,
                OutputStorageKind::Direct,
                Offset,
                RadientSceneState::CHANGE_FLAG_TRANSFORMS,
                NodeFinalizeTransform,
            };
        };

    static constexpr auto MakeNodeVisibilityProperty =
        [](const RadientNodeAnimationPropertyInfo& Info) constexpr noexcept {
            return PropertyDesc{
                {RadientNodeAnimationSchemaID, Info.Property},
                Info.Value,
                Info.Semantic,
                NodeStorage,
                OutputStorageKind::Staged,
                0,
                RadientSceneState::CHANGE_FLAG_NONE,
                NodeFinalizeVisibility,
            };
        };

    static constexpr auto MakeCameraProperty =
        [](RadientAnimationPropertyID Property,
           RadientAnimationValueDesc  Value,
           size_t                     Offset) constexpr noexcept {
            return PropertyDesc{
                {RadientCameraAnimationSchemaID, Property},
                Value,
                RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE,
                CameraStorage,
                OutputStorageKind::Direct,
                Offset,
                RadientSceneState::CHANGE_FLAG_CAMERAS,
            };
        };

    static constexpr PropertyDesc Properties[] = {
        // Light animation
        MakeLightProperty(
            RadientLightColorProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Color)>(),
            offsetof(RadientLightComponent, Color)),
        MakeLightProperty(
            RadientLightIntensityProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Intensity)>(),
            offsetof(RadientLightComponent, Intensity)),
        MakeLightProperty(
            RadientLightRangeProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Range)>(),
            offsetof(RadientLightComponent, Range)),
        MakeLightProperty(
            RadientLightExposureProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Exposure)>(),
            offsetof(RadientLightComponent, Exposure)),
        MakeLightProperty(
            RadientLightInnerConeAngleProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::InnerConeAngle)>(),
            offsetof(RadientLightComponent, InnerConeAngle)),
        MakeLightProperty(
            RadientLightOuterConeAngleProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::OuterConeAngle)>(),
            offsetof(RadientLightComponent, OuterConeAngle)),
        MakeLightProperty(
            RadientLightDiffuseProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Diffuse)>(),
            offsetof(RadientLightComponent, Diffuse)),
        MakeLightProperty(
            RadientLightSpecularProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Specular)>(),
            offsetof(RadientLightComponent, Specular)),
        MakeLightBooleanProperty(
            RadientLightNormalizeProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Normalize)>(),
            LightNormalizeStagedOffset,
            LightFinalizeNormalize),
        MakeLightBooleanProperty(
            RadientLightEnableColorTemperatureProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::EnableColorTemperature)>(),
            LightEnableColorTemperatureStagedOffset,
            LightFinalizeEnableColorTemperature),
        MakeLightProperty(
            RadientLightColorTemperatureProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::ColorTemperature)>(),
            offsetof(RadientLightComponent, ColorTemperature)),
        MakeLightProperty(
            RadientLightRadiusProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Radius)>(),
            offsetof(RadientLightComponent, Radius)),
        MakeLightProperty(
            RadientLightAngleProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::Angle)>(),
            offsetof(RadientLightComponent, Angle)),
        MakeLightProperty(
            RadientLightShapingFocusProperty,
            MakeAnimationValueDesc<decltype(RadientLightComponent::ShapingFocus)>(),
            offsetof(RadientLightComponent, ShapingFocus)),

        // Node animation
        MakeNodeTransformProperty(
            RadientNodeTranslationPropertyInfo,
            offsetof(RadientSceneState::LocalTransformComponent, Transform) +
                offsetof(RadientTransform, Position)),
        MakeNodeTransformProperty(
            RadientNodeRotationPropertyInfo,
            offsetof(RadientSceneState::LocalTransformComponent, Transform) +
                offsetof(RadientTransform, Rotation)),
        MakeNodeTransformProperty(
            RadientNodeScalePropertyInfo,
            offsetof(RadientSceneState::LocalTransformComponent, Transform) +
                offsetof(RadientTransform, Scale)),
        MakeNodeVisibilityProperty(RadientNodeVisibilityPropertyInfo),

        // Camera animation
        MakeCameraProperty(
            RadientCameraHorizontalApertureProperty,
            MakeAnimationValueDesc<decltype(RadientCameraComponent::HorizontalAperture)>(),
            offsetof(RadientCameraComponent, HorizontalAperture)),
        MakeCameraProperty(
            RadientCameraVerticalApertureProperty,
            MakeAnimationValueDesc<decltype(RadientCameraComponent::VerticalAperture)>(),
            offsetof(RadientCameraComponent, VerticalAperture)),
        MakeCameraProperty(
            RadientCameraFocalLengthProperty,
            MakeAnimationValueDesc<decltype(RadientCameraComponent::FocalLength)>(),
            offsetof(RadientCameraComponent, FocalLength)),
        MakeCameraProperty(
            RadientCameraNearClipProperty,
            MakeAnimationValueDesc<decltype(RadientFloat2::x)>(),
            offsetof(RadientCameraComponent, ClippingRange) + offsetof(RadientFloat2, x)),
        MakeCameraProperty(
            RadientCameraFarClipProperty,
            MakeAnimationValueDesc<decltype(RadientFloat2::y)>(),
            offsetof(RadientCameraComponent, ClippingRange) + offsetof(RadientFloat2, y)),
        MakeCameraProperty(
            RadientCameraFStopProperty,
            MakeAnimationValueDesc<decltype(RadientCameraComponent::FStop)>(),
            offsetof(RadientCameraComponent, FStop)),
        MakeCameraProperty(
            RadientCameraFocusDistanceProperty,
            MakeAnimationValueDesc<decltype(RadientCameraComponent::FocusDistance)>(),
            offsetof(RadientCameraComponent, FocusDistance)),
    };

#ifdef DILIGENT_DEBUG
    static const bool PropertiesAreStrictlyOrdered = [] {
        for (size_t PropertyIndex = 1; PropertyIndex < std::size(Properties); ++PropertyIndex)
        {
            if (!(Properties[PropertyIndex - 1].Key < Properties[PropertyIndex].Key))
                return false;
        }
        return true;
    }();
    VERIFY_EXPR(PropertiesAreStrictlyOrdered);
#endif

    const PropertyKey Key{Schema, Property};
    const auto        It = std::lower_bound(
        std::begin(Properties), std::end(Properties), Key,
        [](const PropertyDesc& Desc, const PropertyKey& SearchKey) {
            return Desc.Key < SearchKey;
        });
    return It != std::end(Properties) && It->Key == Key ? It : nullptr;
}

RadientSceneState::CHANGE_FLAGS RadientSceneAnimationDestinationBindingImpl::FinalizeNodeStorage(
    RadientSceneAnimationDestinationBindingImpl& Binding,
    entt::entity                                 Entity,
    StorageGroup&                                Storage)
{
    RadientSceneState::DIRTY_FLAGS  DirtyFlags  = RadientSceneState::DIRTY_FLAG_NONE;
    RadientSceneState::CHANGE_FLAGS ChangeFlags = RadientSceneState::CHANGE_FLAG_NONE;
    if ((Storage.FinalizeFlags & NodeFinalizeTransform) != 0)
        DirtyFlags |= RadientSceneState::DIRTY_FLAG_TRANSFORM;

    if ((Storage.FinalizeFlags & NodeFinalizeVisibility) != 0)
    {
        RadientSceneState::EntityStateComponent& State =
            Binding.m_State.m_CoreStorages.get<RadientSceneState::EntityStateComponent>(Entity);
        const Bool OldVisible = (State.Flags & RADIENT_ENTITY_FLAG_VISIBLE) != 0 ? True : False;
        const Bool NewVisible = Storage.StagedStorage[0] != 0 ? True : False;
        if (OldVisible != NewVisible)
        {
            State.Flags = NewVisible ?
                (State.Flags | RADIENT_ENTITY_FLAG_VISIBLE) :
                (State.Flags & ~RADIENT_ENTITY_FLAG_VISIBLE);
            DirtyFlags |= RadientSceneState::DIRTY_FLAG_VISIBILITY;
            ChangeFlags |= RadientSceneState::CHANGE_FLAG_VISIBILITY;
        }
    }

    if (DirtyFlags != RadientSceneState::DIRTY_FLAG_NONE)
        Binding.m_State.MarkDirty(Entity, DirtyFlags);
    return ChangeFlags;
}

RadientSceneState::CHANGE_FLAGS RadientSceneAnimationDestinationBindingImpl::FinalizeLightStorage(
    RadientSceneAnimationDestinationBindingImpl& Binding,
    entt::entity                                 Entity,
    StorageGroup&                                Storage)
{
    RadientLightComponent& Light = *static_cast<RadientLightComponent*>(Storage.pDirectStorage);
    if ((Storage.FinalizeFlags & LightFinalizeNormalize) != 0)
        Light.Normalize = Storage.StagedStorage[LightNormalizeStagedOffset] != 0 ? True : False;
    if ((Storage.FinalizeFlags & LightFinalizeEnableColorTemperature) != 0)
        Light.EnableColorTemperature = Storage.StagedStorage[LightEnableColorTemperatureStagedOffset] != 0 ? True : False;

    Binding.m_State.RecordRenderableLightUpdated(Entity);
    return RadientSceneState::CHANGE_FLAG_NONE;
}

} // namespace Diligent
