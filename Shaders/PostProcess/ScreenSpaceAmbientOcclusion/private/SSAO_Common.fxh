#ifndef _SSAO_COMMON_FXH_
#define _SSAO_COMMON_FXH_

#include "PostFX_Common.fxh"

#if SSAO_OPTION_INVERTED_DEPTH
    #define MipConvFunc    max
    #define DepthFarPlane  0.0
    #define DepthNearPlane 1.0
#else
    #define MipConvFunc    min
    #define DepthFarPlane  1.0
    #define DepthNearPlane 0.0
#endif // SSAO_OPTION_INVERTED_DEPTH

bool IsBackground(float Depth)
{
#if SSAO_OPTION_INVERTED_DEPTH
    return Depth < 1.e-6f;
#else
    return Depth >= (1.0f - 1.e-6f);
#endif // SSAO_OPTION_INVERTED_DEPTH
}

float ComputeDepthWeight(float CenterDepth, float GuideDepth, float4x4 ProjMatrix, float Sigma)
{
    float LinearDepth0 = DepthToCameraZ(CenterDepth, ProjMatrix);
    float LinearDepth1 = DepthToCameraZ(GuideDepth, ProjMatrix);
    float Alpha = abs(LinearDepth0 - LinearDepth1) / LinearDepth0;
    return exp(-(Alpha * Alpha) / (2.0 * Sigma * Sigma));
}

float ComputeGeometryWeight(float3 CenterPos, float3 TapPos, float3 CenterNormal, float PlaneDistanceNorm)
{
    return saturate(1.0 - abs(dot((TapPos - CenterPos), CenterNormal)) * PlaneDistanceNorm);
}

#endif // _SSAO_COMMON_FXH_
