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

#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include "RadientMath.hpp"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr float EPSILON = 1e-5f;

void ExpectMatrixNear(const RadientMatrix4x4& Matrix,
                      const RadientMatrix4x4& Reference)
{
    for (Uint32 i = 0; i < 16; ++i)
        EXPECT_NEAR(Matrix.Data[i], Reference.Data[i], EPSILON) << "i = " << i;
}

void ExpectFloat3Near(const RadientFloat3& Value,
                      const RadientFloat3& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
    EXPECT_NEAR(Value.z, Reference.z, EPSILON);
}

void ExpectQuaternionNear(const RadientQuaternion& Value,
                          const RadientQuaternion& Reference)
{
    const float Dot =
        Value.x * Reference.x +
        Value.y * Reference.y +
        Value.z * Reference.z +
        Value.w * Reference.w;
    const float Sign = Dot < 0.f ? -1.f : 1.f;

    EXPECT_NEAR(Value.x * Sign, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y * Sign, Reference.y, EPSILON);
    EXPECT_NEAR(Value.z * Sign, Reference.z, EPSILON);
    EXPECT_NEAR(Value.w * Sign, Reference.w, EPSILON);
}

TEST(RadientMathTest, ToFloat2)
{
    const float2 Value = RadientMath::ToFloat2(RadientFloat2{1.f, 2.f});
    EXPECT_EQ(Value.x, 1.f);
    EXPECT_EQ(Value.y, 2.f);
}

TEST(RadientMathTest, ToFloat3)
{
    const float3 Value = RadientMath::ToFloat3(RadientFloat3{1.f, 2.f, 3.f});
    EXPECT_EQ(Value.x, 1.f);
    EXPECT_EQ(Value.y, 2.f);
    EXPECT_EQ(Value.z, 3.f);
}

TEST(RadientMathTest, ToFloat4)
{
    const float4 Value = RadientMath::ToFloat4(RadientFloat4{1.f, 2.f, 3.f, 4.f});
    EXPECT_EQ(Value.x, 1.f);
    EXPECT_EQ(Value.y, 2.f);
    EXPECT_EQ(Value.z, 3.f);
    EXPECT_EQ(Value.w, 4.f);
}

TEST(RadientMathTest, ToQuaternion)
{
    const QuaternionF ZeroQuat = RadientMath::ToQuaternion(RadientQuaternion{0.f, 0.f, 0.f, 0.f});
    EXPECT_EQ(ZeroQuat.q.x, 0.f);
    EXPECT_EQ(ZeroQuat.q.y, 0.f);
    EXPECT_EQ(ZeroQuat.q.z, 0.f);
    EXPECT_EQ(ZeroQuat.q.w, 1.f);

    const QuaternionF NormalizedQuat = RadientMath::ToQuaternion(RadientQuaternion{0.f, 0.f, 0.f, 2.f});
    EXPECT_EQ(NormalizedQuat.q.x, 0.f);
    EXPECT_EQ(NormalizedQuat.q.y, 0.f);
    EXPECT_EQ(NormalizedQuat.q.z, 0.f);
    EXPECT_EQ(NormalizedQuat.q.w, 1.f);
}

TEST(RadientMathTest, RoundTripsMatrixType)
{
    const float4x4 Source{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f};

    const RadientMatrix4x4 RadientMatrix = RadientMath::ToRadientMatrix(Source);
    const float4x4         RoundTrip     = RadientMath::ToFloat4x4(RadientMatrix);

    for (Uint32 i = 0; i < 16; ++i)
        EXPECT_EQ(RoundTrip.Data()[i], Source.Data()[i]);
}

TEST(RadientMathTest, BuildsTransformMatrixWithPositionOnly)
{
    RadientTransform Transform;
    Transform.Position = {1.f, 2.f, 3.f};
    Transform.Scale    = {1.f, 1.f, 1.f};

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);
    const RadientMatrix4x4 Reference{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        1.f, 2.f, 3.f, 1.f};
    EXPECT_EQ(Matrix, Reference);
}

