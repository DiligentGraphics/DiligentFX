#ifndef _RADIENT_POST_PROCESS_STRUCTURES_FXH_
#define _RADIENT_POST_PROCESS_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ToneMappingStructures.fxh"
#endif

struct RadientPostProcessAttribs
{
    ToneMappingAttribs ToneMapping;

    float AverageLogLum DEFAULT_VALUE(0.3f);
    float Padding0      DEFAULT_VALUE(0.f);
    float Padding1      DEFAULT_VALUE(0.f);
    float Padding2      DEFAULT_VALUE(0.f);
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(RadientPostProcessAttribs);
#endif

#endif // _RADIENT_POST_PROCESS_STRUCTURES_FXH_
