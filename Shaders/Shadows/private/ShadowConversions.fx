
#include "FullScreenTriangleVSOutput.fxh"

struct ConversionAttribs
{
    int iCascade;
    int iFilterRadius;
};

cbuffer cbConversionAttribs
{
    ConversionAttribs g_Attribs;
}

Texture2DArray g_tex2DShadowMap;

float4 VSMHorzPS(FullScreenTriangleVSOutput VSOut) : SV_Target
{
    float2 f2Moments = float2(0.0, 0.0);
    for (int i = -g_Attribs.iFilterRadius; i <= +g_Attribs.iFilterRadius; ++i)
    {
        float fDepth = g_tex2DShadowMap.Load( int4( int(VSOut.f4PixelPos.x) + i, int(VSOut.f4PixelPos.y), g_Attribs.iCascade, 0) ).r;
        f2Moments += float2(fDepth, fDepth*fDepth);
    }
    return float4(f2Moments / float(g_Attribs.iFilterRadius*2 + 1), 0.0, 0.0);
}

float4 VSMVertPS(FullScreenTriangleVSOutput VSOut) : SV_Target
{
    float2 f2Moments = float2(0.0, 0.0);
    for (int i = -g_Attribs.iFilterRadius; i <= +g_Attribs.iFilterRadius; ++i)
    {
        f2Moments += g_tex2DShadowMap.Load( int4( int(VSOut.f4PixelPos.x), int(VSOut.f4PixelPos.y) + i, 0, 0) ).rg;
    }
    return float4(f2Moments / float(g_Attribs.iFilterRadius*2 + 1), 0.0, 0.0);
}
