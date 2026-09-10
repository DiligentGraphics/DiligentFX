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

#include "Core/RadientValidation.hpp"

#include "gtest/gtest.h"

#include <limits>

using namespace Diligent;

TEST(RadientValidationTest, IsSumRepresentableAcceptsZero)
{
    EXPECT_TRUE(RadientValidation::IsSumRepresentable<Uint32>(0, 0));
    EXPECT_TRUE(RadientValidation::IsSumRepresentable<Uint32>(0, (std::numeric_limits<Uint32>::max)()));
    EXPECT_TRUE(RadientValidation::IsSumRepresentable<Uint32>((std::numeric_limits<Uint32>::max)(), 0));
}

TEST(RadientValidationTest, IsSumRepresentableAcceptsExactUint32Maximum)
{
    EXPECT_TRUE(RadientValidation::IsSumRepresentable<Uint32>((std::numeric_limits<Uint32>::max)() - 1, 1));
}

TEST(RadientValidationTest, IsSumRepresentableAcceptsExactUint64Maximum)
{
    EXPECT_TRUE(RadientValidation::IsSumRepresentable<Uint64>((std::numeric_limits<Uint64>::max)() - 1, 1));
}

TEST(RadientValidationTest, IsSumRepresentableRejectsLhsBeyondUint32Maximum)
{
    EXPECT_FALSE(RadientValidation::IsSumRepresentable<Uint32>(
        static_cast<Uint64>((std::numeric_limits<Uint32>::max)()) + 1, 0));
}

TEST(RadientValidationTest, IsSumRepresentableRejectsSumOnePastUint32Maximum)
{
    EXPECT_FALSE(RadientValidation::IsSumRepresentable<Uint32>((std::numeric_limits<Uint32>::max)(), 1));
}

TEST(RadientValidationTest, IsSumRepresentableRejectsUint64Overflow)
{
    EXPECT_FALSE(RadientValidation::IsSumRepresentable<Uint64>((std::numeric_limits<Uint64>::max)(), 1));
}

TEST(RadientValidationTest, IsProductRepresentableAcceptsZeroProduct)
{
    EXPECT_TRUE(RadientValidation::IsProductRepresentable<Uint32>(0, (std::numeric_limits<Uint64>::max)()));
    EXPECT_TRUE(RadientValidation::IsProductRepresentable<Uint32>((std::numeric_limits<Uint64>::max)(), 0));
}

TEST(RadientValidationTest, IsProductRepresentableAcceptsExactMaximum)
{
    EXPECT_TRUE(RadientValidation::IsProductRepresentable<Uint32>((std::numeric_limits<Uint32>::max)(), 1));
    EXPECT_TRUE(RadientValidation::IsProductRepresentable<Uint64>((std::numeric_limits<Uint64>::max)(), 1));
}

TEST(RadientValidationTest, IsProductRepresentableRejectsProductAboveUint32Maximum)
{
    EXPECT_FALSE(RadientValidation::IsProductRepresentable<Uint32>((std::numeric_limits<Uint32>::max)(), 2));
}

TEST(RadientValidationTest, IsProductRepresentableRejectsUint64Overflow)
{
    EXPECT_FALSE(RadientValidation::IsProductRepresentable<Uint64>((std::numeric_limits<Uint64>::max)(), 2));
}

TEST(RadientValidationTest, CheckedMultiplyComputesProduct)
{
    Uint64 Result = 0;
    EXPECT_TRUE(RadientValidation::CheckedMultiply(6, 7, Result));
    EXPECT_EQ(Result, 42u);
}

TEST(RadientValidationTest, CheckedMultiplyAcceptsZeroProduct)
{
    Uint64 Result = 1;
    EXPECT_TRUE(RadientValidation::CheckedMultiply(0, (std::numeric_limits<Uint64>::max)(), Result));
    EXPECT_EQ(Result, 0u);
}

TEST(RadientValidationTest, CheckedMultiplyAcceptsExactUint64Maximum)
{
    Uint64 Result = 0;
    EXPECT_TRUE(RadientValidation::CheckedMultiply((std::numeric_limits<Uint64>::max)(), 1, Result));
    EXPECT_EQ(Result, (std::numeric_limits<Uint64>::max)());
}

TEST(RadientValidationTest, CheckedMultiplyRejectsOverflow)
{
    Uint64 Result = 0;
    EXPECT_FALSE(RadientValidation::CheckedMultiply((std::numeric_limits<Uint64>::max)(), 2, Result));
}

TEST(RadientValidationTest, IsAddressableSizeAcceptsZero)
{
    EXPECT_TRUE(RadientValidation::IsAddressableSize(0));
}

TEST(RadientValidationTest, IsAddressableSizeAcceptsSizeTMaximum)
{
    const Uint64 MaxAddressableSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)());
    EXPECT_TRUE(RadientValidation::IsAddressableSize(MaxAddressableSize));
}

TEST(RadientValidationTest, IsAddressableSizeHandlesUint64Maximum)
{
    const Uint64 MaxAddressableSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)());
    if (MaxAddressableSize < (std::numeric_limits<Uint64>::max)())
        EXPECT_FALSE(RadientValidation::IsAddressableSize(MaxAddressableSize + 1));
    else
        EXPECT_TRUE(RadientValidation::IsAddressableSize((std::numeric_limits<Uint64>::max)()));
}

TEST(RadientValidationTest, IsAddressableArrayAcceptsZeroProduct)
{
    EXPECT_TRUE(RadientValidation::IsAddressableArray(0, (std::numeric_limits<Uint64>::max)()));
}

TEST(RadientValidationTest, IsAddressableArrayAcceptsExactSizeTMaximum)
{
    const Uint64 MaxAddressableSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)());
    EXPECT_TRUE(RadientValidation::IsAddressableArray(MaxAddressableSize, 1));
}

TEST(RadientValidationTest, IsAddressableArrayRejectsProductPastSizeTMaximum)
{
    const Uint64 MaxAddressableSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)());
    EXPECT_FALSE(RadientValidation::IsAddressableArray(MaxAddressableSize, 2));
}

TEST(RadientValidationTest, IsValidSubrangeAcceptsZeroRange)
{
    EXPECT_TRUE(RadientValidation::IsValidSubrange(0, 0, 0));
}

TEST(RadientValidationTest, IsValidSubrangeAcceptsRangeEndingAtTotal)
{
    EXPECT_TRUE(RadientValidation::IsValidSubrange(2, 8, 10));
}

TEST(RadientValidationTest, IsValidSubrangeAcceptsEmptyRangeAtEnd)
{
    EXPECT_TRUE(RadientValidation::IsValidSubrange(10, 0, 10));
}

TEST(RadientValidationTest, IsValidSubrangeAcceptsExactUint64Range)
{
    EXPECT_TRUE(RadientValidation::IsValidSubrange(0, (std::numeric_limits<Uint64>::max)(),
                                                   (std::numeric_limits<Uint64>::max)()));
}

TEST(RadientValidationTest, IsValidSubrangeRejectsFirstPastEnd)
{
    EXPECT_FALSE(RadientValidation::IsValidSubrange(11, 0, 10));
}

TEST(RadientValidationTest, IsValidSubrangeRejectsCountPastEnd)
{
    EXPECT_FALSE(RadientValidation::IsValidSubrange(3, 8, 10));
}

TEST(RadientValidationTest, IsValidSubrangeRejectsOverflowingEnd)
{
    EXPECT_FALSE(RadientValidation::IsValidSubrange((std::numeric_limits<Uint64>::max)() - 1, 2,
                                                    (std::numeric_limits<Uint64>::max)()));
}
