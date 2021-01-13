#ifndef _GLTF_PBR_STRUCTURES_FXH_
#define _GLTF_PBR_STRUCTURES_FXH_

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


#ifndef GLTF_ALPHA_MODE_OPAQUE
#   define GLTF_ALPHA_MODE_OPAQUE 0
#endif

#ifndef GLTF_ALPHA_MODE_MASK
#   define GLTF_ALPHA_MODE_MASK 1
#endif

#ifndef GLTF_ALPHA_MODE_BLEND
#   define GLTF_ALPHA_MODE_BLEND 2
#endif


struct GLTFNodeShaderTransforms
{
	float4x4 NodeMatrix;

	int      JointCount;
    float    Dummy0;
    float    Dummy1;
    float    Dummy2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFNodeShaderTransforms);
#endif


struct GLTFRendererShaderParameters
{
	float AverageLogLum;
	float MiddleGray;
    float WhitePoint;
	float PrefilteredCubeMipLevels;

	float IBLScale;
	int   DebugViewType;
    float OcclusionStrength;
    float EmissionScale;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFRendererShaderParameters);
#endif

struct GLTFMaterialShaderInfo
{
	float4  BaseColorFactor;
	float4  EmissiveFactor;
	float4  SpecularFactor;

	int     Workflow;
	float   BaseColorTextureUVSelector;
	float   PhysicalDescriptorTextureUVSelector;
	float   NormalTextureUVSelector; 

	float   OcclusionTextureUVSelector;
	float   EmissiveTextureUVSelector;
    float   BaseColorSlice;
    float   PhysicalDescriptorSlice;

    float   NormalSlice;
    float   OcclusionSlice;
    float   EmissiveSlice;
	float   MetallicFactor;

	float   RoughnessFactor;
	int     AlphaMode;	
	float   AlphaMaskCutoff;
    float   Dummy0;

    // When texture atlas is used, UV scale and bias applied to
    // each texture coordinate set
    float4 BaseColorUVScaleBias;
    float4 PhysicalDescriptorUVScaleBias;
    float4 NormalMapUVScaleBias;
    float4 OcclusionUVScaleBias;
    float4 EmissiveUVScaleBias;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFMaterialShaderInfo);
#endif

#endif // _GLTF_PBR_STRUCTURES_FXH_
