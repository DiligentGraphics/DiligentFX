#include "OIT.fxh"
#include "BasicStructures.fxh"

cbuffer cbFrameAttribs 
{
    CameraAttribs g_Camera;
}

RWStructuredBuffer<uint> g_rwOITLayers;

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
    uint2 ScreenSize = uint2(g_Camera.f4ViewportSize.x, g_Camera.f4ViewportSize.y);
    if (ThreadID.x >= ScreenSize.x ||
        ThreadID.y >= ScreenSize.y)
        return;
    
    uint Offset = GetOITLayerDataOffset(ThreadID.xy, ScreenSize, uint(NUM_OIT_LAYERS));
    for (uint layer = 0; layer < uint(NUM_OIT_LAYERS); ++layer)
    {
        g_rwOITLayers[Offset + layer] = 0xFFFFFFFFu;
    }
}
