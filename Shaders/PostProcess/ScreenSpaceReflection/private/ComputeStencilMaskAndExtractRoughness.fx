#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float4> g_TextureMaterialParameters;
Texture2D<float>  g_TextureDepth;

float SampleRoughness(uint2 PixelCoord)
{
    float Roughness = g_TextureMaterialParameters.Load(int3(PixelCoord, 0))[g_SSRAttribs.RoughnessChannel];
    if (g_SSRAttribs.IsRoughnessPerceptual)
        Roughness *= Roughness;
    return Roughness;
}

float SampleDepth(uint2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeStencilMaskAndExtractRoughnessPS(in float4 Position : SV_Position) : SV_Target0
{
    float Roughness = SampleRoughness(uint2(Position.xy));
    float Depth = SampleDepth(uint2(Position.xy));
    
    if (!IsGlossyReflection(Roughness, g_SSRAttribs.RoughnessThreshold, g_SSRAttribs.IsRoughnessPerceptual) || IsBackground(Depth))
        discard;

    return Roughness;
}
