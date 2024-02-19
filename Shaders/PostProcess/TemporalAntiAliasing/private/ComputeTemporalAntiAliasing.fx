#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"
#include "TemporalAntiAliasingStructures.fxh"

#define FLT_EPS   5.960464478e-8
#define FLT_MAX   3.402823466e+38

#if TAA_OPTION_INVERTED_DEPTH
    #define DepthFarPlane 0.0
#else
    #define DepthFarPlane 1.0
#endif // TAA_OPTION_INVERTED_DEPTH

#pragma warning(disable : 3078)

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbTemporalAntiAliasingAttribs
{
    TemporalAntiAliasingAttribs g_TAAAttribs;
}

Texture2D<float3> g_TextureCurrColor;
Texture2D<float3> g_TexturePrevColor;
Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureDepth;
Texture2D<float2> g_TexturePrevMotion;
Texture2D<float>  g_TexturePrevDepth;

SamplerState g_TexturePrevColor_sampler;

struct PixelStatistic
{
    float3 Mean;
    float3 Variance;
    float3 StdDev;
};

float3 RGBToYCoCg(float3 RGB)
{
    float Y  = dot(float3(+0.25, +0.50, +0.25), RGB);
    float Co = dot(float3(+0.50, +0.00, -0.50), RGB);
    float Cg = dot(float3(-0.25, +0.50, -0.25), RGB);
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 YCoCg)
{
    float R = dot(float3(+1.0, +1.0, -1.0), YCoCg);
    float G = dot(float3(+1.0, +0.0, +1.0), YCoCg);
    float B = dot(float3(+1.0, -1.0, -1.0), YCoCg);
    return float3(R, G, B);
}

float3 HDRToSDR(float3 Color)
{
    return Color * rcp(1.0 + Color);
}

float3 SDRToHDR(float3 Color)
{
    return Color * rcp(1.0 - Color + FLT_EPS);
}

float3 SampleCurrColor(int2 PixelCoord)
{
    return g_TextureCurrColor.Load(int3(PixelCoord, 0));
}

