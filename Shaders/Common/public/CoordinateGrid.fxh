#ifndef _COORDINATE_GRID_FXH_
#define _COORDINATE_GRID_FXH_

#include "CoordinateGridStructures.fxh"
#include "PostFX_Common.fxh"

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

Ray CreateCameraRay(float2   NormalizedXY,
                    float4x4 CameraViewProjInv,
                    float3   CameraPosition)
{
    Ray CameraRay;
    float4 RayStart = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthNearPlane), 1.0f), CameraViewProjInv);
    float4 RayEnd   = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthFarPlane),  1.0f), CameraViewProjInv);

    RayStart.xyz /= RayStart.w;
    RayEnd.xyz   /= RayEnd.w;
    CameraRay.Direction = normalize(RayEnd.xyz - RayStart.xyz);
    CameraRay.Origin    = CameraPosition;
    return CameraRay;
}

float ComputeRayPlaneIntersection(Ray RayWS, float3 PlaneNormal, float3 PlaneOrigin)
{
    float NdotD = dot(RayWS.Direction, PlaneNormal);
    return dot(PlaneNormal, (PlaneOrigin - RayWS.Origin)) / NdotD;
}

float4 ComputeGrid(float2 PlanePos, float Scale, bool IsVisible) 
{
    float2 Coord = PlanePos * Scale; // use the scale variable to set the distance between the lines
    float2 Magnitude = fwidth(Coord);
    if (IsVisible)
    {
        float2 Grid = abs(frac(Coord - 0.5) - 0.5) / Magnitude;
        float Line = min(Grid.x, Grid.y);
        return float4(0.2, 0.2, 0.2, 1.0 - min(Line, 1.0));
    }
    else
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
}

float ComputeAxisAlpha(float Axis, bool IsVisible)
{
    float Magnitude = 2.5 * fwidth(Axis);
    float Line = abs(Axis) / min(Magnitude, 1.0);
    return (1.0 - min(Line, 1.0)) * (IsVisible ? 1.0 : 0.0);
}

float ComputeNDCDepth(float3 Position, float4x4 CameraViewProj)
{
    float4 Position_NDC = mul(float4(Position, 1.0), CameraViewProj);
    return Position_NDC.z / Position_NDC.w;
}

float4 ComputeCoordinateGrid(in float2                f2NormalizedXY,
                             in float3                CameraPos,
                             in float4x4              CameraProj,
                             in float4x4              CameraViewProj,
                             in float4x4              CameraViewProjInv,
                             in float                 FarPlaneZ,
                             in float                 GeometryDepth,
                             in CoordinateGridAttribs GridAttribs)
{
    Ray RayWS = CreateCameraRay(f2NormalizedXY, CameraViewProjInv, CameraPos);

    float3 Normals[3];
    Normals[0] = float3(1.0, 0.0, 0.0); // YZ plane
    Normals[1] = float3(0.0, 1.0, 0.0); // XZ plane
    Normals[2] = float3(0.0, 0.0, 1.0); // XY plane

    float3 Positions[3];
    {
        for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx) 
            Positions[PlaneIdx] = RayWS.Origin + RayWS.Direction * ComputeRayPlaneIntersection(RayWS, Normals[PlaneIdx], float3(0, 0, 0)); 
    }

    float Depth[3];
    {
        for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx)
            Depth[PlaneIdx] = ComputeNDCDepth(Positions[PlaneIdx], CameraViewProj);
    }
    
    float DepthAlpha[3];
    {
        for (int PlaneIdx = 0; PlaneIdx < 3; ++PlaneIdx)
            DepthAlpha[PlaneIdx] = saturate(1.0 - DepthToCameraZ(NormalizedDeviceZToDepth(Depth[PlaneIdx]), CameraProj) / FarPlaneZ);
    }

    float4 GridResult = float4(0.0, 0.0, 0.0, 0.0);
    float4 AxisResult = float4(0.0, 0.0, 0.0, 0.0);

#if GRID_AXES_OPTION_AXIS_X
    {
        bool IsVisible = DepthCompare(Depth[1], GeometryDepth);
        AxisResult += float4(GridAttribs.XAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[1].x, IsVisible) * DepthAlpha[1];
    }
#endif

#if GRID_AXES_OPTION_AXIS_Y
    {
        bool IsVisible = DepthCompare(Depth[0], GeometryDepth);
        AxisResult += float4(GridAttribs.YAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[0].z, IsVisible) * DepthAlpha[0];
    }
#endif

#if GRID_AXES_OPTION_AXIS_Z
    {
        bool IsVisible = DepthCompare(Depth[1], GeometryDepth); 
        AxisResult += float4(GridAttribs.ZAxisColor.xyz, 1.0) * ComputeAxisAlpha(Positions[1].z, IsVisible) * DepthAlpha[1];
    }
#endif

#if GRID_AXES_OPTION_PLANE_YZ
    {
        bool IsVisible = DepthCompare(Depth[0], GeometryDepth);
        GridResult += (0.2 * ComputeGrid(Positions[0].yz, GridAttribs.GridSubdivision.x * GridAttribs.GridScale.x, IsVisible) +
                       0.8 * ComputeGrid(Positions[0].yz, GridAttribs.GridScale.x, IsVisible)) * DepthAlpha[0];
    }
#endif 

#if GRID_AXES_OPTION_PLANE_XZ
    {
        bool IsVisible = DepthCompare(Depth[1], GeometryDepth);
        GridResult += (0.2 * ComputeGrid(Positions[1].xz, GridAttribs.GridSubdivision.y * GridAttribs.GridScale.y, IsVisible) +
                       0.8 * ComputeGrid(Positions[1].xz, GridAttribs.GridScale.y, IsVisible)) * DepthAlpha[1]; 
    }
#endif

#if GRID_AXES_OPTION_PLANE_XY
    {
        bool IsVisible = DepthCompare(Depth[2], GeometryDepth);
        GridResult += (0.2 * ComputeGrid(Positions[2].xy, GridAttribs.GridSubdivision.z * GridAttribs.GridScale.z, IsVisible) +
                       0.8 * ComputeGrid(Positions[2].xy, GridAttribs.GridScale.z, IsVisible)) * DepthAlpha[2]; 
    }
#endif

    float4 Result;
    Result.rgb = GridResult.rgb * exp(-10.0 * AxisResult.a * AxisResult.a) + AxisResult.rgb;
    Result.a   = GridResult.a * (1.0 - AxisResult.a) + AxisResult.a;

    return Result;
}

#endif //_COORDINATE_GRID_FXH_
