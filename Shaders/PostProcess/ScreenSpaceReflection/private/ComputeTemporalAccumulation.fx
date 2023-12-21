#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"

#pragma warning(disable : 3078)

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureRoughness;
Texture2D<float>  g_TextureHitDepth;

Texture2D<float>  g_TextureCurrDepth;
Texture2D<float4> g_TextureCurrRadiance;
Texture2D<float>  g_TextureCurrVariance;

Texture2D<float>       g_TexturePrevDepth;
Texture2DArray<float4> g_TexturePrevRadiance;
Texture2DArray<float>  g_TexturePrevVariance;

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
    float Variance  : SV_Target1;
};

float2 SampleMotion(uint2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0));
}

float SampleCurrDepth(uint2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float SamplePrevDepth(uint2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float SampleHitDepth(uint2 PixelCoord)
{
    return g_TextureHitDepth.Load(int3(PixelCoord, 0));
}

float SampleRoughness(uint2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));;
}

float4 SampleCurrRadiance(uint2 PixelCoord)
{
    return g_TextureCurrRadiance.Load(int3(PixelCoord, 0));
}

float SampleCurrVariance(uint2 PixelCoord)
{
    return g_TextureCurrVariance.Load(int3(PixelCoord, 0));
}

float4 SamplePrevRadiance(uint2 PixelCoord)
{
    return g_TexturePrevRadiance.Load(uint4(PixelCoord, 0, 0));
}

float SamplePrevVariance(uint2 PixelCoord)
{
    return g_TexturePrevVariance.Load(uint4(PixelCoord, 0, 0));
}

float SamplePrevDepthLinear(float2 PixelCoord)
{
    const float2 Texcoord = PixelCoord * g_SSRAttribs.InverseRenderSize;
    return g_TexturePrevDepth.SampleLevel(g_TexturePrevDepth_sampler, float2(Texcoord), 0);
}

float4 SamplePrevRadianceLinear(float2 PixelCoord)
{
    const float2 Texcoord = PixelCoord * g_SSRAttribs.InverseRenderSize;
    return g_TexturePrevRadiance.SampleLevel(g_TexturePrevRadiance_sampler, float3(Texcoord, 0), 0);
}

float SamplePrevVarianceLinear(float2 PixelCoord)
{
    const float2 Texcoord = PixelCoord * g_SSRAttribs.InverseRenderSize;
    return g_TexturePrevVariance.SampleLevel(g_TexturePrevVariance_sampler, float3(Texcoord, 0), 0);
}

float2 ComputeReflectionHitPosition(uint2 PixelCoord, float Depth)
{
    const float2 UV = (float2(PixelCoord) + 0.5) * g_SSRAttribs.InverseRenderSize;
    const float3 PositionWS = InvProjectPosition(float3(UV, Depth), g_SSRAttribs.InvViewProjMatrix);
    const float3 PrevCoordUV = ProjectPosition(PositionWS, g_SSRAttribs.PrevViewProjMatrix);
    return PrevCoordUV.xy * g_SSRAttribs.RenderSize;
}

// TODO: Use normals to compute disocclusion
float ComputeDisocclusion(float CurrDepth, float PrevDepth)
{
    const float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_SSRAttribs.ProjMatrix);
    const float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_SSRAttribs.ProjMatrix);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr * SSR_DISOCCLUSION_DEPTH_WEIGHT);
}

