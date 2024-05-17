#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "BasicStructures.fxh"
#include "PostFX_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float>  g_TextureCurrCoC;
Texture2D<float>  g_TexturePrevCoC;
Texture2D<float2> g_TextureMotion;

SamplerState g_TextureCurrCoC_sampler;
SamplerState g_TexturePrevCoC_sampler;

struct PixelStatistic
{
    float Mean;
    float Variance;
    float StdDev;
};

float SampleCoCCurr(int2 PixelCoord)
{
    return g_TextureCurrCoC.Load(int3(PixelCoord, 0));
}

float SampleCoCPrev(int2 PixelCoord)
{
    return g_TexturePrevCoC.Load(int3(PixelCoord, 0));
}

float SampleCoCPrevBilinear(float2 PixelCoord)
{
    return g_TexturePrevCoC.SampleLevel(g_TexturePrevCoC_sampler, PixelCoord * g_Camera.f4ViewportSize.zw, 0.0);
}

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

PixelStatistic ComputePixelStatistic(float2 PixelCoord)
{
    PixelStatistic Desc;
    float M1 = 0.0;
    float M2 = 0.0;

    const int StatisticRadius = 1;
    for (int x = -StatisticRadius; x <= StatisticRadius; x++)
    {
        for (int y = -StatisticRadius; y <= StatisticRadius; y++)
        {
            float2 Location = PixelCoord + float2(x, y);
            float CoC = g_TextureCurrCoC.SampleLevel(g_TextureCurrCoC_sampler, Location * g_Camera.f4ViewportSize.zw, 0.0);

            M1 += CoC;
            M2 += CoC * CoC;
        }
    }

    Desc.Mean = M1 / 9.0;
    Desc.Variance = (M2 / 9.0) - (Desc.Mean * Desc.Mean);
    Desc.StdDev = sqrt(max(Desc.Variance, 0.0f));
    return Desc;
}

float ComputeTemporalCircleOfConfusionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Position = VSOut.f4PixelPos.xy;
    float2 Motion = SampleMotion(int2(Position.xy));
    float2 PrevPosition = Position.xy - Motion * g_Camera.f4ViewportSize.xy;

    if (!IsInsideScreen(PrevPosition, g_Camera.f4ViewportSize.xy))
        return SampleCoCCurr(int2(Position.xy));

    float CoCCurr = SampleCoCCurr(int2(Position));
    float CoCPrev = SampleCoCPrevBilinear(PrevPosition);

    PixelStatistic PixelStat = ComputePixelStatistic(Position);
    float CoCMin = PixelStat.Mean - DOF_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;
    float CoCMax = PixelStat.Mean + DOF_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;

    return lerp(CoCCurr, clamp(CoCPrev, CoCMin, CoCMax), g_DOFAttribs.TemporalStabilityFactor);
}
