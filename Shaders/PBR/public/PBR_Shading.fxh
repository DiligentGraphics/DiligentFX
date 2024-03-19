#ifndef _PBR_SHADING_FXH_
#define _PBR_SHADING_FXH_

#include "PBR_Structures.fxh"
#include "PBR_Common.fxh"
#include "ShaderUtilities.fxh"
#include "SRGBUtilities.fxh"

#ifndef TEX_COLOR_CONVERSION_MODE_NONE
#   define TEX_COLOR_CONVERSION_MODE_NONE 0
#endif

#ifndef TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR
#   define TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR 1
#endif

#ifndef TEX_COLOR_CONVERSION_MODE
#   define TEX_COLOR_CONVERSION_MODE TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR
#endif

#if TEX_COLOR_CONVERSION_MODE == TEX_COLOR_CONVERSION_MODE_NONE
#   define  TO_LINEAR(x) (x)
#elif TEX_COLOR_CONVERSION_MODE == TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR
#   define  TO_LINEAR FastSRGBToLinear
#endif

#ifndef USE_IBL_ENV_MAP_LOD
#   define USE_IBL_ENV_MAP_LOD 1
#endif

#ifndef USE_HDR_IBL_CUBEMAPS
#   define USE_HDR_IBL_CUBEMAPS 1
#endif

#ifndef USE_IBL_MULTIPLE_SCATTERING
#   define USE_IBL_MULTIPLE_SCATTERING 1
#endif


#ifndef ENABLE_CLEAR_COAT
#   define ENABLE_CLEAR_COAT 0
#endif
    
#ifndef ENABLE_SHEEN
#   define ENABLE_SHEEN 0
#endif

#ifndef ENABLE_ANISOTROPY
#   define ENABLE_ANISOTROPY 0
#endif
    
#ifndef ENABLE_IRIDESCENCE
#   define ENABLE_IRIDESCENCE 0
#endif

#ifndef ENABLE_TRANSMISSION
#   define ENABLE_TRANSMISSION 0
#endif
    
#ifndef ENABLE_VOLUME
#   define ENABLE_VOLUME 0
#endif

#ifndef USE_IBL
#   define USE_IBL 1
#endif


#ifndef PBR_LIGHT_TYPE_DIRECTIONAL
#   define PBR_LIGHT_TYPE_DIRECTIONAL 1
#endif

#ifndef PBR_LIGHT_TYPE_POINT
#   define PBR_LIGHT_TYPE_POINT 2
#endif

#ifndef PBR_LIGHT_TYPE_SPOT
#   define PBR_LIGHT_TYPE_SPOT 3
#endif


#ifndef ENABLE_SHADOWS
#   define ENABLE_SHADOWS 0
#endif

#if ENABLE_SHADOWS
#   ifndef PCF_FILTER_SIZE
#       define PCF_FILTER_SIZE 3
#   endif
#   include "PCF.fxh"
#endif

float GetPerceivedBrightness(float3 rgb)
{
    return sqrt(0.299 * rgb.r * rgb.r + 0.587 * rgb.g * rgb.g + 0.114 * rgb.b * rgb.b);
}

// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows/js/three.pbrUtilities.js#L34
float SolveMetallic(float3 diffuse,
                    float3 specular,
                    float  oneMinusSpecularStrength)
{
    const float c_MinReflectance = 0.04;
    float specularBrightness = GetPerceivedBrightness(specular);
    if (specularBrightness < c_MinReflectance)
    {
        return 0.0;
    }

    float diffuseBrightness = GetPerceivedBrightness(diffuse);

    float a = c_MinReflectance;
    float b = diffuseBrightness * oneMinusSpecularStrength / (1.0 - c_MinReflectance) + specularBrightness - 2.0 * c_MinReflectance;
    float c = c_MinReflectance - specularBrightness;
    float D = b * b - 4.0 * a * c;

    return clamp((-b + sqrt(D)) / (2.0 * a), 0.0, 1.0);
}

float3 ApplyDirectionalLightGGX(float3 lightDir, float3 lightColor, SurfaceReflectanceInfo srfInfo, float3 N, float3 V)
{
    float3 L = -lightDir;
    float3 diffuseContrib, specContrib;
    float  NdotL;
    SmithGGX_BRDF(L, N, V, srfInfo, diffuseContrib, specContrib, NdotL);
    // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
    float3 shade = (diffuseContrib + specContrib) * NdotL;
    return lightColor * shade;
}

float3 ApplyDirectionalLightSheen(float3 lightDir, float3 lightColor, float3 SheenColor, float SheenRoughness, float3 N, float3 V)
{
    float3 L = -lightDir;
    float3 H = normalize(V + L);
    float NdotL = dot_sat(N, L);
    float NdotV = dot_sat(N, V);
    float NdotH = dot_sat(N, H);

    return lightColor * NdotL * SheenSpecularBRDF(SheenColor, SheenRoughness, NdotL, NdotV, NdotH);
}

