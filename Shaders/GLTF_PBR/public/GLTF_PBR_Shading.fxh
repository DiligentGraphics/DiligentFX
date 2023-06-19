#ifndef _GLTF_PBR_SHADING_FXH_
#define _GLTF_PBR_SHADING_FXH_

#include "PBR_Common.fxh"
#include "ShaderUtilities.fxh"

#ifndef GLTF_PBR_MANUAL_SRGB
#   define  GLTF_PBR_MANUAL_SRGB    1
#endif

#ifndef SRGB_FAST_APPROXIMATION
#   define  SRGB_FAST_APPROXIMATION 1
#endif

#define GLTF_PBR_USE_ENV_MAP_LOD
#define GLTF_PBR_USE_HDR_CUBEMAPS

float GetPerceivedBrightness(FLOAT3 rgb)
{
    return sqrt(FLOAT(0.299) * rgb.r * rgb.r + FLOAT(0.587) * rgb.g * rgb.g + FLOAT(0.114) * rgb.b * rgb.b);
}

// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows/js/three.pbrUtilities.js#L34
FLOAT GLTF_PBR_SolveMetallic(FLOAT3 diffuse,
                             FLOAT3 specular,
                             FLOAT  oneMinusSpecularStrength)
{
    const FLOAT c_MinReflectance = 0.04;
    FLOAT specularBrightness = GetPerceivedBrightness(specular);
    if (specularBrightness < c_MinReflectance)
    {
        return _0f;
    }

    FLOAT diffuseBrightness = GetPerceivedBrightness(diffuse);

    FLOAT a = c_MinReflectance;
    FLOAT b = diffuseBrightness * oneMinusSpecularStrength / (_1f - c_MinReflectance) + specularBrightness - _2f * c_MinReflectance;
    FLOAT c = c_MinReflectance - specularBrightness;
    FLOAT D = b * b - FLOAT(4.0) * a * c;

    return clamp((-b + sqrt(D)) / (_2f * a), _0f, _1f);
}


FLOAT3 SRGBtoLINEAR(FLOAT3 srgbIn)
{
#ifdef GLTF_PBR_MANUAL_SRGB
#   ifdef SRGB_FAST_APPROXIMATION
        FLOAT3 linOut = pow(saturate(srgbIn.xyz), FLOAT3(2.2, 2.2, 2.2));
#   else
        FLOAT3 bLess  = step(FLOAT3(0.04045, 0.04045, 0.04045), srgbIn.xyz);
        FLOAT3 linOut = mix( srgbIn.xyz / FLOAT(12.92), pow(saturate((srgbIn.xyz + FLOAT3(0.055, 0.055, 0.055)) / FLOAT(1.055)), FLOAT3(2.4, 2.4, 2.4)), bLess );
#   endif
        return linOut;
#else
    return srgbIn;
#endif
}

FLOAT4 SRGBtoLINEAR(FLOAT4 srgbIn)
{
    return FLOAT4(SRGBtoLINEAR(srgbIn.xyz), srgbIn.w);
}


FLOAT3 GLTF_PBR_ApplyDirectionalLight(FLOAT3 lightDir, FLOAT3 lightColor, SurfaceReflectanceInfo srfInfo, FLOAT3 normal, FLOAT3 view)
{
    FLOAT3 pointToLight = -lightDir;
    FLOAT3 diffuseContrib, specContrib;
    FLOAT  NdotL;
    SmithGGX_BRDF(pointToLight, normal, view, srfInfo, diffuseContrib, specContrib, NdotL);
    // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
    FLOAT3 shade = (diffuseContrib + specContrib) * NdotL;
    return lightColor * shade;
}


// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
FLOAT3 GLTF_PBR_PerturbNormal(in FLOAT3 dPos_dx,
                              in FLOAT3 dPos_dy,
                              in FLOAT2 dUV_dx,
                              in FLOAT2 dUV_dy,
                              in FLOAT3 Normal,
                              in FLOAT3 TSNormal,
                              bool      HasUV,
                              bool      IsFrontFace)
{
    // Retrieve the tangent space matrix
    FLOAT NormalLen = length(Normal);
    FLOAT3 ng;
    if (NormalLen > 1e-5)
    {
        ng = Normal/NormalLen;
    }
    else
    {
        ng = normalize(cross(dPos_dx, dPos_dy));
#if (defined(GLSL) || defined(GL_ES)) && !defined(VULKAN)
        // In OpenGL screen is upside-down, so we have to invert the vector
        ng *= -_1f;
#endif
    }

    if (HasUV)
    {
        return TransformTangentSpaceNormalGrad(dPos_dx, dPos_dy, dUV_dx, dUV_dy, ng, TSNormal * (IsFrontFace ? +_1f : -_1f));
    }
    else
    {
        return ng * (IsFrontFace ? +_1f : -_1f);
    }
}


struct GLTF_PBR_IBL_Contribution
{
    FLOAT3 f3Diffuse;
    FLOAT3 f3Specular;
};

// Calculation of the lighting contribution from an optional Image Based Light source.
// Precomputed Environment Maps are required uniform inputs and are computed as outlined in [1].
// See our README.md on Environment Maps [3] for additional discussion.
GLTF_PBR_IBL_Contribution GLTF_PBR_GetIBLContribution(
                        in SurfaceReflectanceInfo SrfInfo,
                        in FLOAT3                 n,
                        in FLOAT3                 v,
                        in FLOAT                  PrefilteredCubeMipLevels,
                        in Texture2D              BRDF_LUT,
                        in SamplerState           BRDF_LUT_sampler,
                        in TextureCube            IrradianceMap,
                        in SamplerState           IrradianceMap_sampler,
                        in TextureCube            PrefilteredEnvMap,
                        in SamplerState           PrefilteredEnvMap_sampler)
{
    FLOAT NdotV = clamp(dot(n, v), _0f, _1f);

    FLOAT lod = clamp(SrfInfo.PerceptualRoughness * PrefilteredCubeMipLevels, _0f, PrefilteredCubeMipLevels);
    FLOAT3 reflection = normalize(reflect(-v, n));

    FLOAT2 brdfSamplePoint = clamp(FLOAT2(NdotV, SrfInfo.PerceptualRoughness), FLOAT2(0.0, 0.0), FLOAT2(1.0, 1.0));
    // retrieve a scale and bias to F0. See [1], Figure 3
    FLOAT2 brdf = BRDF_LUT.Sample(BRDF_LUT_sampler, brdfSamplePoint).rg;

    FLOAT4 diffuseSample = IrradianceMap.Sample(IrradianceMap_sampler, n);

#ifdef GLTF_PBR_USE_ENV_MAP_LOD
    FLOAT4 specularSample = PrefilteredEnvMap.SampleLevel(PrefilteredEnvMap_sampler, reflection, lod);
#else
    FLOAT4 specularSample = PrefilteredEnvMap.Sample(PrefilteredEnvMap_sampler, reflection);
#endif

#ifdef GLTF_PBR_USE_HDR_CUBEMAPS
    // Already linear.
    FLOAT3 diffuseLight  = diffuseSample.rgb;
    FLOAT3 specularLight = specularSample.rgb;
#else
    FLOAT3 diffuseLight  = SRGBtoLINEAR(diffuseSample).rgb;
    FLOAT3 specularLight = SRGBtoLINEAR(specularSample).rgb;
#endif

    GLTF_PBR_IBL_Contribution IBLContrib;
    IBLContrib.f3Diffuse  = diffuseLight * SrfInfo.DiffuseColor;
    IBLContrib.f3Specular = specularLight * (SrfInfo.Reflectance0 * brdf.x + SrfInfo.Reflectance90 * brdf.y);
    return IBLContrib;
}

/// Calculates surface reflectance info

