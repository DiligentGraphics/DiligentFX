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
 */

#include "Radient/interface/RadientSkinning.h"

using namespace Diligent;

static_assert(InvalidRadientJointIndex == ~Uint32{0}, "Unexpected invalid joint index");
static_assert(RADIENT_ANIMATION_INTERPOLATION_STEP == 0, "Unexpected step interpolation value");
static_assert(RADIENT_ANIMATION_INTERPOLATION_LINEAR == 1, "Unexpected linear interpolation value");
static_assert(RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE == 2, "Unexpected cubic-spline interpolation value");

void RadientSkinning_CPP_UseInterfaces(IRadientAssetManager*           pAssetManager,
                                       IRadientSkeletonAsset*          pSkeleton,
                                       IRadientSkinAsset*              pSkin,
                                       IRadientSkeletonAnimationAsset* pAnimation)
{
    RadientSkeletonDesc          SkeletonDesc;
    RadientSkinDesc              SkinDesc;
    RadientSkeletonAnimationDesc AnimationDesc;
    IRadientSkeletonPose*        pPose   = nullptr;
    IRadientSkeletonPoseWriter*  pWriter = nullptr;

    RADIENT_STATUS Status = pAssetManager->CreateSkeleton(SkeletonDesc, &pSkeleton);
    Status                = pAssetManager->CreateSkin(SkinDesc, &pSkin);
    Status                = pAssetManager->CreateSkeletonAnimation(AnimationDesc, &pAnimation);
    Status                = pSkeleton->CreatePose(&pPose);
    Status                = pPose->CreateWriter(&pWriter);

    (void)pSkeleton->GetDesc();
    (void)pSkin->GetDesc();
    (void)pPose->GetSkeleton();
    (void)pPose->GetVersion();
    Status = pPose->UpdateGlobalTransforms();
    Status = pWriter->Commit(True);
    (void)pAnimation->GetDesc();
    Status = pAnimation->Evaluate(0.0, pPose, True);
    (void)Status;
}
