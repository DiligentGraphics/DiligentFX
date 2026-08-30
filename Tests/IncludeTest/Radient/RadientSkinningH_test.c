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

void RadientSkinning_C_UseTypes(void)
{
    RadientSkeletonJointDesc          SkeletonJoint  = {0};
    RadientSkeletonDesc               SkeletonDesc   = {0};
    RadientSkinJointBindingDesc       SkinJoint      = {0};
    RadientSkinDesc                   SkinDesc       = {0};
    RadientAnimationCurveDesc         CurveDesc      = {0};
    RadientSkeletonAnimationTrackDesc AnimationTrack = {0};
    RadientSkeletonAnimationDesc      AnimationDesc  = {0};

    (void)SkeletonJoint;
    (void)SkeletonDesc;
    (void)SkinJoint;
    (void)SkinDesc;
    (void)CurveDesc;
    (void)AnimationTrack;
    (void)AnimationDesc;
}

void RadientSkinning_C_TestMacros(IRadientAssetManager*           pAssetManager,
                                  IRadientSkeletonAsset*          pSkeleton,
                                  IRadientSkinAsset*              pSkin,
                                  IRadientSkeletonPose*           pPose,
                                  IRadientSkeletonPoseWriter*     pWriter,
                                  IRadientSkeletonAnimationAsset* pAnimation)
{
    RadientSkeletonDesc          SkeletonDesc  = {0};
    RadientSkinDesc              SkinDesc      = {0};
    RadientSkeletonAnimationDesc AnimationDesc = {0};
    RadientTransform             Transform     = {0};
    RadientMatrix4x4             Matrix        = {0};
    RADIENT_STATUS               Status        = RADIENT_STATUS_OK;

    Status = IRadientAssetManager_CreateSkeleton(pAssetManager, &SkeletonDesc, &pSkeleton);
    Status = IRadientAssetManager_CreateSkin(pAssetManager, &SkinDesc, &pSkin);
    Status = IRadientAssetManager_CreateSkeletonAnimation(pAssetManager, &AnimationDesc, &pAnimation);

    (void)IRadientSkeletonAsset_GetDesc(pSkeleton);
    Status = IRadientSkeletonAsset_CreatePose(pSkeleton, &pPose);

    (void)IRadientSkinAsset_GetDesc(pSkin);

    (void)IRadientSkeletonPose_GetSkeleton(pPose);
    (void)IRadientSkeletonPose_GetVersion(pPose);
    Status = IRadientSkeletonPose_GetJointLocalTransforms(pPose, 0, 1, &Transform);
    Status = IRadientSkeletonPose_GetJointGlobalMatrices(pPose, 0, 1, &Matrix);
    Status = IRadientSkeletonPose_ComputeSkinningMatrices(pPose, pSkin, &Matrix, False, True);
    Status = IRadientSkeletonPose_UpdateGlobalTransforms(pPose);
    Status = IRadientSkeletonPose_CreateWriter(pPose, &pWriter);

    Status = IRadientSkeletonPoseWriter_SetJointLocalTransforms(pWriter, 0, 1, &Transform);
    Status = IRadientSkeletonPoseWriter_ResetToRestPose(pWriter);
    Status = IRadientSkeletonPoseWriter_Commit(pWriter, True);

    (void)IRadientSkeletonAnimationAsset_GetDesc(pAnimation);
    Status = IRadientSkeletonAnimationAsset_Evaluate(pAnimation, 0.0, pPose, True);

    (void)Status;
}
