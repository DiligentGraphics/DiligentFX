#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "PostFX_Common.fxh"
#include "DOF_Common.fx"

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float3> g_TextureColor;
Texture2D<float>  g_TextureCoC;
Texture2D<float>  g_TextureDilationCoC;

SamplerState g_TextureDilationCoC_sampler;

struct PSOutput
{
    float4 ForegroundColor : SV_Target0;
    float4 BackgroundColor : SV_Target1;
};

PSOutput ComputePrefilteredTexturePS(in FullScreenTriangleVSOutput VSOut)
{
    int2 Position = int2(VSOut.f4PixelPos.xy);
    float2 Texcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);

    float CoCMin = +FLT_MAX;
    float CoCMax = -FLT_MAX;

	float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
   
    for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
    {
        int2 Location = 2 * Position + int2(SampleIdx & 0x01, SampleIdx >> 1);
        float3 Color = g_TextureColor.Load(int3(Location, 0));
        float CoC    = g_TextureCoC.Load(int3(Location, 0));
        float Weight = ComputeSDRWeight(Color);
       
        CoCMin = min(CoCMin, CoC);
        CoCMax = max(CoCMax, CoC);
        ColorSum += float4(Color, 1.0) * Weight;
    }

    float ForegroundAlpha = g_TextureDilationCoC.SampleLevel(g_TextureDilationCoC_sampler, Texcoord, 0.0);
    float BackgroundAlpha = abs(CoCMax) * float(CoCMax > 0.0);

    PSOutput Output;
    Output.ForegroundColor = float4(ColorSum.xyz / max(ColorSum.w, 1.e-5), ForegroundAlpha);
    Output.BackgroundColor = float4(ColorSum.xyz / max(ColorSum.w, 1.e-5), BackgroundAlpha);
    return Output;
}
