#include "FullScreenTriangleVSOutput.fxh"

Texture2D<float3> g_TextureInput;
SamplerState      g_TextureInput_sampler;

float3 ComputeDownsampledTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 TextureResolution;
    g_TextureInput.GetDimensions(TextureResolution.x, TextureResolution.y);

    float2 TexelSize = rcp(TextureResolution);
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
   
    float2 Coords[13];
    Coords[0] = float2(-1.0f, +1.0f); Coords[1] = float2(+1.0f, +1.0f);
    Coords[2] = float2(-1.0f, -1.0f); Coords[3] = float2(+1.0f, -1.0f);

    Coords[4]  = float2(-2.0f, +2.0f); Coords[5]  = float2(+0.0f, +2.0f); Coords[6]  = float2(+2.0f, +2.0f);
    Coords[7]  = float2(-2.0f, +0.0f); Coords[8]  = float2(+0.0f, +0.0f); Coords[9]  = float2(+2.0f, +0.0f);
    Coords[10] = float2(-2.0f, -2.0f); Coords[11] = float2(+0.0f, -2.0f); Coords[12] = float2(+2.0f, -2.0f);


    float Weights[13];
    // 4 samples
    // (1 / 4) * 0.5f = 0.125f
    Weights[0] = 0.125f; Weights[1] = 0.125f;
    Weights[2] = 0.125f; Weights[3] = 0.125f;

    // 9 samples
    // (1 / 9) * 0.5f
    Weights[4]  = 0.0555555f; Weights[5]  = 0.0555555f; Weights[6]  = 0.0555555f,
    Weights[7]  = 0.0555555f; Weights[8]  = 0.0555555f; Weights[9]  = 0.0555555f,
    Weights[10] = 0.0555555f; Weights[11] = 0.0555555f; Weights[12] = 0.0555555f;

    float3 OutColor = float3(0.0f, 0.0f, 0.0f);
    for (int SampleIdx = 0; SampleIdx < 13; SampleIdx++ )
    {
        float2 Texcoord = CenterTexcoord + Coords[SampleIdx] * TexelSize;
        OutColor += Weights[SampleIdx] * g_TextureInput.SampleLevel(g_TextureInput_sampler, Texcoord, 0.0);
    }

    return OutColor;
}
