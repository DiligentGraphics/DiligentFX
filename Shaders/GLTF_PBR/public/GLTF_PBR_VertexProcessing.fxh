#ifndef _GLTF_PBR_VERTEX_SHADING_FXH_
#define _GLTF_PBR_VERTEX_SHADING_FXH_

#include "GLTF_PBR_Structures.fxh"

struct GLTF_Vertex
{
    float3 Pos;
    float3 Normal;
    float4 Joint0;
    float4 Weight0;
};

struct GLTF_TransformedVertex
{
    float3 WorldPos;
    float3 Normal;
};


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

GLTF_TransformedVertex GLTF_TransformVertex(GLTF_Vertex              Vert,
                                            GLTFNodeShaderTransforms Transforms)
{
    GLTF_TransformedVertex TransformedVert;

    float4x4 Transform = Transforms.NodeMatrix;
	if (Transforms.JointCount > 0)
    {
		// Mesh is skinned
		float4x4 SkinMat = 
			Vert.Weight0.x * Transforms.JointMatrix[int(Vert.Joint0.x)] +
			Vert.Weight0.y * Transforms.JointMatrix[int(Vert.Joint0.y)] +
			Vert.Weight0.z * Transforms.JointMatrix[int(Vert.Joint0.z)] +
			Vert.Weight0.w * Transforms.JointMatrix[int(Vert.Joint0.w)];
        Transform = mul(Transform, SkinMat);
	}
    
	float4 locPos = mul(Transform, float4(Vert.Pos, 1.0));
    float3x3 NormalTransform = float3x3(Transform[0].xyz, Transform[1].xyz, Transform[2].xyz);
    NormalTransform = InverseTranspose3x3(NormalTransform);
    float3 Normal = mul(NormalTransform, Vert.Normal);
    float NormalLen = length(Normal);
    TransformedVert.Normal = Normal / max(NormalLen, 1e-5);

	TransformedVert.WorldPos = locPos.xyz / locPos.w;

    return TransformedVert;
}

#endif // _GLTF_PBR_VERTEX_SHADING_FXH_
