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

#include "Math/RadientMath.hpp"

#include <algorithm>
#include <cmath>

namespace Diligent
{

namespace
{

RadientQuaternion MakeUnitAxisRotation(Float32 X, Float32 Y, Float32 Z, Float32 Angle)
{
    const Float32 HalfAngle = Angle * 0.5f;
    const Float32 SinAngle  = std::sin(HalfAngle);
    const Float32 CosAngle  = std::cos(HalfAngle);
    return {X * SinAngle, Y * SinAngle, Z * SinAngle, CosAngle};
}

} // namespace

void MakeAxisRotation(const RadientFloat3& Axis, Float32 Angle, RadientQuaternion& Rotation)
{
    const Float32 AxisLengthSq = Axis.x * Axis.x + Axis.y * Axis.y + Axis.z * Axis.z;
    if (AxisLengthSq == 0.f)
    {
        Rotation = {};
        return;
    }

    const Float32 InvAxisLength = (AxisLengthSq != 1.f) ?
        1.f / std::sqrt(AxisLengthSq) :
        1.f;
    Rotation                    = MakeUnitAxisRotation(Axis.x * InvAxisLength,
                                                       Axis.y * InvAxisLength,
                                                       Axis.z * InvAxisLength,
                                                       Angle);
}

void MakeXRotation(Float32 Angle, RadientQuaternion& Rotation)
{
    Rotation = MakeUnitAxisRotation(1.f, 0.f, 0.f, Angle);
}

void MakeYRotation(Float32 Angle, RadientQuaternion& Rotation)
{
    Rotation = MakeUnitAxisRotation(0.f, 1.f, 0.f, Angle);
}

void MakeZRotation(Float32 Angle, RadientQuaternion& Rotation)
{
    Rotation = MakeUnitAxisRotation(0.f, 0.f, 1.f, Angle);
}

namespace RadientMath
{

RadientFloat3 Lerp(const RadientFloat3& Start,
                   const RadientFloat3& End,
                   Float32              Factor) noexcept
{
    return Start * (1.f - Factor) + End * Factor;
}

RadientQuaternion Slerp(const RadientQuaternion& Start,
                        const RadientQuaternion& End,
                        Float32                  Factor) noexcept
{
    const RadientQuaternion Q0 = Normalize(Start);
    RadientQuaternion       Q1 = Normalize(End);
    Float32 Dot = Q0.x * Q1.x + Q0.y * Q1.y + Q0.z * Q1.z + Q0.w * Q1.w;

    if (Dot < 0.f)
    {
        Dot  = -Dot;
        Q1.x = -Q1.x;
        Q1.y = -Q1.y;
        Q1.z = -Q1.z;
        Q1.w = -Q1.w;
    }

    Dot = std::min(Dot, 1.f);
    if (Dot > 0.9995f)
    {
        return Normalize(RadientQuaternion{
            Q0.x + (Q1.x - Q0.x) * Factor,
            Q0.y + (Q1.y - Q0.y) * Factor,
            Q0.z + (Q1.z - Q0.z) * Factor,
            Q0.w + (Q1.w - Q0.w) * Factor,
        });
    }

    const Float32 Angle    = std::acos(Dot);
    const Float32 SinAngle = std::sin(Angle);
    const Float32 W0       = std::sin((1.f - Factor) * Angle) / SinAngle;
    const Float32 W1       = std::sin(Factor * Angle) / SinAngle;
    return Normalize(RadientQuaternion{
        Q0.x * W0 + Q1.x * W1,
        Q0.y * W0 + Q1.y * W1,
        Q0.z * W0 + Q1.z * W1,
        Q0.w * W0 + Q1.w * W1,
    });
}

namespace
{

void GetCubicHermiteWeights(Float32  Factor,
                            Float32  Duration,
                            Float32& StartValueWeight,
                            Float32& StartTangentWeight,
                            Float32& EndValueWeight,
                            Float32& EndTangentWeight) noexcept
{
    const Float32 Factor2 = Factor * Factor;
    const Float32 Factor3 = Factor2 * Factor;
    StartValueWeight      = 2.f * Factor3 - 3.f * Factor2 + 1.f;
    StartTangentWeight    = (Factor3 - 2.f * Factor2 + Factor) * Duration;
    EndValueWeight        = -2.f * Factor3 + 3.f * Factor2;
    EndTangentWeight      = (Factor3 - Factor2) * Duration;
}

} // namespace

RadientFloat3 CubicHermite(const RadientFloat3& Start,
                           const RadientFloat3& StartTangent,
                           const RadientFloat3& End,
                           const RadientFloat3& EndTangent,
                           Float32              Factor,
                           Float32              Duration) noexcept
{
    Float32 StartValueWeight;
    Float32 StartTangentWeight;
    Float32 EndValueWeight;
    Float32 EndTangentWeight;
    GetCubicHermiteWeights(Factor, Duration,
                           StartValueWeight, StartTangentWeight,
                           EndValueWeight, EndTangentWeight);
    return Start * StartValueWeight +
        StartTangent * StartTangentWeight +
        End * EndValueWeight +
        EndTangent * EndTangentWeight;
}

RadientQuaternion CubicHermite(const RadientQuaternion& Start,
                               const RadientQuaternion& StartTangent,
                               const RadientQuaternion& End,
                               const RadientQuaternion& EndTangent,
                               Float32                  Factor,
                               Float32                  Duration) noexcept
{
    Float32 StartValueWeight;
    Float32 StartTangentWeight;
    Float32 EndValueWeight;
    Float32 EndTangentWeight;
    GetCubicHermiteWeights(Factor, Duration,
                           StartValueWeight, StartTangentWeight,
                           EndValueWeight, EndTangentWeight);
    return Normalize(RadientQuaternion{
        Start.x * StartValueWeight + StartTangent.x * StartTangentWeight + End.x * EndValueWeight + EndTangent.x * EndTangentWeight,
        Start.y * StartValueWeight + StartTangent.y * StartTangentWeight + End.y * EndValueWeight + EndTangent.y * EndTangentWeight,
        Start.z * StartValueWeight + StartTangent.z * StartTangentWeight + End.z * EndValueWeight + EndTangent.z * EndTangentWeight,
        Start.w * StartValueWeight + StartTangent.w * StartTangentWeight + End.w * EndValueWeight + EndTangent.w * EndTangentWeight,
    });
}

CameraProjection GetCameraProjection(const RadientCameraComponent& Camera,
                                     float                         Aspect,
                                     bool                          NDCMinusOneToOne,
                                     bool                          UseReverseDepth)
{
    static constexpr RadientCameraComponent DefaultCamera{};

    CameraProjection Projection;
    Projection.FocalLength        = Camera.FocalLength > 0.f ? Camera.FocalLength : DefaultCamera.FocalLength;
    Projection.HorizontalAperture = Camera.HorizontalAperture > 0.f ? Camera.HorizontalAperture : DefaultCamera.HorizontalAperture;
    Projection.VerticalAperture   = Camera.VerticalAperture > 0.f ? Camera.VerticalAperture : DefaultCamera.VerticalAperture;
    Projection.NearPlaneZ         = Camera.ClippingRange.x > 0.f ? Camera.ClippingRange.x : DefaultCamera.ClippingRange.x;
    Projection.FarPlaneZ          = Camera.ClippingRange.y > Projection.NearPlaneZ ? Camera.ClippingRange.y : Projection.NearPlaneZ + 1.f;

    const float MatrixNearPlaneZ = UseReverseDepth ? Projection.FarPlaneZ : Projection.NearPlaneZ;
    const float MatrixFarPlaneZ  = UseReverseDepth ? Projection.NearPlaneZ : Projection.FarPlaneZ;

    if (Camera.Projection == RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC)
    {
        Projection.Matrix = float4x4::Ortho(Projection.HorizontalAperture, Projection.VerticalAperture,
                                            MatrixNearPlaneZ, MatrixFarPlaneZ, NDCMinusOneToOne);
    }
    else
    {
        const float FovY  = 2.f * std::atan(Projection.VerticalAperture / (2.f * Projection.FocalLength));
        Projection.Matrix = float4x4::Projection(FovY, Aspect, MatrixNearPlaneZ, MatrixFarPlaneZ, NDCMinusOneToOne);
    }

    return Projection;
}

} // namespace RadientMath

} // namespace Diligent

extern "C"
{

    void Diligent_MakeAxisRotation(const Diligent::RadientFloat3& Axis,
                                   Diligent::Float32              Angle,
                                   Diligent::RadientQuaternion&   Rotation)
    {
        Diligent::MakeAxisRotation(Axis, Angle, Rotation);
    }

    void Diligent_MakeXRotation(Diligent::Float32            Angle,
                                Diligent::RadientQuaternion& Rotation)
    {
        Diligent::MakeXRotation(Angle, Rotation);
    }

    void Diligent_MakeYRotation(Diligent::Float32            Angle,
                                Diligent::RadientQuaternion& Rotation)
    {
        Diligent::MakeYRotation(Angle, Rotation);
    }

    void Diligent_MakeZRotation(Diligent::Float32            Angle,
                                Diligent::RadientQuaternion& Rotation)
    {
        Diligent::MakeZRotation(Angle, Rotation);
    }
}
