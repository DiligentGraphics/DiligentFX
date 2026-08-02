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

#include "Render/RadientPostFXParameters.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace RadientPostFX
{

namespace
{

Int32 GetToneMappingMode(RADIENT_TONE_MAPPING_MODE Mode)
{
    switch (Mode)
    {
        case RADIENT_TONE_MAPPING_MODE_NONE: return TONE_MAPPING_MODE_NONE;
        case RADIENT_TONE_MAPPING_MODE_EXP: return TONE_MAPPING_MODE_EXP;
        case RADIENT_TONE_MAPPING_MODE_REINHARD: return TONE_MAPPING_MODE_REINHARD;
        case RADIENT_TONE_MAPPING_MODE_REINHARD_MOD: return TONE_MAPPING_MODE_REINHARD_MOD;
        case RADIENT_TONE_MAPPING_MODE_UNCHARTED2: return TONE_MAPPING_MODE_UNCHARTED2;
        case RADIENT_TONE_MAPPING_MODE_FILMIC_ALU: return TONE_MAPPING_MODE_FILMIC_ALU;
        case RADIENT_TONE_MAPPING_MODE_LOGARITHMIC: return TONE_MAPPING_MODE_LOGARITHMIC;
        case RADIENT_TONE_MAPPING_MODE_ADAPTIVE_LOG: return TONE_MAPPING_MODE_ADAPTIVE_LOG;
        case RADIENT_TONE_MAPPING_MODE_AGX: return TONE_MAPPING_MODE_AGX;
        case RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM: return TONE_MAPPING_MODE_AGX_CUSTOM;
        case RADIENT_TONE_MAPPING_MODE_PBR_NEUTRAL: return TONE_MAPPING_MODE_PBR_NEUTRAL;
        case RADIENT_TONE_MAPPING_MODE_COMMERCE: return TONE_MAPPING_MODE_COMMERCE;

        default:
            UNEXPECTED("Unexpected Radient tone mapping mode");
            return TONE_MAPPING_MODE_NONE;
    }
}

Uint32 GetSSAOAlgorithm(RADIENT_SSAO_ALGORITHM Algorithm)
{
    switch (Algorithm)
    {
        case RADIENT_SSAO_ALGORITHM_GTAO: return SSAO_ALGORITHM_GTAO;
        case RADIENT_SSAO_ALGORITHM_HBAO: return SSAO_ALGORITHM_HBAO;
        case RADIENT_SSAO_ALGORITHM_VBAO: return SSAO_ALGORITHM_VBAO;

        default:
            UNEXPECTED("Unexpected Radient SSAO algorithm");
            return SSAO_ALGORITHM_GTAO;
    }
}

} // namespace

HLSL::ToneMappingAttribs MakeToneMappingAttribs(const RadientToneMappingDesc& Desc)
{
    HLSL::ToneMappingAttribs Attribs;
    Attribs.iToneMappingMode     = GetToneMappingMode(Desc.Mode);
    Attribs.bAutoExposure        = Desc.AutoExposure;
    Attribs.fMiddleGray          = Desc.MiddleGray;
    Attribs.bLightAdaptation     = Desc.LightAdaptation;
    Attribs.fWhitePoint          = Desc.WhitePoint;
    Attribs.fLuminanceSaturation = Desc.LuminanceSaturation;
    Attribs.AgX.Saturation       = Desc.AgX.Saturation;
    Attribs.AgX.Slope            = Desc.AgX.Slope;
    Attribs.AgX.Power            = Desc.AgX.Power;
    Attribs.AgX.Offset           = Desc.AgX.Offset;
    return Attribs;
}

HLSL::BloomAttribs MakeBloomAttribs(const RadientBloomDesc& Desc)
{
    HLSL::BloomAttribs Attribs;
    Attribs.Intensity    = Desc.Intensity;
    Attribs.Threshold    = Desc.Threshold;
    Attribs.SoftTreshold = Desc.SoftThreshold;
    Attribs.Radius       = Desc.Radius;
    return Attribs;
}

HLSL::TemporalAntiAliasingAttribs MakeTemporalAntiAliasingAttribs(const RadientTemporalAntiAliasingDesc& Desc)
{
    HLSL::TemporalAntiAliasingAttribs Attribs;
    Attribs.TemporalStabilityFactor = Desc.TemporalStabilityFactor;
    return Attribs;
}

HLSL::ScreenSpaceAmbientOcclusionAttribs MakeSSAOAttribs(const RadientSSAODesc& Desc)
{
    HLSL::ScreenSpaceAmbientOcclusionAttribs Attribs;
    Attribs.EffectRadius                = Desc.EffectRadius;
    Attribs.EffectFalloffRange          = Desc.EffectFalloffRange;
    Attribs.RadiusMultiplier            = Desc.RadiusMultiplier;
    Attribs.DepthMIPSamplingOffset      = Desc.DepthMIPSamplingOffset;
    Attribs.TemporalStabilityFactor     = Desc.TemporalStabilityFactor;
    Attribs.SpatialReconstructionRadius = Desc.SpatialReconstructionRadius;
    Attribs.BitmaskThickness            = Desc.BitmaskThickness;
    Attribs.Algorithm                   = GetSSAOAlgorithm(Desc.Algorithm);
    return Attribs;
}

HLSL::ScreenSpaceReflectionAttribs MakeSSRAttribs(const RadientSSRDesc& Desc)
{
    HLSL::ScreenSpaceReflectionAttribs Attribs;
    Attribs.DepthBufferThickness               = Desc.DepthBufferThickness;
    Attribs.RoughnessThreshold                 = Desc.RoughnessThreshold;
    Attribs.MostDetailedMip                    = Desc.MostDetailedMip;
    Attribs.MaxTraversalIntersections          = Desc.MaxTraversalIntersections;
    Attribs.GGXImportanceSampleBias            = Desc.GGXImportanceSampleBias;
    Attribs.SpatialReconstructionRadius        = Desc.SpatialReconstructionRadius;
    Attribs.TemporalRadianceStabilityFactor    = Desc.TemporalRadianceStabilityFactor;
    Attribs.TemporalVarianceStabilityFactor    = Desc.TemporalVarianceStabilityFactor;
    Attribs.BilateralCleanupSpatialSigmaFactor = Desc.BilateralCleanupSpatialSigmaFactor;
    return Attribs;
}

HLSL::DepthOfFieldAttribs MakeDepthOfFieldAttribs(const RadientDepthOfFieldDesc& Desc)
{
    HLSL::DepthOfFieldAttribs Attribs;
    Attribs.MaxCircleOfConfusion    = Desc.MaxCircleOfConfusion;
    Attribs.TemporalStabilityFactor = Desc.TemporalStabilityFactor;
    Attribs.BokehKernelRingCount    = static_cast<Int32>(Desc.BokehKernelRingCount);
    Attribs.BokehKernelRingDensity  = static_cast<Int32>(Desc.BokehKernelRingDensity);
    return Attribs;
}

DepthOfField::FEATURE_FLAGS GetDepthOfFieldFeatureFlags(const RadientDepthOfFieldDesc& Desc)
{
    DepthOfField::FEATURE_FLAGS Flags = DepthOfField::FEATURE_FLAG_NONE;
    if (Desc.TemporalSmoothing)
        Flags |= DepthOfField::FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING;
    if (Desc.KarisInverse)
        Flags |= DepthOfField::FEATURE_FLAG_ENABLE_KARIS_INVERSE;
    return Flags;
}

} // namespace RadientPostFX

} // namespace Diligent
