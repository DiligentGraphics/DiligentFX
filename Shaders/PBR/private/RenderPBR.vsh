#include "BasicStructures.fxh"
#include "VertexProcessing.fxh"
#include "PBR_Structures.fxh"
#include "RenderPBRCommon.fxh"

#ifndef MAX_JOINT_COUNT
#   define MAX_JOINT_COUNT 64
#endif

struct VertexAttribs
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV0    : ATTRIB2;
    float2 UV1    : ATTRIB3;
};

#if MAX_JOINT_COUNT > 0
struct SkinningAttribs
{
    float4 Joint0  : ATTRIB4;
    float4 Weight0 : ATTRIB5;
};
#endif

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

void VSMainInternal(in  VertexAttribs   Vert,
#if MAX_JOINT_COUNT > 0
                    in  SkinningAttribs Skinning,
#endif
                    out PbrVsOutput     VSOut)
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
            Skinning.Weight0.x * g_Joints[int(Skinning.Joint0.x)] +
            Skinning.Weight0.y * g_Joints[int(Skinning.Joint0.y)] +
            Skinning.Weight0.z * g_Joints[int(Skinning.Joint0.z)] +
            Skinning.Weight0.w * g_Joints[int(Skinning.Joint0.w)];
        Transform = mul(Transform, SkinMat);
    }
#endif

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(Vert.Pos, Vert.Normal, Transform);

    VSOut.ClipPos  = mul(float4(TransformedVert.WorldPos, 1.0), g_CameraAttribs.mViewProj);
    VSOut.WorldPos = TransformedVert.WorldPos;
    VSOut.Normal   = TransformedVert.Normal;
    VSOut.UV0      = Vert.UV0;
    VSOut.UV1      = Vert.UV1;
}

#if MAX_JOINT_COUNT > 0

void VSMainSkinned(in  VertexAttribs   Vert,
                   in  SkinningAttribs Skinning,
                   out PbrVsOutput     VSOut)
{
    VSMainInternal(Vert, Skinning, VSOut);
}

#else

void VSMain(in  VertexAttribs Vert,
            out PbrVsOutput   VSOut)
{
    VSMainInternal(Vert, VSOut);
}

#endif
