#include "BasicStructures.fxh"
#include "GLTF_PBR_VertexProcessing.fxh"

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
    GLTFNodeShaderTransforms g_Transforms;
}
    
void main(in  GLTF_VS_Input  VSIn,
          out float4 ClipPos  : SV_Position,
          out float3 WorldPos : WORLD_POS,
          out float3 Normal   : NORMAL,
          out float2 UV0      : UV0,
          out float2 UV1      : UV1) 
{
    GLTF_Vertex Vert;
    Vert.Pos     = VSIn.Pos;
    Vert.Normal  = VSIn.Normal;
    Vert.Joint0  = VSIn.Joint0;
    Vert.Weight0 = VSIn.Weight0;

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(Vert, g_Transforms);

    ClipPos  = mul(float4(TransformedVert.WorldPos, 1.0), g_CameraAttribs.mViewProj);
    WorldPos = TransformedVert.WorldPos;
    Normal   = TransformedVert.Normal;
    UV0      = VSIn.UV0;
    UV1      = VSIn.UV1;
}
