#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#pragma warning(disable : 3078)

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float> g_TextureRoughness;
Texture2D<float> g_TextureDepth;

float SampleRoughness(uint2 Location, uint2 Offset, uint2 Dimension)
{
    uint2 Position = Location + Offset;
    if (Position.x >= Dimension.x || Position.y >= Dimension.y)
        return g_TextureRoughness.Load(int3(Location, 0));
    return g_TextureRoughness.Load(int3(Position, 0));
}

float SampleDepth(uint2 Location, uint2 Offset, uint2 Dimension)
{
    uint2 Position = Location + Offset;
    if (Position.x >= Dimension.x || Position.y >= Dimension.y)
        return g_TextureDepth.Load(int3(Location, 0));
    return g_TextureDepth.Load(int3(Position, 0));
}

void ComputeDownsampledStencilMaskPS(in FullScreenTriangleVSOutput VSOut)
{
    uint2 RemappedPosition = uint2(2.0 * floor(VSOut.f4PixelPos.xy));

    uint2 TextureDimension;
    g_TextureDepth.GetDimensions(TextureDimension.x, TextureDimension.y);

    float MinDepth = DepthFarPlane;
    float MaxRoughness = 0.0f;

    for (uint SampleIdx = 0u; SampleIdx < 4u; ++SampleIdx)
    {
        uint2 Offset = uint2(SampleIdx & 0x01u, SampleIdx >> 1u);
        MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, Offset, TextureDimension));
        MaxRoughness = max(MaxRoughness, SampleRoughness(RemappedPosition, Offset, TextureDimension));
    }

    bool IsWidthOdd  = (TextureDimension.x & 1u) != 0u;
    bool IsHeightOdd = (TextureDimension.y & 1u) != 0u;
    
    if (IsWidthOdd)
    {
        for (uint SampleIdx = 0u; SampleIdx < 2u; ++SampleIdx)
        {
            uint2 Offset = uint2(2u, SampleIdx);
            MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, Offset, TextureDimension));
            MaxRoughness = max(MaxRoughness, SampleRoughness(RemappedPosition, Offset, TextureDimension));
        }
    }
    
    if (IsHeightOdd)
    {
        for (uint SampleIdx = 0u; SampleIdx < 2u; ++SampleIdx)
        {
            uint2 Offset = uint2(SampleIdx, 2);
            MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, Offset, TextureDimension));
            MaxRoughness = max(MaxRoughness, SampleRoughness(RemappedPosition, Offset, TextureDimension));
        }
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, uint2(2, 2), TextureDimension));
        MaxRoughness = max(MaxRoughness, SampleRoughness(RemappedPosition, uint2(2, 2), TextureDimension));
    }

    if (!IsReflectionSample(MaxRoughness, MinDepth, g_SSRAttribs.RoughnessThreshold))
        discard;
}
