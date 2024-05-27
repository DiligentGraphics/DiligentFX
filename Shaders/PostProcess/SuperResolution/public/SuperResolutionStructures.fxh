#ifndef _SUPER_RESOLUTION_STRUCTURES_FXH_
#define _SUPER_RESOLUTION_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

struct SuperResolutionAttribs
{
    float4 SourceSize;
    float4 OutputSize;
    float  ResolutionScale DEFAULT_VALUE(1.0f);
    float  Sharpening      DEFAULT_VALUE(1.0f);
    float  Padding0        DEFAULT_VALUE(0.0f);
    float  Padding1        DEFAULT_VALUE(0.0f);
};

#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(SuperResolutionAttribs);
#endif


#endif //_SUPER_RESOLUTION_STRUCTURES_FXH_
