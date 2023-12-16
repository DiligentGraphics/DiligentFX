#ifndef _PBR_STRUCTURES_FXH_
#define _PBR_STRUCTURES_FXH_

#ifdef __cplusplus

#   ifndef CHECK_STRUCT_ALIGNMENT
        // Note that defining empty macros causes GL shader compilation error on Mac, because
        // it does not allow standalone semicolons outside of main.
        // On the other hand, adding semicolon at the end of the macro definition causes gcc error.
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
#   endif

#endif

#ifndef PBR_WORKFLOW_METALLIC_ROUGHNESS
#   define PBR_WORKFLOW_METALLIC_ROUGHNESS 0
#endif

#ifndef PBR_WORKFLOW_SPECULAR_GLOSINESS
#   define PBR_WORKFLOW_SPECULAR_GLOSINESS 1
#endif


#ifndef PBR_ALPHA_MODE_OPAQUE
#   define PBR_ALPHA_MODE_OPAQUE 0
#endif

#ifndef PBR_ALPHA_MODE_MASK
#   define PBR_ALPHA_MODE_MASK 1
#endif

#ifndef PBR_ALPHA_MODE_BLEND
#   define PBR_ALPHA_MODE_BLEND 2
#endif


#ifndef PBR_NUM_TEXTURE_ATTRIBUTES
#   define PBR_NUM_TEXTURE_ATTRIBUTES 5
#endif


struct GLTFNodeShaderTransforms
{
	float4x4 NodeMatrix;

	int   JointCount;
    float Dummy0;
    float Dummy1;
    float Dummy2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFNodeShaderTransforms);
#endif


struct PBRRendererShaderParameters
{
	float AverageLogLum;
	float MiddleGray;
    float WhitePoint;
	float PrefilteredCubeMipLevels;

	float IBLScale;
    float OcclusionStrength;
    float EmissionScale;
    float PointSize; // OpenGL and Vulkan

    float4 UnshadedColor;
    float4 HighlightColor;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRRendererShaderParameters);
#endif

struct PBRMaterialBasicAttribs
{
    float4 BaseColorFactor;
    float4 EmissiveFactor;
    float4 SpecularFactor;

    int   Workflow;
    int   AlphaMode;
    float AlphaMaskCutoff;
    float MetallicFactor;

    float RoughnessFactor;
    float OcclusionFactor;
    float Padding0;
    float Padding1;

    // Any user-specific data
    float4 CustomData;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialBasicAttribs);
#endif

struct PBRMaterialSheenAttribs
{
    float ColorFactorR;
    float ColorFactorG;
    float ColorFactorB;
    float RoughnessFactor;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialSheenAttribs);
#endif

struct PBRMaterialAnisotropyAttribs
{
    float Strength;
    float Rotation;
    float Padding0;
    float Padding1;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialAnisotropyAttribs);
#endif

struct PBRMaterialIridescenceAttribs
{
    float Factor;
    float IOR;
    float ThicknessMinimum;
    float ThicknessMaximum;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialIridescenceAttribs);
#endif

struct PBRMaterialTransmissionAttribs
{
    float Factor;
    float Padding0;
    float Padding1;
    float Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialTransmissionAttribs);
#endif

struct PBRMaterialTextureAttribs
{
    float UVSelector;
    float TextureSlice;
    float UBias;
    float VBias;

    float4 UVScaleAndRotation;
    float4 AtlasUVScaleAndBias;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialTextureAttribs);
#endif

struct PBRMaterialShaderInfo
{
    PBRMaterialBasicAttribs Basic;
    
#if ENABLE_SHEEN
    PBRMaterialSheenAttribs Sheen
#endif
    
#if ENABLE_ANISOTROPY
    PBRMaterialAnisotropyAttribs Anisotropy;
#endif
    
#if ENABLE_IRIDESCENCE
    PBRMaterialIridescenceAttribs Iridescence;
#endif
    
#if ENABLE_TRANSMISSION
    PBRMaterialTransmissionAttribs Transmission;
#endif
    
#if PBR_NUM_TEXTURE_ATTRIBUTES > 0
    PBRMaterialTextureAttribs Textures[PBR_NUM_TEXTURE_ATTRIBUTES];
#endif
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialShaderInfo);
#endif

struct PBRLightAttribs
{
    float4 Direction;
    float4 Intensity;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRLightAttribs);
#endif

#endif // _PBR_STRUCTURES_FXH_
