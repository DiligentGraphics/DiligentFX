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

#include "Radient/interface/RadientAnimation.h"

#include <type_traits>

namespace
{

using namespace Diligent;

static_assert(std::is_standard_layout<RadientAnimationTarget>::value, "RadientAnimationTarget must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationTarget>::value, "RadientAnimationTarget must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationRegistryEntry>::value, "RadientAnimationRegistryEntry must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationRegistryEntry>::value, "RadientAnimationRegistryEntry must be trivially copyable");
static_assert(std::is_standard_layout<RadientAnimationRegistryState>::value, "RadientAnimationRegistryState must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAnimationRegistryState>::value, "RadientAnimationRegistryState must be trivially copyable");

constexpr RadientAnimationTarget DefaultTarget{};
static_assert(DefaultTarget.Entity == InvalidRadientEntityID, "Unexpected RadientAnimationTarget entity default value");
static_assert(DefaultTarget.pPose == nullptr, "Unexpected RadientAnimationTarget pose default value");

constexpr RadientAnimationRegistryEntry DefaultEntry{};
static_assert(DefaultEntry.pAnimation == nullptr, "Unexpected RadientAnimationRegistryEntry animation default value");
static_assert(DefaultEntry.pTargets == nullptr, "Unexpected RadientAnimationRegistryEntry targets default value");
static_assert(DefaultEntry.TargetCount == 0, "Unexpected RadientAnimationRegistryEntry target count default value");

constexpr RadientAnimationRegistryState DefaultState{};
static_assert(DefaultState.Revision == 0, "Unexpected RadientAnimationRegistryState revision default value");
static_assert(DefaultState.pEntries == nullptr, "Unexpected RadientAnimationRegistryState entries default value");
static_assert(DefaultState.EntryCount == 0, "Unexpected RadientAnimationRegistryState entry count default value");

} // namespace

void RadientAnimation_CPP_UseInterface(Diligent::IRadientAnimationRegistry*      pRegistry,
                                       Diligent::IRadientSkeletonAnimationAsset* pAnimation)
{
    using namespace Diligent;

    const RadientEntityID Entity = 1;
    (void)pRegistry->GetScene();
    RADIENT_STATUS Status = pRegistry->AddAnimatedEntities(pAnimation, &Entity, 1);
    Status                = pRegistry->RemoveAnimatedEntities(pAnimation, &Entity, 1);
    Status                = pRegistry->RemoveEntity(Entity);
    Status                = pRegistry->RemoveAnimation(pAnimation);
    (void)pRegistry->GetState();

    (void)Status;
}
