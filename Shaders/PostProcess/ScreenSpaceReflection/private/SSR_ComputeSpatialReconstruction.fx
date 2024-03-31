#include "ScreenSpaceReflectionStructures.fxh"
#include "BasicStructures.fxh"
#include "PBR_Common.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

struct PSOutput
{
    float4 ResolvedRadiance : SV_Target0;
    float  ResolvedVariance : SV_Target1;
    float  ResolvedDepth    : SV_Target2;
};

Texture2D<float>  g_TextureRoughness;
Texture2D<float3> g_TextureNormal;
Texture2D<float>  g_TextureDepth;
Texture2D<float4> g_TextureRayDirectionPDF;
Texture2D<float4> g_TextureIntersectSpecular;

struct PixelAreaStatistic
{
    float Mean;
    float Variance;
    float WeightSum;
    float4 ColorSum;
};

float SampleRoughness(int2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeGaussianWeight(float Distance)
{
    return exp(-0.66 * Distance * Distance); // assuming Distance is normalized to 1
}

float4 ComputeBlurKernelRotation(uint2 PixelCoord, uint FrameIndex)
{
    float Angle = Bayer4x4(PixelCoord, FrameIndex);
    return GetRotator(2.0 * M_PI * Angle);
}

float2 ComputeWeightRayLength(int2 PixelCoord, float3 V, float3 N, float Roughness, float NdotV, float Weight)
{
    float4 RayDirectionPDF = g_TextureRayDirectionPDF.Load(int3(PixelCoord, 0));
    float InvRayLength = rsqrt(dot(RayDirectionPDF.xyz, RayDirectionPDF.xyz));
    if (isnan(InvRayLength))
        return float2(1.0e-6f, 1.0e-6f);

    float3 RayDirection = RayDirectionPDF.xyz * InvRayLength;
    float PDF = RayDirectionPDF.w;
    float AlphaRoughness = Roughness * Roughness;

    float3 L = RayDirection;
    float3 H = normalize(L + V);

    float NdotH = saturate(dot(N, H));
    float NdotL = saturate(dot(N, L));

    float Vis = SmithGGXVisibilityCorrelated(NdotL, NdotV, AlphaRoughness);
    float D = NormalDistribution_GGX(NdotH, AlphaRoughness);
    float LocalBRDF = Vis * D * NdotL;
    LocalBRDF *= ComputeGaussianWeight(Weight);
    return float2(max(LocalBRDF / max(PDF, 1.0e-5f), 1e-6), rcp(InvRayLength));
}

// Weighted incremental variance
// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
void ComputeWeightedVariance(inout PixelAreaStatistic Stat, float4 SampleColor, float Weight)
{
    Stat.ColorSum += Weight * SampleColor;
    Stat.WeightSum += Weight;

    float Value = Luminance(SampleColor.rgb);
    float PrevMean = Stat.Mean;

    Stat.Mean += Weight * rcp(Stat.WeightSum) * (Value - PrevMean);
    Stat.Variance += Weight * (Value - PrevMean) * (Value - Stat.Mean);
}

float ComputeResolvedDepth(float3 PositionWS, float SurfaceHitDistance)
{
    float CameraSurfaceDistance = distance(g_Camera.f4Position.xyz, PositionWS);
    return CameraZToDepth(CameraSurfaceDistance + SurfaceHitDistance, g_Camera.mProj);
}

float3 ScreenSpaceToWorldSpace(float3 ScreenCoordUV)
{
    return InvProjectPosition(ScreenCoordUV, g_Camera.mViewProjInv);
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeSpatialReconstructionPS(in FullScreenTriangleVSOutput VSOut)
{
    // samples = 8, min distance = 0.5, average samples on radius = 2
    float3 Poisson[SSR_SPATIAL_RECONSTRUCTION_SAMPLES];
    Poisson[0] = float3(-0.4706069, -0.4427112, +0.6461146);
    Poisson[1] = float3(-0.9057375, +0.3003471, +0.9542373);
    Poisson[2] = float3(-0.3487388, +0.4037880, +0.5335386);
    Poisson[3] = float3(+0.1023042, +0.6439373, +0.6520134);
    Poisson[4] = float3(+0.5699277, +0.3513750, +0.6695386);
    Poisson[5] = float3(+0.2939128, -0.1131226, +0.3149309);
    Poisson[6] = float3(+0.7836658, -0.4208784, +0.8895339);
    Poisson[7] = float3(+0.1564120, -0.8198990, +0.8346850);

    float4 Position = VSOut.f4PixelPos;
    float2 ScreenCoordUV = Position.xy * g_Camera.f4ViewportSize.zw;
    float3 PositionWS = ScreenSpaceToWorldSpace(float3(ScreenCoordUV, SampleDepth(int2(Position.xy))));
    float3 NormalWS = SampleNormalWS(int2(Position.xy));
    float3 ViewWS = normalize(g_Camera.f4Position.xyz - PositionWS);
    float NdotV = saturate(dot(NormalWS, ViewWS));

    float Roughness = SampleRoughness(int2(Position.xy));
    float RoughnessFactor = saturate(float(SSR_SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR) * Roughness);
    float Radius = lerp(0.0, g_SSRAttribs.SpatialReconstructionRadius, RoughnessFactor);
    float4 Rotator = ComputeBlurKernelRotation(uint2(Position.xy), g_Camera.uiFrameIndex);

    PixelAreaStatistic PixelAreaStat;
    PixelAreaStat.ColorSum = float4(0.0, 0.0, 0.0, 0.0);
    PixelAreaStat.WeightSum = 0.0;
    PixelAreaStat.Variance = 0.0;
    PixelAreaStat.Mean = 0.0;

    float NearestSurfaceHitDistance = 0.0;

    // TODO: Try to implement sampling from https://youtu.be/MyTOGHqyquU?t=1043
    for (int SampleIdx = 0; SampleIdx < SSR_SPATIAL_RECONSTRUCTION_SAMPLES; SampleIdx++)
    {
        float2 Xi = RotateVector(Rotator, Poisson[SampleIdx].xy);
#if SSR_OPTION_HALF_RESOLUTION
        int2 SampleCoord = ClampScreenCoord(int2(0.5 * (floor(Position.xy) + Radius * Xi) + float2(0.5, 0.5)), int2(0.5 * g_Camera.f4ViewportSize.xy));
#else
        int2 SampleCoord = ClampScreenCoord(int2(Position.xy + Radius * Xi), int2(g_Camera.f4ViewportSize.xy));
#endif
        float WeightS = ComputeSpatialWeight(Poisson[SampleIdx].z * Poisson[SampleIdx].z, SSR_SPATIAL_RECONSTRUCTION_SIGMA);
        float2 WeightLength = ComputeWeightRayLength(SampleCoord, ViewWS, NormalWS, Roughness, NdotV, WeightS);
        float4 SampleColor = g_TextureIntersectSpecular.Load(int3(SampleCoord, 0));
        ComputeWeightedVariance(PixelAreaStat, SampleColor, WeightLength.x);

        if (WeightLength.x > 1.0e-6)
            NearestSurfaceHitDistance = max(WeightLength.y, NearestSurfaceHitDistance);
    }

    PSOutput Output;
    Output.ResolvedRadiance = PixelAreaStat.ColorSum / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedVariance = PixelAreaStat.Variance / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedDepth = ComputeResolvedDepth(PositionWS, NearestSurfaceHitDistance);
    return Output;
}
