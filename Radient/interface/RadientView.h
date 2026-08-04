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

/// \file
/// Defines Radient render view interfaces.

#include "RadientScene.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientRenderTarget IRadientRenderTarget;
typedef struct IRadientView         IRadientView;

// clang-format off

/// Debug visualization mode.
///
/// Render techniques may use these modes to replace the regular shaded output
/// with diagnostic material, geometry, lighting, or depth information.
DILIGENT_TYPED_ENUM(RADIENT_DEBUG_VISUALIZATION, Uint8)
{
    /// Render the regular shaded image.
    RADIENT_DEBUG_VISUALIZATION_NONE = 0,

    RADIENT_DEBUG_VISUALIZATION_TEXCOORD0,
    RADIENT_DEBUG_VISUALIZATION_TEXCOORD1,
    RADIENT_DEBUG_VISUALIZATION_BASE_COLOR,
    RADIENT_DEBUG_VISUALIZATION_TRANSPARENCY,
    RADIENT_DEBUG_VISUALIZATION_OCCLUSION,
    RADIENT_DEBUG_VISUALIZATION_EMISSIVE,
    RADIENT_DEBUG_VISUALIZATION_METALLIC,
    RADIENT_DEBUG_VISUALIZATION_ROUGHNESS,
    RADIENT_DEBUG_VISUALIZATION_DIFFUSE_COLOR,
    RADIENT_DEBUG_VISUALIZATION_SPECULAR_COLOR,
    RADIENT_DEBUG_VISUALIZATION_REFLECTANCE90,
    RADIENT_DEBUG_VISUALIZATION_MESH_NORMAL,
    RADIENT_DEBUG_VISUALIZATION_SHADING_NORMAL,
    RADIENT_DEBUG_VISUALIZATION_MOTION_VECTORS,
    RADIENT_DEBUG_VISUALIZATION_N_DOT_V,
    RADIENT_DEBUG_VISUALIZATION_PUNCTUAL_LIGHTING,
    RADIENT_DEBUG_VISUALIZATION_DIFFUSE_IBL,
    RADIENT_DEBUG_VISUALIZATION_SPECULAR_IBL,
    RADIENT_DEBUG_VISUALIZATION_WHITE_BASE_COLOR,
    RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT,
    RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_FACTOR,
    RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_ROUGHNESS,
    RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_NORMAL,
    RADIENT_DEBUG_VISUALIZATION_SHEEN,
    RADIENT_DEBUG_VISUALIZATION_SHEEN_COLOR,
    RADIENT_DEBUG_VISUALIZATION_SHEEN_ROUGHNESS,
    RADIENT_DEBUG_VISUALIZATION_ANISOTROPY_STRENGTH,
    RADIENT_DEBUG_VISUALIZATION_ANISOTROPY_DIRECTION,
    RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE,
    RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE_FACTOR,
    RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE_THICKNESS,
    RADIENT_DEBUG_VISUALIZATION_TRANSMISSION,
    RADIENT_DEBUG_VISUALIZATION_THICKNESS,
    RADIENT_DEBUG_VISUALIZATION_SCENE_DEPTH,

    RADIENT_DEBUG_VISUALIZATION_COUNT
};

/// Tone mapping mode.
DILIGENT_TYPED_ENUM(RADIENT_TONE_MAPPING_MODE, Uint8)
{
    RADIENT_TONE_MAPPING_MODE_NONE = 0,
    RADIENT_TONE_MAPPING_MODE_EXP,
    RADIENT_TONE_MAPPING_MODE_REINHARD,
    RADIENT_TONE_MAPPING_MODE_REINHARD_MOD,
    RADIENT_TONE_MAPPING_MODE_UNCHARTED2,
    RADIENT_TONE_MAPPING_MODE_FILMIC_ALU,
    RADIENT_TONE_MAPPING_MODE_LOGARITHMIC,
    RADIENT_TONE_MAPPING_MODE_ADAPTIVE_LOG,
    RADIENT_TONE_MAPPING_MODE_AGX,
    RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM,
    RADIENT_TONE_MAPPING_MODE_PBR_NEUTRAL,
    RADIENT_TONE_MAPPING_MODE_COMMERCE,
    RADIENT_TONE_MAPPING_MODE_COUNT
};

