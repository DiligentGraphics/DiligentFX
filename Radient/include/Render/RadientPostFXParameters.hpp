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

#include "RadientView.h"
#include "DepthOfField.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
#include "Shaders/PostProcess/DepthOfField/public/DepthOfFieldStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL

namespace RadientPostFX
{

/// Converts persistent per-view settings to the corresponding Diligent PostFX parameters.
/// Effect enablement and frame-transient parameters are controlled by the render technique.
HLSL::ToneMappingAttribs                 MakeToneMappingAttribs(const RadientToneMappingDesc& Desc);
HLSL::BloomAttribs                       MakeBloomAttribs(const RadientBloomDesc& Desc);
HLSL::TemporalAntiAliasingAttribs        MakeTemporalAntiAliasingAttribs(const RadientTemporalAntiAliasingDesc& Desc);
HLSL::ScreenSpaceAmbientOcclusionAttribs MakeSSAOAttribs(const RadientSSAODesc& Desc);
HLSL::ScreenSpaceReflectionAttribs       MakeSSRAttribs(const RadientSSRDesc& Desc);
HLSL::DepthOfFieldAttribs                MakeDepthOfFieldAttribs(const RadientDepthOfFieldDesc& Desc);

/// Returns the Diligent depth-of-field feature flags selected by the view settings.
DepthOfField::FEATURE_FLAGS GetDepthOfFieldFeatureFlags(const RadientDepthOfFieldDesc& Desc);

} // namespace RadientPostFX

} // namespace Diligent
