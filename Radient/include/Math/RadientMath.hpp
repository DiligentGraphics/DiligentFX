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

#include "RadientAssets.h"
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

inline constexpr float4 ToFloat4(const RadientColorRGBA8& Color)
{
    constexpr float Inv255 = 1.f / 255.f;
    return float4{
        Color.r * Inv255,
        Color.g * Inv255,
        Color.b * Inv255,
        Color.a * Inv255,
    };
}

inline constexpr float4 ToFloat4(const RadientBoneIndices4& Indices)
{
    return float4{
        static_cast<Float32>(Indices.x),
        static_cast<Float32>(Indices.y),
        static_cast<Float32>(Indices.z),
        static_cast<Float32>(Indices.w),
    };
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

inline RadientTransform NormalizeTransform(const RadientTransform& Transform)
{
    RadientQuaternion Rotation = Transform.Rotation;

    const float LengthSq = Rotation.x * Rotation.x + Rotation.y * Rotation.y + Rotation.z * Rotation.z + Rotation.w * Rotation.w;
    if (LengthSq > 0.f)
    {
        const float InvLength = 1.f / std::sqrt(LengthSq);
        Rotation.x *= InvLength;
        Rotation.y *= InvLength;
        Rotation.z *= InvLength;
        Rotation.w *= InvLength;
    }
    else
    {
        Rotation = {};
    }

    return {
        Transform.Position,
        Rotation,
        Transform.Scale,
    };
}

inline RadientMatrix4x4 TransformToMatrix(const RadientTransform& Transform)
{
    // Rotation is expected to be normalized when the transform is stored.
    const float sx = Transform.Scale.x;
    const float sy = Transform.Scale.y;
    const float sz = Transform.Scale.z;

    const float tx = Transform.Position.x;
    const float ty = Transform.Position.y;
    const float tz = Transform.Position.z;

    const float x = Transform.Rotation.x;
    const float y = Transform.Rotation.y;
    const float z = Transform.Rotation.z;
    const float w = Transform.Rotation.w;

    const float sx2 = sx + sx;
    const float sy2 = sy + sy;
    const float sz2 = sz + sz;

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;

    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;

    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    // S * R * T for row-vector convention.
    // clang-format off
    return RadientMatrix4x4{
        sx - (yy + zz) * sx2, (xy + wz) * sx2,      (xz - wy) * sx2,      0.0f,
        (xy - wz) * sy2,      sy - (xx + zz) * sy2, (yz + wx) * sy2,      0.0f,
        (xz + wy) * sz2,      (yz - wx) * sz2,      sz - (xx + yy) * sz2, 0.0f,
        tx,                   ty,                   tz,                   1.0f,
    };
    // clang-format on
}

inline RadientMatrix4x4 MultiplyMatrices(
    const RadientMatrix4x4& A,
    const RadientMatrix4x4& B)
{
    RadientMatrix4x4 Result;
    Diligent::MultiplyMatrix4x4(A.Data, B.Data, Result.Data);
    return Result;
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
