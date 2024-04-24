#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"


Texture2D<float4> g_TextureDoF;
SamplerState      g_TextureDoF_sampler;

float4 SampleColor(float2 Texcoord, float2 Offset)
{
    return g_TextureDoF.SampleLevel(g_TextureDoF_sampler, Texcoord + Offset, 0.0);
}

float4 ComputePostfilteredTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 TextureResolution;
    g_TextureDoF.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
   
    float4 A = SampleColor(CenterTexcoord, TexelSize * float2(-0.5, -0.5));
    float4 B = SampleColor(CenterTexcoord, TexelSize * float2(-0.5, +0.5));
    float4 C = SampleColor(CenterTexcoord, TexelSize * float2(+0.5, -0.5));
    float4 D = SampleColor(CenterTexcoord, TexelSize * float2(+0.5, +0.5));

    return 0.25 * (A + B + C + D);
}
