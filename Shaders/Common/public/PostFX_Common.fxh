#ifndef _POST_FX_COMMON_FXH_
#define _POST_FX_COMMON_FXH_

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

float3 ProjectPosition(float3 Origin, float4x4 Transform)
{
    float4 Projected = mul(float4(Origin, 1.0), Transform);
    Projected.xyz /= Projected.w;
    Projected.xy = NormalizedDeviceXYToTexUV(Projected.xy);
    return Projected.xyz;
}

float3 ProjectDirection(float3 Origin, float3 Direction, float3 OriginSS, float4x4 Mat)
{
    return ProjectPosition(Origin + Direction, Mat) - OriginSS;
}

float3 InvProjectPosition(float3 Coord, float4x4 Transform)
{
    Coord.xy = TexUVToNormalizedDeviceXY(Coord.xy);
    float4 Projected = mul(float4(Coord, 1.0), Transform);
    return Projected.xyz /= Projected.w;
}

float DepthToCameraZ(in float fDepth, in float4x4 mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first

    float z = DepthToNormalizedDeviceZ(fDepth);
    return MATRIX_ELEMENT(mProj, 3, 2) / (z - MATRIX_ELEMENT(mProj, 2, 2));
}

float CameraZToDepth(in float fDepth, in float4x4 mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first

    float z = MATRIX_ELEMENT(mProj, 3, 2) / fDepth + MATRIX_ELEMENT(mProj, 2, 2);
    return NormalizedDeviceZToDepth(z);
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

#endif // _POST_FX_COMMON_FXH_
