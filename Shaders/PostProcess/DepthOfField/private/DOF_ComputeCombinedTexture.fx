#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "BasicStructures.fxh"
#include "PostFX_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float3> g_TextureColor;
Texture2D<float>  g_TextureCoC;
Texture2D<float4> g_TextureDoFNearPlane;
Texture2D<float4> g_TextureDoFFarPlane;

SamplerState g_TextureDoFNearPlane_sampler;
SamplerState g_TextureDoFFarPlane_sampler;

float4 SampleDoFNearPlane(float2 Texcoord)
{
    return g_TextureDoFNearPlane.SampleLevel(g_TextureDoFNearPlane_sampler, Texcoord, 0.0);
}

float4 SampleDoFFarPlane(float2 Texcoord)
{
    return g_TextureDoFFarPlane.SampleLevel(g_TextureDoFFarPlane_sampler, Texcoord, 0.0);
}

float3 ComputeCombinedTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Texcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
    float3 SourceFullRes = g_TextureColor.Load(int3(VSOut.f4PixelPos.xy, 0));

    float4 DoFNear = SampleDoFNearPlane(Texcoord);
    float4 DoFFar  = SampleDoFFarPlane(Texcoord);

    float3 Result = SourceFullRes;
    Result.rgb = lerp(Result, DoFFar.rgb, smoothstep(0.1, 1.0, DoFFar.a));
    Result.rgb = lerp(Result.rgb, DoFNear.rgb, smoothstep(0.1, 1.0, DoFNear.a));
    return lerp(SourceFullRes, Result, g_DOFAttribs.AlphaInterpolation);
}
