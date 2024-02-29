#ifndef _SSR_COMMON_FXH_
#define _SSR_COMMON_FXH_

#include "PostFX_Common.fxh"

#if SSR_OPTION_INVERTED_DEPTH
    #define MipConvFunc max
    #define DepthFarPlane 0.0
#else
    #define MipConvFunc min
    #define DepthFarPlane 1.0
#endif // SSR_OPTION_INVERTED_DEPTH

#if !defined(DESKTOP_GL) && !defined(GL_ES)
    #define SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL [earlydepthstencil]
#else
    #define SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL 
#endif

float VanDerCorputSequenceBase2(uint SampleIdx)
{
    return float(reversebits(SampleIdx)) / exp2(32.0);
}

float2 HammersleySequence(uint SampleIdx, uint N)
{
    return float2(float(SampleIdx) / float(N), VanDerCorputSequenceBase2(SampleIdx));
}

float2 VogelDiskSample(uint SampleIdx, uint N, float Phi)
{
    float GoldenAngle = 2.4;
    float R = sqrt(float(SampleIdx) + 0.5) / sqrt(float(N));
    float Theta = float(SampleIdx) * GoldenAngle + Phi;

    float Sine, Cosine;
    sincos(Theta, Sine, Cosine);
    return float2(R * Cosine, R * Sine);
}

float2 MapSquareToDisk(float2 Point)
{
    float Lam = sqrt(Point.x);
    float Phi = 2.0 * M_PI * Point.y;
    return float2(cos(Phi) * Lam, sin(Phi) * Lam);
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

bool IsBackground(float Depth)
{
#if SSR_OPTION_INVERTED_DEPTH
    return Depth < 1.0e-6f;
#else
    return Depth >= (1.0f - 1.0e-6f);
#endif // SSR_OPTION_INVERTED_DEPTH
}

bool IsReflectionSample(float Roughness, float Depth, float RoughnessThreshold)
{
    return Roughness <= RoughnessThreshold && !IsBackground(Depth);
}

bool IsMirrorReflection(float Roughness)
{
    return Roughness < 0.01;
}

#endif // _SSR_COMMON_FXH_