TEST(RadientMathTest, BuildsTransformMatrixWithScaleOnly)
{
    RadientTransform Transform;
    Transform.Scale = {2.f, 3.f, 4.f};

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);
    const RadientMatrix4x4 Reference{
        2.f, 0.f, 0.f, 0.f,
        0.f, 3.f, 0.f, 0.f,
        0.f, 0.f, 4.f, 0.f,
        0.f, 0.f, 0.f, 1.f};
    EXPECT_EQ(Matrix, Reference);
}

TEST(RadientMathTest, BuildsTransformMatrixWithRotationOnly)
{
    RadientTransform Transform;
    Transform.Scale      = {1.f, 1.f, 1.f};
    Transform.Rotation.z = 0.70710678f;
    Transform.Rotation.w = 0.70710678f;

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);
    const RadientMatrix4x4 Reference{
        0.f, 1.f, 0.f, 0.f,
        -1.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f};
    ExpectMatrixNear(Matrix, Reference);
}

TEST(RadientMathTest, BuildsTransformMatrixWithScaleAndPosition)
{
    RadientTransform Transform;
    Transform.Position = {1.f, 2.f, 3.f};
    Transform.Scale    = {2.f, 3.f, 4.f};

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);
    const RadientMatrix4x4 Reference{
        2.f, 0.f, 0.f, 0.f,
        0.f, 3.f, 0.f, 0.f,
        0.f, 0.f, 4.f, 0.f,
        1.f, 2.f, 3.f, 1.f};
    EXPECT_EQ(Matrix, Reference);
}

TEST(RadientMathTest, BuildsTransformMatrixWithScaleRotationAndPosition)
{
    RadientTransform Transform;
    Transform.Position   = {1.f, 2.f, 3.f};
    Transform.Scale      = {2.f, 3.f, 4.f};
    Transform.Rotation.z = 0.70710678f;
    Transform.Rotation.w = 0.70710678f;

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);
    const RadientMatrix4x4 Reference{
        0.f, 2.f, 0.f, 0.f,
        -3.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 4.f, 0.f,
        1.f, 2.f, 3.f, 1.f};
    ExpectMatrixNear(Matrix, Reference);
}

TEST(RadientMathTest, InvertsTransformMatrix)
{
    RadientTransform Transform;
    Transform.Position   = {1.f, 2.f, 3.f};
    Transform.Scale      = {2.f, 3.f, 4.f};
    Transform.Rotation.z = 0.70710678f;
    Transform.Rotation.w = 0.70710678f;

    const RadientMatrix4x4 Matrix = RadientMath::TransformToMatrix(Transform);

    RadientMatrix4x4 Inverse;
    EXPECT_TRUE(RadientMath::TryInverseMatrix(Matrix, Inverse));

    const RadientMatrix4x4 Identity = RadientMath::MultiplyMatrices(Matrix, Inverse);
    const RadientMatrix4x4 Reference{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f};
    ExpectMatrixNear(Identity, Reference);
}

TEST(RadientMathTest, RejectsSingularMatrixInverse)
{
    RadientTransform Transform;
    Transform.Scale = {0.f, 1.f, 1.f};

    RadientMatrix4x4 Inverse;
    EXPECT_FALSE(RadientMath::TryInverseMatrix(RadientMath::TransformToMatrix(Transform), Inverse));
}

TEST(RadientMathTest, DecomposesTransformMatrix)
{
    RadientTransform Transform;
    Transform.Position   = {1.f, 2.f, 3.f};
    Transform.Scale      = {2.f, 3.f, 4.f};
    Transform.Rotation.z = 0.70710678f;
    Transform.Rotation.w = 0.70710678f;

    const RadientTransform Decomposed = RadientMath::MatrixToTransform(RadientMath::TransformToMatrix(Transform));
    ExpectFloat3Near(Decomposed.Position, Transform.Position);
    ExpectFloat3Near(Decomposed.Scale, Transform.Scale);
    ExpectQuaternionNear(Decomposed.Rotation, Transform.Rotation);
}

} // namespace
