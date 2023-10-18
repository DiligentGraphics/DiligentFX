struct PSInput
{
    float4 Pos : SV_POSITION;
};
void main(in  uint    VertID : SV_VertexID,
          out PSInput PSIn)
{
    float2 ClipXY[3];
    ClipXY[0] = float2(-1.0, -1.0);
    ClipXY[1] = float2(-1.0,  3.0);
    ClipXY[2] = float2( 3.0, -1.0);

    PSIn.Pos = float4(ClipXY[VertID], 0.0, 1.0);
}
