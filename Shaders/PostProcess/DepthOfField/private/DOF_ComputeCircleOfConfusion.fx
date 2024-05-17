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
    // Linear Depth in camera space in meters
    float LinearDepth = DepthToCameraZ(SampleDepth(int2(VSOut.f4PixelPos.xy)), g_Camera.mProj);

    // Focal Length converts from millimeters to meters
    float f = g_Camera.fFocalLength / 1000.0f;

    // Lens Coefficient f * f / (N * (F - f))
    float K = f * f / (g_Camera.fFStop * (g_Camera.fFocusDistance - f));

    // Circle of Confusion K * (x - F) / x in millimeters
    float CoC = K * (LinearDepth - g_Camera.fFocusDistance) / max(LinearDepth, 1e-4);

    // The blur disc size for the pixel at relative texture coordinate. Near Plane: < 0.0; Focus Plane: 0; Far Plane: > 0.0; Range: [-1.0, 1,0]
    return clamp(1000.0 * CoC / (g_Camera.fSensorWidth * g_DOFAttribs.MaxCircleOfConfusion), -1.0, 1.0);
}