struct PerturbNormalInfo
{
    float3 dPos_dx;
    float3 dPos_dy;
    float3 Normal;
    float  Face; // +1.0 if front face, -1.0 if back face
};

PerturbNormalInfo GetPerturbNormalInfo(in float3 Pos,           // World position
                                       in float3 Normal,        // Mesh normal
                                       in bool   IsFrontFace,   // True if front face
                                       in float  CameraHandness // 1.0 if right-handed, -1.0 if left-handed
                                      )
{
    PerturbNormalInfo Info;
    Info.dPos_dx = ddx(Pos);
    Info.dPos_dy = ddy(Pos);
    Info.Face    = IsFrontFace ? +1.0 : -1.0;

    float NormalLen = length(Normal);
    if (NormalLen > 1e-5)
    {
        Info.Normal = Normal / (NormalLen * Info.Face);
    }
    else
    {
        Info.Normal = normalize(cross(Info.dPos_dx, Info.dPos_dy)) * CameraHandness;
#if (defined(GLSL) || defined(GL_ES)) && !defined(VULKAN)
        // In OpenGL, the screen is upside-down, so we have to invert the vector
        Info.Normal *= -1.0;
#endif
        // The normal computed from gradients is always facing the viewer
    }
    
    return Info;
}


// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
float3 PerturbNormal(PerturbNormalInfo NormalInfo,
                     in float2         dUV_dx,
                     in float2         dUV_dy,
                     in float3         TSNormal,
                     bool              HasUV)
{
    if (HasUV)
    {
        TSNormal.xy *= NormalInfo.Face;
        return TransformTangentSpaceNormalGrad(NormalInfo.dPos_dx, NormalInfo.dPos_dy, dUV_dx, dUV_dy, NormalInfo.Normal, TSNormal);
    }
    else
    {
        return NormalInfo.Normal;
    }
}

float3 SamplePrefilteredEnvMap(in TextureCube  PrefilteredEnvMap,
                               in SamplerState PrefilteredEnvMap_sampler,
                               in float3       Dir,
                               in float        LOD)
{
#if USE_IBL_ENV_MAP_LOD
    float3 Sample = PrefilteredEnvMap.SampleLevel(PrefilteredEnvMap_sampler, Dir, LOD).rgb;
#else
    float3 Sample = PrefilteredEnvMap.Sample(PrefilteredEnvMap_sampler, Dir).rgb;
#endif

#if USE_HDR_IBL_CUBEMAPS
    // Already linear.
    return Sample;
#else
    return TO_LINEAR(Sample);
#endif
}

struct IBLSamplingInfo
{
    float3 N;
    float3 V;
    float3 L;
    float  NdotV;
    float2 PreIntBRDF;
#if USE_IBL_MULTIPLE_SCATTERING
    float3 k_S;
#endif
};

IBLSamplingInfo GetIBLSamplingInfo(in SurfaceReflectanceInfo SrfInfo,
                                   in Texture2D    PreintegratedBRDF,
                                   in SamplerState PreintegratedBRDF_sampler,
#if ENABLE_IRIDESCENCE
                                   in float3       IridescenceFresnel,
                                   in float        IridescenceFactor,
#endif
                                   in float3       N,
                                   in float3       V)
{
    IBLSamplingInfo Info;

    Info.N = N;
    Info.V = V;
    Info.L = normalize(reflect(-V, N));
    
    Info.NdotV = dot_sat(N, V);
    
    Info.PreIntBRDF = PreintegratedBRDF.Sample(PreintegratedBRDF_sampler, float2(Info.NdotV, SrfInfo.PerceptualRoughness)).rg;

#   if USE_IBL_MULTIPLE_SCATTERING
    {
        // https://bruop.github.io/ibl/#single_scattering_results
        // Roughness-dependent fresnel, from Fdez-Aguera
        float OneMinusRoughness = 1.0 - SrfInfo.PerceptualRoughness;
        float3 Reflectance90 = max(float3(OneMinusRoughness, OneMinusRoughness, OneMinusRoughness), SrfInfo.Reflectance0);
        Info.k_S = SchlickReflection(Info.NdotV, SrfInfo.Reflectance0, Reflectance90);
#       if ENABLE_IRIDESCENCE
        {
            Info.k_S = lerp(Info.k_S, IridescenceFresnel, IridescenceFactor);
        }
#       endif
    }
#   endif

    return Info;
}

