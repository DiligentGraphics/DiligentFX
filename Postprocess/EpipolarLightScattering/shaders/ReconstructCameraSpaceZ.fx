// ReconstructCameraSpaceZ.fx
// Reconstructs camera space z from depth

#include "Structures.fxh"
#include "AtmosphereShadersCommon.fxh"

Texture2D<float>  g_tex2DDepthBuffer;

cbuffer cbCameraAttribs
{
    CameraAttribs g_CameraAttribs;
}

float DepthToCameraZ(in float fDepth, in matrix mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first

    float z = DepthToNormalizedDeviceZ(fDepth);
    return MATRIX_ELEMENT(mProj,3,2) / (z - MATRIX_ELEMENT(mProj,2,2));
}

void ReconstructCameraSpaceZPS(ScreenSizeQuadVSOutput VSOut,
                               // IMPORTANT: non-system generated pixel shader input
                               // arguments must have the exact same name as vertex shader 
                               // outputs and must go in the same order.
                               // Moreover, even if the shader is not using the argument,
                               // it still must be declared.

                               in float4 f4Pos : SV_Position,
                               out float fCamSpaceZ : SV_Target)
{
    float fDepth = g_tex2DDepthBuffer.Load( int3(f4Pos.xy,0) );
    fCamSpaceZ = DepthToCameraZ(fDepth, g_CameraAttribs.mProj);
}
