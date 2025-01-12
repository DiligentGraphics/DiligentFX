#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float> g_TextureRoughness;
Texture2D<float> g_TextureDepth;

void UpdateClosestDepthAndMaxRoughness(int2 Location, int2 Dimension, inout float MinDepth, inout float MaxRoughness)
{
    Location = ClampScreenCoord(Location, Dimension);

    float Depth     = g_TextureDepth.Load(int3(Location, 0));
    float Roughness = g_TextureRoughness.Load(int3(Location, 0));

    MinDepth     = ClosestDepth(MinDepth, Depth);
    MaxRoughness = max(MaxRoughness, Roughness);
}

void ComputeDownsampledStencilMaskPS(in FullScreenTriangleVSOutput VSOut)
{
    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy));

    int2 TextureDimension;
    g_TextureDepth.GetDimensions(TextureDimension.x, TextureDimension.y);

    float MinDepth = DepthFarPlane;
    float MaxRoughness = 0.0f;
    
    UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(0, 0), TextureDimension, MinDepth, MaxRoughness);
    UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(1, 0), TextureDimension, MinDepth, MaxRoughness);
    UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(0, 1), TextureDimension, MinDepth, MaxRoughness);
    UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(1, 1), TextureDimension, MinDepth, MaxRoughness);

    bool IsWidthOdd  = (TextureDimension.x & 1) != 0;
    bool IsHeightOdd = (TextureDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(2, 0), TextureDimension, MinDepth, MaxRoughness);
        UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(2, 1), TextureDimension, MinDepth, MaxRoughness);
    }
    
    if (IsHeightOdd)
    {
        UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(0, 2), TextureDimension, MinDepth, MaxRoughness);
        UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(1, 2), TextureDimension, MinDepth, MaxRoughness);
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        UpdateClosestDepthAndMaxRoughness(RemappedPosition + int2(2, 2), TextureDimension, MinDepth, MaxRoughness);
    }

    if (!IsReflectionSample(MaxRoughness, MinDepth, g_SSRAttribs.RoughnessThreshold))
        discard;
}
