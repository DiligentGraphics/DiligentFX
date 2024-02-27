#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"
#include "TemporalAntiAliasingStructures.fxh"
#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "SSAO_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbScreenSpaceAmbientOcclusionAttribs
{
    ScreenSpaceAmbientOcclusionAttribs g_SSAOAttribs;
}

Texture2D<float>  g_TextureCurrOcclusion;
Texture2D<float>  g_TexturePrevOcclusion;
Texture2D<float>  g_TextureCurrDepth;
Texture2D<float>  g_TexturePrevDepth;
Texture2D<float2> g_TextureMotion;

SamplerState g_TexturePrevOcclusion_sampler;

struct PixelStatistic
{
    float Mean;
    float Variance;
    float StdDev;
};

struct ProjectionDesc
{
    float Occlusion;
    float2 PrevCoord;
    bool IsSuccess;
};

float SampleCurrOcclusion(int2 PixelCoord)
{
    return g_TextureCurrOcclusion.Load(int3(PixelCoord, 0));
}

float SampleCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float SamplePrevOcclusionLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevOcclusion.SampleLevel(g_TexturePrevOcclusion_sampler, Texcoord, 0.0);
}

float SamplePrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

// We don't use linear sampler, because WebGL doesn't support linear sampler for depth buffer
float SamplePrevDepthLinear(float2 PixelCoord)
{
    int2 PixelCoordi = int2(PixelCoord - 0.5);
    float2 Weight = float2(frac(PixelCoord.x + 0.5), frac(PixelCoord.y + 0.5));

    float P0 = g_TexturePrevDepth.Load(int3(PixelCoordi + int2(0, 0), 0));
    float P1 = g_TexturePrevDepth.Load(int3(PixelCoordi + int2(1, 0), 0));
    float P2 = g_TexturePrevDepth.Load(int3(PixelCoordi + int2(0, 1), 0));
    float P3 = g_TexturePrevDepth.Load(int3(PixelCoordi + int2(1, 1), 0));
    return lerp(lerp(P0, P1, Weight.x), lerp(P2, P3, Weight.x), Weight.y);
}

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float ComputeDisocclusion(float CurrDepth, float PrevDepth)
{
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_CurrCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr * SSAO_DISOCCLUSION_DEPTH_WEIGHT);
}

PixelStatistic ComputePixelStatistic(int2 PixelCoord)
{
    PixelStatistic Desc;
    float M1 = 0.0;
    float M2 = 0.0;

    const int SearchRadius = 1;
    for (int x = -SearchRadius; x <= SearchRadius; ++x)
    {
        for (int y = -SearchRadius; y <= SearchRadius; ++y)
        {
            int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_CurrCamera.f4ViewportSize.xy));
            float SampleOcclusion = SampleCurrOcclusion(Location);
            M1 += SampleOcclusion;
            M2 += SampleOcclusion * SampleOcclusion;
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
    Desc.IsSuccess = ComputeDisocclusion(CurrDepth, SamplePrevDepth(int2(PrevPos))) > SSAO_DISOCCLUSION_THRESHOLD;
    Desc.Occlusion = SamplePrevOcclusionLinear(Desc.PrevCoord);

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

        Desc.IsSuccess = Disocclusion > SSAO_DISOCCLUSION_THRESHOLD;
        Desc.Occlusion = SamplePrevOcclusionLinear(Desc.PrevCoord);
    }

    Desc.IsSuccess = Desc.IsSuccess && IsInsideScreen(Desc.PrevCoord, g_CurrCamera.f4ViewportSize.xy);
    return Desc;
}

float ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    float Depth = SampleCurrDepth(int2(Position.xy));
    if (IsBackground(Depth))
        discard;

    PixelStatistic PixelStat = ComputePixelStatistic(int2(Position.xy));
    float2 Motion = SampleMotion(int2(Position.xy));
    float2 PrevLocation = Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy;

    ProjectionDesc Reprojection = ComputeReprojection(PrevLocation, Depth);

    float Output;
    if (Reprojection.IsSuccess)
    {
        float OcclusionMin = PixelStat.Mean - SSAO_TEMPORAL_STANDARD_DEVIATION_SCALE * PixelStat.StdDev;
        float OcclusionMax = PixelStat.Mean + SSAO_TEMPORAL_STANDARD_DEVIATION_SCALE * PixelStat.StdDev;
        float PrevOcclusion = clamp(Reprojection.Occlusion, OcclusionMin, OcclusionMax);
        Output = lerp(SampleCurrOcclusion(int2(Position.xy)), PrevOcclusion, g_SSAOAttribs.TemporalStabilityFactor);
    }
    else
    {
        Output = SampleCurrOcclusion(int2(Position.xy));
    }
    return Output;
}
