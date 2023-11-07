#ifndef _HN_AXES_STRUCTURES_FXH_
#define _HN_AXES_STRUCTURES_FXH_

struct AxesConstants
{
    float4x4 Transform;
    float4   AxesColors[6]; // -X, +X, -Y, +Y, -Z, +Z
};

#endif // _HN_AXES_STRUCTURES_FXH_
