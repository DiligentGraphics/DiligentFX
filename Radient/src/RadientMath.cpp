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

#include "RadientMath.hpp"

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

CameraProjection GetCameraProjection(const RadientCameraComponent& Camera,
                                     float                         Aspect,
                                     bool                          NDCMinusOneToOne)
{
    static constexpr RadientCameraComponent DefaultCamera{};

    CameraProjection Projection;
    Projection.FocalLength        = Camera.FocalLength > 0.f ? Camera.FocalLength : DefaultCamera.FocalLength;
    Projection.HorizontalAperture = Camera.HorizontalAperture > 0.f ? Camera.HorizontalAperture : DefaultCamera.HorizontalAperture;
    Projection.VerticalAperture   = Camera.VerticalAperture > 0.f ? Camera.VerticalAperture : DefaultCamera.VerticalAperture;
    Projection.NearPlaneZ         = Camera.ClippingRange.x > 0.f ? Camera.ClippingRange.x : DefaultCamera.ClippingRange.x;
    Projection.FarPlaneZ          = Camera.ClippingRange.y > Projection.NearPlaneZ ? Camera.ClippingRange.y : Projection.NearPlaneZ + 1.f;

    if (Camera.Projection == RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC)
    {
        Projection.Matrix = float4x4::Ortho(Projection.HorizontalAperture, Projection.VerticalAperture,
                                            Projection.NearPlaneZ, Projection.FarPlaneZ, NDCMinusOneToOne);
    }
    else
    {
        const float FovY  = 2.f * std::atan(Projection.VerticalAperture / (2.f * Projection.FocalLength));
        Projection.Matrix = float4x4::Projection(FovY, Aspect, Projection.NearPlaneZ, Projection.FarPlaneZ, NDCMinusOneToOne);
    }

    // Radient cameras follow OpenUSD/glTF convention and look along local -Z,
    // while Diligent projection helpers expect +Z camera space. Bake the
    // adapter into the projection matrix. With row-vector convention, this is
    // equivalent to Scale(1, 1, -1) * Projection, i.e. negating the third row.
    Projection.Matrix._31 = -Projection.Matrix._31;
    Projection.Matrix._32 = -Projection.Matrix._32;
    Projection.Matrix._33 = -Projection.Matrix._33;
    Projection.Matrix._34 = -Projection.Matrix._34;

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
