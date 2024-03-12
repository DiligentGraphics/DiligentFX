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

#ifndef ENABLE_SHEEN
#   define ENABLE_SHEEN 0
#endif
    
#ifndef ENABLE_ANISOTROPY
#   define ENABLE_ANISOTROPY 0
#endif
    
#ifndef ENABLE_IRIDESCENCE
#   define ENABLE_IRIDESCENCE 0
#endif
    
#ifndef ENABLE_TRANSMISSION
#   define ENABLE_TRANSMISSION 0
#endif
    
#ifndef ENABLE_VOLUME
#   define ENABLE_VOLUME 0
#endif
    
#ifndef PBR_NUM_TEXTURE_ATTRIBUTES
#   define PBR_NUM_TEXTURE_ATTRIBUTES 0
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
	float PrefilteredCubeLastMip; // Prefiltered cube map last mip level

	float IBLScale;
    float OcclusionStrength;
    float EmissionScale;
    float PointSize; // OpenGL and Vulkan

    float MipBias;
    int   LightCount;
    float Padding0;
    float Padding1;
    
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
    float ClearcoatFactor;
    float ClearcoatRoughnessFactor;

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

struct PBRMaterialVolumeAttribs
{
    float AttenuationColorR;
    float AttenuationColorG;
    float AttenuationColorB;
    float ThicknessFactor;

    float AttenuationDistance;
    float Padding0;
    float Padding1;
    float Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialVolumeAttribs);
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
    PBRMaterialSheenAttribs Sheen;
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
    
#if ENABLE_VOLUME
    PBRMaterialVolumeAttribs Volume;
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
    int   Type; // 1 - directional, 2 - point, 3 - spot
    float PosX; // Point or spot light
    float PosY; // position; unused for
    float PosZ; // directional light.
    
    float DirectionX; // Directional and spot lights.
    float DirectionY;
    float DirectionZ;    
    int   ShadowMapIndex; // -1 if light does not cast shadows

    float IntensityR;    
    float IntensityG;
    float IntensityB;
    float Range4;         // Point and spot light range to the power of 4
    
    float SpotAngleScale; // 1.0 / (cos(InnerConeAngle) - cos(OuterConeAngle))
    float SpotAngleOffset;// -cos(OuterConeAngle) * SpotAngleScale;
    float Padding0;
    float Padding1;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRLightAttribs);
#endif


struct PBRShadowMapInfo
{
    float4x4 WorldToLightProjSpace;
    
    float2 UVScale;
    float2 UVBias;
    
    float    ShadowMapSlice;
    float    Padding0;
    float    Padding1;
    float    Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(PBRShadowMapInfo);
#endif

#endif // _PBR_STRUCTURES_FXH_
