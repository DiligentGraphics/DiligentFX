// ReconstructCameraSpaceZ.fx
// Reconstructs camera space z from depth

#include "BasicStructures.fxh"
#include "AtmosphereShadersCommon.fxh"
#include "ShaderUtilities.fxh"

Texture2D<float>  g_tex2DDepthBuffer;

cbuffer cbCameraAttribs
{
    CameraAttribs g_CameraAttribs;
}

void ReconstructCameraSpaceZPS(FullScreenTriangleVSOutput VSOut,
                               // IMPORTANT: non-system generated pixel shader input
                               // arguments must have the exact same name as vertex shader 
                               // outputs and must go in the same order.
                               // Moreover, even if the shader is not using the argument,
                               // it still must be declared.

                               out float fCamSpaceZ : SV_Target)
{
    float fDepth = g_tex2DDepthBuffer.Load( int3(VSOut.f4PixelPos.xy,0) );
    fCamSpaceZ = DepthToCameraZ(fDepth, g_CameraAttribs.mProj);
}
