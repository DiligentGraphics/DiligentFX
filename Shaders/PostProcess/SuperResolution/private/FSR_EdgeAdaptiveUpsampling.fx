#include "FullScreenTriangleVSOutput.fxh"
#include "SuperResolutionStructures.fxh"
#include "SRGBUtilities.fxh"

cbuffer cbFSRAttribs
{
    SuperResolutionAttribs g_FSRAttribs;
}

Texture2D<float4> g_TextureSource;
SamplerState      g_TextureSource_sampler;

#define FFX_GPU
#define FFX_HLSL
#define FFX_HALF    0
#define FFX_HLSL_SM 50
#include "ffx_core.h"

#define FFX_FSR_EASU_FLOAT 1
#define FSR_RCAS_DENOISE   1

FfxFloat32x4 FsrEasuRF(FfxFloat32x2 Texcoord)
{
    // Need to replace g_TextureSource.GatherRed(g_TextureSource_sampler, Position));

    float2 Position = g_FSRAttribs.SourceSize.xy * Texcoord - float2(0.5, 0.5);

    FfxFloat32x4 Gather;
    Gather.x = g_TextureSource.Load(int3(int2(Position) + int2(0, 1), 0)).r;
    Gather.y = g_TextureSource.Load(int3(int2(Position) + int2(1, 1), 0)).r;
    Gather.z = g_TextureSource.Load(int3(int2(Position) + int2(1, 0), 0)).r;
    Gather.w = g_TextureSource.Load(int3(int2(Position) + int2(0, 0), 0)).r;
    return Gather;
}

FfxFloat32x4 FsrEasuGF(FfxFloat32x2 Texcoord)
{
    // Need to replace g_TextureSource.GatherGreen(g_TextureSource_sampler, Position);

    float2 Position = g_FSRAttribs.SourceSize.xy * Texcoord - float2(0.5, 0.5);

    FfxFloat32x4 Gather;
    Gather.x = g_TextureSource.Load(int3(int2(Position) + int2(0, 1), 0)).g;
    Gather.y = g_TextureSource.Load(int3(int2(Position) + int2(1, 1), 0)).g;
    Gather.z = g_TextureSource.Load(int3(int2(Position) + int2(1, 0), 0)).g;
    Gather.w = g_TextureSource.Load(int3(int2(Position) + int2(0, 0), 0)).g;
    return Gather;
}

FfxFloat32x4 FsrEasuBF(FfxFloat32x2 Texcoord)
{
    // Need to replace g_TextureSource.GatherBlue(g_TextureSource_sampler, Position);

    float2 Position = g_FSRAttribs.SourceSize.xy * Texcoord - float2(0.5, 0.5);

    FfxFloat32x4 Gather;
    Gather.x = g_TextureSource.Load(int3(int2(Position) + int2(0, 1), 0)).b;
    Gather.y = g_TextureSource.Load(int3(int2(Position) + int2(1, 1), 0)).b;
    Gather.z = g_TextureSource.Load(int3(int2(Position) + int2(1, 0), 0)).b;
    Gather.w = g_TextureSource.Load(int3(int2(Position) + int2(0, 0), 0)).b;
    return Gather;
}

#include "ffx_fsr1.h"

FfxFloat32x4 ComputeEdgeAdaptiveUpsamplingPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    FfxUInt32x2 Location = FfxUInt32x2(VSOut.f4PixelPos.xy);
    FfxUInt32x4 Constants0 = FfxUInt32x4(0u, 0u, 0u, 0u);
    FfxUInt32x4 Constants1 = FfxUInt32x4(0u, 0u, 0u, 0u);
    FfxUInt32x4 Constants2 = FfxUInt32x4(0u, 0u, 0u, 0u);
    FfxUInt32x4 Constants3 = FfxUInt32x4(0u, 0u, 0u, 0u);

    ffxFsrPopulateEasuConstants(Constants0, Constants1, Constants2, Constants3,
                                g_FSRAttribs.SourceSize.x, g_FSRAttribs.SourceSize.y,
                                g_FSRAttribs.SourceSize.x, g_FSRAttribs.SourceSize.y,
                                g_FSRAttribs.OutputSize.x, g_FSRAttribs.OutputSize.y);

    FfxFloat32x3 ResultColor = FfxFloat32x3(0.0, 0.0, 0.0);
    ffxFsrEasuFloat(ResultColor, Location, Constants0, Constants1, Constants2, Constants3);
    return FfxFloat32x4(ResultColor, 1.0);
}
