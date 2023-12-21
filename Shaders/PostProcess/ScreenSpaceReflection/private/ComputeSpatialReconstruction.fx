#include "ScreenSpaceReflectionStructures.fxh"
#include "PBR_Common.fxh"
#include "SSR_Common.fxh"

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

float SampleRoughness(uint2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(uint2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float SampleDepth(uint2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float2 ComputeWeightRayLength(int2 PixelCoord, float3 V, float3 N, float Roughness, float NdotV)
{
    const float4 RayDirectionPDF = g_TextureRayDirectionPDF.Load(uint3(PixelCoord, 0));
    precise const float InvRayLength = rsqrt(dot(RayDirectionPDF.xyz, RayDirectionPDF.xyz));
    if (isnan(InvRayLength))
        return float2(1.0e-6f, 1.0e-6f);

    const float3 RayDirection = RayDirectionPDF.xyz * InvRayLength;
    const float PDF = RayDirectionPDF.w;

    const float3 L = RayDirection;
    const float3 H = normalize(L + V);

    const float NdotH = saturate(dot(N, H));
    const float NdotL = saturate(dot(N, L));

    const float Vis = SmithGGXVisibilityCorrelated(NdotL, NdotV, Roughness);
    const float D = NormalDistribution_GGX(NdotH, Roughness);
    const float LocalBRDF = Vis * D * NdotL;
    return float2(max(LocalBRDF / max(PDF, 1.0e-5f), 1e-6), rcp(InvRayLength));
}

// Weighted incremental variance
// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
void ComputeWeightedVariance(inout PixelAreaStatistic Stat, float4 SampleColor, float Weight)
{
    Stat.ColorSum += Weight * SampleColor;
    Stat.WeightSum += Weight;

    const float Value = Luminance(SampleColor.rgb);
    const float PrevMean = Stat.Mean;

    Stat.Mean += Weight * rcp(Stat.WeightSum) * (Value - PrevMean);
    Stat.Variance += Weight * (Value - PrevMean) * (Value - Stat.Mean);
}

float ComputeResolvedDepth(float3 PositionWS, float SurfaceHitDistance)
{
    const float CameraSurfaceDistance = distance(g_SSRAttribs.CameraPosition.xyz, PositionWS);
    return CameraZToDepth(CameraSurfaceDistance + SurfaceHitDistance, g_SSRAttribs.ProjMatrix);
}

float3 ScreenSpaceToWorldSpace(float3 ScreenCoordUV)
{
    return InvProjectPosition(ScreenCoordUV, g_SSRAttribs.InvViewProjMatrix);
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeSpatialReconstructionPS(in float4 Position : SV_Position)
{
    CRNG Rng = InitCRND(uint2(Position.xy), 0);

    const float2 ScreenCoordUV = Position.xy * g_SSRAttribs.InverseRenderSize;
    const float3 PositionWS = ScreenSpaceToWorldSpace(float3(ScreenCoordUV, SampleDepth(uint2(Position.xy))));
    const float3 NormalWS = SampleNormalWS(uint2(Position.xy));
    const float3 ViewWS = normalize(g_SSRAttribs.CameraPosition.xyz - PositionWS);
    const float NdotV = saturate(dot(NormalWS, ViewWS));

    const float Roughness = SampleRoughness(uint2(Position.xy));
    const float RoughnessFactor = saturate(SSR_SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR * sqrt(Roughness));
    const float Radius = lerp(0, g_SSRAttribs.SpatialReconstructionRadius, RoughnessFactor);
    const int SampleCount = int(lerp(1.0, float(SSR_SPATIAL_RECONSTRUCTION_SAMPLES), Radius / g_SSRAttribs.SpatialReconstructionRadius));
    const float2 RandomOffset = float2(Rand(Rng), Rand(Rng));
  
    PixelAreaStatistic PixelAreaStat;
    PixelAreaStat.ColorSum = float4(0.0, 0.0, 0.0, 0.0);
    PixelAreaStat.WeightSum = 0.0;
    PixelAreaStat.Variance = 0.0;
    PixelAreaStat.Mean = 0.0;

    float NearestSurfaceHitDistance = FLT_MAX;

    // TODO: Try to implement sampling from https://youtu.be/MyTOGHqyquU?t=1043
    for (int SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
    {
        const float2 Xi = 2.0 * frac(HammersleySequence(SampleIdx, SampleCount) + RandomOffset) - 1.0;
        const int2 SampleCoord = int2(Position.xy + Radius * Xi);
        if (IsInsideScreen(SampleCoord, g_SSRAttribs.RenderSize))
        {
            const float2 WeightLength = ComputeWeightRayLength(SampleCoord, ViewWS, NormalWS, Roughness, NdotV);
            const float4 SampleColor = g_TextureIntersectSpecular.Load(uint3(SampleCoord, 0));
            ComputeWeightedVariance(PixelAreaStat, SampleColor, WeightLength.x);

            if (WeightLength.x > 1.0e-6)
                NearestSurfaceHitDistance = min(WeightLength.y, NearestSurfaceHitDistance);
        }
    }

    PSOutput Output;
    Output.ResolvedRadiance = PixelAreaStat.ColorSum / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedVariance = PixelAreaStat.Variance / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedDepth = ComputeResolvedDepth(PositionWS, NearestSurfaceHitDistance);
    return Output;
}
