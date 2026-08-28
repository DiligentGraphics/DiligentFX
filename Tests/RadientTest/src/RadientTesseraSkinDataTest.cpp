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
#include "Math/RadientMath.hpp"
#include "Render/Tessera/RadientTesseraSkinData.hpp"

#include "RefCntAutoPtr.hpp"
#include "gtest/gtest.h"

#include <array>

using namespace Diligent;

namespace
{

RadientTransform MakeTransform(Float32 TranslationX, Float32 ScaleX = 1.f)
{
    RadientTransform Transform;
    Transform.Position.x = TranslationX;
    Transform.Scale.x    = ScaleX;
    return Transform;
}

RadientMatrix4x4 MakeTranslationMatrix(Float32 TranslationX)
{
    return RadientMath::TransformToMatrix(MakeTransform(TranslationX));
}

void ExpectMatrixNear(const RadientMatrix4x4& Value,
                      const RadientMatrix4x4& Expected)
{
    for (Uint32 Element = 0; Element < 16; ++Element)
        EXPECT_NEAR(Value.Data[Element], Expected.Data[Element], 1e-6f) << "Element " << Element;
}

struct TestSkinningObjects
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager;
    RefCntAutoPtr<IRadientSkeletonAsset>   pSkeleton;
    RefCntAutoPtr<IRadientSkinAsset>       pSkin;
    RefCntAutoPtr<IRadientSkeletonPose>    pPose;
};

