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


float4 SampleCurrColor(int2 PixelCoord)
{
    return g_TextureCurrColor.Load(int3(PixelCoord, 0));
}

float4 SamplePrevColor(int2 PixelCoord)
{
    return g_TexturePrevColor.Load(int3(PixelCoord, 0));
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

    float4 CurrColor = SampleCurrColor(int2(Position.xy));
    float4 PrevColor = SamplePrevColor(int2(PrevLocation));

    float IsBackground  = float(Depth >= 1.0 - FLT_EPS);
    float IsTransparent = float(CurrColor.a < 0.5);

    Motion = abs(Motion * g_CurrCamera.f4ViewportSize.xy);

    float MotionThreshold = 2.0;
    float StaticThreshold = 1.0;
    // If motion is within 1 pixel, the pixel is considered to be static
    float MotionWeight = saturate((max(Motion.x, Motion.y) - StaticThreshold) / (MotionThreshold - StaticThreshold));

    float BackgroundWeight  = float(Depth >= 1.0 - FLT_EPS) * 0.5;
    float TransparentWeight = 1.0 - CurrColor.a;
    float ResetWeight       = float(VSOut.uInstID);
    float CurrFrameWeight = clamp(MotionWeight + BackgroundWeight + TransparentWeight + ResetWeight, 0.1, 1.0);

    return float4(lerp(PrevColor.rgb, CurrColor.rgb, CurrFrameWeight), CurrColor.a);
}
