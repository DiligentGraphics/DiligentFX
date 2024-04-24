#ifndef _SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_
#define _SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

// Maximum mip level of depth buffer used in XeGTAO algorithm
#define SSAO_DEPTH_PREFILTERED_MAX_MIP 4

// Maximum mip level of history buffer used in the resampled history step
#define SSAO_DEPTH_HISTORY_CONVOLUTED_MAX_MIP 4

// Maximum number of frames using the fixed history buffer
#define SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_HISTORY_FIX 4

// Maximum number of frames using denoiser
#define SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_DENOISING 8

// Number of slices used in the calculation of ambient occlusion
#define SSAO_SLICE_COUNT 3

// Number of samples per slice used in the calculation of ambient occlusion
#define SSAO_SAMPLES_PER_SLICE 3

// Number of samples on the Poisson disc used in the spatial reconstruction step
#define SSAO_SPATIAL_RECONSTRUCTION_SAMPLES 8

// Sets the sigma in Gaussian weighting for points on the Poisson disk at the spatial reconstruction step
#define SSAO_SPATIAL_RECONSTRUCTION_SIGMA 0.9

// Sets the sigma of spatial component in the bilateral upsampling step
#define SSAO_BILATERAL_UPSAMPLING_SIGMA 0.9

// Sets the sigma of depth component in the bilateral upsampling step
#define SSAO_BILATERAL_UPSAMPLING_DEPTH_SIGMA 0.0075

// Defines the threshold for pixel velocity difference that determines whether a pixel is considered to have "no history" in the temporal accumulation step
#define SSAO_TEMPORAL_MOTION_VECTOR_DIFF_FACTOR 128.0

// Sets the minimum value for the variance gamma in the temporal accumulation step
#define SSAO_TEMPORAL_MIN_VARIANCE_GAMMA 0.5

// Sets the maximum value for the variance gamma in the temporal accumulation step
#define SSAO_TEMPORAL_MAX_VARIANCE_GAMMA 2.5

// Determines the similarity of the depth in the current and previous frame to calculate the disoclusion in the temporal accumulation step
#define SSAO_DISOCCLUSION_DEPTH_THRESHOLD 0.01

struct ScreenSpaceAmbientOcclusionAttribs
{
    // The value defines world space radius of ambient occlusion
    float EffectRadius                DEFAULT_VALUE(1.0f);

    // Gently reduces sample impact as it gets out of the 'Effect radius' bounds
    float EffectFalloffRange          DEFAULT_VALUE(0.615f);

    // Use different value as compared to the ground truth radius to counter inherent screen space biases
    float RadiusMultiplier            DEFAULT_VALUE(1.457f);

    // Controls the main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)
    float DepthMIPSamplingOffset      DEFAULT_VALUE(3.3f);

    // The value is responsible for interpolating between the current and previous frame
    float TemporalStabilityFactor     DEFAULT_VALUE(0.9f);

    // Controls the kernel size in the spatial reconstruction step. Increasing the value increases the deviation from the ground truth but reduces the noise
    float SpatialReconstructionRadius DEFAULT_VALUE(4.0f);
    
    // If this parameter is set to true, the current frame will be written to the current history buffer without interpolation with the previous history buffer
    BOOL  ResetAccumulation           DEFAULT_VALUE(FALSE);
    
    // Padding 1
    float Padding1                    DEFAULT_VALUE(0);
};

#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ScreenSpaceAmbientOcclusionAttribs);
#endif

#endif //_SCREEN_SPACE_AMBIENT_OCCLUSION_STRUCTURES_FXH_