TestSkinningObjects CreateTestSkinningObjects()
{
    TestSkinningObjects Objects;
    Objects.pAssetManager = RadientAssetManagerImpl::Create({});
    EXPECT_NE(Objects.pAssetManager, nullptr);
    if (!Objects.pAssetManager)
        return Objects;

    std::array<RadientSkeletonJointDesc, 2> SkeletonJoints{};
    SkeletonJoints[0].LocalRestTransform = MakeTransform(5.f, 2.f);
    SkeletonJoints[1].LocalRestTransform = MakeTransform(9.f);

    RadientSkeletonDesc SkeletonDesc;
    SkeletonDesc.Name       = "Tessera test skeleton";
    SkeletonDesc.pJoints    = SkeletonJoints.data();
    SkeletonDesc.JointCount = static_cast<Uint32>(SkeletonJoints.size());
    EXPECT_EQ(Objects.pAssetManager->CreateSkeleton(SkeletonDesc, Objects.pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    if (!Objects.pSkeleton)
        return Objects;

    // Deliberately reverse the skeleton-joint order. The second inverse bind
    // transform does not commute with the scaled joint transform, which also
    // verifies inverse-bind * global-transform multiplication order.
    std::array<RadientSkinJointBindingDesc, 2> SkinJoints{};
    SkinJoints[0].SkeletonJointIndex = 1;
    SkinJoints[0].InverseBindMatrix  = MakeTranslationMatrix(-1.f);
    SkinJoints[1].SkeletonJointIndex = 0;
    SkinJoints[1].InverseBindMatrix  = MakeTranslationMatrix(-2.f);

    RadientSkinDesc SkinDesc;
    SkinDesc.Name       = "Tessera test skin";
    SkinDesc.pSkeleton  = Objects.pSkeleton;
    SkinDesc.pJoints    = SkinJoints.data();
    SkinDesc.JointCount = static_cast<Uint32>(SkinJoints.size());
    EXPECT_EQ(Objects.pAssetManager->CreateSkin(SkinDesc, Objects.pSkin.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    if (!Objects.pSkin)
        return Objects;

    EXPECT_EQ(Objects.pSkeleton->CreatePose(Objects.pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return Objects;
}

} // namespace

TEST(RadientTesseraSkinDataTest, BuildsVersionedCurrentAndPreviousPalettes)
{
    TestSkinningObjects Objects = CreateTestSkinningObjects();
    ASSERT_NE(Objects.pSkin, nullptr);
    ASSERT_NE(Objects.pPose, nullptr);

    RadientTesseraSkinData SkinData{Objects.pSkin, Objects.pPose};
    EXPECT_EQ(SkinData.GetSkin(), Objects.pSkin.RawPtr());
    EXPECT_EQ(SkinData.GetPose(), Objects.pPose.RawPtr());
    EXPECT_EQ(SkinData.GetJointCount(), 2u);
    EXPECT_FALSE(SkinData.IsPrepared());

    ASSERT_EQ(SkinData.Prepare(), RADIENT_STATUS_OK);
    EXPECT_TRUE(SkinData.IsPrepared());
    EXPECT_EQ(SkinData.GetPreparedPoseVersion(), Objects.pPose->GetVersion());

    const auto& InitialCurrent  = SkinData.GetCurrentJointMatrices();
    const auto& InitialPrevious = SkinData.GetPreviousJointMatrices();
    ASSERT_EQ(InitialCurrent.size(), 2u);
    ASSERT_EQ(InitialPrevious.size(), 2u);
    EXPECT_FLOAT_EQ(InitialCurrent[0].Data[12], 8.f);
    EXPECT_FLOAT_EQ(InitialCurrent[1].Data[0], 2.f);
    EXPECT_FLOAT_EQ(InitialCurrent[1].Data[12], 1.f);
    ExpectMatrixNear(InitialPrevious[0], InitialCurrent[0]);
    ExpectMatrixNear(InitialPrevious[1], InitialCurrent[1]);
    EXPECT_EQ(SkinData.Prepare(), RADIENT_STATUS_NO_CHANGE);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array UpdatedTransforms{
        MakeTransform(13.f, 2.f),
        MakeTransform(17.f),
    };
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, static_cast<Uint32>(UpdatedTransforms.size()), UpdatedTransforms.data()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    ASSERT_EQ(SkinData.Prepare(), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetPreparedPoseVersion(), Objects.pPose->GetVersion());

    const auto& UpdatedCurrent  = SkinData.GetCurrentJointMatrices();
    const auto& UpdatedPrevious = SkinData.GetPreviousJointMatrices();
    EXPECT_FLOAT_EQ(UpdatedCurrent[0].Data[12], 16.f);
    EXPECT_FLOAT_EQ(UpdatedCurrent[1].Data[0], 2.f);
    EXPECT_FLOAT_EQ(UpdatedCurrent[1].Data[12], 9.f);
    EXPECT_FLOAT_EQ(UpdatedPrevious[0].Data[12], 8.f);
    EXPECT_FLOAT_EQ(UpdatedPrevious[1].Data[0], 2.f);
    EXPECT_FLOAT_EQ(UpdatedPrevious[1].Data[12], 1.f);
}

TEST(RadientTesseraSkinDataTest, UpdatesDirtyPoseGlobalsBeforeBuildingPalette)
{
    TestSkinningObjects Objects = CreateTestSkinningObjects();
    ASSERT_NE(Objects.pSkin, nullptr);
    ASSERT_NE(Objects.pPose, nullptr);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const RadientTransform UpdatedTransform = MakeTransform(21.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(1, 1, &UpdatedTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);

    RadientTesseraSkinData UnpreparedData{Objects.pSkin, Objects.pPose};
    ASSERT_EQ(UnpreparedData.Prepare(), RADIENT_STATUS_OK);
    EXPECT_TRUE(UnpreparedData.IsPrepared());
    EXPECT_FLOAT_EQ(UnpreparedData.GetCurrentJointMatrices()[0].Data[12], 20.f);
    ExpectMatrixNear(UnpreparedData.GetPreviousJointMatrices()[0],
                     UnpreparedData.GetCurrentJointMatrices()[0]);
    EXPECT_EQ(Objects.pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);

    const RadientTransform DeferredTransform = MakeTransform(31.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(1, 1, &DeferredTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);

    ASSERT_EQ(UnpreparedData.Prepare(), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(UnpreparedData.GetCurrentJointMatrices()[0].Data[12], 30.f);
    EXPECT_FLOAT_EQ(UnpreparedData.GetPreviousJointMatrices()[0].Data[12], 20.f);
    EXPECT_EQ(Objects.pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);
}
