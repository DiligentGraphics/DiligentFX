// MarkRayMarchingSamples.fx
// Marks ray marching samples in the stencil by increasing stencil value and discarding all other samples

#include "AtmosphereShadersCommon.fxh"

Texture2D<uint2>  g_tex2DInterpolationSource;

void MarkRayMarchingSamplesInStencilPS(ScreenSizeQuadVSOutput VSOut,
                                       // IMPORTANT: non-system generated pixel shader input
                                       // arguments must have the exact same name as vertex shader 
                                       // outputs and must go in the same order.
                                       // Moreover, even if the shader is not using the argument,
                                       // it still must be declared.

                                       in float4 f4PosPS : SV_Position)
{
    uint2 ui2InterpolationSources = g_tex2DInterpolationSource.Load( int3(f4PosPS.xy,0) );
    // Ray marching samples are interpolated from themselves, so it is easy to detect them:
    if( ui2InterpolationSources.x != ui2InterpolationSources.y )
          discard;
}
