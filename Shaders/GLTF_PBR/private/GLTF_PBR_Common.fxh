
struct GLTF_VS_Output
{
    float4 Pos      : SV_Position;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float2 UV0      : UV0;
    float2 UV1      : UV1;
};
