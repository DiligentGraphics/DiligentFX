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

Texture2D<float> g_TextureDepth;

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeCircleOfConfusionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float LinearDepth = DepthToCameraZ(SampleDepth(int2(VSOut.f4PixelPos.xy)), g_Camera.mProj);
	float CoC = (LinearDepth - g_DOFAttribs.FocusDistance) / g_DOFAttribs.FocusRange;
	return clamp(CoC, -1, 1) * g_DOFAttribs.BokehRadius;
}