// Welford's online algorithm:
//  https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
PixelStatistic ComputePixelStatistic(uint2 PixelCoord)
{
    PixelStatistic Desc;
    float4 M1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 M2 = float4(0.0, 0.0, 0.0, 0.0);
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            const int2 Offset = int2(x, y);
            const int2 Coord = int2(PixelCoord) + Offset;
            const float4 SampleColor = g_TextureCurrRadiance.Load(int3(Coord, 0));

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
                const float2 Location = PrevPos + int2(x, y);
                const float PrevDepth = SamplePrevDepthLinear(Location);
                const float Weight = ComputeDisocclusion(CurrDepth, PrevDepth);
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
        const int2 Offset[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
        const int2 PrevPosi = int2(Desc.PrevCoord - 0.5);
        const float x = frac(Desc.PrevCoord.x + 0.5);
        const float y = frac(Desc.PrevCoord.y + 0.5);
        float Weight[4] = { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };
        for (uint SampleIdx = 0; SampleIdx < 4; SampleIdx++)
        {
            const int2 Location = PrevPosi + Offset[SampleIdx];
            const float PrevDepth = SamplePrevDepth(Location);
            const bool IsValidSample = ComputeDisocclusion(CurrDepth, PrevDepth) > (SSR_DISOCCLUSION_THRESHOLD / 2);
            Weight[SampleIdx] *= float(IsValidSample);
        }

        float WeightSum = 0;
        float DepthSum = 0.0;
        float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);

        for (uint SampleIdx = 0; SampleIdx < 4; SampleIdx++)
        {
            const int2 Location = PrevPosi + Offset[SampleIdx];
            ColorSum  += Weight[SampleIdx] * SamplePrevRadiance(Location);
            DepthSum  += Weight[SampleIdx] * SamplePrevDepth(Location);
            WeightSum += Weight[SampleIdx];
        }
        DepthSum /= max(WeightSum, 1.0e-6f);
        ColorSum /= max(WeightSum, 1.0e-6f);

        Desc.IsSuccess = ComputeDisocclusion(CurrDepth, DepthSum) > SSR_DISOCCLUSION_THRESHOLD;
        Desc.Color = ColorSum;
    }

    Desc.IsSuccess = Desc.IsSuccess && IsInsideScreen(int2(Desc.PrevCoord), g_SSRAttribs.RenderSize);
    return Desc;
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeTemporalAccumulationPS(in float4 Position : SV_Position)
{
    // Secondary reprojection based on ray lengths:
    // https://www.ea.com/seed/news/seed-dd18-presentation-slides-raytracing (Slide 45)

    const PixelStatistic PixelStat = ComputePixelStatistic(uint2(Position.xy));
    const float Depth = SampleCurrDepth(uint2(Position.xy));
    const float HitDepth = SampleHitDepth(uint2(Position.xy));
    const float2 Motion = SampleMotion(uint2(Position.xy));
    const float Roughness = SampleRoughness(uint2(Position.xy));

    const float2 PrevIncidentPoint = Position.xy + Motion * g_SSRAttribs.RenderSize;
    const float2 PrevReflectionHit = ComputeReflectionHitPosition(uint2(Position.xy), HitDepth);

    const float4 PrevColorIncidentPoint = SamplePrevRadianceLinear(PrevIncidentPoint); 
    const float4 PrevColorReflectionHit = SamplePrevRadianceLinear(PrevReflectionHit);

    const float PrevDistanceIncidentPoint = abs(Luminance(PrevColorIncidentPoint.rgb) - Luminance(PixelStat.Mean.rgb));
    const float PrevDistanceReflectionHit = abs(Luminance(PrevColorReflectionHit.rgb) - Luminance(PixelStat.Mean.rgb));

    const float2 PrevCoord = PrevDistanceIncidentPoint < PrevDistanceReflectionHit ? PrevIncidentPoint : PrevReflectionHit;
    const ProjectionDesc Reprojection = ComputeReprojection(PrevCoord, Depth);

    PSOutput Output;
    if (Reprojection.IsSuccess)
    {
        const float Alpha = IsMirrorReflection(Roughness) ? 0.95 : 1.0;
        const float4 ColorMin = PixelStat.Mean - SSR_TEMPORAL_STANDARD_DEVIATION_SCALE * PixelStat.StdDev;
        const float4 ColorMax = PixelStat.Mean + SSR_TEMPORAL_STANDARD_DEVIATION_SCALE * PixelStat.StdDev;
        const float4 PrevRadiance = clamp(Reprojection.Color, ColorMin, ColorMax);
        const float  PrevVariance = SamplePrevVarianceLinear(Reprojection.PrevCoord);
        Output.Radiance = lerp(SampleCurrRadiance(uint2(Position.xy)), PrevRadiance, Alpha * g_SSRAttribs.TemporalRadianceStabilityFactor);
        Output.Variance = lerp(SampleCurrVariance(uint2(Position.xy)), PrevVariance, Alpha * g_SSRAttribs.TemporalVarianceStabilityFactor);
    }
    else
    {
        Output.Radiance = SampleCurrRadiance(uint2(Position.xy));
        Output.Variance = 1.0f;
    }
    return Output;
}
