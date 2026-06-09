/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include "RadientMath.h"
#include "RadientScene.h"
#include "BasicMath.hpp"

#include <cmath>

namespace Diligent
{

constexpr RadientFloat3 operator+(const RadientFloat3& Lhs, const RadientFloat3& Rhs)
{
    return RadientFloat3{Lhs.x + Rhs.x, Lhs.y + Rhs.y, Lhs.z + Rhs.z};
}

constexpr RadientFloat3 operator-(const RadientFloat3& Lhs, const RadientFloat3& Rhs)
{
    return RadientFloat3{Lhs.x - Rhs.x, Lhs.y - Rhs.y, Lhs.z - Rhs.z};
}

constexpr RadientFloat3 operator*(const RadientFloat3& Value, Float32 Scale)
{
    return RadientFloat3{Value.x * Scale, Value.y * Scale, Value.z * Scale};
}

constexpr RadientFloat3 operator*(Float32 Scale, const RadientFloat3& Value)
{
    return RadientFloat3{Value.x * Scale, Value.y * Scale, Value.z * Scale};
}

namespace RadientMath
{

inline constexpr float2 ToFloat2(const RadientFloat2& Value)
{
    return float2{Value.x, Value.y};
}

inline constexpr float3 ToFloat3(const RadientFloat3& Value)
{
    return float3{Value.x, Value.y, Value.z};
}

inline constexpr float4 ToFloat4(const RadientFloat4& Value)
{
    return float4{Value.x, Value.y, Value.z, Value.w};
}

inline bool IsFinite(Float32 Value)
{
    return std::isfinite(Value);
}

inline bool IsFinite(const RadientFloat2& Value)
{
    return IsFinite(Value.x) && IsFinite(Value.y);
}

inline bool IsFinite(const RadientFloat3& Value)
{
    return IsFinite(Value.x) && IsFinite(Value.y) && IsFinite(Value.z);
}

inline bool IsFinite(const RadientFloat4& Value)
{
    return IsFinite(Value.x) && IsFinite(Value.y) && IsFinite(Value.z) && IsFinite(Value.w);
}

inline bool IsFinitePositive(Float32 Value)
{
    return IsFinite(Value) && Value > 0.f;
}

inline bool IsFiniteNonNegative(Float32 Value)
{
    return IsFinite(Value) && Value >= 0.f;
}

inline bool IsFiniteNonNegative(const RadientFloat2& Value)
{
    return IsFiniteNonNegative(Value.x) && IsFiniteNonNegative(Value.y);
}

inline bool IsFiniteNonNegative(const RadientFloat3& Value)
{
    return IsFiniteNonNegative(Value.x) && IsFiniteNonNegative(Value.y) && IsFiniteNonNegative(Value.z);
}

inline bool IsFiniteNonNegative(const RadientFloat4& Value)
{
    return IsFiniteNonNegative(Value.x) && IsFiniteNonNegative(Value.y) && IsFiniteNonNegative(Value.z) && IsFiniteNonNegative(Value.w);
}

inline constexpr Float32 LengthSq(const RadientFloat3& Value)
{
    return Value.x * Value.x + Value.y * Value.y + Value.z * Value.z;
}

inline RadientFloat3 Normalize(const RadientFloat3& Value)
{
    const Float32 LenSq = LengthSq(Value);
    if (LenSq <= 0.f)
        return {};

    return Value * (1.f / std::sqrt(LenSq));
}

inline QuaternionF ToQuaternion(const RadientQuaternion& Value)
{
    return (Value.x != 0.f || Value.y != 0.f || Value.z != 0.f || Value.w != 0.f) ?
        normalize(QuaternionF{Value.x, Value.y, Value.z, Value.w}) :
        QuaternionF{};
}

inline float4x4 ToFloat4x4(const RadientMatrix4x4& Matrix)
{
    return float4x4::MakeMatrix(Matrix.Data);
}

inline RadientMatrix4x4 ToRadientMatrix(const float4x4& Matrix)
{
    return RadientMatrix4x4{Matrix.Data()};
}

inline constexpr RadientFloat3 ToRadientFloat3(const float3& Value)
{
    return RadientFloat3{Value.x, Value.y, Value.z};
}

inline RadientQuaternion ToRadientQuaternion(const QuaternionF& Value)
{
    const QuaternionF Q = Value.q != float4{} ? normalize(Value) : QuaternionF{};
    return RadientQuaternion{Q.q.x, Q.q.y, Q.q.z, Q.q.w};
}

inline RadientMatrix4x4 TransformToMatrix(const RadientTransform& Transform)
{
    const float3      S = ToFloat3(Transform.Scale);
    const float3      T = ToFloat3(Transform.Position);
    const QuaternionF Q = ToQuaternion(Transform.Rotation);

    float4x4 R = Q.ToMatrix();

    // S * R * T for row-vector convention:
    R._11 *= S.x;
    R._12 *= S.x;
    R._13 *= S.x;
    R._21 *= S.y;
    R._22 *= S.y;
    R._23 *= S.y;
    R._31 *= S.z;
    R._32 *= S.z;
    R._33 *= S.z;

    R._41 = T.x;
    R._42 = T.y;
    R._43 = T.z;
    R._44 = 1.f;

    return ToRadientMatrix(R);
}

inline RadientMatrix4x4 MultiplyMatrices(const RadientMatrix4x4& A, const RadientMatrix4x4& B)
{
    return ToRadientMatrix(ToFloat4x4(A) * ToFloat4x4(B));
}

inline bool TryInverseMatrix(const RadientMatrix4x4& Matrix, RadientMatrix4x4& Inverse)
{
    float4x4 InverseMatrix;
    if (!ToFloat4x4(Matrix).TryInverse(InverseMatrix))
        return false;

    Inverse = ToRadientMatrix(InverseMatrix);
    return true;
}

inline RadientTransform MatrixToTransform(const RadientMatrix4x4& Matrix)
{
    const float4x4 M = ToFloat4x4(Matrix);

    RadientTransform Result;

    float3      Position;
    float3      Scale;
    QuaternionF Rotation;
    M.Decompose(Position, Rotation, Scale);

    Result.Position = ToRadientFloat3(Position);
    Result.Rotation = ToRadientQuaternion(Rotation);
    Result.Scale    = ToRadientFloat3(Scale);
    return Result;
}

struct CameraProjection
{
    float4x4 Matrix = float4x4::Identity();

    float NearPlaneZ         = 0.f;
    float FarPlaneZ          = 0.f;
    float FocalLength        = 0.f;
    float HorizontalAperture = 0.f;
    float VerticalAperture   = 0.f;
};

CameraProjection GetCameraProjection(const RadientCameraComponent& Camera,
                                     float                         Aspect,
                                     bool                          NDCMinusOneToOne);

} // namespace RadientMath

} // namespace Diligent
