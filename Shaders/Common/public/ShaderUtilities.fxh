#ifndef _SHADER_UTILITIES_FXH_
#define _SHADER_UTILITIES_FXH_

// Transforms depth to normalized device z coordinate
float DepthToNormalizedDeviceZ(in float Depth, in float4x4 mProj)
{
    // In Direct3D anv Vulkan, normalized device z range is [0, +1]
    // In OpengGL, normalized device z range is [-1, +1] (unless GL_ARB_clip_control extension is used to correct this nonsense).
    return MATRIX_ELEMENT(mProj,2,2) + MATRIX_ELEMENT(mProj,3,2) / Depth;
}

#endif //_SHADER_UTILITIES_FXH_
