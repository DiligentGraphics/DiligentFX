#ifndef _SHADER_UTILITIES_FXH_
#define _SHADER_UTILITIES_FXH_

// Transforms camera-space Z to normalized device z coordinate
float CameraZToNormalizedDeviceZ(in float CameraZ, in float4x4 mProj)
{
    // In Direct3D and Vulkan, normalized device z range is [0, +1]
    // In OpengGL, normalized device z range is [-1, +1] (unless GL_ARB_clip_control extension is used to correct this nonsense).
    return MATRIX_ELEMENT(mProj,2,2) + MATRIX_ELEMENT(mProj,3,2) / CameraZ;
}

float CameraZToDepth(in float CameraZ, in float4x4 mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first
    return NormalizedDeviceZToDepth(CameraZToNormalizedDeviceZ(CameraZ, mProj));
}

float NormalizedDeviceZToCameraZ(float NdcZ, in float4x4 mProj)
{
    return MATRIX_ELEMENT(mProj, 3, 2) / (NdcZ - MATRIX_ELEMENT(mProj, 2, 2));
}

float DepthToCameraZ(in float fDepth, in float4x4 mProj)
{
    // Transformations to/from normalized device coordinates are the
    // same in both APIs.
    // However, in GL, depth must be transformed to NDC Z first
    return NormalizedDeviceZToCameraZ(DepthToNormalizedDeviceZ(fDepth), mProj);
}

// Transforms the normal from tangent space to world space using the
// position and UV derivatives.
float3 TransformTangentSpaceNormalGrad(in float3 dPos_dx,     // Position dx derivative
                                       in float3 dPos_dy,     // Position dy derivative
                                       in float2 dUV_dx,      // Normal map UV coordinates dx derivative
                                       in float2 dUV_dy,      // Normal map UV coordinates dy derivative
                                       in float3 MacroNormal, // Macro normal, must be normalized
                                       in float3 TSNormal     // Tangent-space normal
                                       )

{
    float d = dUV_dx.x * dUV_dy.y - dUV_dy.x * dUV_dx.y;
    if (d != 0.0)
    {
       	float3 n = MacroNormal; 
        float3 t = (dUV_dy.y * dPos_dx - dUV_dx.y * dPos_dy) / d;
        t = normalize(t - n * dot(n, t));

        float3 b = normalize(cross(t, n));

        float3x3 tbn = MatrixFromRows(t, b, n);

        return normalize(mul(TSNormal, tbn));
    }
    else
    {
        return MacroNormal;
    }
}

// Transforms the normal from tangent space to world space, without using the
// explicit tangent frame.
float3 TransformTangentSpaceNormal(in float3 Position,    // Vertex position in world space
                                   in float3 MacroNormal, // Macro normal, must be normalized
                                   in float3 TSNormal,    // Tangent-space normal
                                   in float2 NormalMapUV  // Normal map uv coordinates
                                   )
{
    float3 dPos_dx = ddx(Position);
    float3 dPos_dy = ddy(Position);

    float2 dUV_dx = ddx(NormalMapUV);
    float2 dUV_dy = ddy(NormalMapUV);

    return TransformTangentSpaceNormalGrad(dPos_dx, dPos_dy, dUV_dx, dUV_dy, MacroNormal, TSNormal);
}

float2 GetMotionVector(float2 ClipPos, float2 PrevClipPos, float2 Jitter, float2 PrevJitter)
{
    return (ClipPos - Jitter) - (PrevClipPos - PrevJitter);
}

float2 GetMotionVector(float2 ClipPos, float2 PrevClipPos)
{
    return ClipPos - PrevClipPos;
}

float2 TransformDirectionToSphereMapUV(float3 Direction)
{
    float OneOverPi = 0.3183098862;
    return OneOverPi * float2(0.5 * atan2(Direction.z, Direction.x), asin(Direction.y)) + float2(0.5, 0.5);
}

void BasisFromNormal(in  float3 N,
                     out float3 T,
                     out float3 B)
{
    T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    B = cross(T, N);
}
#endif //_SHADER_UTILITIES_FXH_
