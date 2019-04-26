#ifndef _GLTF_PBR_STRUCTURES_FXH_
#define _GLTF_PBR_STRUCTURES_FXH_

#ifdef __cplusplus

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
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
CHECK_STRUCT_ALIGNMENT(GLTFNodeTransforms);



struct GLTFRenderParameters
{
	float Exposure;
	float Gamma;
	float PrefilteredCubeMipLevels;
	float ScaleIBLAmbient;

	int   DebugViewType;
    float Dummy0;
    float Dummy1;
    float Dummy2;
};
CHECK_STRUCT_ALIGNMENT(GLTFRenderParameters);

struct GLTFMaterialInfo
{
	float4  BaseColorFactor;
	float4  EmissiveFactor;
	float4  DiffuseFactor;
	float4  SpecularFactor;

	int     Workflow;
	int     BaseColorTextureSet;
	int     PhysicalDescriptorTextureSet;
	int     NormalTextureSet;	

	int     OcclusionTextureSet;
	int     EmissiveTextureSet;
	float   MetallicFactor;	
	float   RoughnessFactor;	

	int     UseAlphaMask;	
	float   AlphaMaskCutoff;
    float   Dummy0;
    float   Dummy1;
};
CHECK_STRUCT_ALIGNMENT(GLTFMaterialInfo);

#endif // _GLTF_PBR_STRUCTURES_FXH_
