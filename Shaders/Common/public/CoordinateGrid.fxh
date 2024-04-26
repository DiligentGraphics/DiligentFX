#ifndef _COORDINATE_GRID_FXH_
#define _COORDINATE_GRID_FXH_

#include "CoordinateGridStructures.fxh"
#include "ShaderUtilities.fxh"

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
    NdotD = max(abs(NdotD), 1e-6) * (NdotD > 0.0 ? +1.0 : -1.0);
    return dot(PlaneNormal, (PlaneOrigin - RayWS.Origin)) / NdotD;
}

float4 ComputeGrid(float2 PlanePos, float Scale) 
{
    float2 Coord = PlanePos * Scale; // use the scale variable to set the distance between the lines
    float2 Magnitude = fwidth(Coord);
    float2 Grid = abs(frac(Coord - 0.5) - 0.5) / Magnitude;
    float Line = min(Grid.x, Grid.y);
    return float4(0.2, 0.2, 0.2, 1.0 - min(Line, 1.0));
}


float ComputeDepth(float3 Position, float4x4 CameraViewProj)
{
    float4 Position_NDC = mul(float4(Position, 1.0), CameraViewProj);
    return NormalizedDeviceZToDepth(Position_NDC.z / Position_NDC.w);
}

// Compute axis using the closest distance between the axis and the view ray
#define COORDINATE_AXES_MODE_CLOSEST_DISTANCE 0

// Compute axes using the two planes
#define COORDINATE_AXES_MODE_TWO_PLANES 1

#define COORDINATE_AXES_MODE COORDINATE_AXES_MODE_CLOSEST_DISTANCE

#if COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_TWO_PLANES

float2 ComputeAxisAlphaFromSinglePlane(float Axis, float Width)
{
    float Magnitude = Width * fwidth(Axis);
    Magnitude = max(Magnitude, 1e-10);
    float Line = abs(Axis) / Magnitude;
    float Alpha = 1.0 - min(Line * Line, 1.0);
    
    // Confidence is inversely proportional to the magnitude of the axis as
    // large magnitudes indicate dicontinuities
    float Confidence = Magnitude > 1e-10 ? (1.0 / Magnitude) : 0.0;
    
    return float2(Alpha, Confidence);
}

float ComputeAxisAlphaFromTwoPlanes(float Axis0, float PlaneAlpha0, float PlaneConfidence0,
                                    float Axis1, float PlaneAlpha1, float PlaneConfidence1,
                                    float Width)
{
    // Blend two planes using the idea from multiple importance sampling
    float2 AxisAlpha0 = ComputeAxisAlphaFromSinglePlane(Axis0, Width);
    float2 AxisAlpha1 = ComputeAxisAlphaFromSinglePlane(Axis1, Width);
    AxisAlpha0.y *= PlaneConfidence0;
    AxisAlpha1.y *= PlaneConfidence1;
    return (AxisAlpha0.x * PlaneAlpha0 * AxisAlpha0.y + AxisAlpha1.x * PlaneAlpha1 * AxisAlpha1.y) / max(AxisAlpha0.y + AxisAlpha1.y, 1e-10);
}

#elif COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_CLOSEST_DISTANCE

float ComputeAxisAlphaFromClosestDistance(float3 AxisDirection, Ray ViewRay, float AxisLen, float PixelSize, float GeometryZ, float4x4 CameraView)
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

    // Shortest distance between the axis and the view ray
    float  DistToAxis = abs(dot(Delta, Cross)) / max(length(Cross), 0.001);

    // Axis width in world space
    float AxisWidth = PixelSize * DistFromCamera;       

    float3 AxisPos = AxisOrigin + AxisDirection * DistFromOrigin;
    // Move the point along the view direction to avoid z-fighting with the geometry
    AxisPos += ViewRay.Direction * AxisWidth;

    float Line = abs(DistToAxis) / AxisWidth;
    float Alpha = (1.0 - min(Line * Line, 1.0)) * saturate(1.0 - DistFromCamera/AxisLen);

    // Use smooth depth test
    float AxisPosZ = mul(float4(AxisPos, 1.0), CameraView).z;
    Alpha *= saturate((GeometryZ - AxisPosZ) / AxisWidth);
    
    Alpha *= saturate((1.0 - abs(dot(normalize(ViewRay.Origin), AxisDirection))) * 1e+6);

    return Alpha;
}

#endif

void ComputePlaneIntersectionAttribs(in CameraAttribs Camera,
                                     in Ray           RayWS,
                                     in float3        Normal,
                                     in float         GeometryDepth,
                                     out float3       Position,
                                     out float        Alpha,
                                     out float        Confidence)
{
    float DistToPlane = ComputeRayPlaneIntersection(RayWS, Normal, float3(0.0, 0.0, 0.0));
    // Slightly offset the intersection point to avoid z-fighting with geometry in the plane
    DistToPlane = DistToPlane * (1.0 + 1e-5) + 1e-6;
    
    Position = RayWS.Origin + RayWS.Direction * DistToPlane; 
    float CameraZ = mul(float4(Position, 1.0), Camera.mView).z;
    float Depth   = CameraZToDepth(CameraZ, Camera.mProj);
    
    // Check if the intersection point is visible
    Alpha = DepthCompare(Depth, GeometryDepth) ? 1.0 : 0.0;
    
    // Attenuate alpha based on the CameraZ to make the grid fade out in the distance
    Alpha *= saturate(1.0 - CameraZ / Camera.fFarPlaneZ);
    
    Confidence = DistToPlane > 0.0 ? 1.0 : 0.0;
    // Reduce confidence when camera is close to the plane
    Confidence *= saturate(abs(dot(RayWS.Origin, Normal) / Camera.fNearPlaneZ) - 1.0);
}

