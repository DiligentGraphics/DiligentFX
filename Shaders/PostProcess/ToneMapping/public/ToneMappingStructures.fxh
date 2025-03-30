#ifndef _TONE_MAPPING_STRUCTURES_FXH_
#define _TONE_MAPPING_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

// Tone mapping mode
#define TONE_MAPPING_MODE_NONE          0
#define TONE_MAPPING_MODE_EXP           1
#define TONE_MAPPING_MODE_REINHARD      2
#define TONE_MAPPING_MODE_REINHARD_MOD  3
#define TONE_MAPPING_MODE_UNCHARTED2    4
#define TONE_MAPPING_MODE_FILMIC_ALU    5
#define TONE_MAPPING_MODE_LOGARITHMIC   6
#define TONE_MAPPING_MODE_ADAPTIVE_LOG  7
#define TONE_MAPPING_MODE_AGX           8
#define TONE_MAPPING_MODE_AGX_CUSTOM    9

struct AgXAttribs
{
    float Saturation  DEFAULT_VALUE(1.f);
    float Slope       DEFAULT_VALUE(1.f);
    float Power       DEFAULT_VALUE(1.f);
    float Offset      DEFAULT_VALUE(0.f);
};

struct ToneMappingAttribs
{
    // Tone mapping mode.
    int   iToneMappingMode                  DEFAULT_VALUE(TONE_MAPPING_MODE_UNCHARTED2);
    // Automatically compute exposure to use in tone mapping.
    BOOL  bAutoExposure                     DEFAULT_VALUE(TRUE);
    // Middle gray value used by tone mapping operators.
    float fMiddleGray                       DEFAULT_VALUE(0.18f);
    // Simulate eye adaptation to light changes.
    BOOL  bLightAdaptation                  DEFAULT_VALUE(TRUE);

    // White point to use in tone mapping.
    float fWhitePoint                       DEFAULT_VALUE(3.f);
    // Luminance point to use in tone mapping.
    float fLuminanceSaturation              DEFAULT_VALUE(1.f);
    uint  Padding0                          DEFAULT_VALUE(0);
    uint  Padding1                          DEFAULT_VALUE(0);

    // Settings for AGX custom tone mapping operator.
    AgXAttribs AgX                          DEFAULT_VALUE({});
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ToneMappingAttribs);
#endif

#endif // _TONE_MAPPING_STRUCTURES_FXH_
