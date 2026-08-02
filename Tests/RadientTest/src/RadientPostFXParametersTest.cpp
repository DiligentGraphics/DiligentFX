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

#include "gtest/gtest.h"

#include <array>

namespace Diligent
{

TEST(RadientPostFXParametersTest, ConvertsToneMappingParameters)
{
    struct ModeMapping
    {
        RADIENT_TONE_MAPPING_MODE RadientMode;
        Int32                     PostFXMode;
    };

    constexpr std::array ModeMappings{
        ModeMapping{RADIENT_TONE_MAPPING_MODE_NONE, TONE_MAPPING_MODE_NONE},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_EXP, TONE_MAPPING_MODE_EXP},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_REINHARD, TONE_MAPPING_MODE_REINHARD},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_REINHARD_MOD, TONE_MAPPING_MODE_REINHARD_MOD},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_UNCHARTED2, TONE_MAPPING_MODE_UNCHARTED2},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_FILMIC_ALU, TONE_MAPPING_MODE_FILMIC_ALU},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_LOGARITHMIC, TONE_MAPPING_MODE_LOGARITHMIC},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_ADAPTIVE_LOG, TONE_MAPPING_MODE_ADAPTIVE_LOG},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_AGX, TONE_MAPPING_MODE_AGX},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM, TONE_MAPPING_MODE_AGX_CUSTOM},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_PBR_NEUTRAL, TONE_MAPPING_MODE_PBR_NEUTRAL},
        ModeMapping{RADIENT_TONE_MAPPING_MODE_COMMERCE, TONE_MAPPING_MODE_COMMERCE},
    };

    for (const ModeMapping& Mapping : ModeMappings)
    {
        RadientToneMappingDesc Desc;
        Desc.Mode = Mapping.RadientMode;
        EXPECT_EQ(RadientPostFX::MakeToneMappingAttribs(Desc).iToneMappingMode, Mapping.PostFXMode);
    }

    RadientToneMappingDesc Desc;
    Desc.Mode                = RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM;
    Desc.AutoExposure        = False;
    Desc.MiddleGray          = 0.25f;
    Desc.LightAdaptation     = False;
    Desc.WhitePoint          = 4.f;
    Desc.LuminanceSaturation = 0.75f;
    Desc.AgX                 = {0.8f, 1.1f, 1.2f, -0.1f};

    const HLSL::ToneMappingAttribs Attribs = RadientPostFX::MakeToneMappingAttribs(Desc);
    EXPECT_EQ(Attribs.iToneMappingMode, TONE_MAPPING_MODE_AGX_CUSTOM);
    EXPECT_EQ(Attribs.bAutoExposure, FALSE);
    EXPECT_FLOAT_EQ(Attribs.fMiddleGray, Desc.MiddleGray);
    EXPECT_EQ(Attribs.bLightAdaptation, FALSE);
    EXPECT_FLOAT_EQ(Attribs.fWhitePoint, Desc.WhitePoint);
    EXPECT_FLOAT_EQ(Attribs.fLuminanceSaturation, Desc.LuminanceSaturation);
    EXPECT_FLOAT_EQ(Attribs.AgX.Saturation, Desc.AgX.Saturation);
    EXPECT_FLOAT_EQ(Attribs.AgX.Slope, Desc.AgX.Slope);
    EXPECT_FLOAT_EQ(Attribs.AgX.Power, Desc.AgX.Power);
    EXPECT_FLOAT_EQ(Attribs.AgX.Offset, Desc.AgX.Offset);
    EXPECT_EQ(Attribs.Padding0, 0u);
    EXPECT_EQ(Attribs.Padding1, 0u);
}

TEST(RadientPostFXParametersTest, ConvertsBloomParameters)
{
    RadientBloomDesc Desc;
    Desc.Enabled       = True;
    Desc.Intensity     = 0.35f;
    Desc.Threshold     = 1.25f;
    Desc.SoftThreshold = 0.4f;
    Desc.Radius        = 0.9f;

    const HLSL::BloomAttribs Attribs = RadientPostFX::MakeBloomAttribs(Desc);
    EXPECT_FLOAT_EQ(Attribs.Intensity, Desc.Intensity);
    EXPECT_FLOAT_EQ(Attribs.Threshold, Desc.Threshold);
    EXPECT_FLOAT_EQ(Attribs.SoftTreshold, Desc.SoftThreshold);
    EXPECT_FLOAT_EQ(Attribs.Radius, Desc.Radius);
    EXPECT_FLOAT_EQ(Attribs.AlphaInterpolation, 1.f);
    EXPECT_FLOAT_EQ(Attribs.Padding0, 0.f);
    EXPECT_FLOAT_EQ(Attribs.Padding1, 0.f);
    EXPECT_FLOAT_EQ(Attribs.Padding2, 0.f);
}

