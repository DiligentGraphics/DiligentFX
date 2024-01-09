#include "ScreenSpaceReflectionStructures.fxh"
#include "BasicStructures.fxh"
#include "PBR_Common.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

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

float2 ComputeWeightRayLength(int2 PixelCoord, float3 V, float3 N, float Roughness, float NdotV)
{
    float4 RayDirectionPDF = g_TextureRayDirectionPDF.Load(int3(PixelCoord, 0));
    float InvRayLength = rsqrt(dot(RayDirectionPDF.xyz, RayDirectionPDF.xyz));
    if (isnan(InvRayLength))
        return float2(1.0e-6f, 1.0e-6f);

    float3 RayDirection = RayDirectionPDF.xyz * InvRayLength;
    float PDF = RayDirectionPDF.w;

    float3 L = RayDirection;
    float3 H = normalize(L + V);

    float NdotH = saturate(dot(N, H));
    float NdotL = saturate(dot(N, L));

    float Vis = SmithGGXVisibilityCorrelated(NdotL, NdotV, Roughness);
    float D = NormalDistribution_GGX(NdotH, Roughness);
    float LocalBRDF = Vis * D * NdotL;
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
    float4 Position = VSOut.f4PixelPos;
    
    CRNG Rng = InitCRND(uint2(Position.xy), 0u);

    float2 ScreenCoordUV = Position.xy * g_Camera.f4ViewportSize.zw;
    float3 PositionWS = ScreenSpaceToWorldSpace(float3(ScreenCoordUV, SampleDepth(int2(Position.xy))));
    float3 NormalWS = SampleNormalWS(int2(Position.xy));
    float3 ViewWS = normalize(g_Camera.f4Position.xyz - PositionWS);
    float NdotV = saturate(dot(NormalWS, ViewWS));

    float Roughness = SampleRoughness(int2(Position.xy));
    float RoughnessFactor = saturate(float(SSR_SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR) * sqrt(Roughness));
    float Radius = lerp(0.0, g_SSRAttribs.SpatialReconstructionRadius, RoughnessFactor);
    uint SampleCount = uint(lerp(1.0, float(SSR_SPATIAL_RECONSTRUCTION_SAMPLES), Radius / g_SSRAttribs.SpatialReconstructionRadius));
    float2 RandomOffset = float2(Rand(Rng), Rand(Rng));

    PixelAreaStatistic PixelAreaStat;
    PixelAreaStat.ColorSum = float4(0.0, 0.0, 0.0, 0.0);
    PixelAreaStat.WeightSum = 0.0;
    PixelAreaStat.Variance = 0.0;
    PixelAreaStat.Mean = 0.0;

    float NearestSurfaceHitDistance = 0.0;

    // TODO: Try to implement sampling from https://youtu.be/MyTOGHqyquU?t=1043
    for (uint SampleIdx = 0u; SampleIdx < SampleCount; SampleIdx++)
    {
        float2 Xi = 2.0 * frac(HammersleySequence(SampleIdx, SampleCount) + RandomOffset) - 1.0;
        int2 SampleCoord = int2(Position.xy + Radius * Xi);
        if (IsInsideScreen(SampleCoord, int2(g_Camera.f4ViewportSize.xy)))
        {
            float2 WeightLength = ComputeWeightRayLength(SampleCoord, ViewWS, NormalWS, Roughness, NdotV);
            float4 SampleColor = g_TextureIntersectSpecular.Load(int3(SampleCoord, 0));
            ComputeWeightedVariance(PixelAreaStat, SampleColor, WeightLength.x);

            if (WeightLength.x > 1.0e-6)
                NearestSurfaceHitDistance = max(WeightLength.y, NearestSurfaceHitDistance);
        }
    }

    PSOutput Output;
    Output.ResolvedRadiance = PixelAreaStat.ColorSum / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedVariance = PixelAreaStat.Variance / max(PixelAreaStat.WeightSum, 1e-6f);
    Output.ResolvedDepth = ComputeResolvedDepth(PositionWS, NearestSurfaceHitDistance);
    return Output;
}
