#include "SSR_Common.fxh"

Texture2D<float> g_TextureLastMip;

float SampleDepth(uint2 Location, uint2 Offset, uint2 Dimension)
{
    uint2 Position = Location + Offset;
    if (Position.x >= Dimension.x || Position.y >= Dimension.y)
        return g_TextureLastMip.Load(int3(Location, 0));
    return g_TextureLastMip.Load(int3(Position, 0));
}

float ComputeHierarchicalDepthBufferPS(in float4 Position : SV_Position) : SV_Depth
{
    uint2 LastMipDimension;
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);

    const uint2 RemappedPosition = uint2(2 * uint2(Position.xy));

    float4 SampledPixels;
    SampledPixels.x = SampleDepth(RemappedPosition, uint2(0, 0), LastMipDimension);
    SampledPixels.y = SampleDepth(RemappedPosition, uint2(0, 1), LastMipDimension);
    SampledPixels.z = SampleDepth(RemappedPosition, uint2(1, 0), LastMipDimension);
    SampledPixels.w = SampleDepth(RemappedPosition, uint2(1, 1), LastMipDimension);

    float MinDepth = MipConvFunc(MipConvFunc(SampledPixels.x, SampledPixels.y), MipConvFunc(SampledPixels.z, SampledPixels.w));

    const bool IsWidthOdd = (LastMipDimension.x & 1) != 0;
    const bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        SampledPixels.x = SampleDepth(RemappedPosition, uint2(2, 0), LastMipDimension);
        SampledPixels.y = SampleDepth(RemappedPosition, uint2(2, 1), LastMipDimension);
        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsHeightOdd)
    {
        SampledPixels.x = SampleDepth(RemappedPosition, uint2(0, 2), LastMipDimension);
        SampledPixels.y = SampleDepth(RemappedPosition, uint2(1, 2), LastMipDimension);
        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, uint2(2, 2), LastMipDimension));
    }

    return MinDepth;
}
