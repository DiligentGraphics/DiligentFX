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
#include "gtest/gtest.h"

namespace Diligent
{

namespace Testing
{

inline constexpr Float32 DefaultFloatComparisonTolerance = 1e-5f;

template <typename Float2Type>
void ExpectFloat2Eq(const Float2Type& Actual, const Float2Type& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
}

template <typename Float3Type>
void ExpectFloat3Eq(const Float3Type& Actual, const Float3Type& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
}

template <typename Float4Type>
void ExpectFloat4Eq(const Float4Type& Actual, const Float4Type& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
    EXPECT_FLOAT_EQ(Actual.w, Expected.w);
}

template <typename Float2Type>
void ExpectFloat2Near(const Float2Type& Actual,
                      const Float2Type& Expected,
                      Float32           Tolerance = DefaultFloatComparisonTolerance)
{
    EXPECT_NEAR(Actual.x, Expected.x, Tolerance);
    EXPECT_NEAR(Actual.y, Expected.y, Tolerance);
}

template <typename Float3Type>
void ExpectFloat3Near(const Float3Type& Actual,
                      const Float3Type& Expected,
                      Float32           Tolerance = DefaultFloatComparisonTolerance)
{
    EXPECT_NEAR(Actual.x, Expected.x, Tolerance);
    EXPECT_NEAR(Actual.y, Expected.y, Tolerance);
    EXPECT_NEAR(Actual.z, Expected.z, Tolerance);
}

template <typename Float4Type>
void ExpectFloat4Near(const Float4Type& Actual,
                      const Float4Type& Expected,
                      Float32           Tolerance = DefaultFloatComparisonTolerance)
{
    EXPECT_NEAR(Actual.x, Expected.x, Tolerance);
    EXPECT_NEAR(Actual.y, Expected.y, Tolerance);
    EXPECT_NEAR(Actual.z, Expected.z, Tolerance);
    EXPECT_NEAR(Actual.w, Expected.w, Tolerance);
}

inline void ExpectQuaternionEq(const RadientQuaternion& Actual,
                               const RadientQuaternion& Expected)
{
    ExpectFloat4Eq(Actual, Expected);
}

inline void ExpectQuaternionNear(const RadientQuaternion& Actual,
                                 const RadientQuaternion& Expected,
                                 Float32                  Tolerance = DefaultFloatComparisonTolerance)
{
    ExpectFloat4Near(Actual, Expected, Tolerance);
}

inline void ExpectMatrixNear(const RadientMatrix4x4& Actual,
                             const RadientMatrix4x4& Expected,
                             Float32                 Tolerance = DefaultFloatComparisonTolerance)
{
    for (Uint32 Element = 0; Element < 16; ++Element)
    {
        EXPECT_NEAR(Actual.Data[Element], Expected.Data[Element], Tolerance)
            << "Matrix element " << Element;
    }
}

inline RadientTransform MakeTranslation(Float32 X, Float32 Y, Float32 Z)
{
    RadientTransform Transform;
    Transform.Position = {X, Y, Z};
    return Transform;
}

} // namespace Testing

} // namespace Diligent