IBLSamplingInfo GetClearcoatIBLSamplingInfo(in SurfaceReflectanceInfo SrfInfo,
                                            in Texture2D              PreintegratedBRDF,
                                            in SamplerState           PreintegratedBRDF_sampler,
                                            in float3                 N,
                                            in float3                 V)
{
    IBLSamplingInfo Info;

    Info.N = N;
    Info.V = V;
    Info.L = normalize(reflect(-V, N));

    // Do not allow clear coat normal to face away from the view direction as this
    // produces artifacts (see https://github.com/DiligentGraphics/DiligentFX/issues/27).
    Info.NdotV = max(dot(N, V), 0.1);
    
    Info.PreIntBRDF = PreintegratedBRDF.Sample(PreintegratedBRDF_sampler, float2(Info.NdotV, SrfInfo.PerceptualRoughness)).rg;    
    Info.k_S        = SrfInfo.Reflectance0;

    return Info;
}

// Specular component of the image-based light (IBL) using the split-sum approximation.
float3 GetSpecularIBL_GGX(in SurfaceReflectanceInfo SrfInfo,
                          in IBLSamplingInfo        IBLInfo,
                          in float3                 SpecularLight)
{    
#if USE_IBL_MULTIPLE_SCATTERING
    // https://bruop.github.io/ibl/#single_scattering_results
    return SpecularLight * (IBLInfo.k_S * IBLInfo.PreIntBRDF.x + IBLInfo.PreIntBRDF.y);
#else
    return SpecularLight * (SrfInfo.Reflectance0 * IBLInfo.PreIntBRDF.x + SrfInfo.Reflectance90 * IBLInfo.PreIntBRDF.y);
#endif
}

// Specular component of the image-based light (IBL) using the split-sum approximation.
float3 GetSpecularIBL_GGX(in SurfaceReflectanceInfo SrfInfo,
                          in IBLSamplingInfo        IBLInfo,
                          in TextureCube            PrefilteredEnvMap,
                          in SamplerState           PrefilteredEnvMap_sampler,
                          in float                  PrefilteredEnvMapLastMip)
{
    float  lod = SrfInfo.PerceptualRoughness * PrefilteredEnvMapLastMip;
    float3 SpecularLight = SamplePrefilteredEnvMap(PrefilteredEnvMap, PrefilteredEnvMap_sampler, IBLInfo.L, lod);
    return GetSpecularIBL_GGX(SrfInfo, IBLInfo, SpecularLight);
}

float3 GetLambertianIBL(in SurfaceReflectanceInfo SrfInfo,
                        in IBLSamplingInfo        IBLInfo,
                        in TextureCube            IrradianceMap,
                        in SamplerState           IrradianceMap_sampler)
{    
    float3 Irradiance = IrradianceMap.Sample(IrradianceMap_sampler, IBLInfo.N).rgb;
#if !USE_HDR_IBL_CUBEMAPS
    Irradiance = TO_LINEAR(Irradiance);
#endif

#if USE_IBL_MULTIPLE_SCATTERING
    // A Multiple-Scattering Microfacet Model for Real-Time Image-based Lighting by Fdez-Aguera.
    // https://www.jcgt.org/published/0008/01/03/paper.pdf
    // Also: https://bruop.github.io/ibl/
        
    float3 FssEss = IBLInfo.k_S * IBLInfo.PreIntBRDF.x + IBLInfo.PreIntBRDF.y;
    float  Ess    = IBLInfo.PreIntBRDF.x + IBLInfo.PreIntBRDF.y;
    float  Ems    = 1.0 - Ess;
    float3 Favg   = SrfInfo.Reflectance0 + (float3(1.0, 1.0, 1.0) - SrfInfo.Reflectance0) / 21.0;
    float3 Fms    = FssEss * Favg / (float3(1.0, 1.0, 1.0) - Ems * Favg);
    
    float3 Edss = float3(1.0, 1.0, 1.0) - (FssEss + Fms * Ems);
    float3 kD   = SrfInfo.DiffuseColor * Edss;

    return (Fms * Ems + kD) * Irradiance;
#else
    return Irradiance * SrfInfo.DiffuseColor;
#endif
}

float3 GetSpecularIBL_Charlie(in float3       SheenColor,
                              in float        SheenRoughness,
                              in float3       n,
                              in float3       v,
                              in float        PrefilteredCubeLastMip,
                              in Texture2D    PreintegratedCharlie,
                              in SamplerState PreintegratedCharlie_sampler,
                              in TextureCube  PrefilteredEnvMap,
                              in SamplerState PrefilteredEnvMap_sampler)
{
    float NdotV = dot_sat(n, v);

    float  lod        = SheenRoughness * PrefilteredCubeLastMip;
    float3 reflection = normalize(reflect(-v, n));

    float  brdf = PreintegratedCharlie.Sample(PreintegratedCharlie_sampler, float2(NdotV, SheenRoughness)).r;

    float3 SpecularLight = SamplePrefilteredEnvMap(PrefilteredEnvMap, PrefilteredEnvMap_sampler, reflection, lod);
    return SpecularLight * SheenColor * brdf;
}