float3 SamplePrevColor(int2 PixelCoord)
{
    return g_TexturePrevColor.Load(int3(PixelCoord, 0));
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float SamplePrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float2 SampleClosestMotion(int2 PixelCoord)
{
    float ClosestDepth = DepthFarPlane;
    int2 ClosestOffset = int2(0, 0);

    const int SearchRadius = 1;
    for (int x = -SearchRadius; x <= SearchRadius; x++)
    {
        for (int y = -SearchRadius; y <= SearchRadius; y++)
        {
            int2 Coord = int2(PixelCoord) + int2(x, y);
            float NeighborDepth = SampleDepth(Coord);
#if TAA_OPTION_INVERTED_DEPTH
            if (NeighborDepth > ClosestDepth)
#else
            if (NeighborDepth < ClosestDepth)
#endif
            {
                ClosestOffset = int2(x, y);
                ClosestDepth = NeighborDepth;
            }
        }
    }

    return g_TextureMotion.Load(int3(PixelCoord + ClosestOffset, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float3 ClipToAABB(float3 AABBMin, float3 AABBMax, float3 P, float3 Q)
{
    float3 R = Q - P;
    float3 TMin = (AABBMin - P.xyz);
    float3 TMax = (AABBMax - P.xyz);

    if (R.x > TMax.x + FLT_EPS)
        R *= (TMax.x / R.x);
    if (R.y > TMax.y + FLT_EPS)
        R *= (TMax.y / R.y);
    if (R.z > TMax.z + FLT_EPS)
        R *= (TMax.z / R.z);

    if (R.x < TMin.x - FLT_EPS)
        R *= (TMin.x / R.x);
    if (R.y < TMin.y - FLT_EPS)
        R *= (TMin.y / R.y);
    if (R.z < TMin.z - FLT_EPS)
        R *= (TMin.z / R.z);

    return P + R;
}

float ComputeDepthDisocclusionWeight(float CurrDepth, float PrevDepth)
{
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_CurrCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr);
}

float ComputeMotionDisocclusion(float2 Position)
{
    float2 CurrMotion = g_TextureMotion.Load(int3(Position, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
    float2 PrevLocation = Position.xy - CurrMotion * g_CurrCamera.f4ViewportSize.xy;
    float2 PrevMotion = g_TexturePrevMotion.Load(int3(PrevLocation, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
    float Disocclusion = length((PrevMotion - CurrMotion) * g_CurrCamera.f4ViewportSize.xy) - TAA_MOTION_VECTOR_DELTA_ERROR;
    return saturate(Disocclusion * TAA_MOTION_DISOCCLUSION_FACTOR);
}

float ComputeDepthDisocclusion(float2 Position)
{
    float2 Motion = g_TextureMotion.Load(int3(Position, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
    int2 PrevLocation = int2(Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy);
    float CurrDepth = SampleDepth(int2(Position));
    float Disocclusion = 0.0;

    const int SearchRadius = 1;
    for (int y = -SearchRadius; y <= SearchRadius; y++)
    {
        for (int x = -SearchRadius; x <= SearchRadius; x++)
        {
            int2 Location = PrevLocation + int2(x, y);
            float PrevDepth = SamplePrevDepth(Location);
            float Weight = ComputeDepthDisocclusionWeight(CurrDepth, PrevDepth);
            Disocclusion = max(Disocclusion, Weight);
        }
    }

    return Disocclusion > TAA_DEPTH_DISOCCLUSION_THRESHOLD ? 0.0 : 1.0;
}

float ComputeDisocclusion(float2 Position)
{
    float Disocclusion = 0.0;
#if TAA_OPTION_MOTION_DISOCCLUSION
    Disocclusion += ComputeMotionDisocclusion(Position);
#endif

#if TAA_OPTION_DEPTH_DISOCCLUSION
    Disocclusion += ComputeDepthDisocclusion(Position);
#endif
    return saturate(Disocclusion);
}

float ComputeAlpha(float2 Position, float LuminanceInput, float LuminanceHistory)
{
    float Alpha = saturate((1.0 - g_TAAAttribs.TemporalStabilityFactor) + ComputeDisocclusion(Position.xy));
    float Delta = 1.0 - abs(LuminanceInput - LuminanceHistory) / max(LuminanceInput, max(LuminanceHistory, 1.0));
    return lerp(0.0, Alpha, Delta * Delta);
}

float3 SamplePrevColorCatmullRom(float2 Position)
{
    // Source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    // License: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae

    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 TexelSize = g_CurrCamera.f4ViewportSize.zw;
    float2 TexPos1 = floor(Position - 0.5) + 0.5;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 F = Position - TexPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 W0 = F * (-0.5 + F * (1.0 - 0.5 * F));
    float2 W1 = 1.0 + F * F * (-2.5 + 1.5 * F);
    float2 W2 = F * (0.5 + F * (2.0 - 1.5 * F));
    float2 W3 = F * F * (-0.5 + 0.5 * F);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 W12 = W1 + W2;
    float2 Offset12 = W2 / (W1 + W2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 TexPos0  = TexPos1 - 1.0;
    float2 TexPos3  = TexPos1 + 2.0;
    float2 TexPos12 = TexPos1 + Offset12;

    TexPos0  *= TexelSize;
    TexPos3  *= TexelSize;
    TexPos12 *= TexelSize;

    float3 Result = float3(0.0, 0.0, 0.0);

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x, TexPos0.y),  0.0).xyz * W0.x * W0.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos0.y), 0.0).xyz * W12.x * W0.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x, TexPos0.y),  0.0).xyz * W3.x * W0.y;

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x, TexPos12.y),  0.0).xyz * W0.x * W12.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos12.y), 0.0).xyz * W12.x * W12.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x, TexPos12.y),  0.0).xyz * W3.x * W12.y;

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x,  TexPos3.y), 0.0).xyz * W0.x * W3.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos3.y), 0.0).xyz * W12.x * W3.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x,  TexPos3.y), 0.0).xyz * W3.x * W3.y;

    return max(Result, 0.0);
}

float3 SamplePrevColorBilinear(float2 Position)
{
    return g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, Position * g_CurrCamera.f4ViewportSize.zw, 0.0);
}

