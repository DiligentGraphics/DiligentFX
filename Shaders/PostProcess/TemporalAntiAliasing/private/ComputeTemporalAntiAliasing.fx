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
Texture2D<float4> g_TexturePrevColor;
Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureCurrDepth;
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

float4 SamplePrevColor(int2 PixelCoord)
{
    return g_TexturePrevColor.Load(int3(PixelCoord, 0));
}

float SampleCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
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
            float NeighborDepth = SampleCurrDepth(Coord);
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

float3 ClipToAABB(float3 ColorPrev, float3 ColorCurr, float3 AABBCentre, float3 AABBExtents)
{
    float MaxT = TAA_VARIANCE_INTERSECTION_MAX_T;
    float3 Direction = ColorCurr - ColorPrev;
    float3 Intersection = ((AABBCentre - sign(Direction) * AABBExtents) - ColorPrev) / Direction;
    float3 PossibleT = lerp(float3(MaxT + 1.0, MaxT + 1.0, MaxT + 1.0), Intersection, GreaterEqual(Intersection, float3(0.0, 0.0, 0.0)));
    float T = min(MaxT, min(PossibleT.x, min(PossibleT.y, PossibleT.z)));
    return lerp(ColorPrev, ColorPrev + Direction * T, Less(float3(T, T, T), float3(MaxT, MaxT, MaxT)));
}

float ComputeDepthDisocclusionWeight(float CurrDepth, float PrevDepth)
{
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_CurrCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr);
}

float ComputeDepthDisocclusion(float2 Position, float2 Motion)
{
    int2 PrevLocation = int2(Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy);
    float CurrDepth = SampleCurrDepth(int2(Position));
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

    return Disocclusion > TAA_DEPTH_DISOCCLUSION_THRESHOLD ? 1.0 : 0.0;
}

float4 SamplePrevColorCatmullRom(float2 Position)
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

    float4 Result = float4(0.0, 0.0, 0.0, 0.0);

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x, TexPos0.y),  0.0) * W0.x * W0.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos0.y), 0.0) * W12.x * W0.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x, TexPos0.y),  0.0) * W3.x * W0.y;

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x, TexPos12.y),  0.0) * W0.x * W12.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos12.y), 0.0) * W12.x * W12.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x, TexPos12.y),  0.0) * W3.x * W12.y;

    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x,  TexPos3.y), 0.0) * W0.x * W3.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos3.y), 0.0) * W12.x * W3.y;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x,  TexPos3.y), 0.0) * W3.x * W3.y;

    return max(Result, 0.0);
}

float4 SamplePrevColorBilinear(float2 Position)
{
    return g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, Position * g_CurrCamera.f4ViewportSize.zw, 0.0);
}

float4 SamplePrevColor(float2 Position)
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
            int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_CurrCamera.f4ViewportSize.xy));
            float3 HDRColor = SampleCurrColor(Location);
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
        return float4(SampleCurrColor(int2(Position.xy)), 0.5);

    float MotionFactor = saturate(1.0 - length(Motion * g_CurrCamera.f4ViewportSize.xy) / TAA_MOTION_VECTOR_PIXEL_DIFF);
    float DepthFactor = ComputeDepthDisocclusion(Position.xy, Motion);

    float3 RGBHDRCurrColor = SampleCurrColor(int2(Position.xy));
    float4 RGBHDRPrevColor = SamplePrevColor(PrevLocation);

    if (g_TAAAttribs.SkipRejection)
        return float4(lerp(RGBHDRCurrColor.xyz, RGBHDRPrevColor.xyz, RGBHDRPrevColor.a), saturate(1.0 / (2.0 - RGBHDRPrevColor.a)));

    float3 YCoCgSDRCurrColor = RGBToYCoCg(HDRToSDR(RGBHDRCurrColor.xyz));
    float3 YCoCgSDRPrevColor = RGBToYCoCg(HDRToSDR(RGBHDRPrevColor.xyz));

    float VarianceGamma = lerp(TAA_MIN_VARIANCE_GAMMA, TAA_MAX_VARIANCE_GAMMA, MotionFactor * MotionFactor);
    PixelStatistic PixelStat = ComputePixelStatisticYCoCgSDR(int2(Position.xy));
    float3 YCoCgSDRClampedColor = ClipToAABB(YCoCgSDRPrevColor, YCoCgSDRCurrColor, PixelStat.Mean, VarianceGamma * PixelStat.StdDev);

    float Alpha = RGBHDRPrevColor.a * MotionFactor * DepthFactor;
    float3 RGBHDROutput = SDRToHDR(YCoCgToRGB(lerp(YCoCgSDRCurrColor, YCoCgSDRClampedColor, Alpha)));
    return float4(RGBHDROutput, saturate(1.0 / (2.0 - Alpha)));
}
