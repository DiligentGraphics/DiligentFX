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

/// \file
/// Defines Radient math and geometry types.

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"

#if DILIGENT_CPP_INTERFACE
#    include <cstring>
#endif

DILIGENT_BEGIN_NAMESPACE(Diligent)

/// Two-component floating-point vector.
struct RadientFloat2
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientFloat2& Rhs) const
    {
        return (x == Rhs.x &&
                y == Rhs.y);
    }

    constexpr bool operator!=(const RadientFloat2& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientFloat2 RadientFloat2;


/// Three-component floating-point vector.
struct RadientFloat3
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientFloat3& Rhs) const
    {
        return (x == Rhs.x &&
                y == Rhs.y &&
                z == Rhs.z);
    }

    constexpr bool operator!=(const RadientFloat3& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientFloat3 RadientFloat3;


/// Four-component floating-point vector.
struct RadientFloat4
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
    Float32 w DEFAULT_INITIALIZER(0.f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientFloat4& Rhs) const
    {
        return (x == Rhs.x &&
                y == Rhs.y &&
                z == Rhs.z &&
                w == Rhs.w);
    }

    constexpr bool operator!=(const RadientFloat4& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientFloat4 RadientFloat4;


/// Quaternion in x, y, z, w order.
struct RadientQuaternion
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
    Float32 w DEFAULT_INITIALIZER(1.f);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientQuaternion& Rhs) const
    {
        return (x == Rhs.x &&
                y == Rhs.y &&
                z == Rhs.z &&
                w == Rhs.w);
    }

    constexpr bool operator!=(const RadientQuaternion& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientQuaternion RadientQuaternion;


/// Affine transform used by the scene graph.
struct RadientTransform
{
    RadientFloat3 Position     DEFAULT_INITIALIZER({});
    RadientQuaternion Rotation DEFAULT_INITIALIZER({});
    RadientFloat3 Scale        DEFAULT_INITIALIZER((RadientFloat3{1.f, 1.f, 1.f}));

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientTransform& Rhs) const
    {
        return (Position == Rhs.Position &&
                Rotation == Rhs.Rotation &&
                Scale == Rhs.Scale);
    }

    constexpr bool operator!=(const RadientTransform& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientTransform RadientTransform;


/// Diligent-style row-major 4x4 matrix.
struct RadientMatrix4x4
{
    Float32 Data[16] DEFAULT_INITIALIZER(
        {1.f, 0.f, 0.f, 0.f,
         0.f, 1.f, 0.f, 0.f,
         0.f, 0.f, 1.f, 0.f,
         0.f, 0.f, 0.f, 1.f});

#if DILIGENT_CPP_INTERFACE
    constexpr RadientMatrix4x4() noexcept = default;

    // clang-format off
    constexpr RadientMatrix4x4(float m00, float m01, float m02, float m03,
                               float m10, float m11, float m12, float m13,
                               float m20, float m21, float m22, float m23,
                               float m30, float m31, float m32, float m33) noexcept :
        // clang-format on
        Data{m00, m01, m02, m03,
             m10, m11, m12, m13,
             m20, m21, m22, m23,
             m30, m31, m32, m33}
    {
    }

    explicit RadientMatrix4x4(const Float32* InData) noexcept
    {
        std::memcpy(Data, InData, sizeof(Data));
    }

    constexpr RadientFloat4 GetRow(Uint32 Row) const
    {
        return RadientFloat4{
            Data[Row * 4u + 0u],
            Data[Row * 4u + 1u],
            Data[Row * 4u + 2u],
            Data[Row * 4u + 3u]};
    }

    bool operator==(const RadientMatrix4x4& Rhs) const
    {
        return std::memcmp(Data, Rhs.Data, sizeof(Data)) == 0;
    }

    bool operator!=(const RadientMatrix4x4& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientMatrix4x4 RadientMatrix4x4;


/// Axis-aligned bounds.
struct RadientBounds
{
    RadientFloat3 Min DEFAULT_INITIALIZER({});
    RadientFloat3 Max DEFAULT_INITIALIZER({});

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientBounds& Rhs) const
    {
        return Min == Rhs.Min &&
            Max == Rhs.Max;
    }

    constexpr bool operator!=(const RadientBounds& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientBounds RadientBounds;


/// Two-dimensional extent.
struct RadientExtent2D
{
    Uint32 Width  DEFAULT_INITIALIZER(0);
    Uint32 Height DEFAULT_INITIALIZER(0);

#if DILIGENT_CPP_INTERFACE
    constexpr bool operator==(const RadientExtent2D& Rhs) const
    {
        return Width == Rhs.Width &&
            Height == Rhs.Height;
    }

    constexpr bool operator!=(const RadientExtent2D& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientExtent2D RadientExtent2D;


#include "../../../DiligentCore/Primitives/interface/DefineGlobalFuncHelperMacros.h"

/// Creates a quaternion that rotates around the specified axis by the specified angle, in radians.
/// If the axis is zero, returns the identity rotation.
void DILIGENT_GLOBAL_FUNCTION(MakeAxisRotation)(const RadientFloat3 REF Axis, Float32 Angle, RadientQuaternion REF Rotation);

/// Creates a quaternion that rotates around the X axis by the specified angle, in radians.
void DILIGENT_GLOBAL_FUNCTION(MakeXRotation)(Float32 Angle, RadientQuaternion REF Rotation);

/// Creates a quaternion that rotates around the Y axis by the specified angle, in radians.
void DILIGENT_GLOBAL_FUNCTION(MakeYRotation)(Float32 Angle, RadientQuaternion REF Rotation);

/// Creates a quaternion that rotates around the Z axis by the specified angle, in radians.
void DILIGENT_GLOBAL_FUNCTION(MakeZRotation)(Float32 Angle, RadientQuaternion REF Rotation);

#if DILIGENT_CPP_INTERFACE

inline RadientQuaternion MakeAxisRotation(const RadientFloat3& Axis, Float32 Angle)
{
    RadientQuaternion Rotation;
    MakeAxisRotation(Axis, Angle, Rotation);
    return Rotation;
}

inline RadientQuaternion MakeXRotation(Float32 Angle)
{
    RadientQuaternion Rotation;
    MakeXRotation(Angle, Rotation);
    return Rotation;
}

inline RadientQuaternion MakeYRotation(Float32 Angle)
{
    RadientQuaternion Rotation;
    MakeYRotation(Angle, Rotation);
    return Rotation;
}

inline RadientQuaternion MakeZRotation(Float32 Angle)
{
    RadientQuaternion Rotation;
    MakeZRotation(Angle, Rotation);
    return Rotation;
}

#endif

#include "../../../DiligentCore/Primitives/interface/UndefGlobalFuncHelperMacros.h"

DILIGENT_END_NAMESPACE // namespace Diligent
