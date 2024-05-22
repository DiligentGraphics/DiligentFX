#include "FullScreenTriangleVSOutput.fxh"

Texture2D<float4> g_TextureColor;
SamplerState      g_TextureColor_sampler;           

float4 CopyColorPS(FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    return g_TextureColor.SampleLevel(g_TextureColor_sampler, NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy), 0.0);
}
