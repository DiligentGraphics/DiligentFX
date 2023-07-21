
#ifndef THREAD_GROUP_SIZE
#   define THREAD_GROUP_SIZE 16
#endif

Texture3D<float3> g_tex3DSingleSctrLUT;
Texture3D<float3> g_tex3DHighOrderSctrLUT;

RWTexture3D</*format = rgba16f*/float4> g_rwtex3DMultipleSctr;

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CombineScatteringOrdersCS(uint3 ThreadId  : SV_DispatchThreadID)
{
    // Combine single & higher order scattering into single look-up table
    float3 f3MultipleSctr = g_tex3DSingleSctrLUT.Load( int4(ThreadId, 0) ) + 
                            g_tex3DHighOrderSctrLUT.Load( int4(ThreadId, 0) );
    g_rwtex3DMultipleSctr[ThreadId] = float4(f3MultipleSctr, 0.0);
}
