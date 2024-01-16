#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

struct PSOutput
{
    float2 BlueNoiseXY : SV_Target0;
    float2 BlueNoiseZW : SV_Target1;
};

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

// Roberts R1 sequence see - https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
float4 SampleRandomVector2D2D(uint2 PixelCoord, uint FrameIndex)
{
    float G = 1.61803398875f; 
    float Alpha = 0.5 + rcp(G) * float(FrameIndex & 0xFFu);
    return float4(
        frac(SampleRandomNumber(PixelCoord, 0u, 0u) + Alpha),
        frac(SampleRandomNumber(PixelCoord, 0u, 1u) + Alpha),
        frac(SampleRandomNumber(PixelCoord, 0u, 2u) + Alpha),
        frac(SampleRandomNumber(PixelCoord, 0u, 3u) + Alpha)
    );
}

PSOutput ComputeBlueNoiseTexturePS(in FullScreenTriangleVSOutput VSOut)
{
    uint FrameIndex = VSOut.uInstID;

    PSOutput Output;
    float4 BlueNoise = SampleRandomVector2D2D(uint2(VSOut.f4PixelPos.xy), FrameIndex);
    Output.BlueNoiseXY = BlueNoise.xy;
    Output.BlueNoiseZW = BlueNoise.zw;
    return Output;
}
