#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#define M_PI                      3.14159265358979
#define M_EPSILON                 1e-3

#define FLT_EPS                   5.960464478e-8
#define FLT_MAX                   3.402823466e+38
#define FLT_MIN                   1.175494351e-38

#pragma warning(disable : 3078)

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

Texture2D<float4> g_TextureCurrColor;
Texture2D<float4> g_TexturePrevColor;
Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureDepth;


float3 SampleCurrColor(int2 PixelCoord)
{
    return g_TextureCurrColor.Load(int3(PixelCoord, 0)).xyz;
}

float3 SamplePrevColor(int2 PixelCoord)
{
    return g_TexturePrevColor.Load(int3(PixelCoord, 0)).xyz;
}

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float4 ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;

    float Depth = SampleDepth(int2(Position.xy));
    float2 Motion = SampleMotion(int2(Position.xy));
    
    float2 PrevLocation = Position.xy - Motion * float2(g_CurrCamera.f4ViewportSize.xy);

    float3 CurrColor = SampleCurrColor(int2(Position.xy));
    float3 PrevColor = SamplePrevColor(int2(PrevLocation));

    /* With this approach, we have flickering
    float3 MinColor = +FLT_MAX, MaxColor = -FLT_MAX;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float3 Color = SampleCurrColor(int2(Position.xy) + int2(x, y));
            MinColor = min(MinColor, Color); // Take min and max
            MaxColor = max(MaxColor, Color);
        }
    } 

    float3 ClampedPrevColor = clamp(PrevColor, MinColor, MaxColor);
    */

    float IsBackground = float(Depth >= 1.0 - FLT_EPS);
    float Alpha = max(abs(Motion.x) * g_CurrCamera.f4ViewportSize.x, abs(Motion.y) * g_CurrCamera.f4ViewportSize.y) + 0.5 * IsBackground;
    float CurrentWeight = clamp(Alpha, 0.1, 0.9);
    float PreviousWeight = 1.0 - CurrentWeight;

    return float4(CurrColor.rgb * CurrentWeight + PrevColor.rgb * PreviousWeight, 1.0);
}
