#include "BasicStructures.fxh"
#include "VertexProcessing.fxh"
#include "PBR_Structures.fxh"
#include "RenderPBR_Structures.fxh"

#include "VSInputStruct.generated"
//struct VSInput
//{
//    float3 Pos     : ATTRIB0;
//    float3 Normal  : ATTRIB1;
//    float2 UV0     : ATTRIB2;
//    float2 UV1     : ATTRIB3;
//    float4 Joint0  : ATTRIB4;
//    float4 Weight0 : ATTRIB5;
//    float4 Color   : ATTRIB6; // May be float3
//    float3 Tangent : ATTRIB7;
//};

#include "VSOutputStruct.generated"
// struct VSOutput
// {
//     float4 ClipPos     : SV_Position;
//     float3 WorldPos    : WORLD_POS;
//     float4 Color       : COLOR;
//     float3 Normal      : NORMAL;
//     float2 UV0         : UV0;
//     float2 UV1         : UV1;
//     float3 Tangent     : TANGENT;
//     float4 PrevClipPos : PREV_CLIP_POS;
// };

#ifndef MAX_JOINT_COUNT
#   define MAX_JOINT_COUNT 64
#endif

cbuffer cbFrameAttribs 
{
    PBRFrameAttribs g_Frame;
}

cbuffer cbPrimitiveAttribs
{
#if PRIMITIVE_ARRAY_SIZE > 0
    PBRPrimitiveAttribs g_Primitive[PRIMITIVE_ARRAY_SIZE];
#else
    PBRPrimitiveAttribs g_Primitive;
#endif
}


#if PRIMITIVE_ARRAY_SIZE > 0
// PRIMITIVE_ID is defined by the host as gl_DrawID or gl_DrawIDARB
#   define PRIMITIVE g_Primitive[PRIMITIVE_ID]
#else
#   define PRIMITIVE g_Primitive
#endif


#if MAX_JOINT_COUNT > 0 && USE_JOINTS
struct SkinnigData
{
#if USE_SKIN_PRE_TRANSFORM
    float4x4 PreTransform;
#   if COMPUTE_MOTION_VECTORS
        float4x4 PrevPreTransform;
#   endif
#endif

#   if COMPUTE_MOTION_VECTORS
        float4x4 Joints[MAX_JOINT_COUNT * 2];
#   else
        float4x4 Joints[MAX_JOINT_COUNT];
#   endif
};

cbuffer cbJointTransforms
{
    SkinnigData g_Skin;
}
#endif

float4 GetVertexColor(float3 Color)
{
    return float4(Color, 1.0);
}

float4 GetVertexColor(float4 Color)
{
    return Color;
}

void main(in  VSInput  VSIn,
          out VSOutput VSOut)
{
    // Warning: moving this block into GLTF_TransformVertex() function causes huge
    // performance degradation on Vulkan because glslang/SPIRV-Tools are apparently not able
    // to eliminate the copy of g_Transforms structure.
    float4x4 Transform = PRIMITIVE.Transforms.NodeMatrix;

#if COMPUTE_MOTION_VECTORS
    float4x4 PrevTransform = PRIMITIVE.PrevNodeMatrix;
#endif
    
#if MAX_JOINT_COUNT > 0 && USE_JOINTS
    int JointCount = PRIMITIVE.Transforms.JointCount;
    if (JointCount > 0)
    {
        // Mesh is skinned
        float4x4 SkinMat = 
            VSIn.Weight0.x * g_Skin.Joints[int(VSIn.Joint0.x)] +
            VSIn.Weight0.y * g_Skin.Joints[int(VSIn.Joint0.y)] +
            VSIn.Weight0.z * g_Skin.Joints[int(VSIn.Joint0.z)] +
            VSIn.Weight0.w * g_Skin.Joints[int(VSIn.Joint0.w)];
        Transform = mul(SkinMat, Transform);
#       if USE_SKIN_PRE_TRANSFORM
        {
            Transform = mul(g_Skin.PreTransform, Transform);
        }
#       endif
    
#       if COMPUTE_MOTION_VECTORS
        {
            float4x4 PrevSkinMat = 
                VSIn.Weight0.x * g_Skin.Joints[JointCount + int(VSIn.Joint0.x)] +
                VSIn.Weight0.y * g_Skin.Joints[JointCount + int(VSIn.Joint0.y)] +
                VSIn.Weight0.z * g_Skin.Joints[JointCount + int(VSIn.Joint0.z)] +
                VSIn.Weight0.w * g_Skin.Joints[JointCount + int(VSIn.Joint0.w)];
            PrevTransform = mul(PrevSkinMat, PrevTransform);
#           if USE_SKIN_PRE_TRANSFORM
            {
                PrevTransform = mul(g_Skin.PrevPreTransform, PrevTransform);
            }
#           endif    
        }
#       endif
    }
#endif

#if USE_VERTEX_NORMALS
    float3 Normal = VSIn.Normal;
#else
    float3 Normal = float3(0.0, 0.0, 1.0);
#endif

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(VSIn.Pos, Normal, Transform);    
    VSOut.ClipPos = mul(float4(TransformedVert.WorldPos, 1.0), g_Frame.Camera.mViewProj);

#if COMPUTE_MOTION_VECTORS
    GLTF_TransformedVertex PrevTransformedVert = GLTF_TransformVertex(VSIn.Pos, Normal, PrevTransform);
    VSOut.PrevClipPos  = mul(float4(PrevTransformedVert.WorldPos, 1.0), g_Frame.PrevCamera.mViewProj);
#endif  
    
    VSOut.WorldPos = TransformedVert.WorldPos;

#if USE_VERTEX_COLORS
    VSOut.Color    = GetVertexColor(VSIn.Color);
#endif

#if USE_VERTEX_NORMALS
    VSOut.Normal   = TransformedVert.Normal;
#endif

#if USE_TEXCOORD0
    VSOut.UV0      = VSIn.UV0;
#endif

#if USE_TEXCOORD1
    VSOut.UV1      = VSIn.UV1;
#endif
    
#if USE_VERTEX_TANGENTS
    VSOut.Tangent  = normalize(mul(VSIn.Tangent, float3x3(Transform[0].xyz, Transform[1].xyz, Transform[2].xyz)));
#endif

#ifdef USE_GL_POINT_SIZE
#   if defined(GLSL) || defined(GL_ES)
        // If gl_PointSize is not defined, points are not rendered in GLES
        gl_PointSize = g_Frame.Renderer.PointSize;
#   else
        VSOut.PointSize = g_Frame.Renderer.PointSize;
#   endif
#endif
    
#if PRIMITIVE_ARRAY_SIZE > 0
    VSOut.PrimitiveID = PRIMITIVE_ID;
#endif
}
