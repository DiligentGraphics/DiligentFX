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

#include "Assets/RadientAssetManagerImpl.hpp"

#include "RadientSkinning.h"

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <limits>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

RadientTransform Translation(Float32 X, Float32 Y = 0.f, Float32 Z = 0.f)
{
    RadientTransform Transform;
    Transform.Position = {X, Y, Z};
    return Transform;
}

Float32 GetTranslationX(const RadientMatrix4x4& Matrix)
{
    return Matrix.Data[12];
}

RefCntAutoPtr<RadientAssetManagerImpl> CreateAssetManager()
{
    return RadientAssetManagerImpl::Create({});
}

RefCntAutoPtr<IRadientSkeletonAsset> CreateSkeleton(
    RadientAssetManagerImpl&        AssetManager,
    const RadientSkeletonJointDesc* pJoints,
    Uint32                          JointCount,
    const Char*                     Name = "Test skeleton")
{
    RadientSkeletonDesc Desc;
    Desc.Name       = Name;
    Desc.pJoints    = pJoints;
    Desc.JointCount = JointCount;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    EXPECT_EQ(AssetManager.CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return pSkeleton;
}

} // namespace

TEST(RadientSkinningTest, SkeletonCopiesHierarchyAndSupportsArbitraryJointOrder)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    char SkeletonName[] = "Skeleton";
    char ChildName[]    = "Child";
    char RootName[]     = "Root";

    std::array<RadientSkeletonJointDesc, 3> Joints{};
    Joints[0].Name                          = ChildName;
    Joints[0].ParentJointIndex              = 1;
    Joints[0].LocalRestTransform            = Translation(2.f);
    Joints[1].Name                          = RootName;
    Joints[1].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].LocalRestTransform            = Translation(3.f);
    Joints[1].LocalRestTransform.Rotation.w = 2.f;
    Joints[2].Name                          = "Grandchild";
    Joints[2].ParentJointIndex              = 0;
    Joints[2].LocalRestTransform            = Translation(4.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()), SkeletonName);
    ASSERT_NE(pSkeleton, nullptr);

    SkeletonName[0]                         = 'X';
    ChildName[0]                            = 'X';
    RootName[0]                             = 'X';
    Joints[0].LocalRestTransform.Position.x = 100.f;

    const RadientSkeletonDesc& Desc = pSkeleton->GetDesc();
    EXPECT_STREQ(Desc.Name, "Skeleton");
    EXPECT_EQ(pSkeleton->GetType(), RADIENT_ASSET_TYPE_SKELETON);
    ASSERT_EQ(Desc.JointCount, 3u);
    ASSERT_NE(Desc.pJoints, nullptr);
    EXPECT_STREQ(Desc.pJoints[0].Name, "Child");
    EXPECT_STREQ(Desc.pJoints[1].Name, "Root");
    EXPECT_EQ(Desc.pJoints[0].ParentJointIndex, 1u);
    EXPECT_FLOAT_EQ(Desc.pJoints[0].LocalRestTransform.Position.x, 2.f);
    EXPECT_FLOAT_EQ(Desc.pJoints[1].LocalRestTransform.Rotation.w, 1.f);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pPose, nullptr);

    std::array<RadientMatrix4x4, 3> GlobalMatrices{};
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 3, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 5.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 3.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[2]), 9.f);
}

