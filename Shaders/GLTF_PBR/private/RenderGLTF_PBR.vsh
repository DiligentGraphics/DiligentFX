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

float3x3 inverse3x3(float3x3 M)
{
    float3x3 M_t = transpose(M); 
    // Note that in HLSL, M_t[0] is the first row, while in GLSL, it is the 
    // first column. Luckily, determinant and inverse matrix can be equally 
    // defined through both rows and columns.
    float det = dot(cross(M_t[0], M_t[1]), M_t[2]);
    float3x3 adjugate = float3x3(cross(M_t[1], M_t[2]),
                                 cross(M_t[2], M_t[0]),
                                 cross(M_t[0], M_t[1]));
    return adjugate / det;
}

void main(in  GLTF_VS_Input  VSIn,
          out float4 ClipPos  : SV_Position,
          out float3 WorldPos : WORLD_POS,
          out float3 Normal   : NORMAL,
          out float2 UV0      : UV0,
          out float2 UV1      : UV1) 
{
    matrix CombinedTransform = g_Transforms.NodeMatrix;
	if (g_Transforms.JointCount > 0.0)
    {
		// Mesh is skinned
		matrix SkinMat = 
			VSIn.Weight0.x * g_Transforms.JointMatrix[int(VSIn.Joint0.x)] +
			VSIn.Weight0.y * g_Transforms.JointMatrix[int(VSIn.Joint0.y)] +
			VSIn.Weight0.z * g_Transforms.JointMatrix[int(VSIn.Joint0.z)] +
			VSIn.Weight0.w * g_Transforms.JointMatrix[int(VSIn.Joint0.w)];
        CombinedTransform *= SkinMat;
	}
    
	float4 locPos = mul(CombinedTransform, float4(VSIn.Pos, 1.0));
    float3x3 NormalTransform = float3x3(CombinedTransform[0].xyz, CombinedTransform[1].xyz, CombinedTransform[2].xyz);
    NormalTransform = transpose(inverse3x3(NormalTransform));
	Normal = normalize(mul(NormalTransform, VSIn.Normal));

	WorldPos = locPos.xyz / locPos.w;
	UV0      = VSIn.UV0;
	UV1      = VSIn.UV1;
	ClipPos  = mul(float4(WorldPos, 1.0), g_CameraAttribs.mViewProj);
}
