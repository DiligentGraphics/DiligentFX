
#include "FullScreenTriangleVSOutput.fxh"

struct ConversionAttribs
{
    int iCascade;
};

cbuffer cbConversionAttribs
{
    ConversionAttribs g_Attribs;
}

Texture2DArray g_tex2DShadowMap;

void VSMHorzPS(FullScreenTriangleVSOutput VSOut,
               out float4 f2Moments : SV_Target)
{
    float fDepth = g_tex2DShadowMap.Load( int4(VSOut.f4PixelPos.xy, g_Attribs.iCascade, 0) ).r;
    f2Moments = float4(fDepth, fDepth*fDepth, 0.0, 0.0);
}

void VSMVertPS(FullScreenTriangleVSOutput VSOut,
               out float4 f2Moments : SV_Target)
{
    float2 f2MomentsHorx = g_tex2DShadowMap.Load( int4(VSOut.f4PixelPos.xy, g_Attribs.iCascade, 0) ).rg;
    f2Moments = float4(f2MomentsHorx, 0.0, 0.0);
}

