#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#if SUPPORTED_SHADER_SRV
Texture2D<float> g_TextureLastMip;
#else
Texture2D<float> g_TextureMips;
#endif

#if SUPPORTED_SHADER_SRV
float LoadDepth(int2 Location, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location, Dimension.xy);
    return g_TextureLastMip.Load(int3(Position, 0));
}
#else
float LoadDepth(int2 Location, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location, Dimension.xy);
    return g_TextureMips.Load(int3(Position, Dimension.z));
}
#endif

void UpdateClosestDepth(int2 Location, int3 LastMipDimension, inout float MinDepth)
{
    float Depth = LoadDepth(Location, LastMipDimension);
    MinDepth = ClosestDepth(MinDepth, Depth);
}

float ComputeHierarchicalDepthBufferPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int3 LastMipDimension;
#if SUPPORTED_SHADER_SRV
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);
    LastMipDimension.z = 0; // Unused
#else
    int Dummy;
    g_TextureMips.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);
    LastMipDimension.x = int(floor(float(LastMipDimension.x) / exp2(float(VSOut.uInstID))));
    LastMipDimension.y = int(floor(float(LastMipDimension.y) / exp2(float(VSOut.uInstID))));
    LastMipDimension.z = int(VSOut.uInstID);
#endif

    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy)); 

    float MinDepth = DepthFarPlane;
    UpdateClosestDepth(RemappedPosition + int2(0, 0), LastMipDimension, MinDepth);
    UpdateClosestDepth(RemappedPosition + int2(0, 1), LastMipDimension, MinDepth);
    UpdateClosestDepth(RemappedPosition + int2(1, 0), LastMipDimension, MinDepth);
    UpdateClosestDepth(RemappedPosition + int2(1, 1), LastMipDimension, MinDepth);

    bool IsWidthOdd  = (LastMipDimension.x & 1) != 0;
    bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        UpdateClosestDepth(RemappedPosition + int2(2, 0), LastMipDimension, MinDepth);
        UpdateClosestDepth(RemappedPosition + int2(2, 1), LastMipDimension, MinDepth);
    }
    
    if (IsHeightOdd)
    {
        UpdateClosestDepth(RemappedPosition + int2(0, 2), LastMipDimension, MinDepth);
        UpdateClosestDepth(RemappedPosition + int2(1, 2), LastMipDimension, MinDepth);
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        UpdateClosestDepth(RemappedPosition + int2(2, 2), LastMipDimension, MinDepth);
    }

    return MinDepth;
}
