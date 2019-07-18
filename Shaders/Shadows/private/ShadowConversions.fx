
#include "FullScreenTriangleVSOutput.fxh"
#include "BasicStructures.fxh"
#include "Shadows.fxh"

struct ConversionAttribs
{
    int iCascade;
    int iFilterRadius;
    float fEVSMPositiveExponent;
    float fEVSMNegativeExponent;

    bool Is32BitEVSM;
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

float4 EVSMHorzPS(FullScreenTriangleVSOutput VSOut) : SV_Target
{
    float2 f2Exponents = GetEVSMExponents(g_Attribs.fEVSMPositiveExponent, g_Attribs.fEVSMNegativeExponent, g_Attribs.Is32BitEVSM);

    float4 f4Moments = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = -g_Attribs.iFilterRadius; i <= +g_Attribs.iFilterRadius; ++i)
    {
        float fDepth = g_tex2DShadowMap.Load( int4( int(VSOut.f4PixelPos.x) + i, int(VSOut.f4PixelPos.y), g_Attribs.iCascade, 0) ).r;
        float2 f2EVSMDepth = WarpDepthEVSM(fDepth, f2Exponents);
        f4Moments += float4(f2EVSMDepth.x, f2EVSMDepth.x*f2EVSMDepth.x, f2EVSMDepth.y, f2EVSMDepth.y*f2EVSMDepth.y);
    }
    return f4Moments / float(g_Attribs.iFilterRadius*2 + 1);
}

float4 VertBlurPS(FullScreenTriangleVSOutput VSOut) : SV_Target
{
    float4 f4Moments = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = -g_Attribs.iFilterRadius; i <= +g_Attribs.iFilterRadius; ++i)
    {
        f4Moments += g_tex2DShadowMap.Load( int4( int(VSOut.f4PixelPos.x), int(VSOut.f4PixelPos.y) + i, 0, 0) );
    }
    return f4Moments / float(g_Attribs.iFilterRadius*2 + 1);
}
