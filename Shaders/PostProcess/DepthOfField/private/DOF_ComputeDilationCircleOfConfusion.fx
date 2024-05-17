#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

#define MipConvFunc max

Texture2D<float> g_TextureLastMip;

float SampleCoC(int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return g_TextureLastMip.Load(int3(Position, 0));
}

float ComputeDilationCoCPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int3 LastMipDimension;
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);

    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy)); 

    float4 SampledPixels;
    SampledPixels.x = SampleCoC(RemappedPosition, int2(0, 0), LastMipDimension);
    SampledPixels.y = SampleCoC(RemappedPosition, int2(0, 1), LastMipDimension);
    SampledPixels.z = SampleCoC(RemappedPosition, int2(1, 0), LastMipDimension);
    SampledPixels.w = SampleCoC(RemappedPosition, int2(1, 1), LastMipDimension);

    float MaxCoC = MipConvFunc(MipConvFunc(SampledPixels.x, SampledPixels.y), MipConvFunc(SampledPixels.z, SampledPixels.w));

    bool IsWidthOdd  = (LastMipDimension.x & 1) != 0;
    bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        SampledPixels.x = SampleCoC(RemappedPosition, int2(2, 0), LastMipDimension);
        SampledPixels.y = SampleCoC(RemappedPosition, int2(2, 1), LastMipDimension);
        MaxCoC = MipConvFunc(MaxCoC, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsHeightOdd)
    {
        SampledPixels.x = SampleCoC(RemappedPosition, int2(0, 2), LastMipDimension);
        SampledPixels.y = SampleCoC(RemappedPosition, int2(1, 2), LastMipDimension);
        MaxCoC = MipConvFunc(MaxCoC, MipConvFunc(SampledPixels.x, SampledPixels.y));
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        MaxCoC = MipConvFunc(MaxCoC, SampleCoC(RemappedPosition, int2(2, 2), LastMipDimension));
    }

    return MaxCoC;
}
