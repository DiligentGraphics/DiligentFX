#include "BasicStructures.fxh"

cbuffer cbBoundBoxAttribs
{
    float4x4 g_BoundBoxTransform;
    float4   g_BoundBoxColor;
}

cbuffer cbCameraAttribs
{
    CameraAttribs g_CameraAttribs;
}

struct BoundBoxVSOutput
{
    float4 Pos   : SV_Position;
    float4 Color : COLOR;
};

void BoundBoxVS(uint id : SV_VertexID,
                out  BoundBoxVSOutput VSOut)
{   
    //               5________________6
    //               /|              /|
    //              / |             / |
    //             /  |            /  |
    //            /   |           /   |
    //          4/____|__________/7   |
    //           |    |__________|____|
    //           |   /1          |    /2
    //           |  /            |   /
    //           | /             |  /
    //           |/              | /
    //           /_______________|/
    //           0               3
    //
    
    if (id < 8u)
    {
        // 0,1, 1,2, 2,3, 3,0
        id = ((id + 1u) >> 1u) & 0x03u;
    }
    else if (id < 16u)
    {
        // 4,5, 5,6, 6,7, 7,4
        id = 4u + (((id + 1u) >> 1u) & 0x03u);
    }
    else
    {
        // 0,4, 1,5, 2,6, 3,7
        id = ((id - 16u) >> 1u) + (id & 0x01u) * 4u;
    }

    float4 BoxCorner;
    BoxCorner.x = (id & 0x02u) == 0u ? 0.0 : 1.0;
    BoxCorner.y = ((id + 1u) & 0x02u) == 0u ? 0.0 : 1.0;
    BoxCorner.z = (id & 0x04u) == 0u ? 0.0 : 1.0;
    BoxCorner.w = 1.0;

    BoxCorner = mul(BoxCorner, g_BoundBoxTransform);
    VSOut.Pos = mul(BoxCorner, g_CameraAttribs.mViewProj);
    
    VSOut.Color = g_BoundBoxColor;
}