/// Screen-space ambient occlusion algorithm.
DILIGENT_TYPED_ENUM(RADIENT_SSAO_ALGORITHM, Uint8)
{
    /// Ground-truth ambient occlusion using cosine-weighted horizon integration.
    RADIENT_SSAO_ALGORITHM_GTAO = 0,

    /// Horizon-based ambient occlusion using uniformly weighted horizon integration.
    RADIENT_SSAO_ALGORITHM_HBAO,

    /// Visibility-bitmask ambient occlusion for partial occlusion by thin geometry.
    RADIENT_SSAO_ALGORITHM_VBAO,

    RADIENT_SSAO_ALGORITHM_COUNT
};

/// Skybox source.
DILIGENT_TYPED_ENUM(RADIENT_SKYBOX_SOURCE, Uint8)
{
    /// Do not render a skybox.
    RADIENT_SKYBOX_SOURCE_NONE = 0,

    /// Render the view environment map as the skybox.
    RADIENT_SKYBOX_SOURCE_ENVIRONMENT,

    /// Render the explicit texture asset as the skybox.
    RADIENT_SKYBOX_SOURCE_TEXTURE
};

// clang-format on

/// Custom AgX tone mapping parameters.
struct RadientToneMappingAgXDesc
{
    Float32 Saturation DEFAULT_INITIALIZER(1.f);
    Float32 Slope      DEFAULT_INITIALIZER(1.f);
    Float32 Power      DEFAULT_INITIALIZER(1.f);
    Float32 Offset     DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientToneMappingAgXDesc& Rhs) const
    {
        return (Saturation == Rhs.Saturation &&
                Slope == Rhs.Slope &&
                Power == Rhs.Power &&
                Offset == Rhs.Offset);
    }

    constexpr bool operator!=(const RadientToneMappingAgXDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientToneMappingAgXDesc RadientToneMappingAgXDesc;

/// Tone mapping settings shared by all render techniques.
struct RadientToneMappingDesc
{
    /// Tone mapping operator. RADIENT_TONE_MAPPING_MODE_NONE disables tone mapping.
    RADIENT_TONE_MAPPING_MODE Mode DEFAULT_INITIALIZER(RADIENT_TONE_MAPPING_MODE_UNCHARTED2);

    /// Enables automatic exposure calculation.
    Bool AutoExposure DEFAULT_INITIALIZER(True);

    /// Middle gray value used by tone mapping operators.
    Float32 MiddleGray DEFAULT_INITIALIZER(0.18f);

    /// Enables temporal adaptation to luminance changes.
    Bool LightAdaptation DEFAULT_INITIALIZER(True);

    /// White point used by tone mapping operators.
    Float32 WhitePoint DEFAULT_INITIALIZER(3.f);

    /// Luminance saturation applied by compatible tone mapping operators.
    Float32 LuminanceSaturation DEFAULT_INITIALIZER(1.f);

    /// Parameters used by RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM.
    RadientToneMappingAgXDesc AgX DEFAULT_INITIALIZER({});

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientToneMappingDesc& Rhs) const
    {
        return (Mode == Rhs.Mode &&
                AutoExposure == Rhs.AutoExposure &&
                MiddleGray == Rhs.MiddleGray &&
                LightAdaptation == Rhs.LightAdaptation &&
                WhitePoint == Rhs.WhitePoint &&
                LuminanceSaturation == Rhs.LuminanceSaturation &&
                AgX == Rhs.AgX);
    }

    constexpr bool operator!=(const RadientToneMappingDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientToneMappingDesc RadientToneMappingDesc;

/// Bloom post-processing settings shared by all render techniques.
struct RadientBloomDesc
{
    /// Enables the Bloom effect.
    Bool Enabled DEFAULT_INITIALIZER(False);

    /// Bloom contribution to the final image.
    Float32 Intensity DEFAULT_INITIALIZER(0.15f);

    /// Minimum brightness that contributes to Bloom.
    Float32 Threshold DEFAULT_INITIALIZER(1.f);

    /// Softness of the brightness threshold, in the [0, 1] range.
    Float32 SoftThreshold DEFAULT_INITIALIZER(0.125f);

    /// Bloom radius, in the [0, 1] range.
    Float32 Radius DEFAULT_INITIALIZER(0.75f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientBloomDesc& Rhs) const
    {
        return (Enabled == Rhs.Enabled &&
                Intensity == Rhs.Intensity &&
                Threshold == Rhs.Threshold &&
                SoftThreshold == Rhs.SoftThreshold &&
                Radius == Rhs.Radius);
    }

    constexpr bool operator!=(const RadientBloomDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientBloomDesc RadientBloomDesc;

/// Temporal anti-aliasing settings shared by all render techniques.
struct RadientTemporalAntiAliasingDesc
{
    /// Enables temporal anti-aliasing.
    Bool Enabled DEFAULT_INITIALIZER(False);

    /// Historical-frame contribution, in the [0, 1] range.
    Float32 TemporalStabilityFactor DEFAULT_INITIALIZER(0.9375f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientTemporalAntiAliasingDesc& Rhs) const
    {
        return Enabled == Rhs.Enabled &&
            TemporalStabilityFactor == Rhs.TemporalStabilityFactor;
    }

    constexpr bool operator!=(const RadientTemporalAntiAliasingDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientTemporalAntiAliasingDesc RadientTemporalAntiAliasingDesc;

/// Per-view screen-space ambient occlusion settings.
/// Currently consumed by the Tessera render technique.
struct RadientSSAODesc
{
    /// Enables screen-space ambient occlusion.
    Bool Enabled DEFAULT_INITIALIZER(False);

    /// Ambient occlusion algorithm.
    RADIENT_SSAO_ALGORITHM Algorithm DEFAULT_INITIALIZER(RADIENT_SSAO_ALGORITHM_GTAO);

    /// World-space radius of ambient occlusion.
    Float32 EffectRadius DEFAULT_INITIALIZER(1.f);

    /// Fraction of EffectRadius over which sample contribution falls off, in the [0, 1] range.
    Float32 EffectFalloffRange DEFAULT_INITIALIZER(0.615f);

    /// Radius correction used to compensate for screen-space sampling bias.
    Float32 RadiusMultiplier DEFAULT_INITIALIZER(1.457f);

    /// Depth-mip sampling offset controlling the bandwidth and quality tradeoff.
    Float32 DepthMIPSamplingOffset DEFAULT_INITIALIZER(3.3f);

    /// Historical-frame contribution, in the [0, 1] range.
    Float32 TemporalStabilityFactor DEFAULT_INITIALIZER(0.9f);

    /// Spatial reconstruction kernel radius.
    Float32 SpatialReconstructionRadius DEFAULT_INITIALIZER(4.f);

    /// View-space occluder thickness used by the visibility-bitmask algorithm.
    Float32 BitmaskThickness DEFAULT_INITIALIZER(0.5f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientSSAODesc& Rhs) const
    {
        return Enabled == Rhs.Enabled &&
            Algorithm == Rhs.Algorithm &&
            EffectRadius == Rhs.EffectRadius &&
            EffectFalloffRange == Rhs.EffectFalloffRange &&
            RadiusMultiplier == Rhs.RadiusMultiplier &&
            DepthMIPSamplingOffset == Rhs.DepthMIPSamplingOffset &&
            TemporalStabilityFactor == Rhs.TemporalStabilityFactor &&
            SpatialReconstructionRadius == Rhs.SpatialReconstructionRadius &&
            BitmaskThickness == Rhs.BitmaskThickness;
    }

    constexpr bool operator!=(const RadientSSAODesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientSSAODesc RadientSSAODesc;

/// Per-view screen-space reflection settings.
/// Currently consumed by the Tessera render technique.
struct RadientSSRDesc
{
    /// Enables screen-space reflections.
    Bool Enabled DEFAULT_INITIALIZER(False);

    /// Depth comparison tolerance used when accepting ray intersections, in the [0, 1] range.
    Float32 DepthBufferThickness DEFAULT_INITIALIZER(0.025f);

    /// Surfaces rougher than this value do not produce screen-space reflection rays, in the [0, 1] range.
    Float32 RoughnessThreshold DEFAULT_INITIALIZER(0.2f);

    /// Most detailed depth-hierarchy mip used for non-mirror surfaces, in the [0, 6] range.
    Uint32 MostDetailedMip DEFAULT_INITIALIZER(0);

    /// Maximum number of depth-hierarchy intersections evaluated for one ray.
    Uint32 MaxTraversalIntersections DEFAULT_INITIALIZER(128);

    /// GGX importance-sampling bias, in the [0, 1] range.
    Float32 GGXImportanceSampleBias DEFAULT_INITIALIZER(0.3f);

    /// Spatial reconstruction kernel radius.
    Float32 SpatialReconstructionRadius DEFAULT_INITIALIZER(4.f);

    /// Historical radiance contribution, in the [0, 1] range.
    Float32 TemporalRadianceStabilityFactor DEFAULT_INITIALIZER(1.f);

    /// Historical variance contribution, in the [0, 1] range.
    Float32 TemporalVarianceStabilityFactor DEFAULT_INITIALIZER(0.9f);

    /// Spatial sigma of the bilateral cleanup filter.
    Float32 BilateralCleanupSpatialSigmaFactor DEFAULT_INITIALIZER(0.9f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientSSRDesc& Rhs) const
    {
        return Enabled == Rhs.Enabled &&
            DepthBufferThickness == Rhs.DepthBufferThickness &&
            RoughnessThreshold == Rhs.RoughnessThreshold &&
            MostDetailedMip == Rhs.MostDetailedMip &&
            MaxTraversalIntersections == Rhs.MaxTraversalIntersections &&
            GGXImportanceSampleBias == Rhs.GGXImportanceSampleBias &&
            SpatialReconstructionRadius == Rhs.SpatialReconstructionRadius &&
            TemporalRadianceStabilityFactor == Rhs.TemporalRadianceStabilityFactor &&
            TemporalVarianceStabilityFactor == Rhs.TemporalVarianceStabilityFactor &&
            BilateralCleanupSpatialSigmaFactor == Rhs.BilateralCleanupSpatialSigmaFactor;
    }

    constexpr bool operator!=(const RadientSSRDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientSSRDesc RadientSSRDesc;

/// Per-view depth-of-field settings.
/// Currently consumed by the Tessera render technique.
struct RadientDepthOfFieldDesc
{
    /// Enables depth of field.
    Bool Enabled DEFAULT_INITIALIZER(False);

    /// Maximum circle-of-confusion radius in texture coordinates.
    Float32 MaxCircleOfConfusion DEFAULT_INITIALIZER(0.01f);

    /// Historical circle-of-confusion contribution, in the [0, 1] range.
    Float32 TemporalStabilityFactor DEFAULT_INITIALIZER(0.9375f);

    /// Number of rings in the bokeh kernel, in the [2, 5] range.
    Uint32 BokehKernelRingCount DEFAULT_INITIALIZER(5);

    /// Number of samples contributed by each bokeh-kernel ring, in the [2, 7] range.
    Uint32 BokehKernelRingDensity DEFAULT_INITIALIZER(7);

    /// Enables temporal smoothing of the circle of confusion.
    Bool TemporalSmoothing DEFAULT_INITIALIZER(True);

    /// Enables inverse Karis weighting to emphasize bokeh circles.
    Bool KarisInverse DEFAULT_INITIALIZER(True);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientDepthOfFieldDesc& Rhs) const
    {
        return Enabled == Rhs.Enabled &&
            MaxCircleOfConfusion == Rhs.MaxCircleOfConfusion &&
            TemporalStabilityFactor == Rhs.TemporalStabilityFactor &&
            BokehKernelRingCount == Rhs.BokehKernelRingCount &&
            BokehKernelRingDensity == Rhs.BokehKernelRingDensity &&
            TemporalSmoothing == Rhs.TemporalSmoothing &&
            KarisInverse == Rhs.KarisInverse;
    }

    constexpr bool operator!=(const RadientDepthOfFieldDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientDepthOfFieldDesc RadientDepthOfFieldDesc;

/// Skybox description.
struct RadientSkyboxDesc
{
    /// Skybox source.
    RADIENT_SKYBOX_SOURCE Source DEFAULT_INITIALIZER(RADIENT_SKYBOX_SOURCE_NONE);

    /// Explicit skybox texture used when Source is RADIENT_SKYBOX_SOURCE_TEXTURE.
    IRadientTextureAsset* pTexture DEFAULT_INITIALIZER(nullptr);

    /// Skybox color multiplier.
    RadientFloat3 Color DEFAULT_INITIALIZER({1.f, 1.f, 1.f});

    /// Skybox intensity multiplier.
    Float32 Intensity DEFAULT_INITIALIZER(1.f);

    /// Exposure multiplier as a power of 2.
    Float32 Exposure DEFAULT_INITIALIZER(0.f);

    /// Mip level used for skybox sampling.
    Float32 MipLevel DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    bool operator==(const RadientSkyboxDesc& Rhs) const
    {
        return Source == Rhs.Source &&
            pTexture == Rhs.pTexture &&
            Color == Rhs.Color &&
            Intensity == Rhs.Intensity &&
            Exposure == Rhs.Exposure &&
            MipLevel == Rhs.MipLevel;
    }

    bool operator!=(const RadientSkyboxDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientSkyboxDesc RadientSkyboxDesc;

/// View environment used for image-based lighting.
struct RadientEnvironmentDesc
{
    /// Environment map texture asset. New views use cleared IBL maps until the asset is ready.
    /// When the environment changes, the current IBL remains active until the replacement is ready.
    IRadientTextureAsset* pEnvironmentMap DEFAULT_INITIALIZER(nullptr);

    /// Environment color multiplier.
    RadientFloat3 Color DEFAULT_INITIALIZER({1.f, 1.f, 1.f});

    /// Environment intensity multiplier.
    Float32 Intensity DEFAULT_INITIALIZER(1.f);

    /// Exposure multiplier as a power of 2.
    Float32 Exposure DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    bool operator==(const RadientEnvironmentDesc& Rhs) const
    {
        return pEnvironmentMap == Rhs.pEnvironmentMap &&
            Color == Rhs.Color &&
            Intensity == Rhs.Intensity &&
            Exposure == Rhs.Exposure;
    }

    bool operator!=(const RadientEnvironmentDesc& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientEnvironmentDesc RadientEnvironmentDesc;

/// View description.
///
/// A view describes one persistent way to render a scene: which scene is
/// rendered, which camera is used, and where the image is written.
struct RadientViewDesc
{
    /// View name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Scene to render.
    IRadientScene* pScene DEFAULT_INITIALIZER(nullptr);

    /// Camera entity to render from.
    RadientEntityID Camera DEFAULT_INITIALIZER(InvalidRadientEntityID);

    /// Render target.
    IRadientRenderTarget* pRenderTarget DEFAULT_INITIALIZER(nullptr);

    /// Debug visualization mode.
    RADIENT_DEBUG_VISUALIZATION DebugVisualization DEFAULT_INITIALIZER(RADIENT_DEBUG_VISUALIZATION_NONE);

    /// Environment used for image-based lighting.
    RadientEnvironmentDesc Environment DEFAULT_INITIALIZER({});

    /// Skybox rendered by the view.
    RadientSkyboxDesc Skybox DEFAULT_INITIALIZER({});

    /// Tone mapping settings.
    RadientToneMappingDesc ToneMapping DEFAULT_INITIALIZER({});

    /// Bloom settings.
    RadientBloomDesc Bloom DEFAULT_INITIALIZER({});

    /// Temporal anti-aliasing settings.
    RadientTemporalAntiAliasingDesc TemporalAntiAliasing DEFAULT_INITIALIZER({});

    /// Screen-space ambient occlusion settings.
    RadientSSAODesc SSAO DEFAULT_INITIALIZER({});

    /// Screen-space reflection settings.
    RadientSSRDesc SSR DEFAULT_INITIALIZER({});

    /// Depth-of-field settings.
    RadientDepthOfFieldDesc DepthOfField DEFAULT_INITIALIZER({});
};
typedef struct RadientViewDesc RadientViewDesc;

// {6BFFEECB-D937-44E2-80E6-696236759008}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientView =
    {0x6bffeecb, 0xd937, 0x44e2, {0x80, 0xe6, 0x69, 0x62, 0x36, 0x75, 0x90, 0x8}};

#define DILIGENT_INTERFACE_NAME IRadientView
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientViewInclusiveMethods \
    IObjectInclusiveMethods;         \
    IRadientViewMethods RadientView

// clang-format off

/// Persistent render view interface.
DILIGENT_BEGIN_INTERFACE(IRadientView, IObject)
{
    /// Returns the view description.
    VIRTUAL const RadientViewDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Sets the scene rendered by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetScene)(THIS_
                                            IRadientScene* pScene) PURE;

    /// Sets the camera rendered by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetCamera)(THIS_
                                             RadientEntityID Camera) PURE;

    /// Sets the render target used by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetRenderTarget)(THIS_
                                                   IRadientRenderTarget* pRenderTarget) PURE;

    /// Sets the debug visualization mode.
    VIRTUAL RADIENT_STATUS METHOD(SetDebugVisualization)(THIS_
                                                         RADIENT_DEBUG_VISUALIZATION DebugVisualization) PURE;

    /// Sets the environment used for image-based lighting.
    VIRTUAL RADIENT_STATUS METHOD(SetEnvironment)(THIS_
                                                  const RadientEnvironmentDesc REF Environment) PURE;

    /// Sets the skybox rendered by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetSkybox)(THIS_
                                             const RadientSkyboxDesc REF Skybox) PURE;

    /// Sets tone mapping settings.
    VIRTUAL RADIENT_STATUS METHOD(SetToneMapping)(THIS_
                                                  const RadientToneMappingDesc REF ToneMapping) PURE;

    /// Sets Bloom settings.
    VIRTUAL RADIENT_STATUS METHOD(SetBloom)(THIS_
                                            const RadientBloomDesc REF Bloom) PURE;

    /// Sets temporal anti-aliasing settings.
    VIRTUAL RADIENT_STATUS METHOD(SetTemporalAntiAliasing)(THIS_
                                                           const RadientTemporalAntiAliasingDesc REF TemporalAntiAliasing) PURE;

    /// Sets screen-space ambient occlusion settings.
    VIRTUAL RADIENT_STATUS METHOD(SetSSAO)(THIS_
                                           const RadientSSAODesc REF SSAO) PURE;

    /// Sets screen-space reflection settings.
    VIRTUAL RADIENT_STATUS METHOD(SetSSR)(THIS_
                                          const RadientSSRDesc REF SSR) PURE;

    /// Sets depth-of-field settings.
    VIRTUAL RADIENT_STATUS METHOD(SetDepthOfField)(THIS_
                                                   const RadientDepthOfFieldDesc REF DepthOfField) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientView_GetDesc(This)                CALL_IFACE_METHOD(RadientView, GetDesc,         This)
#    define IRadientView_SetScene(This, ...)          CALL_IFACE_METHOD(RadientView, SetScene,        This, __VA_ARGS__)
#    define IRadientView_SetCamera(This, ...)         CALL_IFACE_METHOD(RadientView, SetCamera,       This, __VA_ARGS__)
#    define IRadientView_SetRenderTarget(This, ...)   CALL_IFACE_METHOD(RadientView, SetRenderTarget, This, __VA_ARGS__)
#    define IRadientView_SetDebugVisualization(This, ...) CALL_IFACE_METHOD(RadientView, SetDebugVisualization, This, __VA_ARGS__)
#    define IRadientView_SetEnvironment(This, ...)    CALL_IFACE_METHOD(RadientView, SetEnvironment,  This, __VA_ARGS__)
#    define IRadientView_SetSkybox(This, ...)         CALL_IFACE_METHOD(RadientView, SetSkybox,       This, __VA_ARGS__)
#    define IRadientView_SetToneMapping(This, ...)    CALL_IFACE_METHOD(RadientView, SetToneMapping,  This, __VA_ARGS__)
#    define IRadientView_SetBloom(This, ...)          CALL_IFACE_METHOD(RadientView, SetBloom,        This, __VA_ARGS__)
#    define IRadientView_SetTemporalAntiAliasing(This, ...) CALL_IFACE_METHOD(RadientView, SetTemporalAntiAliasing, This, __VA_ARGS__)
#    define IRadientView_SetSSAO(This, ...)           CALL_IFACE_METHOD(RadientView, SetSSAO,         This, __VA_ARGS__)
#    define IRadientView_SetSSR(This, ...)            CALL_IFACE_METHOD(RadientView, SetSSR,          This, __VA_ARGS__)
#    define IRadientView_SetDepthOfField(This, ...)   CALL_IFACE_METHOD(RadientView, SetDepthOfField, This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
