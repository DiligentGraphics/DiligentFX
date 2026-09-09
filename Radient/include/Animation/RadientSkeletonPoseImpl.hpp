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

#include "RadientSkinning.h"

#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <vector>

namespace Diligent
{

class RadientSkeletonPoseWriterImpl;

class RadientSkeletonPoseImpl final : public ObjectBase<IRadientSkeletonPose>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPose>;

    RadientSkeletonPoseImpl(IReferenceCounters*    pRefCounters,
                            IRadientSkeletonAsset* pSkeleton,
                            const Uint32*          pEvaluationOrder);

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPose, TBase)

    virtual IRadientSkeletonAsset* DILIGENT_CALL_TYPE GetSkeleton() const override final;

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointLocalTransforms(Uint32            FirstJoint,
                                                                      Uint32            JointCount,
                                                                      RadientTransform* pTransforms) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointGlobalMatrices(Uint32            FirstJoint,
                                                                     Uint32            JointCount,
                                                                     RadientMatrix4x4* pMatrices) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ComputeSkinningMatrices(IRadientSkinAsset* pSkin,
                                                                      RadientMatrix4x4*  pMatrices,
                                                                      Bool               TransposeMatrices,
                                                                      Bool               UpdateGlobalMatrices) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE UpdateGlobalTransforms() override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientSkeletonPoseWriter** ppWriter) override final;

    // Temporary allocation-free bridge for IRadientSkeletonAnimationAsset::Evaluate().
    // Remove it together with the legacy skeleton animation API.
    RADIENT_STATUS BeginLegacyAnimationUpdate(RadientTransform*& pLocalTransforms) noexcept;

    RADIENT_STATUS EndLegacyAnimationUpdate(Bool UpdateGlobals) noexcept;

private:
    friend class RadientSkeletonPoseWriterImpl;

    struct State
    {
        std::vector<RadientTransform> LocalTransforms;
        std::vector<RadientMatrix4x4> GlobalMatrices;
        Uint64                        Version               = 1;
        bool                          GlobalTransformsDirty = false;
    };

    const std::vector<RadientTransform>& GetLocalTransforms() const noexcept;

    RADIENT_STATUS ApplyLocalTransforms(const std::vector<RadientTransform>& LocalTransforms,
                                        Bool                                 UpdateGlobals) noexcept;

    void ComputeGlobalMatrices() noexcept;

private:
    const RefCntAutoPtr<IRadientSkeletonAsset> m_pSkeleton;
    const Uint32* const                        m_pEvaluationOrder;
    State                                      m_State;
};

} // namespace Diligent
