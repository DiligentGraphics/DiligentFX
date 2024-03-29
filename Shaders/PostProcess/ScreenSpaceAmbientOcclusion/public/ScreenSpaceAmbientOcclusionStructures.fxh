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

#define SSAO_DEPTH_HISTORY_CONVOLUTED_MAX_MIP 4

#define SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_HISTORY_FIX 4

#define SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_DENOISING 8

#define SSAO_SLICE_COUNT 3

#define SSAO_SAMPLES_PER_SLICE 3

#define SSAO_SPATIAL_RECONSTRUCTION_SAMPLES 8

#define SSAO_SPATIAL_RECONSTRUCTION_SIGMA 0.9

#define SSAO_BILATERAL_UPSAMPLING_RADIUS 1

#define SSAO_BILATERAL_UPSAMPLING_SIGMA 0.9

#define SSAO_BILATERAL_UPSAMPLING_DEPTH_SIGMA 0.0075

#define SSAO_TEMPORAL_STANDARD_DEVIATION_SCALE 0.5

#define SSAO_TEMPORAL_MOTION_VECTOR_DIFF_FACTOR 128.0

#define SSAO_TEMPORAL_MIN_VARIANCE_GAMMA 0.5

#define SSAO_TEMPORAL_MAX_VARIANCE_GAMMA 2.5

#define SSAO_DISOCCLUSION_DEPTH_THRESHOLD 0.01

struct ScreenSpaceAmbientOcclusionAttribs
{
    // The value defines world space radius of ambient occlusion
    float EffectRadius                DEFAULT_VALUE(1.0f);

    // The value gently reduces sample impact as it gets out of 'Effect radius' bounds
    float EffectFalloffRange          DEFAULT_VALUE(0.615f);

    // The value allows us to use different value as compared to ground truth radius to counter inherent screen space biases
    float RadiusMultiplier            DEFAULT_VALUE(1.457f);

    // The value defines main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)
    float DepthMIPSamplingOffset      DEFAULT_VALUE(3.3f);

    // The value is responsible for interpolating between the current and previous frame
    float TemporalStabilityFactor     DEFAULT_VALUE(0.9f);

    // The value controls the kernel size in the spatial reconstruction step. Increasing the value increases the deviation from the ground truth but reduces the noise
    float SpatialReconstructionRadius DEFAULT_VALUE(4.0f);
    
    // Padding 0
    float Padding0                    DEFAULT_VALUE(0);
    
    // Padding 1
    float Padding1                    DEFAULT_VALUE(0);
};

#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ScreenSpaceAmbientOcclusionAttribs);
#endif

#endif //_SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_
