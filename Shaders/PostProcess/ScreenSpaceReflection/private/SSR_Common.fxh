#ifndef _SSR_COMMON_FXH_
#define _SSR_COMMON_FXH_

#include "PostFX_Common.fxh"

#ifdef SSR_OPTION_INVERTED_DEPTH
    #define MipConvFunc max
#else
    #define MipConvFunc min
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

bool IsBackground(float Depth)
{
#ifdef SSR_OPTION_INVERTED_DEPTH
    return Depth < 1.0e-6f;
#else
    return Depth >= (1.0f - 1.0e-6f);
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

#endif // _SSR_COMMON_FXH_
