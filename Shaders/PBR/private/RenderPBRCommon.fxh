#ifndef _RENDER_PBR_COMMON_FXH_
#define _RENDER_PBR_COMMON_FXH_

struct PbrVsOutput
{
    float4 ClipPos  : SV_Position;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float2 UV0      : UV0;
    float2 UV1      : UV1;
};


#endif // _RENDER_PBR_COMMON_FXH_
