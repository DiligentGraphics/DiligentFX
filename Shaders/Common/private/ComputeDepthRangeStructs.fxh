#ifndef _COMPUTE_DEPTH_RANGE_STRUCTS_FXH_
#define _COMPUTE_DEPTH_RANGE_STRUCTS_FXH_

// Store floats as ints as atomic operations only work with ints.
// Note: members of this struct mirror the members of the CameraAttribs struct.
struct DepthRangeI
{
    int iSceneNearZ;
    int iSceneFarZ;
    int iSceneNearDepth;
    int iSceneFarDepth;
};

#endif
