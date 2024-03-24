#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

#define HILBERT_LEVEL 7u
#define HILBERT_WIDTH (1u << HILBERT_LEVEL)

struct PSOutput
{
    float2 BlueNoiseXY : SV_Target0;
    float2 BlueNoiseZW : SV_Target1;
};

Texture2D<uint> g_SobolBuffer;
Texture2D<uint> g_ScramblingTileBuffer;

// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
float SampleRandomNumber(uint2 PixelCoord, uint SampleDimension)
{
    // Wrap arguments
    PixelCoord = PixelCoord & 127u;
    SampleDimension = SampleDimension & 255u;

    // Fetch value in sequence
    uint Value = g_SobolBuffer.Load(uint3(SampleDimension, 0, 0));

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    uint OriginalIndex = (SampleDimension % 8u) + (PixelCoord.x + PixelCoord.y * 128u) * 8u;
    Value = Value ^ g_ScramblingTileBuffer.Load(uint3(OriginalIndex % 512u, OriginalIndex / 512u, 0)); // TODO: AMD doesn't support integer division

    return (float(Value) + 0.5f) / 256.0f;
}

uint HilbertIndex(uint2 PixelCoord)
{
    PixelCoord = PixelCoord & (HILBERT_WIDTH - 1u);
    uint Index = 0u;
    for (uint CurLevel = HILBERT_WIDTH / 2u; CurLevel > 0u; CurLevel /= 2u)
    {
        uint RegionX = uint((PixelCoord.x & CurLevel) > 0u);
        uint RegionY = uint((PixelCoord.y & CurLevel) > 0u);
        Index += CurLevel * CurLevel * ((3u * RegionX) ^ RegionY);
        if (RegionY == 0u)
        {
            if (RegionX == 1u)
            {
                PixelCoord.x = uint((HILBERT_WIDTH - 1u)) - PixelCoord.x;
                PixelCoord.y = uint((HILBERT_WIDTH - 1u)) - PixelCoord.y;
            }

            uint Temp = PixelCoord.x;
            PixelCoord.x = PixelCoord.y;
            PixelCoord.y = Temp;
        }
    }
    return Index;
}

// Roberts R1 sequence see - https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
float2 SampleRandomVector2D(uint2 PixelCoord, uint FrameIndex)
{
    float G = 1.61803398875f; 
    float Alpha = 0.5 + rcp(G) * float(FrameIndex & 0xFFu);
    return float2(
        frac(SampleRandomNumber(PixelCoord, 0u) + Alpha),
        frac(SampleRandomNumber(PixelCoord, 1u) + Alpha)
    );
}

 // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
float2 SampleRandomVector1D1D(uint2 PixelCoord, uint FrameIndex)
{
    uint Index = (HilbertIndex(PixelCoord) + FrameIndex);
    Index += 288u * (FrameIndex & (HILBERT_WIDTH - 1u));

    float G = 1.32471795724474602596;
    float2 Alpha = float2(rcp(G), rcp(G * G));
    return frac(float2(0.5, 0.5) + float(Index) * Alpha);
}

PSOutput ComputeBlueNoiseTexturePS(in FullScreenTriangleVSOutput VSOut)
{
    uint FrameIndex = VSOut.uInstID;

    PSOutput Output;
    Output.BlueNoiseXY = SampleRandomVector2D(uint2(VSOut.f4PixelPos.xy), FrameIndex);
    Output.BlueNoiseZW = SampleRandomVector1D1D(uint2(VSOut.f4PixelPos.xy), FrameIndex);
    return Output;
}
