#include "HnAxesStructures.fxh"
#include "BasicStructures.fxh"

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};

cbuffer cbCameraAttribs 
{
    CameraAttribs g_CameraAttribs;
}

cbuffer cbConstants
{
    AxesConstants g_Constants;
}

void main(in  uint    VertID : SV_VertexID,
          out PSInput PSIn)
{
    //float3 Pos[12];
    //Pos[0] = float3(-1.0, 0.0, 0.0);
    //Pos[1] = float3( 0.0, 0.0, 0.0);
    //Pos[2] = float3( 0.0, 0.0, 0.0);
    //Pos[3] = float3(+1.0, 0.0, 0.0);

    //Pos[4] = float3(0.0, -1.0, 0.0);
    //Pos[5] = float3(0.0,  0.0, 0.0);
    //Pos[6] = float3(0.0,  0.0, 0.0);
    //Pos[7] = float3(0.0, +1.0, 0.0);

    //Pos[ 8] = float3(0.0, 0.0, -1.0);
    //Pos[ 9] = float3(0.0, 0.0,  0.0);
    //Pos[10] = float3(0.0, 0.0,  0.0);
    //Pos[11] = float3(0.0, 0.0, +1.0);
    float3 Pos;
    Pos.x = (VertID == 0u) ? -1.0 : ((VertID ==  3u) ? +1.0 : 0.0);
    Pos.y = (VertID == 4u) ? -1.0 : ((VertID ==  7u) ? +1.0 : 0.0);
    Pos.z = (VertID == 8u) ? -1.0 : ((VertID == 11u) ? +1.0 : 0.0);

    PSIn.Pos   = mul(mul(float4(Pos, 1.0), g_Constants.Transform), g_CameraAttribs.mViewProj);
    PSIn.Color = g_Constants.AxesColors[VertID / 2u];
    PSIn.Color.a *= 1.0 - length(Pos);
}
