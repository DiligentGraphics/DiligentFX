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

#include "Assets/RadientAssetStatus.hpp"

#include "gtest/gtest.h"

using namespace Diligent;

TEST(RadientAssetStatusTest, CombinesGenericFailure)
{
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_OK, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_PENDING, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_NO_CHANGE, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_OUT_OF_DATE, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_NO_GPU_DATA, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);

    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_OK), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_PENDING), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_NO_CHANGE), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_OUT_OF_DATE), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_NO_GPU_DATA), RADIENT_STATUS_FAILED);
}

TEST(RadientAssetStatusTest, PrefersSpecificFailure)
{
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_NOT_FOUND), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_NOT_FOUND, RADIENT_STATUS_FAILED), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_INVALID_ARGUMENT), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_INVALID_OPERATION), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_INVALID_DATA), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_UNSUPPORTED), RADIENT_STATUS_UNSUPPORTED);

    // Among specific failures, preserve the first observed status.
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_NOT_FOUND, RADIENT_STATUS_INVALID_ARGUMENT), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientAssetStatusTest, ExecutionFailureTakesPriorityOverCancellation)
{
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_CANCELLED, RADIENT_STATUS_FAILED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_FAILED, RADIENT_STATUS_CANCELLED), RADIENT_STATUS_FAILED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_CANCELLED, RADIENT_STATUS_INVALID_DATA), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_UNSUPPORTED, RADIENT_STATUS_CANCELLED), RADIENT_STATUS_UNSUPPORTED);
}

TEST(RadientAssetStatusTest, CancellationTakesPriorityOverNonFailures)
{
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_OK, RADIENT_STATUS_CANCELLED), RADIENT_STATUS_CANCELLED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_PENDING, RADIENT_STATUS_CANCELLED), RADIENT_STATUS_CANCELLED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_CANCELLED, RADIENT_STATUS_OK), RADIENT_STATUS_CANCELLED);
    EXPECT_EQ(CombineDependencyStatus(RADIENT_STATUS_CANCELLED, RADIENT_STATUS_PENDING), RADIENT_STATUS_CANCELLED);
}
