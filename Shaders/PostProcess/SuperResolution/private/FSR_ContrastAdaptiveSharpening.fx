#include "FullScreenTriangleVSOutput.fxh"
#include "SuperResolutionStructures.fxh"
#include "SRGBUtilities.fxh"

cbuffer cbFSRAttribs
{
    SuperResolutionAttribs g_FSRAttribs;
}

Texture2D<float4> g_TextureSource;

#define FFX_GPU
#define FFX_HLSL
#define FFX_HALF    0
#define FFX_HLSL_SM 50
#include "ffx_core.h"

#define FSR_RCAS_F       1
#define FSR_RCAS_DENOISE 1

FfxFloat32x4 FsrRcasLoadF(FfxInt32x2 Position)
{
    return g_TextureSource.Load(FfxInt32x3(Position, 0));
}

void FsrRcasInputF(FFX_PARAMETER_INOUT FfxFloat32 R, FFX_PARAMETER_INOUT FfxFloat32 G, FFX_PARAMETER_INOUT FfxFloat32 B)
{

}

#include "ffx_fsr1.h"

FfxFloat32x4 ComputeContrastAdaptiveSharpeningPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    FfxUInt32x2 Location = FfxUInt32x2(VSOut.f4PixelPos.xy);

    FfxUInt32x4 Constants = FfxUInt32x4(0, 0, 0, 0);
    FsrRcasCon(Constants, g_FSRAttribs.Sharpening);

    FfxFloat32x3 ResultColor = FfxFloat32x3(0.0, 0.0, 0.0);
    FsrRcasF(ResultColor.r, ResultColor.g, ResultColor.b, Location, Constants);
    return FfxFloat32x4(ResultColor, 1.0);
}
