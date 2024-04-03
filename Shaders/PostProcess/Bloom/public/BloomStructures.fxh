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
    // Linear interpolation blending between the upsample passes
    float InternalBlend  DEFAULT_VALUE(0.5f);
    
    // Linear interpolation blending between the current frame and the bloom texture
    float ExternalBlend  DEFAULT_VALUE(0.15f);
    
    // Padding 0
    float Padding0       DEFAULT_VALUE(0);
    
    // Padding 1
    float Padding1       DEFAULT_VALUE(0);
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(BloomAttribs);
#endif

#endif //_BLOOM_STRUCTURES_FXH_
