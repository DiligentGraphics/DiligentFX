#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float4> g_TextureMaterialParameters;
Texture2D<float>  g_TextureDepth;

float LoadRoughness(int2 PixelCoord)
{
    float4 MaterialParams = g_TextureMaterialParameters.Load(int3(PixelCoord, 0));
    float4 RoughnessSelector = float4(
        g_SSRAttribs.RoughnessChannel == 0u ? 1.0 : 0.0,
        g_SSRAttribs.RoughnessChannel == 1u ? 1.0 : 0.0,
        g_SSRAttribs.RoughnessChannel == 2u ? 1.0 : 0.0,
        g_SSRAttribs.RoughnessChannel == 3u ? 1.0 : 0.0);
    float Roughness = dot(MaterialParams, RoughnessSelector);
    if (!g_SSRAttribs.IsRoughnessPerceptual)
        Roughness = sqrt(Roughness);
    return Roughness;
}

float LoadDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeStencilMaskAndExtractRoughnessPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float Roughness = LoadRoughness(int2(VSOut.f4PixelPos.xy));
    float Depth     = LoadDepth(int2(VSOut.f4PixelPos.xy));
    if (!IsReflectionSample(Roughness, Depth, g_SSRAttribs.RoughnessThreshold))
        discard;

    return Roughness;
}
