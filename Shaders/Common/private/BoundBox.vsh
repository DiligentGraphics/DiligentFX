#include "BasicStructures.fxh"
#include "BoundBoxStructures.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbBoundBoxAttribs
{
    BoundBoxAttribs g_Attribs;
}

float4 GetBoxCorner(uint id)
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

    BoxCorner = mul(BoxCorner, g_Attribs.Transform);
    
    return BoxCorner;
}

void BoundBoxVS(uint id : SV_VertexID,
                out  BoundBoxVSOutput VSOut)
{   
    float4 BoxCorner = GetBoxCorner(id);
    float4 EdgeStart = GetBoxCorner(id & ~0x01u);
    
    VSOut.Pos = mul(BoxCorner, g_Camera.mViewProj);

    VSOut.ClipPos = VSOut.Pos;
    VSOut.EdgeStartClipPos = mul(EdgeStart, g_Camera.mViewProj);
#if COMPUTE_MOTION_VECTORS
    VSOut.PrevClipPos = mul(BoxCorner, g_PrevCamera.mViewProj);
#else
    VSOut.PrevClipPos = VSOut.ClipPos;
#endif
}
