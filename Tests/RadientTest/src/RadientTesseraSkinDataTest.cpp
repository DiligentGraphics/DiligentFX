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

#include "ObjectBase.hpp"
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

RadientTesseraBufferSuballocator MakeJointBuffer()
{
    return RadientTesseraBufferSuballocator{GetTesseraJointBufferCreateInfo()};
}

struct TestSkinningObjects
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager;
    RefCntAutoPtr<IRadientSkeletonAsset>   pSkeleton;
    RefCntAutoPtr<IRadientSkinAsset>       pSkin;
    RefCntAutoPtr<IRadientSkeletonPose>    pPose;
};

class FailingSkeletonPose final : public ObjectBase<IRadientSkeletonPose>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPose>;

    FailingSkeletonPose(IReferenceCounters* pRefCounters, IRadientSkeletonAsset* pSkeleton) :
        TBase{pRefCounters},
        m_pSkeleton{pSkeleton}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPose, TBase)

    virtual IRadientSkeletonAsset* DILIGENT_CALL_TYPE GetSkeleton() const override final
    {
        return m_pSkeleton;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return 0;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointLocalTransforms(Uint32,
                                                                      Uint32,
                                                                      RadientTransform*) const override final
    {
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointGlobalMatrices(Uint32,
                                                                     Uint32,
                                                                     RadientMatrix4x4*) const override final
    {
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ComputeSkinningMatrices(IRadientSkinAsset*,
                                                                      RadientMatrix4x4*,
                                                                      Bool,
                                                                      Bool) override final
    {
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE UpdateGlobalTransforms() override final
    {
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientSkeletonPoseWriter** ppWriter) override final
    {
        if (ppWriter != nullptr)
            *ppWriter = nullptr;
        return RADIENT_STATUS_FAILED;
    }

private:
    RefCntAutoPtr<IRadientSkeletonAsset> m_pSkeleton;
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

    RadientTesseraBufferSuballocator JointBuffer = MakeJointBuffer();
    RadientTesseraSkinData           SkinData{Objects.pSkin, Objects.pPose, JointBuffer};
    EXPECT_EQ(SkinData.GetSkin(), Objects.pSkin.RawPtr());
    EXPECT_EQ(SkinData.GetPose(), Objects.pPose.RawPtr());
    EXPECT_TRUE(SkinData.Matches(Objects.pSkin, Objects.pPose));

    RefCntAutoPtr<IRadientSkeletonPose> pOtherPose;
    ASSERT_EQ(Objects.pSkeleton->CreatePose(pOtherPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_FALSE(SkinData.Matches(Objects.pSkin, pOtherPose));
    EXPECT_EQ(SkinData.GetJointCount(), 2u);
    EXPECT_FALSE(SkinData.IsPrepared());
    EXPECT_EQ(SkinData.GetPreparationStatus(), RADIENT_STATUS_PENDING);
    EXPECT_EQ(SkinData.GetFirstJoint(), ~Uint32{0});
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), ~Uint32{0});

    const RadientTesseraBufferAllocation& Allocation = SkinData.GetJointBufferAllocation();
    ASSERT_TRUE(Allocation);
    EXPECT_EQ(Allocation.GetOffset() % sizeof(RadientMatrix4x4), 0u);
    EXPECT_EQ(Allocation.GetSize(),
              2u * SkinData.GetJointCount() * static_cast<Uint32>(sizeof(RadientMatrix4x4)));

    ASSERT_EQ(SkinData.Prepare(0), RADIENT_STATUS_OK);
    EXPECT_TRUE(SkinData.IsPrepared());
    EXPECT_EQ(SkinData.GetPreparationStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetPreparedPoseVersion(), Objects.pPose->GetVersion());
    const Uint32 InitialFirstJoint = SkinData.GetFirstJoint();
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), InitialFirstJoint);
    EXPECT_EQ(SkinData.Prepare(0), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(SkinData.GetPreparationStatus(), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array UpdatedTransforms{
        MakeTransform(13.f, 2.f),
        MakeTransform(17.f),
    };
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, static_cast<Uint32>(UpdatedTransforms.size()), UpdatedTransforms.data()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    ASSERT_EQ(SkinData.Prepare(1), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetPreparedPoseVersion(), Objects.pPose->GetVersion());
    EXPECT_EQ(SkinData.GetFirstJoint(), InitialFirstJoint + SkinData.GetJointCount());
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), InitialFirstJoint);

    // Repeated preparation of the same frame must preserve its motion history.
    ASSERT_EQ(SkinData.Prepare(1), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(SkinData.GetFirstJoint(), InitialFirstJoint + SkinData.GetJointCount());
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), InitialFirstJoint);

    const std::array SameFrameTransforms{
        MakeTransform(19.f, 2.f),
        MakeTransform(21.f),
    };
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, static_cast<Uint32>(SameFrameTransforms.size()), SameFrameTransforms.data()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    // A newer pose prepared before this frame is rendered replaces the current
    // palette in place and retains the last frame's palette as history.
    ASSERT_EQ(SkinData.Prepare(1), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetPreparedPoseVersion(), Objects.pPose->GetVersion());
    EXPECT_EQ(SkinData.GetFirstJoint(), InitialFirstJoint + SkinData.GetJointCount());
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), InitialFirstJoint);

    // The first stationary subsequent frame collapses the previous palette
    // onto the current one so skinning motion vectors become zero.
    ASSERT_EQ(SkinData.Prepare(2), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetFirstJoint(), InitialFirstJoint + SkinData.GetJointCount());
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), SkinData.GetFirstJoint());
    EXPECT_EQ(SkinData.Prepare(2), RADIENT_STATUS_NO_CHANGE);

    const std::array SecondUpdatedTransforms{
        MakeTransform(23.f, 2.f),
        MakeTransform(29.f),
    };
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, static_cast<Uint32>(SecondUpdatedTransforms.size()), SecondUpdatedTransforms.data()),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);
    ASSERT_EQ(SkinData.Prepare(3), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetFirstJoint(), InitialFirstJoint);
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), InitialFirstJoint + SkinData.GetJointCount());
}