TEST(RadientPostFXParametersTest, ConvertsTemporalAntiAliasingParameters)
{
    RadientTemporalAntiAliasingDesc Desc;
    Desc.Enabled                 = True;
    Desc.TemporalStabilityFactor = 0.8f;

    const HLSL::TemporalAntiAliasingAttribs Attribs = RadientPostFX::MakeTemporalAntiAliasingAttribs(Desc);
    EXPECT_FLOAT_EQ(Attribs.TemporalStabilityFactor, Desc.TemporalStabilityFactor);
    EXPECT_EQ(Attribs.ResetAccumulation, FALSE);
    EXPECT_EQ(Attribs.SkipRejection, FALSE);
    EXPECT_FLOAT_EQ(Attribs.Padding0, 0.f);
}

TEST(RadientPostFXParametersTest, ConvertsSSAOParameters)
{
    struct AlgorithmMapping
    {
        RADIENT_SSAO_ALGORITHM RadientAlgorithm;
        Uint32                 PostFXAlgorithm;
    };

    constexpr std::array AlgorithmMappings{
        AlgorithmMapping{RADIENT_SSAO_ALGORITHM_GTAO, SSAO_ALGORITHM_GTAO},
        AlgorithmMapping{RADIENT_SSAO_ALGORITHM_HBAO, SSAO_ALGORITHM_HBAO},
        AlgorithmMapping{RADIENT_SSAO_ALGORITHM_VBAO, SSAO_ALGORITHM_VBAO},
    };

    for (const AlgorithmMapping& Mapping : AlgorithmMappings)
    {
        RadientSSAODesc Desc;
        Desc.Algorithm = Mapping.RadientAlgorithm;
        EXPECT_EQ(RadientPostFX::MakeSSAOAttribs(Desc).Algorithm, Mapping.PostFXAlgorithm);
    }

    RadientSSAODesc Desc;
    Desc.Enabled                     = True;
    Desc.Algorithm                   = RADIENT_SSAO_ALGORITHM_VBAO;
    Desc.EffectRadius                = 2.f;
    Desc.EffectFalloffRange          = 0.7f;
    Desc.RadiusMultiplier            = 1.2f;
    Desc.DepthMIPSamplingOffset      = 2.5f;
    Desc.TemporalStabilityFactor     = 0.85f;
    Desc.SpatialReconstructionRadius = 3.f;
    Desc.BitmaskThickness            = 0.6f;

    const HLSL::ScreenSpaceAmbientOcclusionAttribs Attribs = RadientPostFX::MakeSSAOAttribs(Desc);
    EXPECT_FLOAT_EQ(Attribs.EffectRadius, Desc.EffectRadius);
    EXPECT_FLOAT_EQ(Attribs.EffectFalloffRange, Desc.EffectFalloffRange);
    EXPECT_FLOAT_EQ(Attribs.RadiusMultiplier, Desc.RadiusMultiplier);
    EXPECT_FLOAT_EQ(Attribs.DepthMIPSamplingOffset, Desc.DepthMIPSamplingOffset);
    EXPECT_FLOAT_EQ(Attribs.TemporalStabilityFactor, Desc.TemporalStabilityFactor);
    EXPECT_FLOAT_EQ(Attribs.SpatialReconstructionRadius, Desc.SpatialReconstructionRadius);
    EXPECT_FLOAT_EQ(Attribs.BitmaskThickness, Desc.BitmaskThickness);
    EXPECT_EQ(Attribs.Algorithm, SSAO_ALGORITHM_VBAO);
    EXPECT_EQ(Attribs.ResetAccumulation, FALSE);
    EXPECT_FLOAT_EQ(Attribs.AlphaInterpolation, 1.f);
    EXPECT_FLOAT_EQ(Attribs.Padding0, 0.f);
    EXPECT_FLOAT_EQ(Attribs.Padding1, 0.f);
}

