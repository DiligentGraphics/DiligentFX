#include "BasicStructures.fxh"
#include "GLTF_PBR_Structures.fxh"
#include "GLTF_PBR_Common.fxh"

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

//mat3 inverse3x3( mat3 M )
//{
//    // The original was written in HLSL, but this is GLSL, 
//    // therefore
//    // - the array index selects columns, so M_t[0] is the 
//    //   first row of M, etc.
//    // - the mat3 constructor assembles columns, so 
//    //   cross( M_t[1], M_t[2] ) becomes the first column
//    //   of the adjugate, etc.
//    // - for the determinant, it does not matter whether it is
//    //   computed with M or with M_t; but using M_t makes it
//    //   easier to follow the derivation in the text
//    mat3 M_t = transpose( M ); 
//    float det = dot( cross( M_t[0], M_t[1] ), M_t[2] );
//    mat3 adjugate = mat3( cross( M_t[1], M_t[2] ),
//                          cross( M_t[2], M_t[0] ),
//                          cross( M_t[0], M_t[1] ) );
//    return adjugate / det;
//}

//float4x4 inverse(float4x4 m)
//{
//    float n11 = m[0][0], n12 = m[1][0], n13 = m[2][0], n14 = m[3][0];
//    float n21 = m[0][1], n22 = m[1][1], n23 = m[2][1], n24 = m[3][1];
//    float n31 = m[0][2], n32 = m[1][2], n33 = m[2][2], n34 = m[3][2];
//    float n41 = m[0][3], n42 = m[1][3], n43 = m[2][3], n44 = m[3][3];
//
//    float t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
//    float t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
//    float t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
//    float t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
//
//    float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
//    float idet = 1.0f / det;
//
//    float4x4 ret;
//
//    ret[0][0] = t11 * idet;
//    ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
//    ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
//    ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;
//
//    ret[1][0] = t12 * idet;
//    ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
//    ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
//    ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;
//
//    ret[2][0] = t13 * idet;
//    ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
//    ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
//    ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;
//
//    ret[3][0] = t14 * idet;
//    ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
//    ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
//    ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;
//
//    return ret;
//}

void main(in  GLTF_VS_Input  VSIn,
          out GLTF_VS_Output VSOut) 
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
    
	float4 locPos = mul(float4(VSIn.Pos, 1.0), CombinedTransform);
    float3x3 NormalTransform = float3x3(CombinedTransform[0].xyz, CombinedTransform[1].xyz, CombinedTransform[2].xyz);
    //NormalTransform = transpose(inverse(NormalTransform));
	VSOut.Normal = normalize(mul(NormalTransform, VSIn.Normal));
	
	locPos.y       = -locPos.y;
	VSOut.WorldPos = locPos.xyz / locPos.w;
	VSOut.UV0      = VSIn.UV0;
	VSOut.UV1      = VSIn.UV1;
	VSOut.Pos      = mul(transpose(g_CameraAttribs.mViewProj), float4(VSOut.WorldPos, 1.0));
}
