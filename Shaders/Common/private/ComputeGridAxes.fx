#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "SRGBUtilities.fxh"
#include "PostFX_Common.fxh"

#pragma warning(disable : 3078)

#define FLT_EPS                   5.960464478e-8
#define FLT_MAX                   3.402823466e+38
#define FLT_MIN                   1.175494351e-38

#if GRID_AXES_OPTION_INVERTED_DEPTH
    #define DepthNearPlane     1.0
    #define DepthFarPlane      0.0
    #define DepthMin           max
    #define DepthCompare(x, y) ((x)>(y))
#else
    #define DepthNearPlane     0.0
    #define DepthFarPlane      1.0
    #define DepthMin           min
    #define DepthCompare(x, y) ((x)<(y))
#endif // GRID_AXES_OPTION_INVERTED_DEPTH

struct Ray 
{
    float3 Origin;
    float3 Direction;
};

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbGridAxesAttribs
{
    GridAxesRendererAttribs g_GridAxesAttribs;
}

Texture2D<float>  g_TextureDepth;

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

Ray CreateCameraRay(float2 NormalizedXY)
{
    Ray CameraRay;
    float4 RayStart = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthNearPlane), 1.0f), g_Camera.mViewProjInv);
    float4 RayEnd   = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthFarPlane),  1.0f), g_Camera.mViewProjInv);

    RayStart.xyz /= RayStart.w;
    RayEnd.xyz   /= RayEnd.w;
    CameraRay.Direction = normalize(RayEnd.xyz - RayStart.xyz);
    CameraRay.Origin    = g_Camera.f4Position.xyz;
    return CameraRay;
}

float ComputeRayPlaneIntersection(Ray RayWS, float3 PlaneNormal, float3 PlaneOrigin)
{
    float NdotD = dot(RayWS.Direction, PlaneNormal);
    return dot(PlaneNormal, (PlaneOrigin - RayWS.Origin)) / NdotD;
}

float4 ComputeGrid(float2 PlanePos, float Scale) 
{
    float2 Coord = PlanePos * Scale; // use the scale variable to set the distance between the lines
    float2 Magnitude = fwidth(Coord);
    float2 Grid = abs(frac(Coord - 0.5) - 0.5) / Magnitude;
    float Line = min(Grid.x, Grid.y);
    return float4(0.2, 0.2, 0.2, 1.0 - min(Line, 1.0));;
}

float ComputeAxisAlpha(float Axis) 
{
    float Magnitude = 2.5 * fwidth(Axis);
    float Line = abs(Axis) / Magnitude;
    return 1.0 - min(Line, 1.0);
}

float ComputeNDCDepth(float3 Position)
{
    float4 Position_NDC = mul(float4(Position, 1.0), g_Camera.mViewProj);
    return Position_NDC.z / Position_NDC.w;
}

float4 ComputeGridAxesPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    Ray RayWS = CreateCameraRay(VSOut.f2NormalizedXY + g_Camera.f2Jitter);

    float3 Normals[3];
    Normals[0] = float3(1.0, 0.0, 0.0); // YZ plane
    Normals[1] = float3(0.0, 1.0, 0.0); // XZ plane
    Normals[2] = float3(0.0, 0.0, 1.0); // XY plane

    float3 Positions[3];
    for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx)
        Positions[PlaneIdx] = RayWS.Origin + RayWS.Direction *  ComputeRayPlaneIntersection(RayWS, Normals[PlaneIdx], float3(0, 0, 0));

    float Depth[3]; 
    for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx)
        Depth[PlaneIdx] = ComputeNDCDepth(Positions[PlaneIdx]);

    float DepthAlpha[3];
    for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx)
        DepthAlpha[PlaneIdx] = saturate(1.0 - 1.5 * DepthToCameraZ(Depth[PlaneIdx], g_Camera.mProj) / g_Camera.fFarPlaneZ); 
    
    float GeometryDepth = DepthToNormalizedDeviceZ(SampleDepth(int2(VSOut.f4PixelPos.xy)));

    float4 GridResult = float4(0.0, 0.0, 0.0, 0.0);
    float4 AxisResult = float4(0.0, 0.0, 0.0, 0.0);

#if GRID_AXES_OPTION_AXIS_X
    if (DepthCompare(Depth[1], GeometryDepth)) 
        AxisResult += float4(g_GridAxesAttribs.XAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[1].x) * DepthAlpha[1];
#endif

#if GRID_AXES_OPTION_AXIS_Y
    if (DepthCompare(Depth[0], GeometryDepth)) 
        AxisResult += float4(g_GridAxesAttribs.YAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[0].z) * DepthAlpha[0];
#endif

#if GRID_AXES_OPTION_AXIS_Z
    if (DepthCompare(Depth[1], GeometryDepth)) 
        AxisResult += float4(g_GridAxesAttribs.ZAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[1].z) * DepthAlpha[1];
#endif

#if GRID_AXES_OPTION_PLANE_YZ    
    if (DepthCompare(Depth[0], GeometryDepth)) 
        GridResult += (0.2 * ComputeGrid(Positions[0].yz, g_GridAxesAttribs.GridSubdivision.x * g_GridAxesAttribs.GridScale.x) +
                       0.8 * ComputeGrid(Positions[0].yz, g_GridAxesAttribs.GridScale.x)) * DepthAlpha[0];
#endif 

#if GRID_AXES_OPTION_PLANE_XZ 
    if (DepthCompare(Depth[1], GeometryDepth)) 
        GridResult += (0.2 * ComputeGrid(Positions[1].xz, g_GridAxesAttribs.GridSubdivision.y * g_GridAxesAttribs.GridScale.y) +
                       0.8 * ComputeGrid(Positions[1].xz, g_GridAxesAttribs.GridScale.y)) * DepthAlpha[1]; 
#endif 

#if GRID_AXES_OPTION_PLANE_XY  
    if (DepthCompare(Depth[2], GeometryDepth)) 
        GridResult += (0.2 * ComputeGrid(Positions[2].xy,  g_GridAxesAttribs.GridSubdivision.z * g_GridAxesAttribs.GridScale.z) +
                       0.8 * ComputeGrid(Positions[2].xy, g_GridAxesAttribs.GridScale.z)) * DepthAlpha[2]; 
#endif 

    float4 Result;
    Result.rgb = GridResult.rgb * exp(-10.0 * AxisResult.a * AxisResult.a) + AxisResult.rgb;
    Result.a   = GridResult.a * (1.0 - AxisResult.a) + AxisResult.a;

#if GRID_AXES_OPTION_CONVERT_OUTPUT_TO_SRGB
    Result = LinearToSRGB(Result.rgb);
#endif
    return float4(Result.rgb, Result.a);
}
