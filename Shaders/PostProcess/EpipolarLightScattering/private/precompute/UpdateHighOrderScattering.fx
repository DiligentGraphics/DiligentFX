
#ifndef THREAD_GROUP_SIZE
#   define THREAD_GROUP_SIZE 16
#endif


Texture3D<float3> g_tex3DCurrentOrderScattering;
Texture3D<float3> g_tex3DHighOrderOrderScattering;

RWTexture3D</*format = rgba16f*/float4> g_rwtex3DHighOrderSctr;

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void UpdateHighOrderScatteringCS(uint3 ThreadId  : SV_DispatchThreadID)
{
    // Accumulate in-scattering using alpha-blending
    float3 f3HighOrderSctr = g_tex3DHighOrderOrderScattering.Load( int4(ThreadId, 0) ) + 
                             g_tex3DCurrentOrderScattering.Load( int4(ThreadId, 0) );
    g_rwtex3DHighOrderSctr[ThreadId] = float4(f3HighOrderSctr, 0.0);
}
