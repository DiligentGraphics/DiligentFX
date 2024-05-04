#ifndef _COORDINATE_GRID_FXH_
#define _COORDINATE_GRID_FXH_

#include "CoordinateGridStructures.fxh"
#include "ShaderUtilities.fxh"

#if COORDINATE_GRID_INVERTED_DEPTH
    #define DepthNearPlane     1.0
    #define DepthFarPlane      0.0
    #define DepthMin           max
    #define DepthCompare(x, y) ((x)>(y))
#else
    #define DepthNearPlane     0.0
    #define DepthFarPlane      1.0
    #define DepthMin           min
    #define DepthCompare(x, y) ((x)<(y))
#endif // COORDINATE_GRID_INVERTED_DEPTH

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
    NdotD = max(abs(NdotD), 1e-6) * (NdotD > 0.0 ? +1.0 : -1.0);
    return dot(PlaneNormal, (PlaneOrigin - RayWS.Origin)) / NdotD;
}

float MinComponent2(float2 v)
{
    return min(v.x, v.y);
}

float MaxComponent2(float2 v)
{
    return max(v.x, v.y);
}

float4 ComputeGrid(float2 PlanePos, float Scale, float Subdivision, CoordinateGridAttribs GridAttribs)
{
    float2 Coord = PlanePos * Scale;
    float2 Magnitude = fwidth(Coord);

    float3 ThickColor = GridAttribs.GridMajorColor.xyz;
    float3 ThinColor  = GridAttribs.GridMinorColor.xyz;
    float2 LineWidth = 0.5 * Magnitude * GridAttribs.GridLineWidth;

    float LodLevel = max(0.0, log10(length(Magnitude) * GridAttribs.GridMinCellWidth / GridAttribs.GridMinCellSize) + 1.0);
    float LodFade = frac(LodLevel);

    float Lod[3];
    Lod[0] = GridAttribs.GridMinCellSize * pow(Subdivision, floor(LodLevel));
    Lod[1] = Lod[0] * Subdivision;
    Lod[2] = Lod[1] * Subdivision;

    float LodAlpha[3];
    for (int LodIdx = 0; LodIdx < 3; LodIdx++) 
        LodAlpha[LodIdx] = MaxComponent2(float2(1.0, 1.0) - saturate(abs((fmod(abs(Coord - 0.5 * Lod[LodIdx]), Lod[LodIdx]) - 0.5 * Lod[LodIdx]) / LineWidth)));

    return float4(LodAlpha[2] > 0.0 ? ThickColor  : LodAlpha[1] > 0.0 ? lerp(ThickColor, ThinColor, LodFade) : ThinColor,
                  LodAlpha[2] > 0.0 ? LodAlpha[2] : LodAlpha[1] > 0.0 ? LodAlpha[1] : LodAlpha[0] * (1.0 - LodFade));
}

float ComputeAxisAlpha(float3   AxisDirection, 
                       Ray      ViewRay,
                       float    AxisLen,
                       float    PixelSize,
                       float    MaxCameraZ,
                       float    CameraZRange,
                       float4x4 CameraView)
{
    float3 AxisOrigin = float3(0.0, 0.0, 0.0);

    float3 Cross = cross(AxisDirection, ViewRay.Direction);
    float3 Delta = ViewRay.Origin - AxisOrigin;
    float  Denom = dot(Cross, Cross);
    if (abs(Denom) < 1e-7)
        return 0.0;

    // Distance from the camera to the point on the camera ray that is closest to the axis ray
    float DistFromCamera = dot(cross(Delta, AxisDirection), Cross) / Denom;
    if (DistFromCamera < 0.0)
    {
        // Closest point is behind the camera
        return 0.0;
    }
    
    // Distance from the origin of the axis to the point on the axis ray that is closest to the camera ray
    float DistFromOrigin = dot(cross(Delta, ViewRay.Direction), Cross) / Denom;
    // Point on the axis ray that is closest to the camera ray
    float3 AxisPos = AxisOrigin + AxisDirection * DistFromOrigin;

    // Shortest distance between the axis and the view ray
    float  DistToAxis = abs(dot(Delta, Cross)) / max(length(Cross), 0.001);

    // Axis width in world space
    float AxisWidth = PixelSize * DistFromCamera;       
    float Line = abs(DistToAxis) / AxisWidth;
    float Alpha = (1.0 - min(Line * Line, 1.0)) * saturate(1.0 - DistFromCamera/AxisLen);

    float AxisPosZ = mul(float4(AxisPos, 1.0), CameraView).z;
    // Move the point along the view direction to alleviate z-fighting with the geometry
    AxisPosZ += AxisWidth;
    // Compute smooth visibility
    // Note: using minimum depth when TAA is enabled looks bad from the distance
    //       when there is small geometry (e.g. bicycle while spokes)
    float Visibility = saturate((MaxCameraZ - AxisPosZ) / CameraZRange);
    Alpha *= Visibility;
    
    // Make axis fade out when looking straight along it
    Alpha *= saturate((1.0 - abs(dot(normalize(ViewRay.Origin), AxisDirection))) * 1e+6);

    return Alpha;
}