TEST(RadientTesseraSkinDataTest, UsesFullRenderFrameID)
{
    TestSkinningObjects Objects = CreateTestSkinningObjects();
    ASSERT_NE(Objects.pSkin, nullptr);
    ASSERT_NE(Objects.pPose, nullptr);

    RadientTesseraBufferSuballocator JointBuffer = MakeJointBuffer();
    RadientTesseraSkinData           SkinData{Objects.pSkin, Objects.pPose, JointBuffer};

    constexpr RadientFrameID FirstFrame = 7;
    ASSERT_EQ(SkinData.Prepare(FirstFrame), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const RadientTransform UpdatedTransform = MakeTransform(31.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, 1, &UpdatedTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    constexpr RadientFrameID ChangedFrame = FirstFrame + 1;
    ASSERT_EQ(SkinData.Prepare(ChangedFrame), RADIENT_STATUS_OK);
    ASSERT_NE(SkinData.GetPreviousFirstJoint(), SkinData.GetFirstJoint());

    // This ID has the same low 32 bits as ChangedFrame. It must still be
    // recognized as a later frame and collapse the now-stationary palette.
    constexpr RadientFrameID LaterFrame = (RadientFrameID{1} << 32u) + ChangedFrame;
    EXPECT_EQ(SkinData.Prepare(LaterFrame), RADIENT_STATUS_OK);
    EXPECT_EQ(SkinData.GetPreviousFirstJoint(), SkinData.GetFirstJoint());
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

    RadientTesseraBufferSuballocator JointBuffer = MakeJointBuffer();
    RadientTesseraSkinData           UnpreparedData{Objects.pSkin, Objects.pPose, JointBuffer};
    ASSERT_EQ(UnpreparedData.Prepare(0), RADIENT_STATUS_OK);
    EXPECT_TRUE(UnpreparedData.IsPrepared());
    const Uint32 InitialFirstJoint = UnpreparedData.GetFirstJoint();
    EXPECT_EQ(UnpreparedData.GetPreviousFirstJoint(), InitialFirstJoint);
    EXPECT_EQ(Objects.pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);

    const RadientTransform DeferredTransform = MakeTransform(31.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(1, 1, &DeferredTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);

    ASSERT_EQ(UnpreparedData.Prepare(1), RADIENT_STATUS_OK);
    EXPECT_EQ(UnpreparedData.GetFirstJoint(), InitialFirstJoint + UnpreparedData.GetJointCount());
    EXPECT_EQ(UnpreparedData.GetPreviousFirstJoint(), InitialFirstJoint);
    EXPECT_EQ(Objects.pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientTesseraSkinDataTest, ReportsFailedPreparation)
{
    TestSkinningObjects Objects = CreateTestSkinningObjects();
    ASSERT_NE(Objects.pSkin, nullptr);
    ASSERT_NE(Objects.pSkeleton, nullptr);

    RefCntAutoPtr<IRadientSkeletonPose> pFailingPose{
        MakeNewRCObj<FailingSkeletonPose>()(Objects.pSkeleton)};
    ASSERT_NE(pFailingPose, nullptr);

    RadientTesseraBufferSuballocator JointBuffer = MakeJointBuffer();
    RadientTesseraSkinData           SkinData{Objects.pSkin, pFailingPose, JointBuffer};
    EXPECT_EQ(SkinData.GetPreparationStatus(), RADIENT_STATUS_PENDING);

    EXPECT_EQ(SkinData.Prepare(0), RADIENT_STATUS_FAILED);
    EXPECT_EQ(SkinData.GetPreparationStatus(), RADIENT_STATUS_FAILED);
    EXPECT_FALSE(SkinData.IsPrepared());
}
