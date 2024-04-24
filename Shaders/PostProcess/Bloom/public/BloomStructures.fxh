#ifndef _BLOOM_STRUCTURES_FXH_
#define _BLOOM_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

// Implemented based on this article
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
struct BloomAttribs
{
    // The intensity of the bloom effect.
    float Intensity      DEFAULT_VALUE(0.15f);

    // This value determines the minimum brightness required for a pixel to contribute to the bloom effect.
    float Threshold      DEFAULT_VALUE(1.0);

    // This value determines the softness of the threshold. A higher value will result in a softer threshold.
    float SoftTreshold  DEFAULT_VALUE(0.125);

    // This variable controls the size of the bloom effect. A larger radius will result in a larger area of the image being affected by the bloom effect.
    float Radius         DEFAULT_VALUE(0.75);
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(BloomAttribs);
#endif

#endif //_BLOOM_STRUCTURES_FXH_
