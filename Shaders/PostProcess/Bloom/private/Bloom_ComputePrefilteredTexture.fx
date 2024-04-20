#include "FullScreenTriangleVSOutput.fxh"
#include "BloomStructures.fxh"
#include "SRGBUtilities.fxh"
#include "PostFX_Common.fxh"

cbuffer cbBloomAttribs
{
    BloomAttribs g_BloomAttribs;
}

Texture2D<float3> g_TextureInput;
SamplerState      g_TextureInput_sampler;

float3 SampleColor(float2 Texcoord, float2 Offset)
{
    return g_TextureInput.SampleLevel(g_TextureInput_sampler, Texcoord + Offset, 0.0);
}

float KarisAverage(float3 Color) 
{
    return 1.0 / (1.0 + Luminance(Color));
}

float3 Prefilter(float3 Color) 
{
	float Brightness = max(Color.r, max(Color.g, Color.b));
    float Knee = g_BloomAttribs.Threshold * g_BloomAttribs.SoftTreshold;
    float Soft = Brightness - g_BloomAttribs.Threshold + Knee;
    Soft = clamp(Soft, 0.0, 2.0 * Knee);
    Soft = Soft * Soft * 0.25 / (Knee + 1.0e-5);

	float Contribution = max(Soft, Brightness - g_BloomAttribs.Threshold);
	Contribution /= max(Brightness, 1.0e-5);
	return Color * Contribution;
}

float3 ComputePrefilteredTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 TextureResolution;
    g_TextureInput.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
   
    float3 A = SampleColor(CenterTexcoord, TexelSize * float2(-2.0, +2.0));
    float3 B = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, +2.0));
    float3 C = SampleColor(CenterTexcoord, TexelSize * float2(+2.0, +2.0));

    float3 D = SampleColor(CenterTexcoord, TexelSize * float2(-2.0, +0.0));
    float3 E = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, +0.0));
    float3 F = SampleColor(CenterTexcoord, TexelSize * float2(+2.0, +0.0));

    float3 G = SampleColor(CenterTexcoord, TexelSize * float2(-2.0, -2.0));
    float3 H = SampleColor(CenterTexcoord, TexelSize * float2(+0.0, -2.0));
    float3 I = SampleColor(CenterTexcoord, TexelSize * float2(+2.0, -2.0));

    float3 J = SampleColor(CenterTexcoord, TexelSize * float2(-1.0, +1.0));
    float3 K = SampleColor(CenterTexcoord, TexelSize * float2(+1.0, +1.0));
    float3 L = SampleColor(CenterTexcoord, TexelSize * float2(-1.0, -1.0));
    float3 M = SampleColor(CenterTexcoord, TexelSize * float2(+1.0, -1.0));

    float Weights[5];
    Weights[0] = 0.125f;
    Weights[1] = 0.125f;
    Weights[2] = 0.125f;
    Weights[3] = 0.125f;
    Weights[4] = 0.5f;

    float3 Groups[5];
    Groups[0] = (A + B + D + E) / 4.0f;
    Groups[1] = (B + C + E + F) / 4.0f;
    Groups[2] = (D + E + G + H) / 4.0f;
    Groups[3] = (E + F + H + I) / 4.0f;
    Groups[4] = (J + K + L + M) / 4.0f;
    
    float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
    for (int GroupId = 0; GroupId < 5; ++GroupId) {
        float Weight = Weights[GroupId] * KarisAverage(Groups[GroupId]);
        ColorSum += float4(Groups[GroupId], 1.0) * Weight;
    }
    
    return Prefilter(ColorSum.xyz / (ColorSum.w + 1.0e-5));
}