TEST(RadientSkinningTest, SkeletonRejectsInvalidHierarchyAndTransforms)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    RadientSkeletonDesc                  Desc;

    EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    {
        TestingEnvironment::ErrorScope ExpectedErrors{"at least one joint"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Desc.JointCount = 1;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"joint data must not be null"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Desc.pJoints    = Joints.data();
    Desc.JointCount = static_cast<Uint32>(Joints.size());

    Joints[0].ParentJointIndex = 2;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"invalid parent joint"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Joints[0].ParentJointIndex = 1;
    Joints[1].ParentJointIndex = 0;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"contains a cycle"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Joints[0].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].LocalRestTransform.Position.x = std::numeric_limits<Float32>::infinity();
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"non-finite local rest transform"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientSkinningTest, SkinCopiesMappingsAndRetainsSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> SkeletonJoints{};
    SkeletonJoints[0].Name             = "Root";
    SkeletonJoints[1].Name             = "Child";
    SkeletonJoints[1].ParentJointIndex = 0;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, SkeletonJoints.data(), static_cast<Uint32>(SkeletonJoints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    std::array<RadientSkinJointBindingDesc, 2> SkinJoints{};
    SkinJoints[0].SkeletonJointIndex         = 1;
    SkinJoints[0].InverseBindMatrix.Data[12] = -2.f;
    SkinJoints[1].SkeletonJointIndex         = 0;
    SkinJoints[1].InverseBindMatrix.Data[12] = -1.f;

    RadientSkinDesc Desc;
    Desc.Name       = "Test skin";
    Desc.pSkeleton  = pSkeleton;
    Desc.pJoints    = SkinJoints.data();
    Desc.JointCount = static_cast<Uint32>(SkinJoints.size());

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pSkin, nullptr);

    SkinJoints[0].SkeletonJointIndex               = 0;
    SkinJoints[0].InverseBindMatrix.Data[12]       = 100.f;
    IRadientSkeletonAsset* const pExpectedSkeleton = pSkeleton;
    pSkeleton.Release();

    const RadientSkinDesc& StoredDesc = pSkin->GetDesc();
    EXPECT_EQ(pSkin->GetType(), RADIENT_ASSET_TYPE_SKIN);
    EXPECT_STREQ(StoredDesc.Name, "Test skin");
    EXPECT_EQ(StoredDesc.pSkeleton, pExpectedSkeleton);
    ASSERT_EQ(StoredDesc.JointCount, 2u);
    ASSERT_NE(StoredDesc.pJoints, nullptr);
    EXPECT_EQ(StoredDesc.pJoints[0].SkeletonJointIndex, 1u);
    EXPECT_FLOAT_EQ(StoredDesc.pJoints[0].InverseBindMatrix.Data[12], -2.f);
}

TEST(RadientSkinningTest, SkinRejectsInvalidMappings)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkinDesc                  Desc;
    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    EXPECT_EQ(pAssetManager->CreateSkin(Desc, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    {
        TestingEnvironment::ErrorScope ExpectedErrors{"must reference a skeleton"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);
    Desc.pSkeleton = pSkeleton;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"at least one joint"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    RadientSkinJointBindingDesc SkinJoint{};
    Desc.JointCount = 1;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"joint data must not be null"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    SkinJoint.SkeletonJointIndex = 1;
    Desc.pJoints                 = &SkinJoint;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"invalid skeleton joint"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    SkinJoint.SkeletonJointIndex        = 0;
    SkinJoint.InverseBindMatrix.Data[0] = std::numeric_limits<Float32>::quiet_NaN();
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"non-finite inverse-bind matrix"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientSkinningTest, PoseWriterCommitsVersionedPose)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Joints[0].LocalRestTransform = Translation(1.f);
    Joints[1].ParentJointIndex   = 0;
    Joints[1].LocalRestTransform = Translation(2.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pPose, nullptr);
    EXPECT_EQ(pPose->GetSkeleton(), pSkeleton.RawPtr());
    EXPECT_EQ(pPose->GetVersion(), 1u);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    std::array UpdatedTransforms{
        Translation(10.f),
        Translation(20.f),
    };
    UpdatedTransforms[0].Rotation.w = 2.f;
    EXPECT_EQ(pWriter->SetJointLocalTransforms(0, 2, UpdatedTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetJointLocalTransforms(0, 2, UpdatedTransforms.data()), RADIENT_STATUS_OK);

    std::array<RadientTransform, 2> LocalTransforms{};
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 2, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms[0], Joints[0].LocalRestTransform);
    EXPECT_EQ(LocalTransforms[1], Joints[1].LocalRestTransform);
    EXPECT_EQ(pPose->GetVersion(), 1u);

    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pPose->GetVersion(), 2u);

    std::array<RadientMatrix4x4, 2> GlobalMatrices{};
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 2, LocalTransforms.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms, UpdatedTransforms);
    EXPECT_FLOAT_EQ(LocalTransforms[0].Rotation.w, 2.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 10.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 30.f);

    ASSERT_EQ(pWriter->ResetToRestPose(), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 3u);
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 1.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 3.f);
}
