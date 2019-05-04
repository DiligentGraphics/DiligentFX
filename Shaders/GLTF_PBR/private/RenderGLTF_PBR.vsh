#include "BasicStructures.fxh"
#include "GLTF_PBR_Structures.fxh"

struct GLTF_VS_Input
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float2 UV0     : ATTRIB2;
    float2 UV1     : ATTRIB3;
    float4 Joint0  : ATTRIB4;
    float4 Weight0 : ATTRIB5;
};

cbuffer cbCameraAttribs
{
    CameraAttribs g_CameraAttribs;
}

cbuffer cbTransforms
{
    GLTFNodeTransforms g_Transforms;
}

float3x3 InverseTranspose3x3(float3x3 M)
{
    // Note that in HLSL, M_t[0] is the first row, while in GLSL, it is the 
    // first column. Luckily, determinant and inverse matrix can be equally 
    // defined through both rows and columns.
    float det = dot(cross(M[0], M[1]), M[2]);
    float3x3 adjugate = float3x3(cross(M[1], M[2]),
                                 cross(M[2], M[0]),
                                 cross(M[0], M[1]));
    return adjugate / det;
}

void main(in  GLTF_VS_Input  VSIn,
          out float4 ClipPos  : SV_Position,
          out float3 WorldPos : WORLD_POS,
          out float3 Normal   : NORMAL,
          out float2 UV0      : UV0,
          out float2 UV1      : UV1) 
{
    float4x4 Transform = g_Transforms.NodeMatrix;
	if (g_Transforms.JointCount > 0)
    {
		// Mesh is skinned
		float4x4 SkinMat = 
			VSIn.Weight0.x * g_Transforms.JointMatrix[int(VSIn.Joint0.x)] +
			VSIn.Weight0.y * g_Transforms.JointMatrix[int(VSIn.Joint0.y)] +
			VSIn.Weight0.z * g_Transforms.JointMatrix[int(VSIn.Joint0.z)] +
			VSIn.Weight0.w * g_Transforms.JointMatrix[int(VSIn.Joint0.w)];
        Transform = mul(Transform, SkinMat);
	}
    
	float4 locPos = mul(Transform, float4(VSIn.Pos, 1.0));
    float3x3 NormalTransform = float3x3(Transform[0].xyz, Transform[1].xyz, Transform[2].xyz);
    NormalTransform = InverseTranspose3x3(NormalTransform);
    Normal = mul(NormalTransform, VSIn.Normal);
    float NormalLen = length(Normal);
    Normal /= max(NormalLen, 1e-5);

	WorldPos = locPos.xyz / locPos.w;
	UV0      = VSIn.UV0;
	UV1      = VSIn.UV1;
	ClipPos  = mul(float4(WorldPos, 1.0), g_CameraAttribs.mViewProj);
}
