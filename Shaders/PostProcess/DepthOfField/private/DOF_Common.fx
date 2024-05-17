
#ifndef _DOF_COMMON_FXH_
#define _DOF_COMMON_FXH_

int ComputeSampleCount(int RingCount, int RingDensity)
{
    return 1 + RingDensity * ((RingCount - 1) * RingCount >> 1);
}

float ComputeHDRWeight(float3 Color)
{
    return 1.0 + Luminance(Color);
}

float ComputeSDRWeight(float3 Color)
{
    return 1.0 / (1.0 + Luminance(Color));
}

#endif // _DOF_COMMON_FXH_
