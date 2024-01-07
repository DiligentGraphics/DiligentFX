#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<uint> g_SobolBuffer;
Texture2D<uint> g_ScramblingTileBuffer;

// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
float SampleRandomNumber(uint2 PixelCoord, uint SampleIndex, uint SampleDimension)
{
    // Wrap arguments
    PixelCoord = PixelCoord & 127u;
    SampleIndex = SampleIndex & 255u;
    SampleDimension = SampleDimension & 255u;

    // xor index based on optimized ranking
    uint RankedSampleIndex = SampleIndex;

    // Fetch value in sequence
    uint Value = g_SobolBuffer.Load(uint3(SampleDimension, RankedSampleIndex * 256u, 0));

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    uint OriginalIndex = (SampleDimension % 8u) + (PixelCoord.x + PixelCoord.y * 128u) * 8u;
    Value = Value ^ g_ScramblingTileBuffer.Load(uint3(OriginalIndex % 512u, OriginalIndex / 512u, 0)); // TODO: AMD doesn't support integer division

    return (float(Value) + 0.5f) / 256.0f;
}

float2 SampleRandomVector2D(uint2 PixelCoord)
{
    return float2(
        fmod(SampleRandomNumber(PixelCoord, 0u, 0u) + float(g_SSRAttribs.FrameIndex & 0xFFu) * M_GOLDEN_RATIO, 1.0),
        fmod(SampleRandomNumber(PixelCoord, 0u, 1u) + float(g_SSRAttribs.FrameIndex & 0xFFu) * M_GOLDEN_RATIO, 1.0));
}


float2 ComputeBlueNoiseTexturePS(in FullScreenTriangleVSOutput VSOut) : SV_Target
{
    return SampleRandomVector2D(uint2(VSOut.f4PixelPos.xy));
}