float4 ComputeCoordinateGrid(in float2                f2NormalizedXY,
                             in CameraAttribs         Camera,
                             in float                 GeometryDepth,
                             in CoordinateGridAttribs GridAttribs)
{
    Ray RayWS = CreateCameraRay(f2NormalizedXY, Camera.mViewProjInv, Camera.f4Position.xyz);

    float3 Positions[3];
    float  PlaneAlpha[3];
    float  PlaneConfidence[3];
    
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(1.0, 0.0, 0.0), GeometryDepth, Positions[0], PlaneAlpha[0], PlaneConfidence[0]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 1.0, 0.0), GeometryDepth, Positions[1], PlaneAlpha[1], PlaneConfidence[1]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 0.0, 1.0), GeometryDepth, Positions[2], PlaneAlpha[2], PlaneConfidence[2]);

    float4 GridResult = float4(0.0, 0.0, 0.0, 0.0);
    float4 AxisResult = float4(0.0, 0.0, 0.0, 0.0);

#if COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_CLOSEST_DISTANCE
    float PixelSize = length(Camera.f4ViewportSize.zw);
    float GeometryZ = DepthToCameraZ(GeometryDepth, Camera.mProj);
#endif
    
#if GRID_AXES_OPTION_AXIS_X
    {
        float AxisAlpha = 0.0;
#       if COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_TWO_PLANES
        {
            AxisAlpha = ComputeAxisAlphaFromTwoPlanes(Positions[1].z, PlaneAlpha[1], PlaneConfidence[1],
                                                      Positions[2].y, PlaneAlpha[2], PlaneConfidence[2],
                                                      GridAttribs.ZAxisWidth);
        }
#       elif COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_CLOSEST_DISTANCE
        {
            AxisAlpha = ComputeAxisAlphaFromClosestDistance(float3(1.0, 0.0, 0.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.XAxisWidth,
                                                            GeometryZ, Camera.mView);
        }
#       endif
        AxisResult += float4(GridAttribs.XAxisColor.xyz, 1.0) * AxisAlpha;
    }
#endif

#if GRID_AXES_OPTION_AXIS_Y
    {
        float AxisAlpha = 0.0;
#       if COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_TWO_PLANES
        {    
            AxisAlpha = ComputeAxisAlphaFromTwoPlanes(Positions[0].z, PlaneAlpha[0], PlaneConfidence[0],
                                                      Positions[2].x, PlaneAlpha[2], PlaneConfidence[2],
                                                      GridAttribs.YAxisWidth);
        }
#       elif COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_CLOSEST_DISTANCE
        {
            AxisAlpha = ComputeAxisAlphaFromClosestDistance(float3(0.0, 1.0, 0.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.YAxisWidth,
                                                            GeometryZ, Camera.mView);
        }
#       endif
        AxisResult += float4(GridAttribs.YAxisColor.xyz, 1.0) * AxisAlpha;
    }
#endif

#if GRID_AXES_OPTION_AXIS_Z
    {
        float AxisAlpha = 0.0;
#       if COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_TWO_PLANES
        {
           AxisAlpha = ComputeAxisAlphaFromTwoPlanes(Positions[1].x, PlaneAlpha[1], PlaneConfidence[1],
                                                     Positions[0].y, PlaneAlpha[0], PlaneConfidence[0],
                                                     GridAttribs.XAxisWidth);
        }
#       elif COORDINATE_AXES_MODE == COORDINATE_AXES_MODE_CLOSEST_DISTANCE
        {
            AxisAlpha = ComputeAxisAlphaFromClosestDistance(float3(0.0, 0.0, 1.0), RayWS, Camera.fFarPlaneZ, PixelSize * GridAttribs.ZAxisWidth,
                                                            GeometryZ, Camera.mView);
        }
#       endif
        AxisResult += float4(GridAttribs.ZAxisColor.xyz, 1.0) * AxisAlpha;
    }
#endif

#if GRID_AXES_OPTION_PLANE_YZ
    {
        GridResult += (0.2 * ComputeGrid(Positions[0].yz, GridAttribs.GridSubdivision.x * GridAttribs.GridScale.x) +
                       0.8 * ComputeGrid(Positions[0].yz, GridAttribs.GridScale.x)) * PlaneAlpha[0];
    }
#endif 

#if GRID_AXES_OPTION_PLANE_XZ
    {
        GridResult += (0.2 * ComputeGrid(Positions[1].xz, GridAttribs.GridSubdivision.y * GridAttribs.GridScale.y) +
                       0.8 * ComputeGrid(Positions[1].xz, GridAttribs.GridScale.y)) * PlaneAlpha[1]; 
    }
#endif

#if GRID_AXES_OPTION_PLANE_XY
    {
        GridResult += (0.2 * ComputeGrid(Positions[2].xy, GridAttribs.GridSubdivision.z * GridAttribs.GridScale.z) +
                       0.8 * ComputeGrid(Positions[2].xy, GridAttribs.GridScale.z)) * PlaneAlpha[2]; 
    }
#endif

    float4 Result;
    Result.rgb = GridResult.rgb * exp(-10.0 * AxisResult.a * AxisResult.a) + AxisResult.rgb;
    Result.a   = GridResult.a * (1.0 - AxisResult.a) + AxisResult.a;

    return Result;
}

#undef COORDINATE_AXES_MODE
#undef COORDINATE_AXES_MODE_TWO_PLANES
#undef COORDINATE_AXES_MODE_CLOSEST_DISTANCE

#endif //_COORDINATE_GRID_FXH_
