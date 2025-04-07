#include "BasicStructures.fxh"
#include "ComputeDepthRangeStructs.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

RWStructuredBuffer<DepthRangeI> g_DepthRange;

[numthreads(1, 1, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
    DepthRangeI Range;
    // fFarPlaneZ is always greater than fNearPlaneZ.
    Range.iSceneNearZ = asint(g_Camera.fFarPlaneZ);
    Range.iSceneFarZ  = asint(g_Camera.fNearPlaneZ);

    // Note: when reverse depth is used, fFarPlaneDepth is 0.0 and fNearPlaneDepth is 1.0.
    Range.iSceneNearDepth = asint(g_Camera.fFarPlaneDepth);
    Range.iSceneFarDepth  = asint(g_Camera.fNearPlaneDepth);

    g_DepthRange[0] = Range;
}
