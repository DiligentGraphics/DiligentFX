#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#if SUPPORTED_SHADER_SRV
Texture2D<float> g_TextureLastMip;
#else
Texture2D<float> g_TextureMips;
SamplerState     g_TextureMipsSampler;
#endif

#if SUPPORTED_SHADER_SRV
float SampleDepth(uint2 Location, uint2 Offset, uint3 Dimension)
{
    uint2 Position = Location + Offset;
    if (Position.x >= Dimension.x || Position.y >= Dimension.y)
        return g_TextureLastMip.Load(int3(Location, 0));
    return g_TextureLastMip.Load(int3(Position, 0));
}
#else
float SampleDepth(uint2 Location, uint2 Offset, uint3 Dimension)
{
    uint2 Position = Location + Offset;
    if (Position.x >= Dimension.x || Position.y >= Dimension.y)
        return g_TextureMips.Load(int3(Location, Dimension.z));
    return g_TextureMips.Load(int3(Position, Dimension.z));
}
#endif

float ComputeHierarchicalDepthBufferPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    uint3 LastMipDimension;
#if SUPPORTED_SHADER_SRV
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);
#else
    uint Dummy;
    g_TextureMips.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);
    LastMipDimension.x = uint(floor(float(LastMipDimension.x) / exp2(float(VSOut.uInstID))));
    LastMipDimension.y = uint(floor(float(LastMipDimension.y) / exp2(float(VSOut.uInstID))));
    LastMipDimension.z = VSOut.uInstID;
#endif

    uint2 RemappedPosition = uint2(2.0 * floor(VSOut.f4PixelPos.xy)); 

    float4 SampledPixels;
    SampledPixels.x = SampleDepth(RemappedPosition, uint2(0, 0), LastMipDimension);
    SampledPixels.y = SampleDepth(RemappedPosition, uint2(0, 1), LastMipDimension);
    SampledPixels.z = SampleDepth(RemappedPosition, uint2(1, 0), LastMipDimension);
    SampledPixels.w = SampleDepth(RemappedPosition, uint2(1, 1), LastMipDimension);

    float MinDepth = MipConvFunc(MipConvFunc(SampledPixels.x, SampledPixels.y), MipConvFunc(SampledPixels.z, SampledPixels.w));

    bool IsWidthOdd  = (LastMipDimension.x & 1u) != 0u;
    bool IsHeightOdd = (LastMipDimension.y & 1u) != 0u;
    
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
