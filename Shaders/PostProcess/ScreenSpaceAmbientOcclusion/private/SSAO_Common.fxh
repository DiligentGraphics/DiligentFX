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

#endif // _SSAO_COMMON_FXH_
