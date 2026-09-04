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

#include "Radient/interface/RadientMorphTargets.h"

#include <type_traits>

using namespace Diligent;

static_assert(std::is_standard_layout<RadientMorphTargetAttributeCreateInfo>::value, "RadientMorphTargetAttributeCreateInfo must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientMorphTargetAttributeCreateInfo>::value, "RadientMorphTargetAttributeCreateInfo must be trivially copyable");
static_assert(std::is_standard_layout<RadientMorphTargetCreateInfo>::value, "RadientMorphTargetCreateInfo must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientMorphTargetCreateInfo>::value, "RadientMorphTargetCreateInfo must be trivially copyable");

constexpr RadientMorphTargetAttributeCreateInfo DefaultAttribute{};
static_assert(DefaultAttribute.Semantic == nullptr, "Unexpected morph-target attribute semantic default value");
static_assert(DefaultAttribute.pDeltas == nullptr, "Unexpected morph-target attribute data default value");
static_assert(DefaultAttribute.ComponentCount == 0, "Unexpected morph-target attribute component count default value");

constexpr RadientMorphTargetCreateInfo DefaultTarget{};
static_assert(DefaultTarget.Name == nullptr, "Unexpected morph-target name default value");
static_assert(DefaultTarget.pAttributes == nullptr, "Unexpected morph-target attribute array default value");
static_assert(DefaultTarget.AttributeCount == 0, "Unexpected morph-target attribute count default value");
static_assert(DefaultTarget.DefaultWeight == 0.f, "Unexpected morph-target default weight");

void RadientMorphTargets_CPP_UseInterface(IRadientMorphTargetWeights* pTargetWeights)
{
    RadientMorphTargetAttributeCreateInfo Attribute;
    RadientMorphTargetCreateInfo          Target;
    Float32                               Weight = 0.f;

    Attribute.Semantic       = RadientMorphTargetPositionSemantic;
    Attribute.ComponentCount = 3;

    Target.pAttributes    = &Attribute;
    Target.AttributeCount = 1;

    (void)pTargetWeights->GetMesh();
    (void)pTargetWeights->GetVersion();
    (void)pTargetWeights->GetWeightCount();
    (void)pTargetWeights->GetWeights();
    (void)pTargetWeights->SetWeights(0, 1, &Weight);
    (void)pTargetWeights->ResetToDefaults();
    (void)Target;
}
