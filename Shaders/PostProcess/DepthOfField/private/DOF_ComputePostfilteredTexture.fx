#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"

Texture2D<float4> g_TextureColorCoCNear;
Texture2D<float4> g_TextureColorCoCFar;

SamplerState g_TextureColorCoCNear_sampler;
SamplerState g_TextureColorCoCFar_sampler;

struct PSOutput
{
    float4 ForegroundColor : SV_Target0;
    float4 BackgroundColor : SV_Target1;
};

float4 SampleColorCoCNear(float2 Texcoord, float2 Offset)
{
    return g_TextureColorCoCNear.SampleLevel(g_TextureColorCoCNear_sampler, Texcoord + Offset, 0.0);
}

float4 SampleColorCoCFar(float2 Texcoord, float2 Offset)
{
    return g_TextureColorCoCFar.SampleLevel(g_TextureColorCoCFar_sampler, Texcoord + Offset, 0.0);
}

PSOutput ComputePostfilteredTexturePS(in FullScreenTriangleVSOutput VSOut)
{
    float2 TextureResolution;
    g_TextureColorCoCNear.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
   
    float4 A = SampleColorCoCNear(CenterTexcoord, TexelSize * float2(-0.5, -0.5));
    float4 B = SampleColorCoCNear(CenterTexcoord, TexelSize * float2(-0.5, +0.5));
    float4 C = SampleColorCoCNear(CenterTexcoord, TexelSize * float2(+0.5, -0.5));
    float4 D = SampleColorCoCNear(CenterTexcoord, TexelSize * float2(+0.5, +0.5));

    float4 E = SampleColorCoCFar(CenterTexcoord, TexelSize * float2(-0.5, -0.5));
    float4 F = SampleColorCoCFar(CenterTexcoord, TexelSize * float2(-0.5, +0.5));
    float4 G = SampleColorCoCFar(CenterTexcoord, TexelSize * float2(+0.5, -0.5));
    float4 H = SampleColorCoCFar(CenterTexcoord, TexelSize * float2(+0.5, +0.5));

    PSOutput Output;
    Output.ForegroundColor = 0.25 * (A + B + C + D);
    Output.BackgroundColor = 0.25 * (E + F + G + H);
    return Output;
}
