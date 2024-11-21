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

#if JOINTS_BUFFER_MODE == JOINTS_BUFFER_MODE_UNIFORM

struct SkinnigData
{
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

float4x4 GetJointMatrix(int JointIndex, int FirstJoint)
{
    return g_Skin.Joints[JointIndex];
}

#elif JOINTS_BUFFER_MODE == JOINTS_BUFFER_MODE_STRUCTURED

StructuredBuffer<float4x4> g_JointTransforms;

float4x4 GetJointMatrix(int JointIndex, int FirstJoint)
{
    return g_JointTransforms[JointIndex + FirstJoint];
}

#endif // JOINTS_BUFFER_MODE == JOINTS_BUFFER_MODE_STRUCTURED

#endif // MAX_JOINT_COUNT > 0 && USE_JOINTS


float4 GetVertexColor(float3 Color)
{
    return float4(Color, 1.0);
}

float4 GetVertexColor(float4 Color)
{
    return Color;
}

#if PACK_VERTEX_NORMALS
// Reverse of PBR_Renderer::PackVertexNormal()
float3 GetNormal(in uint PackedNormal)
{
    float3 Normal;
    Normal.x = float( PackedNormal         & 0xFFFFu) / 32767.0 - 1.0;
    Normal.y = float((PackedNormal >> 16u) & 0x7FFFu) / 16383.0 - 1.0;
    Normal.z = sqrt(max(1.0 - dot(Normal.xy, Normal.xy), 0.0));
    Normal.z *= (PackedNormal & 0x80000000u) != 0u ? -1.0 : 1.0;
    return Normal;
}
#else
float3 GetNormal(in float3 Normal)
{
    return Normal;
}
#endif

#if VERTEX_POS_PACK_MODE == VERTEX_POS_PACK_MODE_64_BIT
// Reverse of PBR_Renderer::PackVertexPos64()
float3 GetPosition(VSInput VSIn)
{
    uint2 PackedPos = VSIn.Pos;
    
    //       PackedPos.x               PackedPos.y
    //      X           Y             Y		     Z
    // | 0 ... 20 | 21 ... 31|   | 0 ... 9 | 10 ... 30 |  |
    //      21         11             10         21     31
    float U21Scale = 1.0 / 2097151.0; // 2^21 - 1
    uint  U21Mask  = 2097151u;
    
    float3 Pos;
    Pos.x = float(PackedPos.x & U21Mask) * U21Scale;
    Pos.y = float((PackedPos.x >> 21u) | ((PackedPos.y & 1023u) << 11u)) * U21Scale;
    Pos.z = float(PackedPos.y >> 10u) * U21Scale;
    
    GLTFNodeShaderTransforms PrimTransforms = PRIMITIVE.Transforms;
    float3 PosScale = float3(PrimTransforms.PosScaleX, PrimTransforms.PosScaleY, PrimTransforms.PosScaleZ);
    float3 PosBias  = float3(PrimTransforms.PosBiasX,  PrimTransforms.PosBiasY,  PrimTransforms.PosBiasZ);
    return Pos * PosScale + PosBias;
}
#else
float3 GetPosition(VSInput VSIn)
{
    return VSIn.Pos;
}
#endif

void main(in  VSInput  VSIn,
          out VSOutput VSOut)
{
    PBRPrimitiveAttribs Primitive = PRIMITIVE;
    
    // Warning: moving this block into GLTF_TransformVertex() function causes huge
    // performance degradation on Vulkan because glslang/SPIRV-Tools are apparently not able
    // to eliminate the copy of g_Transforms structure.
    float4x4 Transform = Primitive.Transforms.NodeMatrix;

#if COMPUTE_MOTION_VECTORS
    float4x4 PrevTransform = Primitive.Transforms.PrevNodeMatrix;
#endif
    
#if MAX_JOINT_COUNT > 0 && USE_JOINTS
    int JointCount = Primitive.Transforms.JointCount;
    int FirstJoint = Primitive.Transforms.FirstJoint;
    if (JointCount > 0)
    {
        // Mesh is skinned
        float4x4 SkinMat = 
            VSIn.Weight0.x * GetJointMatrix(int(VSIn.Joint0.x), FirstJoint) +
            VSIn.Weight0.y * GetJointMatrix(int(VSIn.Joint0.y), FirstJoint) +
            VSIn.Weight0.z * GetJointMatrix(int(VSIn.Joint0.z), FirstJoint) +
            VSIn.Weight0.w * GetJointMatrix(int(VSIn.Joint0.w), FirstJoint);
        Transform = mul(SkinMat, Transform);
#       if USE_SKIN_PRE_TRANSFORM
        {
            Transform = mul(Primitive.Transforms.SkinPreTransform, Transform);
        }
#       endif
    
#       if COMPUTE_MOTION_VECTORS
        {
            float4x4 PrevSkinMat = 
                VSIn.Weight0.x * GetJointMatrix(JointCount + int(VSIn.Joint0.x), FirstJoint) +
                VSIn.Weight0.y * GetJointMatrix(JointCount + int(VSIn.Joint0.y), FirstJoint) +
                VSIn.Weight0.z * GetJointMatrix(JointCount + int(VSIn.Joint0.z), FirstJoint) +
                VSIn.Weight0.w * GetJointMatrix(JointCount + int(VSIn.Joint0.w), FirstJoint);
            PrevTransform = mul(PrevSkinMat, PrevTransform);
#           if USE_SKIN_PRE_TRANSFORM
            {
                PrevTransform = mul(Primitive.Transforms.PrevSkinPreTransform, PrevTransform);
            }
#           endif    
        }
#       endif
    }
#endif

#if USE_VERTEX_NORMALS
    float3 Normal = GetNormal(VSIn.Normal);
#else
    float3 Normal = float3(0.0, 0.0, 1.0);
#endif

    float3 Pos = GetPosition(VSIn);

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(Pos, Normal, Transform);    
    VSOut.ClipPos = mul(float4(TransformedVert.WorldPos, 1.0), g_Frame.Camera.mViewProj);

#if COMPUTE_MOTION_VECTORS
    GLTF_TransformedVertex PrevTransformedVert = GLTF_TransformVertex(Pos, Normal, PrevTransform);
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
