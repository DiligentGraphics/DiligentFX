#include "SSAO_Common.fxh"
#include "PostFX_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#if SUPPORTED_SHADER_SRV
Texture2D g_TextureHistoryLastMip;
Texture2D g_TextureDepthLastMip;
#else
Texture2D g_TextureHistoryMips;
Texture2D g_TextureDepthMips;
SamplerState g_TextureHistoryMips_sampler;
SamplerState g_TextureDepthMips_sampler;
#endif // SUPPORTED_SHADER_SRV

struct PSOutput 
{
    float History : SV_Target0;
    float Depth   : SV_Target1;
};

#if SUPPORTED_SHADER_SRV
float SampleFromTexture(Texture2D SampledTexture, int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return SampledTexture.Load(int3(Position, 0)).x;
}
#else
float SampleFromTexture(Texture2D SampledTexture, int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return SampledTexture.Load(int3(Position, Dimension.z)).x;
}
#endif // SUPPORTED_SHADER_SRV

float ComputeAverage(in float SampledValues[9], uint Count)
{
    float Result = 0.0f;
    for (uint Idx = 0u; Idx < Count; Idx++)
        Result += SampledValues[Idx];
    
    return Result / float(Count);
}

void ArrayAppend(float Element, inout float Array[9], inout uint Index)
{
    Array[Index] = Element;
    Index++;
}

float ComputeAverageForTexture(Texture2D SampledTexture, int2 RemappedPosition, int MipLevel) {
    
    int3 LastMipDimension;
#if SUPPORTED_SHADER_SRV
    SampledTexture.GetDimensions(LastMipDimension.x, LastMipDimension.y);
#else
    int Dummy;
    SampledTexture.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);
    LastMipDimension.x = int(floor(float(LastMipDimension.x) / exp2(float(MipLevel))));
    LastMipDimension.y = int(floor(float(LastMipDimension.y) / exp2(float(MipLevel))));
    LastMipDimension.z = MipLevel;
#endif // SUPPORTED_SHADER_SRV

    uint SampleCount = 0u;
    float SampledPixels[9];
    ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(0, 0), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(0, 1), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(1, 0), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(1, 1), LastMipDimension), SampledPixels, SampleCount);

    bool IsWidthOdd  = (LastMipDimension.x & 1) != 0;
    bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(2, 0), LastMipDimension), SampledPixels, SampleCount);
        ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(2, 1), LastMipDimension), SampledPixels, SampleCount);
    }
    
    if (IsHeightOdd)
    {
        ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(0, 2), LastMipDimension), SampledPixels, SampleCount);
        ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(1, 2), LastMipDimension), SampledPixels, SampleCount);
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        ArrayAppend(SampleFromTexture(SampledTexture, RemappedPosition, int2(2, 2), LastMipDimension), SampledPixels, SampleCount);
    }

    return ComputeAverage(SampledPixels, SampleCount);
}

PSOutput ComputeConvolutedDepthHistoryPS(in FullScreenTriangleVSOutput VSOut)
{
    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy));
    int MipLevel = int(VSOut.uInstID);
    
    PSOutput Output;
    
#if SUPPORTED_SHADER_SRV
    Output.History = ComputeAverageForTexture(g_TextureHistoryLastMip, RemappedPosition, MipLevel);
    Output.Depth   = ComputeAverageForTexture(g_TextureDepthLastMip, RemappedPosition, MipLevel);
#else
    Output.History = ComputeAverageForTexture(g_TextureHistoryMips, RemappedPosition, MipLevel);
    Output.Depth   = ComputeAverageForTexture(g_TextureDepthMips, RemappedPosition, MipLevel);
#endif // SUPPORTED_SHADER_SRV

    return Output;
}
