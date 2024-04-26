#ifndef _POST_FX_COMMON_FXH_
#define _POST_FX_COMMON_FXH_

#include "ShaderUtilities.fxh"

#define M_PI                      3.14159265358979
#define M_HALF_PI                 1.57079632679490
#define M_EPSILON                 1e-3
#define M_GOLDEN_RATIO            1.61803398875

#define FLT_EPS                   5.960464478e-8
#define FLT_MAX                   3.402823466e+38
#define FLT_MIN                   1.175494351e-38

struct CRNG
{
    uint Seed;
};

uint PCGHash(uint Seed)
{
    uint State = Seed * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

CRNG InitCRND(uint2 id, uint FrameIndex)
{
    CRNG Rng;
    Rng.Seed = FrameIndex + PCGHash((id.x << 16u) | id.y);
    return Rng;
}

float Rand(inout CRNG Rng)
{
    Rng.Seed = PCGHash(Rng.Seed);
    return asfloat(0x3f800000u | (Rng.Seed >> 9u)) - 1.0;
}

float Luminance(float3 Color)
{
    return dot(Color, float3(0.299f, 0.587f, 0.114f));
}

uint ComputeHalfResolutionOffset(uint2 PixelCoord)
{
    // This is the packed matrix:
    //  0 1 2 3
    //  3 2 1 0
    //  1 0 3 2
    //  2 3 0 1
    uint PackedOffsets = 1320229860u;
    uint Idx = ((PixelCoord.x & 0x3u) << 3u) + ((PixelCoord.y & 0x3u) << 1u);
    return (PackedOffsets >> Idx) & 0x3u;
}

float Bayer4x4(uint2 SamplePos, uint FrameIndex)
{
    uint2 SamplePosWrap = SamplePos & 3u;
    uint A = 2068378560u * (1u - (SamplePosWrap.x >> 1u)) + 1500172770u * (SamplePosWrap.x >> 1u);
    uint B = (SamplePosWrap.y + ((SamplePosWrap.x & 1u) << 2u)) << 2u;
    uint SampleOffset = FrameIndex;
    uint Bayer = ((A >> B) + SampleOffset) & 0xFu;
    return float(Bayer) / 16.0;
}

float4 GetRotator(float Angle)
{
    float Sin = 0.0;
    float Cos = 0.0;
    sincos(Angle, Sin, Cos);
    return float4(Cos, Sin, -Sin, Cos);
}

float4 CombineRotators(float4 R1, float4 R2)
{
    return R1.xyxy * R2.xxzz + R1.zwzw * R2.yyww;
}

float2 RotateVector(float4 Rotator, float2 Vec)
{
    return Vec.x * Rotator.xz + Vec.y * Rotator.yw;
}

float3 ProjectPosition(float3 Origin, float4x4 Transform)
{
    float4 Projected = mul(float4(Origin, 1.0), Transform);
    Projected.xyz /= Projected.w;
    Projected.xy = NormalizedDeviceXYToTexUV(Projected.xy);
    Projected.z = NormalizedDeviceZToDepth(Projected.z);
    return Projected.xyz;
}

float3 ProjectDirection(float3 Origin, float3 Direction, float3 OriginSS, float4x4 Mat)
{
    return ProjectPosition(Origin + Direction, Mat) - OriginSS;
}

float3 InvProjectPosition(float3 Coord, float4x4 Transform)
{
    Coord.xy = TexUVToNormalizedDeviceXY(Coord.xy);
    Coord.z = DepthToNormalizedDeviceZ(Coord.z);
    float4 Projected = mul(float4(Coord, 1.0), Transform);
    return Projected.xyz /= Projected.w;
}

float3 ScreenXYDepthToViewSpace(float3 Coord, float4x4 Transform)
{
    float3 NDC = float3(TexUVToNormalizedDeviceXY(Coord.xy), DepthToCameraZ(Coord.z, Transform));
    return float3(NDC.z * NDC.x / MATRIX_ELEMENT(Transform, 0, 0), NDC.z * NDC.y / MATRIX_ELEMENT(Transform, 1, 1), NDC.z);
}

bool IsInsideScreen(int2 PixelCoord, int2 Dimension)
{
    return PixelCoord.x >= 0 &&
           PixelCoord.y >= 0 &&
           PixelCoord.x < Dimension.x &&
           PixelCoord.y < Dimension.y;
}

bool IsInsideScreen(float2 PixelCoord, float2 Dimension)
{
    return PixelCoord.x >= 0.0 &&
           PixelCoord.y >= 0.0 &&
           PixelCoord.x < Dimension.x &&
           PixelCoord.y < Dimension.y;
}

int2 ClampScreenCoord(int2 PixelCoord, int2 Dimension)
{
    return clamp(PixelCoord, int2(0, 0), Dimension - int2(1, 1));
}

float ComputeSpatialWeight(float Distance, float Sigma)
{
    return exp(-(Distance) / (2.0 * Sigma * Sigma));
}

#endif // _POST_FX_COMMON_FXH_
