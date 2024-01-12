#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float4> g_TextureMaterialParameters;
Texture2D<float>  g_TextureDepth;

float SampleRoughness(int2 PixelCoord)
{
    float Roughness = g_TextureMaterialParameters.Load(int3(PixelCoord, 0))[g_SSRAttribs.RoughnessChannel];
    if (!g_SSRAttribs.IsRoughnessPerceptual)
        Roughness = sqrt(Roughness);
    return Roughness;
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeStencilMaskAndExtractRoughnessPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float Roughness = SampleRoughness(int2(VSOut.f4PixelPos.xy));
    float Depth = SampleDepth(int2(VSOut.f4PixelPos.xy));
    if (!IsReflectionSample(Roughness, Depth, g_SSRAttribs.RoughnessThreshold))
        discard;

    return Roughness;
}