float3 SamplePrevColor(float2 Position)
{
#if TAA_OPTION_BICUBIC_FILTER
    return SamplePrevColorCatmullRom(Position);
#else
    return SamplePrevColorBilinear(Position);
#endif
}

// Welford's online algorithm:
//  https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
PixelStatistic ComputePixelStatisticYCoCgSDR(int2 PixelCoord)
{
    PixelStatistic Desc;
    float WeightSum = 0.0;
    float3 M1 = float3(0.0, 0.0, 0.0);
    float3 M2 = float3(0.0, 0.0, 0.0);

    const int StatisticRadius = 1;
    for (int x = -StatisticRadius; x <= StatisticRadius; x++)
    {
        for (int y = -StatisticRadius; y <= StatisticRadius; y++)
        {
            int2 Offset = int2(x, y);
            int2 Coord = int2(PixelCoord) + Offset;

            float3 HDRColor = SampleCurrColor(Coord);
            float3 SDRColor = RGBToYCoCg(HDRColor * rcp(1.0 + HDRColor));
#if TAA_OPTION_GAUSSIAN_WEIGHTING
            float Weight = exp(-3.0 * float(x * x + y * y) / ((float(StatisticRadius) + 1.0) * (float(StatisticRadius) + 1.0)));
#else
            float Weight = 1.0;
#endif

            M1 += SDRColor * Weight;
            M2 += SDRColor * SDRColor * Weight;
            WeightSum += Weight;

        }
    }

    Desc.Mean = M1 / WeightSum;
    Desc.Variance = M2 / WeightSum - (Desc.Mean * Desc.Mean);
    Desc.StdDev = sqrt(max(Desc.Variance, 0.0));
    return Desc;
}

float4 ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    float2 Motion = SampleClosestMotion(int2(Position.xy));
    float2 PrevLocation = Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy;

    if (!IsInsideScreen(PrevLocation, g_CurrCamera.f4ViewportSize.xy) || g_TAAAttribs.ResetAccumulation)
        return float4(SampleCurrColor(int2(Position.xy)), 1.0);

    float Magnitude = max(0.0, length(Motion * g_CurrCamera.f4ViewportSize.xy) - TAA_MOTION_VECTOR_DELTA_ERROR);
    float MotionWeight = TAA_MAGNITUDE_MOTION_FACTOR * Magnitude;

    float3 RGBHDRCurrColor = SampleCurrColor(int2(Position.xy));
    float3 RGBHDRPrevColor = SamplePrevColor(PrevLocation);

    if (g_TAAAttribs.SkipRejection)
        return float4(lerp(RGBHDRCurrColor, RGBHDRPrevColor, g_TAAAttribs.TemporalStabilityFactor), 1.0);

    float3 YCoCgSDRCurrColor = RGBToYCoCg(HDRToSDR(RGBHDRCurrColor));
    float3 YCoCgSDRPrevColor = RGBToYCoCg(HDRToSDR(RGBHDRPrevColor));

    float VarianceGamma = lerp(TAA_MIN_VARIANCE_GAMMA, TAA_MAX_VARIANCE_GAMMA, exp(-MotionWeight));
    PixelStatistic PixelStat = ComputePixelStatisticYCoCgSDR(int2(Position.xy));
    float3 YCoCgColorMin = PixelStat.Mean - VarianceGamma * PixelStat.StdDev;
    float3 YCoCgColorMax = PixelStat.Mean + VarianceGamma * PixelStat.StdDev;
    float3 YCoCgSDRClampedColor = ClipToAABB(YCoCgColorMin, YCoCgColorMax, clamp(PixelStat.Mean, YCoCgColorMin, YCoCgColorMax), YCoCgSDRPrevColor);

    float Alpha = ComputeAlpha(Position.xy, YCoCgSDRCurrColor.r, YCoCgSDRClampedColor.r);
    float3 RGBHDROutput = SDRToHDR(YCoCgToRGB(lerp(YCoCgSDRClampedColor, YCoCgSDRCurrColor, Alpha)));
    return float4(RGBHDROutput, 1.0);
}
