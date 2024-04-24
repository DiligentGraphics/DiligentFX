#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float3> g_TextureColor;
Texture2D<float>  g_TextureCoC;
Texture2D<float4> g_TextureDoF;

SamplerState g_TextureDoF_sampler;

float3 ComputeCombinedTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float3 Source = g_TextureColor.Load(int3(VSOut.f4PixelPos.xy, 0));

	float  CoC = g_TextureCoC.Load(int3(VSOut.f4PixelPos.xy, 0));
	float4 DoF = g_TextureDoF.SampleLevel(g_TextureDoF_sampler, NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy), 0.0);

	float Strength = smoothstep(0.1, 1.0, abs(CoC));
	return lerp(Source.rgb, DoF.rgb, Strength + DoF.a - Strength * DoF.a);
}
