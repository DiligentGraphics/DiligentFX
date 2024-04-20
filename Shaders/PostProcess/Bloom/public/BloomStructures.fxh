#ifndef _BLOOM_STRUCTURES_FXH_
#define _BLOOM_STRUCTURES_FXH_

#ifdef __cplusplus

#   ifndef BOOL
#      define BOOL int32_t // Do not use bool, because sizeof(bool)==1 !
#   endif

#   ifndef TRUE
#      define TRUE 1
#   endif

#   ifndef FALSE
#      define FALSE 0
#   endif

#   ifndef CHECK_STRUCT_ALIGNMENT
        // Note that defining empty macros causes GL shader compilation error on Mac, because
        // it does not allow standalone semicolons outside of main.
        // On the other hand, adding semicolon at the end of the macro definition causes gcc error.
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x) =x
#   endif

#else

#   ifndef BOOL
#       define BOOL bool
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x)
#   endif

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