void ComputePlaneIntersectionAttribs(in CameraAttribs Camera,
                                     in Ray           RayWS,
                                     in float3        Normal,
                                     in float         MaxCameraZ,
                                     in float         CameraZRange,
                                     out float3       Position,
                                     out float        Alpha)
{
    float DistToPlane = ComputeRayPlaneIntersection(RayWS, Normal, float3(0.0, 0.0, 0.0));
    Alpha = DistToPlane > 0.0 ? 1.0 : 0.0;
        
    Position = RayWS.Origin + RayWS.Direction * DistToPlane; 
    float CameraZ = mul(float4(Position, 1.0), Camera.mView).z;

    // Note: using minimum depth when TAA is enabled looks bad from the distance
    //       when there is small geometry (e.g. bicycle while spokes)
    // Add bias to avoid z-fighting with geometry in the plane
    float Visibility = saturate((MaxCameraZ - CameraZ) / CameraZRange + 0.1);
    Alpha *= Visibility;
    
    // Attenuate alpha based on the CameraZ to make the grid fade out in the distance
    Alpha *= saturate(1.0 - CameraZ / Camera.fFarPlaneZ);
}

float4 ComputeCoordinateGrid(in float2                f2NormalizedXY,
                             in CameraAttribs         Camera,
                             in float                 MinDepth,
                             in float                 MaxDepth,
                             in CoordinateGridAttribs GridAttribs)
{
    Ray RayWS = CreateCameraRay(f2NormalizedXY, Camera.mViewProjInv, Camera.f4Position.xyz);

    float3 Positions[3];
    float  PlaneAlpha[3];
    
    float PixelSize = length(Camera.f4ViewportSize.zw);
    float MinCameraZ = DepthToCameraZ(MinDepth, Camera.mProj);
    float MaxCameraZ = DepthToCameraZ(MaxDepth, Camera.mProj);
    float CameraZRange = max(MaxCameraZ - MinCameraZ, 1e-6);

    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(1.0, 0.0, 0.0), MaxCameraZ, CameraZRange, Positions[0], PlaneAlpha[0]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 1.0, 0.0), MaxCameraZ, CameraZRange, Positions[1], PlaneAlpha[1]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 0.0, 1.0), MaxCameraZ, CameraZRange, Positions[2], PlaneAlpha[2]);

    float4 GridResult = float4(0.0, 0.0, 0.0, 0.0);
    float4 AxisResult = float4(0.0, 0.0, 0.0, 0.0);
    
#if COORDINATE_GRID_AXIS_X
    {
        AxisResult += float4(GridAttribs.XAxisColor.xyz, 1.0) *
                      ComputeAxisAlpha(float3(1.0, 0.0, 0.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.XAxisWidth, MaxCameraZ, CameraZRange, Camera.mView);
    }
#endif

#if COORDINATE_GRID_AXIS_Y
    {
        AxisResult += float4(GridAttribs.YAxisColor.xyz, 1.0) *
                      ComputeAxisAlpha(float3(0.0, 1.0, 0.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.YAxisWidth, MaxCameraZ, CameraZRange, Camera.mView);
    }
#endif

#if COORDINATE_GRID_AXIS_Z
    {
        AxisResult += float4(GridAttribs.ZAxisColor.xyz, 1.0) *
                      ComputeAxisAlpha(float3(0.0, 0.0, 1.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.ZAxisWidth, MaxCameraZ, CameraZRange, Camera.mView);
    }
#endif

#if COORDINATE_GRID_PLANE_YZ
    {
        GridResult += ComputeGrid(Positions[0].yz, GridAttribs.GridScale.x, GridAttribs.GridSubdivision.x, GridAttribs) * PlaneAlpha[0];
    }
#endif 

#if COORDINATE_GRID_PLANE_XZ
    {
        GridResult += ComputeGrid(Positions[1].xz, GridAttribs.GridScale.y, GridAttribs.GridSubdivision.y, GridAttribs) * PlaneAlpha[1];
    }
#endif

#if COORDINATE_GRID_PLANE_XY
    {
        GridResult += ComputeGrid(Positions[2].xy, GridAttribs.GridScale.z, GridAttribs.GridSubdivision.z, GridAttribs) * PlaneAlpha[2]; 
    }
#endif

    float4 Result;
    Result.rgb = GridResult.rgb * exp(-10.0 * AxisResult.a * AxisResult.a) + AxisResult.rgb;
    Result.a   = GridResult.a * (1.0 - AxisResult.a) + AxisResult.a;

    return Result;
}

#endif //_COORDINATE_GRID_FXH_
