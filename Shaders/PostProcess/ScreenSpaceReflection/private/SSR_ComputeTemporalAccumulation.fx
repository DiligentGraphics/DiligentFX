#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureHitDepth;

Texture2D<float>  g_TextureCurrDepth;
Texture2D<float4> g_TextureCurrRadiance;
Texture2D<float>  g_TextureCurrVariance;

Texture2D<float>  g_TexturePrevDepth;
Texture2D<float4> g_TexturePrevRadiance;
Texture2D<float>  g_TexturePrevVariance;

SamplerState g_TexturePrevDepth_sampler;
SamplerState g_TexturePrevRadiance_sampler;
SamplerState g_TexturePrevVariance_sampler;

struct ProjectionDesc
{
    float4 Color;
    float2 PrevCoord;
    bool IsSuccess;
};

struct PixelStatistic
{
    float4 Mean;
    float4 Variance;
    float4 StdDev;
};

struct PSOutput
{
    float4 Radiance : SV_Target0;
    float  Variance : SV_Target1;
};

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float SampleCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float SamplePrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float SampleHitDepth(int2 PixelCoord)
{
    return g_TextureHitDepth.Load(int3(PixelCoord, 0));
}

float4 SampleCurrRadiance(int2 PixelCoord)
{
    return g_TextureCurrRadiance.Load(int3(PixelCoord, 0));
}

float SampleCurrVariance(int2 PixelCoord)
{
    return g_TextureCurrVariance.Load(int3(PixelCoord, 0));
}

float4 SamplePrevRadiance(int2 PixelCoord)
{
    return g_TexturePrevRadiance.Load(int3(PixelCoord, 0));
}

float SamplePrevVariance(int2 PixelCoord)
{
    return g_TexturePrevVariance.Load(int3(PixelCoord, 0));
}

float SamplePrevDepthLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevDepth.SampleLevel(g_TexturePrevDepth_sampler, Texcoord, 0);
}

float4 SamplePrevRadianceLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevRadiance.SampleLevel(g_TexturePrevRadiance_sampler, Texcoord, 0);
}

float SamplePrevVarianceLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevVariance.SampleLevel(g_TexturePrevVariance_sampler, Texcoord, 0);
}

float2 ComputeReflectionHitPosition(int2 PixelCoord, float Depth)
{
    float2 Texcoord = (float2(PixelCoord) + 0.5) * g_CurrCamera.f4ViewportSize.zw + F3NDC_XYZ_TO_UVD_SCALE.xy * g_CurrCamera.f2Jitter;
    float3 PositionWS = InvProjectPosition(float3(Texcoord, Depth), g_CurrCamera.mViewProjInv);
    float3 PrevCoordUV = ProjectPosition(PositionWS, g_PrevCamera.mViewProj);
    return (PrevCoordUV.xy - F3NDC_XYZ_TO_UVD_SCALE.xy * g_PrevCamera.f2Jitter) * g_CurrCamera.f4ViewportSize.xy;
}

// TODO: Use normals to compute disocclusion
float ComputeDisocclusion(float CurrDepth, float PrevDepth)
{
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_CurrCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr);
}

// Welford's online algorithm:
//  https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
PixelStatistic ComputePixelStatistic(int2 PixelCoord)
{
    PixelStatistic Desc;
    float4 M1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 M2 = float4(0.0, 0.0, 0.0, 0.0);

    const int StatisticRadius = 1;
    for (int x = -StatisticRadius; x <= StatisticRadius; x++)
    {
        for (int y = -StatisticRadius; y <= StatisticRadius; y++)
        {
            int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_CurrCamera.f4ViewportSize.xy));
            float4 SampleColor = g_TextureCurrRadiance.Load(int3(Location, 0));

            M1 += SampleColor;
            M2 += SampleColor * SampleColor;
        }
    }

    Desc.Mean = M1 / 9.0;
    Desc.Variance = (M2 / 9.0) - (Desc.Mean * Desc.Mean);
    Desc.StdDev = sqrt(max(Desc.Variance, 0.0f));
    return Desc;
}

