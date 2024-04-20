#include "FullScreenTriangleVSOutput.fxh"
#include "BloomStructures.fxh"

cbuffer cbBloomAttribs
{
    BloomAttribs g_BloomAttribs;
}

Texture2D<float3> g_TextureInput;
SamplerState      g_TextureInput_sampler;

Texture2D<float3> g_TextureDownsampled;
SamplerState      g_TextureDownsampled_sampler;

float3 SampleColor(float2 Texcoord, float2 Offset)
{
    return g_TextureDownsampled.SampleLevel(g_TextureDownsampled_sampler, Texcoord + Offset, 0.0);
}

float3 ComputeUpsampledTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 TextureResolution;
    g_TextureInput.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY);
   
    float3 A = SampleColor(CenterTexcoord, TexelSize * float2(-1.0, +1.0));
    float3 B = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, +1.0));
    float3 C = SampleColor(CenterTexcoord, TexelSize * float2(+1.0, +1.0));

    float3 D = SampleColor(CenterTexcoord, TexelSize * float2(-1.0, +0.0));
    float3 E = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, +0.0));
    float3 F = SampleColor(CenterTexcoord, TexelSize * float2(+1.0, +0.0));

    float3 G = SampleColor(CenterTexcoord, TexelSize * float2(-1.0, -1.0));
    float3 H = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, -1.0));
    float3 I = SampleColor(CenterTexcoord, TexelSize * float2(+1.0, -1.0));

    float3 ColorSum = E * 0.25;
    ColorSum += (B + D + F + H) * 0.125;
    ColorSum += (A + C + G + I) * 0.0625;
    
    if (VSOut.uInstID != 0u)
    {
        float3 SourceColor = g_TextureInput.SampleLevel(g_TextureInput_sampler, CenterTexcoord, 0.0);
        return SourceColor + g_BloomAttribs.Intensity * ColorSum;
    } 
    else
    {
        float3 SourceColor = g_TextureInput.SampleLevel(g_TextureInput_sampler, CenterTexcoord, 0.0);
        return SourceColor + ColorSum;
    }
}
