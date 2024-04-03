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

float3 ComputeUpsampledTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 TextureResolution;
    g_TextureDownsampled.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY);
   
    float2 Coords[9];
    Coords[0] = float2( -1.0f,  1.0f); Coords[1] = float2(0.0f,  1.0f); Coords[2] = float2(1.0f,  1.0f);
    Coords[3] = float2( -1.0f,  0.0f); Coords[4] = float2(0.0f,  0.0f); Coords[5] = float2(1.0f,  0.0f);
    Coords[6] = float2( -1.0f, -1.0f); Coords[7] = float2(0.0f, -1.0f); Coords[8] = float2(1.0f, -1.0f);
    
    float Weights[9];
    Weights[0] = 0.0625f; Weights[1] = 0.125f; Weights[2] = 0.0625f;
    Weights[3] = 0.125f;  Weights[4] = 0.25f;  Weights[5] = 0.125f;
    Weights[6] = 0.0625f; Weights[7] = 0.125f; Weights[8] = 0.0625f;
    
    float3 ColorSum = float3(0.0f, 0.0f, 0.0f);
    for (int SampleIdx = 0; SampleIdx < 9; SampleIdx++)
    {
        float2 Texcoord = CenterTexcoord + Coords[SampleIdx] * TexelSize;
        ColorSum += Weights[SampleIdx] * g_TextureDownsampled.SampleLevel(g_TextureDownsampled_sampler, Texcoord, 0.0);
    }
    
    float BlendFactor = VSOut.uInstID != 0u ? g_BloomAttribs.ExternalBlend : g_BloomAttribs.InternalBlend;
    return lerp(g_TextureInput.SampleLevel(g_TextureInput_sampler, CenterTexcoord, 0.0), ColorSum, BlendFactor);
}
