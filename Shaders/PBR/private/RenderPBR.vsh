#include "BasicStructures.fxh"
#include "VertexProcessing.fxh"
#include "PBR_Structures.fxh"
#include "RenderPBRCommon.fxh"
#include "VSInputStruct.generated"

#ifndef MAX_JOINT_COUNT
#   define MAX_JOINT_COUNT 64
#endif

//struct VSInput
//{
//    float3 Pos     : ATTRIB0;
//    float3 Normal  : ATTRIB1;
//    float2 UV0     : ATTRIB2;
//    float2 UV1     : ATTRIB3;
//    float4 Joint0  : ATTRIB4;
//    float4 Weight0 : ATTRIB5;
//};

cbuffer cbCameraAttribs 
{
    CameraAttribs g_CameraAttribs;
}

cbuffer cbPBRAttribs
{
    PBRShaderAttribs g_PBRAttribs;
}

#if MAX_JOINT_COUNT > 0
cbuffer cbJointTransforms
{
    float4x4 g_Joints[MAX_JOINT_COUNT];
}
#endif

void main(in  VSInput     VSIn,
          out PbrVsOutput VSOut)
{
    // Warning: moving this block into GLTF_TransformVertex() function causes huge
    // performance degradation on Vulkan because glslang/SPIRV-Tools are apparently not able
    // to eliminate the copy of g_Transforms structure.
    float4x4 Transform = g_PBRAttribs.Transforms.NodeMatrix;

#if MAX_JOINT_COUNT > 0
    if (g_PBRAttribs.Transforms.JointCount > 0)
    {
        // Mesh is skinned
        float4x4 SkinMat = 
            VSIn.Weight0.x * g_Joints[int(VSIn.Joint0.x)] +
            VSIn.Weight0.y * g_Joints[int(VSIn.Joint0.y)] +
            VSIn.Weight0.z * g_Joints[int(VSIn.Joint0.z)] +
            VSIn.Weight0.w * g_Joints[int(VSIn.Joint0.w)];
        Transform = mul(Transform, SkinMat);
    }
#endif

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(VSIn.Pos, VSIn.Normal, Transform);

    VSOut.ClipPos  = mul(float4(TransformedVert.WorldPos, 1.0), g_CameraAttribs.mViewProj);
    VSOut.WorldPos = TransformedVert.WorldPos;
    VSOut.Normal   = TransformedVert.Normal;
    VSOut.UV0      = VSIn.UV0;
    VSOut.UV1      = VSIn.UV1;
}
