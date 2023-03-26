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

	int   Workflow;
    float UVSelector0;
    float UVSelector1;
    float UVSelector2;

    float UVSelector3;
    float UVSelector4;
    float TextureSlice0;
    float TextureSlice1;

    float TextureSlice2;
    float TextureSlice3;
    float TextureSlice4;
	float MetallicFactor;

	float   RoughnessFactor;
	int     AlphaMode;	
	float   AlphaMaskCutoff;
    float   Dummy0;

    // When texture atlas is used, UV scale and bias applied to
    // each texture coordinate set
    float4 UVScaleBias0;
    float4 UVScaleBias1;
    float4 UVScaleBias2;
    float4 UVScaleBias3;
    float4 UVScaleBias4;

	float4 CustomData;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFMaterialShaderInfo);
#endif

#ifndef BaseColorTextureUVSelector
#   define BaseColorTextureUVSelector UVSelector0
#endif

#ifndef PhysicalDescriptorTextureUVSelector
#   define PhysicalDescriptorTextureUVSelector UVSelector1
#endif

#ifndef NormalTextureUVSelector
#   define NormalTextureUVSelector UVSelector2
#endif

#ifndef OcclusionTextureUVSelector
#   define OcclusionTextureUVSelector UVSelector3
#endif

#ifndef EmissiveTextureUVSelector
#   define EmissiveTextureUVSelector UVSelector4
#endif


#ifndef BaseColorSlice
#   define BaseColorSlice TextureSlice0
#endif

#ifndef PhysicalDescriptorSlice
#   define PhysicalDescriptorSlice TextureSlice1
#endif

#ifndef NormalSlice
#   define NormalSlice TextureSlice2
#endif

#ifndef OcclusionSlice
#   define OcclusionSlice TextureSlice3
#endif

#ifndef EmissiveSlice
#   define EmissiveSlice TextureSlice4
#endif


#ifndef BaseColorUVScaleBias
#   define BaseColorUVScaleBias UVScaleBias0
#endif

#ifndef PhysicalDescriptorUVScaleBias
#   define PhysicalDescriptorUVScaleBias UVScaleBias1
#endif

#ifndef NormalMapUVScaleBias
#   define NormalMapUVScaleBias UVScaleBias2
#endif

#ifndef OcclusionUVScaleBias
#   define OcclusionUVScaleBias UVScaleBias3
#endif

#ifndef EmissiveUVScaleBias
#   define EmissiveUVScaleBias UVScaleBias4
#endif

#endif // _GLTF_PBR_STRUCTURES_FXH_
