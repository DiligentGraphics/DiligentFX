#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#if SUPPORTED_SHADER_SRV
Texture2D<float> g_TextureLastMip;
#else
Texture2D<float> g_TextureMips;
SamplerState     g_TextureMips_sampler;
#endif

#if SUPPORTED_SHADER_SRV
float SampleDepth(int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return g_TextureLastMip.Load(int3(Position, 0));
}
#else
float SampleDepth(int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return g_TextureMips.Load(int3(Position, Dimension.z));
}
#endif

float ComputeHierarchicalDepthBufferPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int3 LastMipDimension;
#if SUPPORTED_SHADER_SRV
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);
#else
    int Dummy;
    g_TextureMips.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);
    LastMipDimension.x = int(floor(float(LastMipDimension.x) / exp2(float(VSOut.uInstID))));
    LastMipDimension.y = int(floor(float(LastMipDimension.y) / exp2(float(VSOut.uInstID))));
    LastMipDimension.z = int(VSOut.uInstID);
#endif

    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy)); 

    float4 SampledPixels;
    SampledPixels.x = SampleDepth(RemappedPosition, int2(0, 0), LastMipDimension);
    SampledPixels.y = SampleDepth(RemappedPosition, int2(0, 1), LastMipDimension);
    SampledPixels.z = SampleDepth(RemappedPosition, int2(1, 0), LastMipDimension);
    SampledPixels.w = SampleDepth(RemappedPosition, int2(1, 1), LastMipDimension);

    float MinDepth = MipConvFunc(MipConvFunc(SampledPixels.x, SampledPixels.y), MipConvFunc(SampledPixels.z, SampledPixels.w));

    bool IsWidthOdd  = (LastMipDimension.x & 1) != 0;
    bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        SampledPixels.x = SampleDepth(RemappedPosition, int2(2, 0), LastMipDimension);
        SampledPixels.y = SampleDepth(RemappedPosition, int2(2, 1), LastMipDimension);
        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsHeightOdd)
    {
        SampledPixels.x = SampleDepth(RemappedPosition, int2(0, 2), LastMipDimension);
        SampledPixels.y = SampleDepth(RemappedPosition, int2(1, 2), LastMipDimension);
        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, int2(2, 2), LastMipDimension));
    }

    return MinDepth;
}
