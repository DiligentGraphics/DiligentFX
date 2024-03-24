#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

#if POSTFX_OPTION_INVERTED_DEPTH
    #define DepthFarPlane  0.0
#else
    #define DepthFarPlane  1.0
#endif // POSTFX_OPTION_INVERTED_DEPTH

Texture2D<float>  g_TextureDepth;
Texture2D<float2> g_TextureMotion;

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0));
}

float2 SampleClosestMotion(int2 PixelCoord)
{
    float ClosestDepth = DepthFarPlane;
    int2 ClosestOffset = int2(0, 0);

    const int SearchRadius = 1;
    for (int x = -SearchRadius; x <= SearchRadius; x++)
    {
        for (int y = -SearchRadius; y <= SearchRadius; y++)
        {
            int2 Coord = int2(PixelCoord) + int2(x, y);
            float NeighborDepth = SampleDepth(Coord);
#if POSTFX_OPTION_INVERTED_DEPTH
            if (NeighborDepth > ClosestDepth)
#else
            if (NeighborDepth < ClosestDepth)
#endif
            {
                ClosestOffset = int2(x, y);
                ClosestDepth = NeighborDepth;
            }
        }
    }

    return SampleMotion(PixelCoord + ClosestOffset);
}

float2 ComputeClosestMotionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    return SampleClosestMotion(int2(Position.xy));
}