/// Calculates surface reflectance info

/// \param [in]  Workflow     - PBR workflow (PBR_WORKFLOW_SPECULAR_GLOSINESS or PBR_WORKFLOW_METALLIC_ROUGHNESS).
/// \param [in]  BaseColor    - Material base color.
/// \param [in]  PhysicalDesc - Physical material description. For Metallic-roughness workflow,
///                             'g' channel stores roughness, 'b' channel stores metallic.
/// \param [out] Metallic     - Metallic value used for shading.
SurfaceReflectanceInfo GetSurfaceReflectance(int       Workflow,
                                             float4    BaseColor,
                                             float4    PhysicalDesc,
                                             out float Metallic)
{
    SurfaceReflectanceInfo SrfInfo;

    float3 SpecularColor;

    float3 f0 = float3(0.04, 0.04, 0.04);

    // Metallic and Roughness material properties are packed together
    // In glTF, these factors can be specified by fixed scalar values
    // or from a metallic-roughness map
    if (Workflow == PBR_WORKFLOW_SPECULAR_GLOSINESS)
    {
        SrfInfo.PerceptualRoughness = 1.0 - PhysicalDesc.a; // glossiness to roughness
        f0 = PhysicalDesc.rgb;

        // f0 = specular
        SpecularColor = f0;
        float oneMinusSpecularStrength = 1.0 - max(max(f0.r, f0.g), f0.b);
        SrfInfo.DiffuseColor = BaseColor.rgb * oneMinusSpecularStrength;

        // do conversion between metallic M-R and S-G metallic
        Metallic = SolveMetallic(BaseColor.rgb, SpecularColor, oneMinusSpecularStrength);
    }
    else if (Workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
    {
        // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
        // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
        SrfInfo.PerceptualRoughness = PhysicalDesc.g;
        Metallic                    = PhysicalDesc.b;

        SrfInfo.DiffuseColor  = BaseColor.rgb * (float3(1.0, 1.0, 1.0) - f0) * (1.0 - Metallic);
        SpecularColor         = lerp(f0, BaseColor.rgb, Metallic);
    }

//#ifdef ALPHAMODE_OPAQUE
//    baseColor.a = 1.0;
//#endif
//
//#ifdef MATERIAL_UNLIT
//    gl_FragColor = float4(gammaCorrection(baseColor.rgb), baseColor.a);
//    return;
//#endif

    SrfInfo.PerceptualRoughness = clamp(SrfInfo.PerceptualRoughness, 0.0, 1.0);

    // Compute reflectance.
    float3 Reflectance0  = SpecularColor;
    float  MaxR0         = max(max(Reflectance0.r, Reflectance0.g), Reflectance0.b);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    float R90 = clamp(MaxR0 * 50.0, 0.0, 1.0);

    SrfInfo.Reflectance0  = Reflectance0;
    SrfInfo.Reflectance90 = float3(R90, R90, R90);

    return SrfInfo;
}

/// Calculates surface reflectance info for Metallic-roughness workflow
SurfaceReflectanceInfo GetSurfaceReflectanceMR(float3 BaseColor,
                                               float  Metallic,
                                               float  Roughness)
{
    SurfaceReflectanceInfo SrfInfo;

    float f0 = 0.04;

    SrfInfo.PerceptualRoughness = Roughness;
    SrfInfo.DiffuseColor        = BaseColor * ((1.0 - f0) * (1.0 - Metallic));

    float3 Reflectance0 = lerp(float3(f0, f0, f0), BaseColor, Metallic);
    float  MaxR0        = max(max(Reflectance0.r, Reflectance0.g), Reflectance0.b);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    float R90 = min(MaxR0 * 50.0, 1.0);

    SrfInfo.Reflectance0  = Reflectance0;
    SrfInfo.Reflectance90 = float3(R90, R90, R90);

    return SrfInfo;
}

/// Gets surface reflectance for the clear coat layer
SurfaceReflectanceInfo GetSurfaceReflectanceClearCoat(float Roughness, float  IOR)
{
    SurfaceReflectanceInfo SrfInfo;

    float f0 = (IOR - 1.0) / (IOR + 1.0);
    f0 *= f0;

    SrfInfo.PerceptualRoughness = Roughness;
    SrfInfo.DiffuseColor        = float3(0.0, 0.0, 0.0);

    float R90 = 1.0;
    SrfInfo.Reflectance0  = float3(f0, f0, f0);
    SrfInfo.Reflectance90 = float3(R90, R90, R90);

    return SrfInfo;
}


struct BaseLayerShadingInfo
{
    SurfaceReflectanceInfo Srf;
    
    float Metallic;
    
    // Shading normal in world space
    float3 Normal;
    
    float NdotV;
};

struct ClearcoatShadingInfo
{
    SurfaceReflectanceInfo Srf;

    // Clearcoat normal in world space
    float3 Normal;
    
    float Factor;
};

struct SheenShadingInfo
{
    float3 Color;
    float  Roughness;
};

struct IridescenceShadingInfo
{
    float  Factor;
    float  Thickness;
    float3 Fresnel;
    float3 F0;
};

struct AnisotropyShadingInfo
{
    float2 Direction;
    float  Strength;
    float3 Tangent;
    float3 Bitangent;
    float  AlphaRoughnessT;
    float  AlphaRoughnessB;
};

struct SurfaceShadingInfo
{
    // World space position
    float3 Pos;
    
    // Camera view direction in world space
    float3 View;
    
    float  Occlusion;
    float3 Emissive;

    BaseLayerShadingInfo BaseLayer;    
    
#if ENABLE_CLEAR_COAT
    ClearcoatShadingInfo Clearcoat;
#endif
    
#if ENABLE_SHEEN
    SheenShadingInfo Sheen;
#endif

#if ENABLE_ANISOTROPY
    AnisotropyShadingInfo Anisotropy;
#endif
    
#if ENABLE_IRIDESCENCE
    IridescenceShadingInfo Iridescence;
#endif

#if ENABLE_TRANSMISSION
    float Transmission;
#endif
    
#if ENABLE_VOLUME
    float VolumeThickness;
#endif
    
    float IBLScale;
};

struct LayerLightingInfo
{
    float3 Punctual;
    float3 DiffuseIBL;
    float3 SpecularIBL;
};

struct SurfaceLightingInfo
{
    LayerLightingInfo Base;

#if ENABLE_SHEEN
    LayerLightingInfo Sheen;
#endif
    
#if ENABLE_CLEAR_COAT
    LayerLightingInfo Clearcoat;
#endif
};

LayerLightingInfo GetDefaultLayerLightingInfo()
{
    LayerLightingInfo Lighting;
    Lighting.Punctual    = float3(0.0, 0.0, 0.0);
    Lighting.DiffuseIBL  = float3(0.0, 0.0, 0.0);
    Lighting.SpecularIBL = float3(0.0, 0.0, 0.0);
    return Lighting;
}

SurfaceLightingInfo GetDefaultSurfaceLightingInfo()
{
    SurfaceLightingInfo Lighting;
    Lighting.Base = GetDefaultLayerLightingInfo();

#if ENABLE_SHEEN
    Lighting.Sheen = GetDefaultLayerLightingInfo();
#endif

#if ENABLE_CLEAR_COAT
    Lighting.Clearcoat = GetDefaultLayerLightingInfo();
#endif

    return Lighting;
}

void ApplyPunctualLight(in    SurfaceShadingInfo     Shading,
                        in    PBRLightAttribs        Light,
#if ENABLE_SHEEN
                        in    Texture2D              AlbedoScalingLUT,
                        in    SamplerState           AlbedoScalingLUT_sampler,
#endif 
#if ENABLE_SHADOWS
                        in    Texture2DArray<float>  ShadowMap,
                        in    SamplerComparisonState ShadowMap_sampler,
                        in    PBRShadowMapInfo       ShadowMapInfo,
#endif
                        inout SurfaceLightingInfo    SrfLighting)
{
    float3 LightDirection = float3(Light.DirectionX, Light.DirectionY, Light.DirectionZ);

    float Attenuation = 1.0;
    if (Light.Type != PBR_LIGHT_TYPE_DIRECTIONAL)
    {
        float3 LightToPoint = Shading.Pos - float3(Light.PosX, Light.PosY, Light.PosZ);
        float  Distance2    = dot(LightToPoint, LightToPoint);
        LightToPoint /= sqrt(Distance2);
        float RangeAttenuation = 1.0 / Distance2;
        if (Light.Range4 > 0.0)
        {
            // Attenuation = clamp(1.0 - (Distance / Range)^4, 0, 1) / Distance^2
            RangeAttenuation *= saturate(1.0 - (Distance2 * Distance2) / Light.Range4);
        }
        
        if (Light.Type == PBR_LIGHT_TYPE_POINT)
        {
            LightDirection = LightToPoint;
        }

        float AngularAttenuation  = 1.0;        
        if (Light.Type == PBR_LIGHT_TYPE_SPOT)
        {
            float CosAngle     = dot(LightToPoint, LightDirection);
            AngularAttenuation = saturate(CosAngle * Light.SpotAngleScale + Light.SpotAngleOffset);
        }

        Attenuation = RangeAttenuation * AngularAttenuation; 
    }
    
#if ENABLE_SHADOWS
    if (Light.ShadowMapIndex >= 0)
    {
        float4 ShadowPos = mul(float4(Shading.Pos, 1.0), ShadowMapInfo.WorldToLightProjSpace);
        ShadowPos.xy /= ShadowPos.w;
        ShadowPos.xy = NormalizedDeviceXYToTexUV(ShadowPos.xy) * ShadowMapInfo.UVScale + ShadowMapInfo.UVBias;
        ShadowPos.z  = NormalizedDeviceZToDepth(ShadowPos.z);
        float4 ShadowMapSize;
        float Elems;
        ShadowMap.GetDimensions(ShadowMapSize.x, ShadowMapSize.y, Elems);
        ShadowMapSize.zw = float2(1.0, 1.0) / ShadowMapSize.xy;
        float Shadowing = FilterShadowMapFixedPCF(ShadowMap, ShadowMap_sampler, ShadowMapSize,
                                                  ShadowPos.xy, ShadowMapInfo.ShadowMapSlice, ShadowPos.z,
                                                  float2(0.0, 0.0));
        Attenuation *= Shadowing;
    }
#endif
    
    if (Attenuation <= 0.0)
        return;
    
    float3 LightIntensity = float3(Light.IntensityR, Light.IntensityG, Light.IntensityB) * Attenuation;
        
    float NdotV = Shading.BaseLayer.NdotV;
    float NdotL = dot_sat(Shading.BaseLayer.Normal, -LightDirection);
    
    float3 BasePunctual;
    {
        float3 BasePunctualDiffuse;
        float3 BasePunctualSpecular;
#       if ENABLE_ANISOTROPY
        {
            SmithGGX_BRDF_Anisotropic(-LightDirection,
                                      Shading.BaseLayer.Normal,
                                      Shading.View,
                                      Shading.Anisotropy.Tangent,
                                      Shading.Anisotropy.Bitangent,
                                      Shading.BaseLayer.Srf,
                                      Shading.Anisotropy.AlphaRoughnessT,
                                      Shading.Anisotropy.AlphaRoughnessB,
                                      BasePunctualDiffuse,
                                      BasePunctualSpecular,
                                      NdotL);
        }
#       else
        {
            SmithGGX_BRDF(-LightDirection, Shading.BaseLayer.Normal, Shading.View, Shading.BaseLayer.Srf, BasePunctualDiffuse, BasePunctualSpecular, NdotL);
        }
#       endif

#if ENABLE_TRANSMISSION
        {
            BasePunctualDiffuse *= 1.0 - Shading.Transmission;
        }
#endif
        BasePunctual = (BasePunctualDiffuse + BasePunctualSpecular) * LightIntensity * NdotL;
    }

#if ENABLE_SHEEN
    {
        SrfLighting.Sheen.Punctual += ApplyDirectionalLightSheen(LightDirection, LightIntensity, Shading.Sheen.Color, Shading.Sheen.Roughness, Shading.BaseLayer.Normal, Shading.View);
    
        float MaxFactor = max(max(Shading.Sheen.Color.r, Shading.Sheen.Color.g), Shading.Sheen.Color.b);
        float AlbedoScaling =
            min(1.0 - MaxFactor * AlbedoScalingLUT.Sample(AlbedoScalingLUT_sampler, float2(NdotV, Shading.Sheen.Roughness)).r,
                1.0 - MaxFactor * AlbedoScalingLUT.Sample(AlbedoScalingLUT_sampler, float2(NdotL, Shading.Sheen.Roughness)).r);
        BasePunctual *= AlbedoScaling;
    }
#endif
    
    SrfLighting.Base.Punctual += BasePunctual;

#if ENABLE_CLEAR_COAT
    {
        SrfLighting.Clearcoat.Punctual += ApplyDirectionalLightGGX(LightDirection, LightIntensity, Shading.Clearcoat.Srf, Shading.Clearcoat.Normal, Shading.View);
    }
#endif
}

#if USE_IBL
void ApplyIBL(in SurfaceShadingInfo Shading,
              in float              PrefilteredCubeLastMip,
              in Texture2D          PreintegratedGGX,
              in SamplerState       PreintegratedGGX_sampler,
              in TextureCube        IrradianceMap,
              in SamplerState       IrradianceMap_sampler,
              in TextureCube        PrefilteredEnvMap,
              in SamplerState       PrefilteredEnvMap_sampler,
#   if ENABLE_SHEEN
              in Texture2D    PreintegratedCharlie,
              in SamplerState PreintegratedCharlie_sampler,
#   endif
              inout SurfaceLightingInfo SrfLighting)
{
    {
        IBLSamplingInfo IBLInfo = GetIBLSamplingInfo(
            Shading.BaseLayer.Srf, PreintegratedGGX, PreintegratedGGX_sampler,
#           if ENABLE_IRIDESCENCE
                Shading.Iridescence.Fresnel, Shading.Iridescence.Factor,
#           endif
            Shading.BaseLayer.Normal, Shading.View);

        SrfLighting.Base.DiffuseIBL =
            GetLambertianIBL(Shading.BaseLayer.Srf, IBLInfo, IrradianceMap, IrradianceMap_sampler);
#       if ENABLE_TRANSMISSION
        {
            SrfLighting.Base.DiffuseIBL *= 1.0 - Shading.Transmission;
        }
#       endif

#       if ENABLE_ANISOTROPY
        {
            // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_anisotropy#image-based-lighting
            float  TangentRoughness   = lerp(Shading.BaseLayer.Srf.PerceptualRoughness, 1.0, Shading.Anisotropy.Strength * Shading.Anisotropy.Strength);
            float3 AnisotropicTangent = cross(Shading.Anisotropy.Bitangent, Shading.View);
            float3 AnisotropicNormal  = cross(AnisotropicTangent, Shading.Anisotropy.Bitangent);
            float  BendFactor         = 1.0 - Shading.Anisotropy.Strength * (1.0 - Shading.BaseLayer.Srf.PerceptualRoughness);
            float  BendFactorPow4     = BendFactor * BendFactor * BendFactor * BendFactor;
            float3 BentNormal         = normalize(lerp(AnisotropicNormal, Shading.BaseLayer.Normal, float3(BendFactorPow4, BendFactorPow4, BendFactorPow4)));

            IBLInfo.N = BentNormal;
            IBLInfo.L = normalize(reflect(-IBLInfo.V, IBLInfo.N));
        }
#       endif

        SrfLighting.Base.SpecularIBL =
            GetSpecularIBL_GGX(Shading.BaseLayer.Srf, IBLInfo, PrefilteredEnvMap, PrefilteredEnvMap_sampler, PrefilteredCubeLastMip);
    }
#   if ENABLE_SHEEN
    {
        // NOTE: to be accurate, we need to use another environment map here prefiltered with the Charlie BRDF.
        SrfLighting.Sheen.SpecularIBL =
             GetSpecularIBL_Charlie(Shading.Sheen.Color, Shading.Sheen.Roughness, Shading.BaseLayer.Normal, Shading.View, PrefilteredCubeLastMip,
                            PreintegratedCharlie, PreintegratedCharlie_sampler,
                            PrefilteredEnvMap,    PrefilteredEnvMap_sampler);
    }
#   endif

#   if ENABLE_CLEAR_COAT
    {
        IBLSamplingInfo IBLInfo = GetClearcoatIBLSamplingInfo(
            Shading.Clearcoat.Srf, PreintegratedGGX, PreintegratedGGX_sampler,
            Shading.Clearcoat.Normal, Shading.View);

        SrfLighting.Clearcoat.SpecularIBL =
            GetSpecularIBL_GGX(Shading.Clearcoat.Srf, IBLInfo, PrefilteredEnvMap, PrefilteredEnvMap_sampler, PrefilteredCubeLastMip);
    }
#   endif
}
#endif

float3 GetBaseLayerDiffuseIBL(in SurfaceShadingInfo  Shading,
                              in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Base.DiffuseIBL * Shading.IBLScale * Shading.Occlusion;
}

float3 GetBaseLayerSpecularIBL(in SurfaceShadingInfo  Shading,
                               in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Base.SpecularIBL * Shading.IBLScale * Shading.Occlusion;
}

float3 GetBaseLayerIBL(in SurfaceShadingInfo  Shading,
                       in SurfaceLightingInfo SrfLighting)
{
    return (SrfLighting.Base.DiffuseIBL + SrfLighting.Base.SpecularIBL) * Shading.IBLScale * Shading.Occlusion;
}

float3 GetBaseLayerLighting(in SurfaceShadingInfo  Shading,
                            in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Base.Punctual + GetBaseLayerIBL(Shading, SrfLighting);
}

#if ENABLE_SHEEN
float3 GetSheenIBL(in SurfaceShadingInfo  Shading,
                   in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Sheen.SpecularIBL * Shading.IBLScale * Shading.Occlusion;
}

float3 GetSheenLighting(in SurfaceShadingInfo  Shading,
                        in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Sheen.Punctual + GetSheenIBL(Shading, SrfLighting);
}
#endif

#if ENABLE_CLEAR_COAT
float3 GetClearcoatIBL(in SurfaceShadingInfo  Shading,
                       in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Clearcoat.SpecularIBL * Shading.IBLScale * Shading.Occlusion * Shading.Clearcoat.Factor;
}

float3 GetClearcoatLighting(in SurfaceShadingInfo  Shading,
                            in SurfaceLightingInfo SrfLighting)
{
    return SrfLighting.Clearcoat.Punctual * Shading.Clearcoat.Factor + GetClearcoatIBL(Shading, SrfLighting);
}
#endif

float3 ResolveLighting(in SurfaceShadingInfo  Shading,
                       in SurfaceLightingInfo SrfLighting)
{
    float3 Color = GetBaseLayerLighting(Shading, SrfLighting) + Shading.Emissive;

#if ENABLE_SHEEN
    {
        Color += GetSheenLighting(Shading, SrfLighting);
    }
#endif

#if ENABLE_CLEAR_COAT
    {
        // Clear coat layer is applied on top of everything
    
        float ccNdotV = dot(Shading.Clearcoat.Normal, Shading.View);
    
        // Do not allow clear coat normal to face away from the view direction as this
        // produces artifacts (see https://github.com/DiligentGraphics/DiligentFX/issues/27).
        ccNdotV = max(ccNdotV, 0.1);
    
        float ClearcoatFresnel = SchlickReflection(ccNdotV, Shading.Clearcoat.Srf.Reflectance0.x, Shading.Clearcoat.Srf.Reflectance90.x);
        Color =
            Color * (1.0 - Shading.Clearcoat.Factor * ClearcoatFresnel) +
            GetClearcoatLighting(Shading, SrfLighting);
    }
#endif

    return Color;
}

#ifdef DEBUG_VIEW
float3 GetDebugColor(in SurfaceShadingInfo  Shading,
                     in SurfaceLightingInfo SrfLighting)
{
#if (DEBUG_VIEW == DEBUG_VIEW_OCCLUSION)
    {
        return Shading.Occlusion * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_EMISSIVE)
    {
        return Shading.Emissive.rgb;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_METALLIC)
    {
        return Shading.BaseLayer.Metallic * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_ROUGHNESS)
    {
        return Shading.BaseLayer.Srf.PerceptualRoughness * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_DIFFUSE_COLOR)
    {
        return Shading.BaseLayer.Srf.DiffuseColor;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SPECULAR_COLOR)
    {
        return Shading.BaseLayer.Srf.Reflectance0;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_REFLECTANCE90)
    {
        return Shading.BaseLayer.Srf.Reflectance90;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SHADING_NORMAL)
    {
        return Shading.BaseLayer.Normal * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_NDOTV)
    {
        return Shading.BaseLayer.NdotV * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_PUNCTUAL_LIGHTING)
    {
        return SrfLighting.Base.Punctual;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_DIFFUSE_IBL && USE_IBL)
    {
        return SrfLighting.Base.DiffuseIBL;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SPECULAR_IBL && USE_IBL)
    {
        return SrfLighting.Base.SpecularIBL;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_CLEAR_COAT && ENABLE_CLEAR_COAT)
    {
        return GetClearcoatLighting(Shading, SrfLighting);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_CLEAR_COAT_FACTOR && ENABLE_CLEAR_COAT)
    {
        return Shading.Clearcoat.Factor * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_CLEAR_COAT_ROUGHNESS && ENABLE_CLEAR_COAT)
    {
        return Shading.Clearcoat.Srf.PerceptualRoughness * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_CLEAR_COAT_NORMAL && ENABLE_CLEAR_COAT)
    {
        return Shading.Clearcoat.Normal * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SHEEN && ENABLE_SHEEN)
    {
        return GetSheenLighting(Shading, SrfLighting);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SHEEN_COLOR && ENABLE_SHEEN)
    {
        return Shading.Sheen.Color.rgb;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_SHEEN_ROUGHNESS && ENABLE_SHEEN)
    {
        return Shading.Sheen.Roughness * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_ANISOTROPY_STRENGTH && ENABLE_ANISOTROPY)
    {
        return Shading.Anisotropy.Strength * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_ANISOTROPY_DIRECTION && ENABLE_ANISOTROPY)
    {
        return float3(Shading.Anisotropy.Direction * 0.5 + 0.5, 0.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_IRIDESCENCE && ENABLE_IRIDESCENCE)
    {        
        return Shading.Iridescence.Fresnel;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_IRIDESCENCE_FACTOR && ENABLE_IRIDESCENCE)
    {        
        return Shading.Iridescence.Factor * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_IRIDESCENCE_THICKNESS && ENABLE_IRIDESCENCE)
    {
        return Shading.Iridescence.Thickness * float3(1.0, 1.0, 1.0) / 1200.0;
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_TRANSMISSION && ENABLE_TRANSMISSION)
    {
        return Shading.Transmission * float3(1.0, 1.0, 1.0);
    }
#elif (DEBUG_VIEW == DEBUG_VIEW_THICKNESS && ENABLE_VOLUME)
    {
        return Shading.VolumeThickness * float3(1.0, 1.0, 1.0);
    }
#endif
    
    return float3(0.0, 0.0, 0.0);
}
#endif // DEBUG_VIEW

#endif // _PBR_SHADING_FXH_
