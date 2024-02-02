#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#define FLT_EPS   5.960464478e-8
#define FLT_MAX   3.402823466e+38

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

    float IsBackground = float(Depth >= 1.0 - FLT_EPS);
    float Alpha = max(abs(Motion.x) * g_CurrCamera.f4ViewportSize.x, abs(Motion.y) * g_CurrCamera.f4ViewportSize.y) + 0.5 * IsBackground;
    float CurrentWeight = clamp(Alpha, 0.1, 0.9);
    float PreviousWeight = 1.0 - CurrentWeight;

    return float4(CurrColor.rgb * CurrentWeight + PrevColor.rgb * PreviousWeight, 1.0);
}
