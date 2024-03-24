#include "SSAO_Common.fxh"
#include "PostFX_Common.fxh"
#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

#pragma warning(disable : 3078)

Texture2D g_TextureDepth;

int ComputeCheckerboardPattern(int2 Position)
{
    return (Position.x + Position.y & 1) & 1;
}

float ComputeDepthCheckerboard(Texture2D SampledTexture, int2 Position) 
{
    float Depth0 = SampledTexture.Load(int3(2 * Position + int2(0, 0), 0)).x;
    float Depth1 = SampledTexture.Load(int3(2 * Position + int2(0, 1), 0)).x;
    float Depth2 = SampledTexture.Load(int3(2 * Position + int2(1, 0), 0)).x;
    float Depth3 = SampledTexture.Load(int3(2 * Position + int2(1, 1), 0)).x;
    float MinDepth = min(min(Depth0, Depth1), min(Depth2, Depth3));
    float MaxDepth = max(max(Depth0, Depth1), max(Depth2, Depth3));
    return lerp(MinDepth, MaxDepth, float(ComputeCheckerboardPattern(Position)));
}

float ComputeDownsampledDepthPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Position = VSOut.f4PixelPos.xy;
    return ComputeDepthCheckerboard(g_TextureDepth, int2(Position));
}
