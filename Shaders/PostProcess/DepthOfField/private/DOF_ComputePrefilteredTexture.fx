#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "PostFX_Common.fxh"

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float3> g_TextureColor;
Texture2D<float>  g_TextureCoC;

float ComputeWeigh(float3 Color) 
{
	return 1.0 / (1.0 + max(max(Color.r, Color.g), Color.b));
}

float4 ComputePrefilteredTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int2 Position = 2 * int2(VSOut.f4PixelPos.xy);

    float CoCMin = +FLT_MAX;
    float CoCMax = -FLT_MAX;

	float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
   
    for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
    {
        int2 Location = Position + int2(SampleIdx & 0x01, SampleIdx >> 1);

        float3 Color = g_TextureColor.Load(int3(Location, 0));
        float CoC    = g_TextureCoC.Load(int3(Location, 0));
        float Weight = ComputeWeigh(Color);
       
        CoCMin = min(CoCMin, CoC);
        CoCMax = max(CoCMax, CoC);

        ColorSum.xyz += Weight * Color;
        ColorSum.w   += Weight;
    }
    
    return float4(ColorSum.xyz / max(ColorSum.w, 1.e-5), CoCMax >= -CoCMin ? CoCMax : CoCMin);
}