ProjectionDesc ComputeReprojection(float2 PrevPos, float CurrDepth)
{
    ProjectionDesc Desc;
    Desc.PrevCoord = PrevPos;
    Desc.IsSuccess = ComputeDisocclusion(CurrDepth, SamplePrevDepth(int2(PrevPos))) > SSR_DISOCCLUSION_THRESHOLD;
    Desc.Color = SamplePrevRadianceLinear(Desc.PrevCoord);

    if (!Desc.IsSuccess)
    {
        float Disocclusion = 0.0;
        const int SearchRadius = 1;
        for (int y = -SearchRadius; y <= SearchRadius; y++)
        {
            for (int x = -SearchRadius; x <= SearchRadius; x++)
            {
                float2 Location = PrevPos + float2(x, y);
                float PrevDepth = SamplePrevDepthLinear(Location);
                float Weight = ComputeDisocclusion(CurrDepth, PrevDepth);
                if (Weight > Disocclusion)
                {
                    Disocclusion = Weight;
                    Desc.PrevCoord = Location;
                }
            }
        }

        Desc.IsSuccess = Disocclusion > SSR_DISOCCLUSION_THRESHOLD;
        Desc.Color = SamplePrevRadianceLinear(Desc.PrevCoord);
    }

    if (!Desc.IsSuccess)
    {
        int2 PrevPosi = int2(Desc.PrevCoord - 0.5);
        float x = frac(Desc.PrevCoord.x + 0.5);
        float y = frac(Desc.PrevCoord.y + 0.5);

        float Weight[4];
        Weight[0] = (1.0 - x) * (1.0 - y);
        Weight[1] = x * (1.0 - y);
        Weight[2] = (1.0 - x) * y;
        Weight[3] = x * y;

        {
            for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
            {
                int2 Location = PrevPosi + int2(SampleIdx & 0x01, SampleIdx >> 1);
                float PrevDepth = SamplePrevDepth(Location);
                bool IsValidSample = ComputeDisocclusion(CurrDepth, PrevDepth) > (SSR_DISOCCLUSION_THRESHOLD / 2.0);
                Weight[SampleIdx] *= float(IsValidSample);
            }
        }

        float WeightSum = 0.0;
        float DepthSum = 0.0;
        float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);

        {
            for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
            {
                int2 Location = PrevPosi + int2(SampleIdx & 0x01, SampleIdx >> 1);
                ColorSum  += Weight[SampleIdx] * SamplePrevRadiance(Location);
                DepthSum  += Weight[SampleIdx] * SamplePrevDepth(Location);
                WeightSum += Weight[SampleIdx];
            }
        }
        
        DepthSum /= max(WeightSum, 1.0e-6f);
        ColorSum /= max(WeightSum, 1.0e-6f);

        Desc.IsSuccess = ComputeDisocclusion(CurrDepth, DepthSum) > SSR_DISOCCLUSION_THRESHOLD;
        Desc.Color = ColorSum;
    } 

    Desc.IsSuccess = Desc.IsSuccess && IsInsideScreen(Desc.PrevCoord, g_CurrCamera.f4ViewportSize.xy);
    return Desc;
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut)
{
    float4 Position = VSOut.f4PixelPos;

    // Secondary reprojection based on ray lengths:
    // https://www.ea.com/seed/news/seed-dd18-presentation-slides-raytracing (Slide 45)
    PixelStatistic PixelStat = ComputePixelStatistic(int2(Position.xy));
    float Depth = SampleCurrDepth(int2(Position.xy));
    float HitDepth = SampleHitDepth(int2(Position.xy));
    float2 Motion = SampleMotion(int2(Position.xy));

    float2 PrevIncidentPoint = Position.xy - Motion * float2(g_CurrCamera.f4ViewportSize.xy);
    float2 PrevReflectionHit = ComputeReflectionHitPosition(int2(Position.xy), HitDepth);

    float4 PrevColorIncidentPoint = SamplePrevRadianceLinear(PrevIncidentPoint); 
    float4 PrevColorReflectionHit = SamplePrevRadianceLinear(PrevReflectionHit);

    float PrevDistanceIncidentPoint = abs(Luminance(PrevColorIncidentPoint.rgb) - Luminance(PixelStat.Mean.rgb));
    float PrevDistanceReflectionHit = abs(Luminance(PrevColorReflectionHit.rgb) - Luminance(PixelStat.Mean.rgb));

    float2 PrevCoord = PrevDistanceIncidentPoint < PrevDistanceReflectionHit ? PrevIncidentPoint : PrevReflectionHit;
    ProjectionDesc Reprojection = ComputeReprojection(PrevCoord, Depth);

    PSOutput Output;
    if (Reprojection.IsSuccess)
    {
        float4 ColorMin = PixelStat.Mean - SSR_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;
        float4 ColorMax = PixelStat.Mean + SSR_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;
        float4 PrevRadiance = clamp(Reprojection.Color, ColorMin, ColorMax);
        float  PrevVariance = SamplePrevVarianceLinear(Reprojection.PrevCoord);
        Output.Radiance = lerp(SampleCurrRadiance(int2(Position.xy)), PrevRadiance, g_SSRAttribs.TemporalRadianceStabilityFactor);
        Output.Variance = lerp(SampleCurrVariance(int2(Position.xy)), PrevVariance, g_SSRAttribs.TemporalVarianceStabilityFactor);
    }
    else
    {
        Output.Radiance = SampleCurrRadiance(int2(Position.xy));
        Output.Variance = 1.0f;
    }
    return Output;
}
