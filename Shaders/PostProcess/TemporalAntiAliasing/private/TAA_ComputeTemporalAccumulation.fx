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
    // float Y  = dot(float3(+0.25, +0.50, +0.25), RGB);
    // float Co = dot(float3(+0.50, +0.00, -0.50), RGB);
    // float Cg = dot(float3(-0.25, +0.50, -0.25), RGB);

#if TAA_OPTION_YCOCG_COLOR_SPACE
    float Co   = RGB.x - RGB.z;
    float Temp = RGB.z + 0.5 * Co;
    float Cg   = RGB.y - Temp;
    float Y    = Temp + 0.5 * Cg;
    return float3(Y, Co, Cg);
#else
    return RGB;
#endif
}

float3 YCoCgToRGB(float3 YCoCg)
{
    // float R = dot(float3(+1.0, +1.0, -1.0), YCoCg);
    // float G = dot(float3(+1.0, +0.0, +1.0), YCoCg);
    // float B = dot(float3(+1.0, -1.0, -1.0), YCoCg);

#if TAA_OPTION_YCOCG_COLOR_SPACE 
    float Tmp = YCoCg.x - 0.5 * YCoCg.z;
    float G   = YCoCg.z + Tmp;
    float B   = Tmp - 0.5 * YCoCg.y;
    float R   = B + YCoCg.y;
    return float3(R, G, B);
#else
    return YCoCg;
#endif
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

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
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
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_PrevCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return exp(-abs(LinearDepthPrev - LinearDepthCurr) / LinearDepthCurr);
}

float ComputeDepthDisocclusion(float2 Position, float2 PrevPosition)
{
    int2 PrevPositioni = int2(PrevPosition);
    float CurrDepth = SampleCurrDepth(int2(Position));
    float Disocclusion = 0.0;

    const int SearchRadius = 1;
    for (int y = -SearchRadius; y <= SearchRadius; y++)
    {
        for (int x = -SearchRadius; x <= SearchRadius; x++)
        {
            int2 Location = PrevPositioni + int2(x, y);
            float PrevDepth = SamplePrevDepth(Location);
            float Weight = ComputeDepthDisocclusionWeight(CurrDepth, PrevDepth);
            Disocclusion = max(Disocclusion, Weight);
        }
    }

    return Disocclusion > TAA_DEPTH_DISOCCLUSION_THRESHOLD ? 1.0 : 0.0;
}

float4 SamplePrevColorCatmullRom(float2 Position)
{
    // Source: https://advances.realtimerendering.com/s2016/Filmic%20SMAA%20v7.pptx Slide 77
    
    float2 TexelSize = g_CurrCamera.f4ViewportSize.zw;
    float2 CenterPosition = floor(Position - 0.5) + 0.5;

    float2 F = Position - CenterPosition;
    float2 F2 = F * F;
    float2 F3 = F2 * F;

    float2 W0 = -0.5 * F3 + F2 - 0.5 * F;
    float2 W1 = 1.5 * F3 - 2.5 * F2 + 1.0;
    float2 W2 = -1.5 * F3 + 2.0 * F2 + 0.5 * F;
    float2 W3 = 0.5 * F3 - 0.5 * F2;
    float2 W12 = W1 + W2;

    float2 TexPos0  = (CenterPosition - 1.0) * TexelSize;
    float2 TexPos3  = (CenterPosition + 2.0) * TexelSize;
    float2 TexPos12 = (CenterPosition + W2 / W12) * TexelSize;

    float P0 = W12.x * W0.y;
    float P1 = W0.x * W12.x;
    float P2 = W12.x * W12.y;
    float P3 = W3.x * W12.y;
    float P4 = W12.x * W3.y;

    float4 Result = float4(0.0, 0.0, 0.0, 0.0);
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos0.y),  0.0) * P0;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos0.x,  TexPos12.y), 0.0) * P1;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos12.y), 0.0) * P2;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos3.x,  TexPos12.y), 0.0) * P3;
    Result += g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, float2(TexPos12.x, TexPos3.y),  0.0) * P4;

    return max(Result * rcp(P0 + P1 + P2 + P3 + P4), 0.0);
}

float4 SamplePrevColorBilinear(float2 Position)
{
    return max(g_TexturePrevColor.SampleLevel(g_TexturePrevColor_sampler, Position * g_CurrCamera.f4ViewportSize.zw, 0.0), 0.0);
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

float ComputeCorrectedAlpha(float Alpha)
{
    return min(g_TAAAttribs.TemporalStabilityFactor, saturate(1.0 / (2.0 - Alpha)));
}

float4 ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Position = VSOut.f4PixelPos.xy;
    float2 Motion = SampleMotion(int2(Position.xy));
    float2 PrevPosition = Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy;

    if (!IsInsideScreen(PrevPosition, g_CurrCamera.f4ViewportSize.xy) || g_TAAAttribs.ResetAccumulation)
        return float4(SampleCurrColor(int2(Position)), 0.5);

    float AspectRatio = g_CurrCamera.f4ViewportSize.x * g_CurrCamera.f4ViewportSize.w;
    float MotionFactor = saturate(1.0 - length(float2(Motion.x * AspectRatio, Motion.y)) * TAA_MOTION_VECTOR_DIFF_FACTOR);
    float DepthFactor = ComputeDepthDisocclusion(Position, PrevPosition);

    float3 RGBHDRCurrColor = SampleCurrColor(int2(Position));
    float4 RGBHDRPrevColor = SamplePrevColor(PrevPosition);

    float3 YCoCgSDRCurrColor = RGBToYCoCg(HDRToSDR(RGBHDRCurrColor.xyz));
    float3 YCoCgSDRPrevColor = RGBToYCoCg(HDRToSDR(RGBHDRPrevColor.xyz));

    if (g_TAAAttribs.SkipRejection) 
    {
        float3 RGBHDROutput = SDRToHDR(YCoCgToRGB(lerp(YCoCgSDRCurrColor, YCoCgSDRPrevColor, RGBHDRPrevColor.a)));
        return float4(RGBHDROutput, ComputeCorrectedAlpha(RGBHDRPrevColor.a));
    }
        
    float VarianceGamma = lerp(TAA_MIN_VARIANCE_GAMMA, TAA_MAX_VARIANCE_GAMMA, MotionFactor * MotionFactor);
    PixelStatistic PixelStat = ComputePixelStatisticYCoCgSDR(int2(Position.xy));
    float3 YCoCgSDRClampedColor = ClipToAABB(YCoCgSDRPrevColor, YCoCgSDRCurrColor, PixelStat.Mean, VarianceGamma * PixelStat.StdDev);

    float Alpha = RGBHDRPrevColor.a * MotionFactor * DepthFactor;
    float3 RGBHDROutput = SDRToHDR(YCoCgToRGB(lerp(YCoCgSDRCurrColor, YCoCgSDRClampedColor, Alpha)));
    return float4(RGBHDROutput, ComputeCorrectedAlpha(Alpha));
}
