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

#include "Radient/interface/RadientMath.h"

#include <cstddef>
#include <type_traits>

namespace
{

using namespace Diligent;

static_assert(std::is_standard_layout<RadientFloat2>::value, "RadientFloat2 must be a standard-layout type");
static_assert(std::is_standard_layout<RadientFloat3>::value, "RadientFloat3 must be a standard-layout type");
static_assert(std::is_standard_layout<RadientFloat4>::value, "RadientFloat4 must be a standard-layout type");
static_assert(std::is_standard_layout<RadientQuaternion>::value, "RadientQuaternion must be a standard-layout type");
static_assert(std::is_standard_layout<RadientTransform>::value, "RadientTransform must be a standard-layout type");
static_assert(std::is_standard_layout<RadientMatrix4x4>::value, "RadientMatrix4x4 must be a standard-layout type");
static_assert(std::is_standard_layout<RadientBounds>::value, "RadientBounds must be a standard-layout type");
static_assert(std::is_standard_layout<RadientExtent2D>::value, "RadientExtent2D must be a standard-layout type");

static_assert(std::is_trivially_copyable<RadientFloat2>::value, "RadientFloat2 must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientFloat3>::value, "RadientFloat3 must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientFloat4>::value, "RadientFloat4 must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientQuaternion>::value, "RadientQuaternion must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientTransform>::value, "RadientTransform must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientMatrix4x4>::value, "RadientMatrix4x4 must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientBounds>::value, "RadientBounds must be trivially copyable");
static_assert(std::is_trivially_copyable<RadientExtent2D>::value, "RadientExtent2D must be trivially copyable");

static_assert(sizeof(RadientFloat2) == sizeof(Float32) * 2, "Unexpected RadientFloat2 size");
static_assert(sizeof(RadientFloat3) == sizeof(Float32) * 3, "Unexpected RadientFloat3 size");
static_assert(sizeof(RadientFloat4) == sizeof(Float32) * 4, "Unexpected RadientFloat4 size");
static_assert(sizeof(RadientQuaternion) == sizeof(Float32) * 4, "Unexpected RadientQuaternion size");
static_assert(sizeof(RadientMatrix4x4) == sizeof(Float32) * 16, "Unexpected RadientMatrix4x4 size");
static_assert(sizeof(RadientExtent2D) == sizeof(Uint32) * 2, "Unexpected RadientExtent2D size");

static_assert(offsetof(RadientFloat3, x) == 0, "Unexpected RadientFloat3::x offset");
static_assert(offsetof(RadientFloat3, y) == sizeof(Float32), "Unexpected RadientFloat3::y offset");
static_assert(offsetof(RadientFloat3, z) == sizeof(Float32) * 2, "Unexpected RadientFloat3::z offset");
static_assert(offsetof(RadientQuaternion, w) == sizeof(Float32) * 3, "Unexpected RadientQuaternion::w offset");
static_assert(offsetof(RadientMatrix4x4, Data) == 0, "Unexpected RadientMatrix4x4::Data offset");
static_assert(offsetof(RadientExtent2D, Height) == sizeof(Uint32), "Unexpected RadientExtent2D::Height offset");

constexpr RadientFloat2 DefaultFloat2{};
static_assert(DefaultFloat2.x == 0.f && DefaultFloat2.y == 0.f, "Unexpected RadientFloat2 default value");

constexpr RadientFloat3 DefaultFloat3{};
static_assert(DefaultFloat3.x == 0.f && DefaultFloat3.y == 0.f && DefaultFloat3.z == 0.f, "Unexpected RadientFloat3 default value");

constexpr RadientFloat4 DefaultFloat4{};
static_assert(DefaultFloat4.x == 0.f && DefaultFloat4.y == 0.f && DefaultFloat4.z == 0.f && DefaultFloat4.w == 0.f, "Unexpected RadientFloat4 default value");

constexpr RadientQuaternion DefaultQuaternion{};
static_assert(DefaultQuaternion.x == 0.f && DefaultQuaternion.y == 0.f && DefaultQuaternion.z == 0.f && DefaultQuaternion.w == 1.f, "Unexpected RadientQuaternion default value");

constexpr RadientTransform DefaultTransform{};
static_assert(DefaultTransform.Position.x == 0.f && DefaultTransform.Position.y == 0.f && DefaultTransform.Position.z == 0.f, "Unexpected RadientTransform position default value");
static_assert(DefaultTransform.Rotation.x == 0.f && DefaultTransform.Rotation.y == 0.f && DefaultTransform.Rotation.z == 0.f && DefaultTransform.Rotation.w == 1.f, "Unexpected RadientTransform rotation default value");
static_assert(DefaultTransform.Scale.x == 1.f && DefaultTransform.Scale.y == 1.f && DefaultTransform.Scale.z == 1.f, "Unexpected RadientTransform scale default value");

constexpr RadientMatrix4x4 DefaultMatrix{};
static_assert(DefaultMatrix.Data[0] == 1.f && DefaultMatrix.Data[5] == 1.f && DefaultMatrix.Data[10] == 1.f && DefaultMatrix.Data[15] == 1.f, "Unexpected RadientMatrix4x4 default diagonal value");
static_assert(DefaultMatrix.Data[1] == 0.f && DefaultMatrix.Data[4] == 0.f && DefaultMatrix.Data[14] == 0.f, "Unexpected RadientMatrix4x4 default off-diagonal value");

constexpr RadientBounds DefaultBounds{};
static_assert(DefaultBounds.Min.x == 0.f && DefaultBounds.Max.z == 0.f, "Unexpected RadientBounds default value");

constexpr RadientExtent2D DefaultExtent{};
static_assert(DefaultExtent.Width == 0 && DefaultExtent.Height == 0, "Unexpected RadientExtent2D default value");

} // namespace
