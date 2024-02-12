#ifndef _TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_
#define _TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_

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

#define TAA_MIN_VARIANCE_GAMMA           0.5

#define TAA_MAX_VARIANCE_GAMMA           2.5

#define TAA_MAGNITUDE_MOTION_FACTOR      5.0

#define TAA_MOTION_VECTOR_DELTA_ERROR    0.025

#define TAA_DEPTH_DISOCCLUSION_THRESHOLD 0.9

#define TAA_MOTION_DISOCCLUSION_FACTOR   0.1

struct TemporalAntiAliasingAttribs
{
    // The value is responsible for interpolating between the current and previous frame.
    // Increasing the value increases temporal stability but may introduce ghosting
    float TemporalStabilityFactor    DEFAULT_VALUE(0.9375f);
    // If this parameter is set to true,
    // the current frame will be written to the current history buffer without interpolation with the previous history buffer
    BOOL  ResetAccumulation          DEFAULT_VALUE(FALSE);
    float Padding0                   DEFAULT_VALUE(0.0);
    float Padding1                   DEFAULT_VALUE(0.0);
};

#endif //_TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_
