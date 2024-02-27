#ifndef _SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_
#define _SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_

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

#define SSAO_DEPTH_PREFILTERED_MAX_MIP 4

#define SSAO_SLICE_COUNT 3

#define SSAO_SAMPLES_PER_SLICE 3

#define SSAO_SPATIAL_RECONSTRUCTION_RADIUS 2

#define SSAO_SPATIAL_RECONSTRUCTION_SIGMA  3.0

#define SSAO_SPATIAL_RECONSTRUCTION_DEPTH_SIGMA 0.01

#define SSAO_TEMPORAL_STANDARD_DEVIATION_SCALE 2.0

#define SSAO_DISOCCLUSION_THRESHOLD 0.975

#define SSAO_DISOCCLUSION_DEPTH_WEIGHT 1.0

struct ScreenSpaceAmbientOcclusionAttribs
{
    // World (viewspace) maximum size of the shadow
    float EffectRadius             DEFAULT_VALUE(1.0f);

    // Gently reduce sample impact as it gets out of 'Effect radius' bounds
    float EffectFalloffRange       DEFAULT_VALUE(0.615f);

    // Allows us to use different value as compared to ground truth radius to counter inherent screen space biases
    float RadiusMultiplier         DEFAULT_VALUE(1.457f);

    // Main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)
    float DepthMIPSamplingOffset   DEFAULT_VALUE(3.3f);

    // The value is responsible for interpolating between the current and previous frame.
    float TemporalStabilityFactor  DEFAULT_VALUE(0.9f);

    float Padding0                 DEFAULT_VALUE(0);
    float Padding1                 DEFAULT_VALUE(0);
    float Padding2                 DEFAULT_VALUE(0);
};

#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ScreenSpaceAmbientOcclusionAttribs);
#endif

#endif //_SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_