/// \param [in]  Workflow     - PBR workflow (PBR_WORKFLOW_SPECULAR_GLOSINESS or PBR_WORKFLOW_METALLIC_ROUGHNESS).
/// \param [in]  BaseColor    - Material base color.
/// \param [in]  PhysicalDesc - Physical material description. For Metallic-roughness workflow,
///                             'g' channel stores roughness, 'b' channel stores metallic.
/// \param [out] Metallic     - Metallic value used for shading.
SurfaceReflectanceInfo GLTF_PBR_GetSurfaceReflectance(int       Workflow,
                                                      FLOAT4    BaseColor,
                                                      FLOAT4    PhysicalDesc,
                                                      out FLOAT Metallic)
{
    SurfaceReflectanceInfo SrfInfo;

    FLOAT3 SpecularColor;

    FLOAT3 f0 = FLOAT3(0.04, 0.04, 0.04);

    // Metallic and Roughness material properties are packed together
    // In glTF, these factors can be specified by fixed scalar values
    // or from a metallic-roughness map
    if (Workflow == PBR_WORKFLOW_SPECULAR_GLOSINESS)
    {
        SrfInfo.PerceptualRoughness = _1f - PhysicalDesc.a; // glossiness to roughness
        f0 = PhysicalDesc.rgb;

        // f0 = specular
        SpecularColor = f0;
        FLOAT oneMinusSpecularStrength = _1f - max(max(f0.r, f0.g), f0.b);
        SrfInfo.DiffuseColor = BaseColor.rgb * oneMinusSpecularStrength;

        // do conversion between metallic M-R and S-G metallic
        Metallic = GLTF_PBR_SolveMetallic(BaseColor.rgb, SpecularColor, oneMinusSpecularStrength);
    }
    else if (Workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
    {
        // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
        // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
        SrfInfo.PerceptualRoughness = PhysicalDesc.g;
        Metallic                    = PhysicalDesc.b;

        SrfInfo.DiffuseColor  = BaseColor.rgb * (FLOAT3(1.0, 1.0, 1.0) - f0) * (_1f - Metallic);
        SpecularColor         = lerp(f0, BaseColor.rgb, Metallic);
    }

//#ifdef ALPHAMODE_OPAQUE
//    baseColor.a = 1.0;
//#endif
//
//#ifdef MATERIAL_UNLIT
//    gl_FragColor = FLOAT4(gammaCorrection(baseColor.rgb), baseColor.a);
//    return;
//#endif

    SrfInfo.PerceptualRoughness = clamp(SrfInfo.PerceptualRoughness, _0f, _1f);

    // Compute reflectance.
    FLOAT3 Reflectance0  = SpecularColor;
    FLOAT  MaxR0         = max(max(Reflectance0.r, Reflectance0.g), Reflectance0.b);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    FLOAT R90 = clamp(MaxR0 * FLOAT(50.0), _0f, _1f);

    SrfInfo.Reflectance0  = Reflectance0;
    SrfInfo.Reflectance90 = FLOAT3(R90, R90, R90);

    return SrfInfo;
}

/// Calculates surface reflectance info for Metallic-roughness workflow
SurfaceReflectanceInfo GLTF_PBR_GetSurfaceReflectanceMR(FLOAT3 BaseColor,
                                                        FLOAT  Metallic,
                                                        FLOAT  Roughness)
{
    SurfaceReflectanceInfo SrfInfo;

    FLOAT f0 = 0.04;

    SrfInfo.PerceptualRoughness = Roughness;
    SrfInfo.DiffuseColor        = BaseColor * ((_1f - f0) * (_1f - Metallic));
        
    FLOAT3 Reflectance0 = lerp(FLOAT3(f0, f0, f0), BaseColor, Metallic);
    FLOAT  MaxR0        = max(max(Reflectance0.r, Reflectance0.g), Reflectance0.b);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    FLOAT R90 = min(MaxR0 * FLOAT(50.0), _1f);
    
    SrfInfo.Reflectance0  = Reflectance0;
    SrfInfo.Reflectance90 = FLOAT3(R90, R90, R90);

    return SrfInfo;
}

#endif // _GLTF_PBR_SHADING_FXH_
