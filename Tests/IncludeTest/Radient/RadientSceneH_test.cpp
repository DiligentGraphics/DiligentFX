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

#include "Radient/interface/RadientScene.h"

#include <type_traits>

namespace
{

using namespace Diligent;

static_assert(sizeof(RADIENT_CAMERA_PROJECTION) == sizeof(Uint8), "Unexpected RADIENT_CAMERA_PROJECTION size");
static_assert(static_cast<Uint8>(RADIENT_CAMERA_PROJECTION_PERSPECTIVE) == 0, "Unexpected RADIENT_CAMERA_PROJECTION_PERSPECTIVE value");
static_assert(static_cast<Uint8>(RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC) == 1, "Unexpected RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC value");

static_assert(sizeof(RADIENT_LIGHT_TYPE) == sizeof(Uint8), "Unexpected RADIENT_LIGHT_TYPE size");
static_assert(static_cast<Uint8>(RADIENT_LIGHT_TYPE_DIRECTIONAL) == 0, "Unexpected RADIENT_LIGHT_TYPE_DIRECTIONAL value");
static_assert(static_cast<Uint8>(RADIENT_LIGHT_TYPE_POINT) == 1, "Unexpected RADIENT_LIGHT_TYPE_POINT value");
static_assert(static_cast<Uint8>(RADIENT_LIGHT_TYPE_SPOT) == 2, "Unexpected RADIENT_LIGHT_TYPE_SPOT value");

static_assert(std::is_standard_layout<RadientCameraComponent>::value, "RadientCameraComponent must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientCameraComponent>::value, "RadientCameraComponent must be trivially copyable");
static_assert(std::is_standard_layout<RadientLightComponent>::value, "RadientLightComponent must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientLightComponent>::value, "RadientLightComponent must be trivially copyable");
static_assert(std::is_standard_layout<RadientCustomComponentData>::value, "RadientCustomComponentData must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientCustomComponentData>::value, "RadientCustomComponentData must be trivially copyable");

constexpr RadientCameraComponent DefaultCamera{};
static_assert(DefaultCamera.Projection == RADIENT_CAMERA_PROJECTION_PERSPECTIVE, "Unexpected RadientCameraComponent projection default value");
static_assert(DefaultCamera.HorizontalAperture == 2.0955f, "Unexpected RadientCameraComponent horizontal aperture default value");
static_assert(DefaultCamera.VerticalAperture == 1.52908f, "Unexpected RadientCameraComponent vertical aperture default value");
static_assert(DefaultCamera.HorizontalApertureOffset == 0.f, "Unexpected RadientCameraComponent horizontal aperture offset default value");
static_assert(DefaultCamera.VerticalApertureOffset == 0.f, "Unexpected RadientCameraComponent vertical aperture offset default value");
static_assert(DefaultCamera.FocalLength == 5.f, "Unexpected RadientCameraComponent focal length default value");
static_assert(DefaultCamera.ClippingRange.x == 0.1f && DefaultCamera.ClippingRange.y == 1000.f, "Unexpected RadientCameraComponent clipping range default value");
static_assert(DefaultCamera.FStop == 0.f, "Unexpected RadientCameraComponent f-stop default value");
static_assert(DefaultCamera.FocusDistance == 0.f, "Unexpected RadientCameraComponent focus distance default value");

constexpr RadientLightComponent DefaultLight{};
static_assert(DefaultLight.Type == RADIENT_LIGHT_TYPE_DIRECTIONAL, "Unexpected RadientLightComponent type default value");
static_assert(DefaultLight.Color.x == 1.f && DefaultLight.Color.y == 1.f && DefaultLight.Color.z == 1.f, "Unexpected RadientLightComponent color default value");
static_assert(DefaultLight.Intensity == 1.f, "Unexpected RadientLightComponent intensity default value");
static_assert(DefaultLight.Exposure == 0.f, "Unexpected RadientLightComponent exposure default value");
static_assert(DefaultLight.Diffuse == 1.f, "Unexpected RadientLightComponent diffuse default value");
static_assert(DefaultLight.Specular == 1.f, "Unexpected RadientLightComponent specular default value");
static_assert(DefaultLight.Normalize == False, "Unexpected RadientLightComponent normalize default value");
static_assert(DefaultLight.EnableColorTemperature == False, "Unexpected RadientLightComponent enable color temperature default value");
static_assert(DefaultLight.ColorTemperature == 6500.f, "Unexpected RadientLightComponent color temperature default value");
static_assert(DefaultLight.Radius == 0.5f, "Unexpected RadientLightComponent radius default value");
static_assert(DefaultLight.Angle == 0.53f, "Unexpected RadientLightComponent angle default value");
static_assert(DefaultLight.ShapingConeAngle == 90.f, "Unexpected RadientLightComponent shaping cone angle default value");
static_assert(DefaultLight.ShapingConeSoftness == 0.f, "Unexpected RadientLightComponent shaping cone softness default value");
static_assert(DefaultLight.ShapingFocus == 0.f, "Unexpected RadientLightComponent shaping focus default value");

constexpr RadientCustomComponentData DefaultCustomComponent{};
static_assert(DefaultCustomComponent.ComponentType == InvalidRadientComponentTypeID, "Unexpected RadientCustomComponentData component type default value");
static_assert(DefaultCustomComponent.Name == nullptr, "Unexpected RadientCustomComponentData name default value");
static_assert(DefaultCustomComponent.Schema == nullptr, "Unexpected RadientCustomComponentData schema default value");
static_assert(DefaultCustomComponent.Version == 0, "Unexpected RadientCustomComponentData version default value");
static_assert(DefaultCustomComponent.pData == nullptr, "Unexpected RadientCustomComponentData data default value");
static_assert(DefaultCustomComponent.DataSize == 0, "Unexpected RadientCustomComponentData data size default value");

} // namespace
