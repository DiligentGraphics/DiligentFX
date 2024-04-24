#ifndef _DEPTH_OF_FIELD_STRUCTURES_FXH_
#define _DEPTH_OF_FIELD_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

#define DOF_KERNEL_SAMPLE_COUNT 22

struct DepthOfFieldAttribs
{
    // The intensity of the depth of field effect.
    float BokehRadius     DEFAULT_VALUE(4.0f);

    // The distance from the camera at which the depth of field effect is focused.
    float FocusDistance   DEFAULT_VALUE(10.0f);

    // The range of distances from the focus distance at which the depth of field effect is applied.
    float FocusRange      DEFAULT_VALUE(3.0f);
   
    float Padding0        DEFAULT_VALUE(0.0f);
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(DepthOfFieldAttribs);
#endif

#endif // _DEPTH_OF_FIELD_STRUCTURES_FXH_
