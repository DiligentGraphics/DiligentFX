#ifndef _SSR_COMMON_FXH_
#define _SSR_COMMON_FXH_

#define M_PI                      3.14159265358979
#define M_EPSILON                 1e-3
#define M_GOLDEN_RATIO            1.61803398875

#define FLT_EPS                   5.960464478e-8
#define FLT_MAX                   3.402823466e+38
#define FLT_MIN                   1.175494351e-38

#if !defined(DESKTOP_GL) && !defined(GL_ES)
    #define SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL [earlydepthstencil]
#else
    #define SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL 
#endif

struct CRNG
{
    uint Seed;
};

float VanDerCorputSequenceBase2(uint SampleIdx)
{
    return reversebits(SampleIdx) / exp2(32);
}

float2 GoldenRatioSequence(int SampleIdx, uint N)
{
    return frac(float2((0.5 / float(N)) + float(SampleIdx) / float(N), 0.5 + rcp(M_GOLDEN_RATIO) * float(SampleIdx)));
}

float2 HammersleySequence(uint SampleIdx, uint N)
{
    return float2(float(SampleIdx) / float(N), VanDerCorputSequenceBase2(SampleIdx));
}

float2 VogelDiskSample(uint SampleIdx, uint N, float Phi)
{
    const float GoldenAngle = 2.4;
    const float R = sqrt(SampleIdx + 0.5) / sqrt(N);
    const float Theta = SampleIdx * GoldenAngle + Phi;

    float Sine, Cosine;
    sincos(Theta, Sine, Cosine);
    return float2(R * Cosine, R * Sine);
}

float2 MapSquareToDisk(float2 Point)
{
    const float Lam = sqrt(Point.x);
    const float Phi = 2 * M_PI * Point.y;
    return float2(cos(Phi) * Lam, sin(Phi) * Lam);
}

uint PCGHash(uint Seed)
{
    uint State = Seed * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

CRNG InitCRND(uint2 id, uint FrameIndex)
{
    CRNG Rng = { FrameIndex + PCGHash((id.x << 16) | id.y) };
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
    Projected.xy = 0.5 * Projected.xy + 0.5;
    Projected.y = (1 - Projected.y);
    return Projected.xyz;
}

float3 ProjectDirection(float3 Origin, float3 Direction, float3 OriginSS, float4x4 Mat)
{
    return ProjectPosition(Origin + Direction, Mat) - OriginSS;
}

float3 InvProjectPosition(float3 Coord, float4x4 Transform)
{
    Coord.y = (1 - Coord.y);
    Coord.xy = 2 * Coord.xy - 1;
    float4 Projected = mul(float4(Coord, 1.0), Transform);
    return Projected.xyz /= Projected.w;
}

bool IsBackground(float depth)
{
#ifdef SSR_OPTION_INVERTED_DEPTH
    return depth < 1.e-6f;
#else
    return depth >= (1.0f - 1.e-6f);
#endif // SSR_OPTION_INVERTED_DEPTH
}

bool IsGlossyReflection(float Roughness, float RoughnessThreshold, bool IsRoughnessPerceptual)
{
    if (IsRoughnessPerceptual)
        RoughnessThreshold *= RoughnessThreshold;
    return Roughness < RoughnessThreshold;
}

bool IsMirrorReflection(float Roughness)
{
    return Roughness < 0.0001;
}

float DepthToCameraZ(in float fDepth, in matrix mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first

    float z = DepthToNormalizedDeviceZ(fDepth);
    return MATRIX_ELEMENT(mProj, 3, 2) / (z - MATRIX_ELEMENT(mProj, 2, 2));
}

float CameraZToDepth(in float fDepth, in matrix mProj)
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


#endif // _SSR_COMMON_FXH_