TEST(RadientPostFXParametersTest, ConvertsSSRParameters)
{
    RadientSSRDesc Desc;
    Desc.Enabled                            = True;
    Desc.DepthBufferThickness               = 0.05f;
    Desc.RoughnessThreshold                 = 0.4f;
    Desc.MostDetailedMip                    = 3;
    Desc.MaxTraversalIntersections          = 64;
    Desc.GGXImportanceSampleBias            = 0.45f;
    Desc.SpatialReconstructionRadius        = 5.f;
    Desc.TemporalRadianceStabilityFactor    = 0.8f;
    Desc.TemporalVarianceStabilityFactor    = 0.7f;
    Desc.BilateralCleanupSpatialSigmaFactor = 1.1f;

    const HLSL::ScreenSpaceReflectionAttribs Attribs = RadientPostFX::MakeSSRAttribs(Desc);
    EXPECT_FLOAT_EQ(Attribs.DepthBufferThickness, Desc.DepthBufferThickness);
    EXPECT_FLOAT_EQ(Attribs.RoughnessThreshold, Desc.RoughnessThreshold);
    EXPECT_EQ(Attribs.MostDetailedMip, Desc.MostDetailedMip);
    EXPECT_EQ(Attribs.MaxTraversalIntersections, Desc.MaxTraversalIntersections);
    EXPECT_FLOAT_EQ(Attribs.GGXImportanceSampleBias, Desc.GGXImportanceSampleBias);
    EXPECT_FLOAT_EQ(Attribs.SpatialReconstructionRadius, Desc.SpatialReconstructionRadius);
    EXPECT_FLOAT_EQ(Attribs.TemporalRadianceStabilityFactor, Desc.TemporalRadianceStabilityFactor);
    EXPECT_FLOAT_EQ(Attribs.TemporalVarianceStabilityFactor, Desc.TemporalVarianceStabilityFactor);
    EXPECT_FLOAT_EQ(Attribs.BilateralCleanupSpatialSigmaFactor, Desc.BilateralCleanupSpatialSigmaFactor);
    EXPECT_EQ(Attribs.IsRoughnessPerceptual, TRUE);
    EXPECT_EQ(Attribs.RoughnessChannel, 0u);
    EXPECT_FLOAT_EQ(Attribs.AlphaInterpolation, 1.f);
}

TEST(RadientPostFXParametersTest, ConvertsDepthOfFieldParametersAndFlags)
{
    RadientDepthOfFieldDesc Desc;
    Desc.Enabled                 = True;
    Desc.MaxCircleOfConfusion    = 0.02f;
    Desc.TemporalStabilityFactor = 0.8f;
    Desc.BokehKernelRingCount    = 4;
    Desc.BokehKernelRingDensity  = 6;

    const HLSL::DepthOfFieldAttribs Attribs = RadientPostFX::MakeDepthOfFieldAttribs(Desc);
    EXPECT_FLOAT_EQ(Attribs.MaxCircleOfConfusion, Desc.MaxCircleOfConfusion);
    EXPECT_FLOAT_EQ(Attribs.TemporalStabilityFactor, Desc.TemporalStabilityFactor);
    EXPECT_EQ(Attribs.BokehKernelRingCount, static_cast<Int32>(Desc.BokehKernelRingCount));
    EXPECT_EQ(Attribs.BokehKernelRingDensity, static_cast<Int32>(Desc.BokehKernelRingDensity));
    EXPECT_FLOAT_EQ(Attribs.AlphaInterpolation, 1.f);
    EXPECT_FLOAT_EQ(Attribs.Padding0, 0.f);
    EXPECT_FLOAT_EQ(Attribs.Padding1, 0.f);
    EXPECT_FLOAT_EQ(Attribs.Padding2, 0.f);

    Desc.TemporalSmoothing = False;
    Desc.KarisInverse      = False;
    EXPECT_EQ(RadientPostFX::GetDepthOfFieldFeatureFlags(Desc), DepthOfField::FEATURE_FLAG_NONE);

    Desc.TemporalSmoothing = True;
    EXPECT_EQ(RadientPostFX::GetDepthOfFieldFeatureFlags(Desc), DepthOfField::FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING);

    Desc.TemporalSmoothing = False;
    Desc.KarisInverse      = True;
    EXPECT_EQ(RadientPostFX::GetDepthOfFieldFeatureFlags(Desc), DepthOfField::FEATURE_FLAG_ENABLE_KARIS_INVERSE);

    Desc.TemporalSmoothing = True;
    EXPECT_EQ(RadientPostFX::GetDepthOfFieldFeatureFlags(Desc),
              DepthOfField::FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING |
                  DepthOfField::FEATURE_FLAG_ENABLE_KARIS_INVERSE);
}

} // namespace Diligent
