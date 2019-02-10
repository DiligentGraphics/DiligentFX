#include "AtmosphereShadersCommon.fxh"

cbuffer cbMiscDynamicParams
{
    MiscDynamicParams g_MiscParams;
}

Texture2D<float>  g_tex2DLowResLuminance;

void UpdateAverageLuminancePS( ScreenSizeQuadVSOutput VSOut, 
                               // We must declare vertex shader output even though we 
                               // do not use it, because otherwise the shader will not
                               // run on NVidia GLES
                               out float4 f4Luminance : SV_Target)
{
#if LIGHT_ADAPTATION
    const float fAdaptationRate = 1.0;
    float fNewLuminanceWeight = 1.0 - exp( - fAdaptationRate * g_MiscParams.fElapsedTime );
#else
    float fNewLuminanceWeight = 1.0;
#endif
    f4Luminance = float4( exp( g_tex2DLowResLuminance.Load( int3(0,0,LOW_RES_LUMINANCE_MIPS-1) ) ), 0, 0, fNewLuminanceWeight );
}
