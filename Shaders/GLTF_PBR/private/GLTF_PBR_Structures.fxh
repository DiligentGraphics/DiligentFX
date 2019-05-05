#ifndef _GLTF_PBR_STRUCTURES_FXH_
#define _GLTF_PBR_STRUCTURES_FXH_

#ifdef __cplusplus

#   ifndef CHECK_STRUCT_ALIGNMENT
        // Note that semicolon must be part of the macro because standalone ';' may cause shader compilation error
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" );
#   endif

#else

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s)
#   endif

#endif

#ifndef MAX_NUM_JOINTS
#   define MAX_NUM_JOINTS 128
#endif

#define  PBR_WORKFLOW_METALLIC_ROUGHNESS  0
#define  PBR_WORKFLOW_SPECULAR_GLOSINESS  1

struct GLTFNodeTransforms
{
	float4x4 NodeMatrix;
	float4x4 JointMatrix[MAX_NUM_JOINTS];

	int      JointCount;
    float    Dummy0;
    float    Dummy1;
    float    Dummy2;
};
CHECK_STRUCT_ALIGNMENT(GLTFNodeTransforms)



struct GLTFRenderParameters
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
CHECK_STRUCT_ALIGNMENT(GLTFRenderParameters)

struct GLTFMaterialInfo
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
	float   MetallicFactor;
	float   RoughnessFactor;

	int     UseAlphaMask;	
	float   AlphaMaskCutoff;
    float   Dummy0;
    float   Dummy1;
};
CHECK_STRUCT_ALIGNMENT(GLTFMaterialInfo)

#endif // _GLTF_PBR_STRUCTURES_FXH_
