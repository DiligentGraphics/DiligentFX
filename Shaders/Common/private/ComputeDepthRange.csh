#include "BasicStructures.fxh"
#include "ComputeDepthRangeStructs.fxh"
#include "ShaderUtilities.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

Texture2D<float>                g_Depth;
RWStructuredBuffer<DepthRangeI> g_DepthRange;

groupshared float2 g_MinMaxDepth[THREAD_GROUP_SIZE][THREAD_GROUP_SIZE];

void UpdateMinMaxDepth(uint2 PixelCoords, uint2 DepthDim, inout float MinDepth, inout float MaxDepth)
{
    PixelCoords = min(PixelCoords, DepthDim - uint2(1u, 1u));
    float D = g_Depth.Load(uint3(PixelCoords, 0u));
    // Ignore the background.
    // Note that fFarPlaneDepth is 0.0 if reverse depth is used.
    if (D != g_Camera.fFarPlaneDepth)
    {
        MinDepth = min(MinDepth, D);
        MaxDepth = max(MaxDepth, D);
    }
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void main(uint3 DispatchID : SV_DispatchThreadID,
          uint3 ThreadID   : SV_GroupThreadID)
{
    uint2 DepthDim;
    g_Depth.GetDimensions(DepthDim.x, DepthDim.y);
    
    float MinDepth = 1.0;
    float MaxDepth = 0.0;
    
    // Each thread processes a 2x2 block of pixels
    UpdateMinMaxDepth(DispatchID.xy * 2u + uint2(0u, 0u), DepthDim, MinDepth, MaxDepth);
    UpdateMinMaxDepth(DispatchID.xy * 2u + uint2(0u, 1u), DepthDim, MinDepth, MaxDepth);
    UpdateMinMaxDepth(DispatchID.xy * 2u + uint2(1u, 0u), DepthDim, MinDepth, MaxDepth);
    UpdateMinMaxDepth(DispatchID.xy * 2u + uint2(1u, 1u), DepthDim, MinDepth, MaxDepth);
         
    g_MinMaxDepth[ThreadID.x][ThreadID.y] = float2(MinDepth, MaxDepth);
    GroupMemoryBarrierWithGroupSync();
    
    // Compute the min/max depth for each row of the thread group
    if (ThreadID.x == 0u)
    {
        for (uint i = 1u; i < uint(THREAD_GROUP_SIZE); ++i)
        {
            MinDepth = min(MinDepth, g_MinMaxDepth[i][ThreadID.y].x);
            MaxDepth = max(MaxDepth, g_MinMaxDepth[i][ThreadID.y].y);
        }
        g_MinMaxDepth[0][ThreadID.y] = float2(MinDepth, MaxDepth);
    }
    GroupMemoryBarrierWithGroupSync();
 
    // Compute the min/max depth for the whole thread group
    if (ThreadID.x == 0u && ThreadID.y == 0u)
    {
        for (uint j = 1u; j < uint(THREAD_GROUP_SIZE); ++j)
        {
            MinDepth = min(MinDepth, g_MinMaxDepth[0][j].x);
            MaxDepth = max(MaxDepth, g_MinMaxDepth[0][j].y);
        }
    }
    
    float MinZ = DepthToCameraZ(MinDepth, g_Camera.mProj);
    float MaxZ = DepthToCameraZ(MaxDepth, g_Camera.mProj);
 
    // Reinterpret floats as ints as atomics only work on ints
    if (g_Camera.fNearPlaneDepth <= g_Camera.fFarPlaneDepth)
    {
        InterlockedMin(g_DepthRange[0].iSceneNearDepth, asint(MinDepth));
        InterlockedMax(g_DepthRange[0].iSceneFarDepth, asint(MaxDepth));
        InterlockedMin(g_DepthRange[0].iSceneNearZ, asint(MinZ));
        InterlockedMax(g_DepthRange[0].iSceneFarZ, asint(MaxZ));
    }
    else
    {
        // When reverse depth is used, minimum depth is the far plane and maximum depth is the near plane
        InterlockedMin(g_DepthRange[0].iSceneFarDepth, asint(MinDepth));
        InterlockedMax(g_DepthRange[0].iSceneNearDepth, asint(MaxDepth));
        // MaxZ corresponds to the near plane and MinZ corresponds to the far plane
        InterlockedMin(g_DepthRange[0].iSceneNearZ, asint(MaxZ));
        InterlockedMax(g_DepthRange[0].iSceneFarZ, asint(MinZ));
    }
}
