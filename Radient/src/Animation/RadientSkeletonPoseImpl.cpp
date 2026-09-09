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

#include "Animation/RadientSkeletonPoseImpl.hpp"
#include "Core/RadientValidation.hpp"
#include "Math/RadientMath.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"

#include <cstring>
#include <exception>
#include <limits>
#include <type_traits>
#include <vector>

namespace Diligent
{

namespace
{

static_assert(std::is_trivially_copyable<RadientTransform>::value,
              "RadientTransform must support byte-wise copying");
static_assert(std::is_trivially_copyable<RadientMatrix4x4>::value,
              "RadientMatrix4x4 must support byte-wise copying");

} // namespace

RadientSkeletonPoseImpl::RadientSkeletonPoseImpl(IReferenceCounters*    pRefCounters,
                                                 IRadientSkeletonAsset* pSkeleton,
                                                 const Uint32*          pEvaluationOrder) :
    TBase{pRefCounters},
    m_pSkeleton{pSkeleton},
    m_pEvaluationOrder{pEvaluationOrder}
{
    VERIFY_EXPR(m_pSkeleton != nullptr);
    VERIFY_EXPR(m_pEvaluationOrder != nullptr);

    const RadientSkeletonDesc& SkeletonDesc = m_pSkeleton->GetDesc();
    m_State.LocalTransforms.reserve(SkeletonDesc.JointCount);
    for (Uint32 JointIndex = 0; JointIndex < SkeletonDesc.JointCount; ++JointIndex)
        m_State.LocalTransforms.push_back(SkeletonDesc.pJoints[JointIndex].LocalRestTransform);
    m_State.GlobalMatrices.resize(m_State.LocalTransforms.size());
    ComputeGlobalMatrices();
}

IRadientSkeletonAsset* RadientSkeletonPoseImpl::GetSkeleton() const
{
    return m_pSkeleton;
}

Uint64 RadientSkeletonPoseImpl::GetVersion() const
{
    return m_State.Version;
}

RADIENT_STATUS RadientSkeletonPoseImpl::GetJointLocalTransforms(Uint32            FirstJoint,
                                                                Uint32            JointCount,
                                                                RadientTransform* pTransforms) const
{
    if (!RadientValidation::IsValidSubrange(FirstJoint, JointCount, static_cast<Uint32>(m_State.LocalTransforms.size())) ||
        (JointCount != 0 && pTransforms == nullptr))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (JointCount != 0)
    {
        std::memcpy(pTransforms,
                    m_State.LocalTransforms.data() + FirstJoint,
                    sizeof(RadientTransform) * JointCount);
    }
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSkeletonPoseImpl::GetJointGlobalMatrices(Uint32            FirstJoint,
                                                               Uint32            JointCount,
                                                               RadientMatrix4x4* pMatrices) const
{
    if (!RadientValidation::IsValidSubrange(FirstJoint, JointCount, static_cast<Uint32>(m_State.GlobalMatrices.size())) ||
        (JointCount != 0 && pMatrices == nullptr))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (JointCount == 0)
        return RADIENT_STATUS_OK;
    if (m_State.GlobalTransformsDirty)
        return RADIENT_STATUS_PENDING;

    std::memcpy(pMatrices,
                m_State.GlobalMatrices.data() + FirstJoint,
                sizeof(RadientMatrix4x4) * JointCount);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSkeletonPoseImpl::ComputeSkinningMatrices(IRadientSkinAsset* pSkin,
                                                                RadientMatrix4x4*  pMatrices,
                                                                Bool               TransposeMatrices,
                                                                Bool               UpdateGlobalMatrices)
{
    if (pSkin == nullptr || pMatrices == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientSkinDesc& SkinDesc = pSkin->GetDesc();
    if (SkinDesc.pSkeleton != m_pSkeleton)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_State.GlobalTransformsDirty)
    {
        if (!UpdateGlobalMatrices)
            return RADIENT_STATUS_PENDING;

        const RADIENT_STATUS Status = UpdateGlobalTransforms();
        if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
            return Status;
    }

    for (Uint32 JointIndex = 0; JointIndex < SkinDesc.JointCount; ++JointIndex)
    {
        const RadientSkinJointBindingDesc& Joint = SkinDesc.pJoints[JointIndex];

        const RadientMatrix4x4 Matrix = RadientMath::MultiplyMatrices(
            Joint.InverseBindMatrix,
            m_State.GlobalMatrices[Joint.SkeletonJointIndex]);

        pMatrices[JointIndex] = TransposeMatrices ?
            RadientMath::TransposeMatrix(Matrix) :
            Matrix;
    }
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSkeletonPoseImpl::UpdateGlobalTransforms()
{
    if (!m_State.GlobalTransformsDirty)
        return RADIENT_STATUS_NO_CHANGE;
    if (m_State.Version == std::numeric_limits<Uint64>::max())
    {
        LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    ComputeGlobalMatrices();
    m_State.GlobalTransformsDirty = false;
    ++m_State.Version;
    return RADIENT_STATUS_OK;
}

const std::vector<RadientTransform>& RadientSkeletonPoseImpl::GetLocalTransforms() const noexcept
{
    return m_State.LocalTransforms;
}

RADIENT_STATUS RadientSkeletonPoseImpl::ApplyLocalTransforms(const std::vector<RadientTransform>& LocalTransforms,
                                                             Bool                                 UpdateGlobals) noexcept
{
    VERIFY_EXPR(LocalTransforms.size() == m_State.LocalTransforms.size());
    if (m_State.Version == std::numeric_limits<Uint64>::max())
    {
        LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    std::memcpy(m_State.LocalTransforms.data(),
                LocalTransforms.data(),
                sizeof(RadientTransform) * LocalTransforms.size());
    m_State.GlobalTransformsDirty = true;

    return UpdateGlobals ?
        UpdateGlobalTransforms() :
        RADIENT_STATUS_OK;
}

void RadientSkeletonPoseImpl::ComputeGlobalMatrices() noexcept
{
    VERIFY_EXPR(m_State.GlobalMatrices.size() == m_State.LocalTransforms.size());
    const RadientSkeletonDesc& SkeletonDesc = m_pSkeleton->GetDesc();
    for (Uint32 OrderIndex = 0; OrderIndex < SkeletonDesc.JointCount; ++OrderIndex)
    {
        const Uint32           JointIndex  = m_pEvaluationOrder[OrderIndex];
        const RadientMatrix4x4 LocalMatrix = RadientMath::TransformToMatrix(m_State.LocalTransforms[JointIndex]);
        const Uint32           ParentIndex = SkeletonDesc.pJoints[JointIndex].ParentJointIndex;
        m_State.GlobalMatrices[JointIndex] = ParentIndex == InvalidRadientJointIndex ?
            LocalMatrix :
            RadientMath::MultiplyMatrices(LocalMatrix, m_State.GlobalMatrices[ParentIndex]);
    }
}

class RadientSkeletonPoseWriterImpl final : public ObjectBase<IRadientSkeletonPoseWriter>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPoseWriter>;

    RadientSkeletonPoseWriterImpl(IReferenceCounters*      pRefCounters,
                                  RadientSkeletonPoseImpl* pPose) :
        TBase{pRefCounters},
        m_pPose{pPose},
        m_LocalTransforms{pPose->GetLocalTransforms()}
    {
        VERIFY_EXPR(m_pPose != nullptr);
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPoseWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetJointLocalTransforms(Uint32                  FirstJoint,
                                                                      Uint32                  JointCount,
                                                                      const RadientTransform* pTransforms) override final
    {
        if (!RadientValidation::IsValidSubrange(FirstJoint, JointCount, static_cast<Uint32>(m_LocalTransforms.size())) ||
            (JointCount != 0 && pTransforms == nullptr))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (JointCount == 0)
            return RADIENT_STATUS_NO_CHANGE;

        std::memcpy(m_LocalTransforms.data() + FirstJoint,
                    pTransforms,
                    sizeof(RadientTransform) * JointCount);
        m_HasPendingChanges = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResetToRestPose() override final
    {
        const RadientSkeletonDesc& SkeletonDesc = m_pPose->GetSkeleton()->GetDesc();

        bool Changed = false;
        for (Uint32 JointIndex = 0; JointIndex < SkeletonDesc.JointCount; ++JointIndex)
        {
            if (m_LocalTransforms[JointIndex] != SkeletonDesc.pJoints[JointIndex].LocalRestTransform)
            {
                m_LocalTransforms[JointIndex] = SkeletonDesc.pJoints[JointIndex].LocalRestTransform;
                Changed                       = true;
            }
        }

        m_HasPendingChanges |= Changed;
        return Changed ? RADIENT_STATUS_OK : RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit(Bool UpdateGlobalTransforms) override final
    {
        if (!m_HasPendingChanges)
            return RADIENT_STATUS_NO_CHANGE;

        const RADIENT_STATUS Status = m_pPose->ApplyLocalTransforms(m_LocalTransforms, UpdateGlobalTransforms);
        if (RADIENT_SUCCEEDED(Status))
            m_HasPendingChanges = false;
        return Status;
    }

private:
    const RefCntAutoPtr<RadientSkeletonPoseImpl> m_pPose;
    std::vector<RadientTransform>                m_LocalTransforms;
    bool                                         m_HasPendingChanges = false;
};

RADIENT_STATUS RadientSkeletonPoseImpl::CreateWriter(IRadientSkeletonPoseWriter** ppWriter)
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppWriter == nullptr, "Output skeleton pose writer pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientSkeletonPoseWriterImpl> pWriter{MakeNewRCObj<RadientSkeletonPoseWriterImpl>()(this)};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton pose writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSkeletonPoseImpl::BeginLegacyAnimationUpdate(RadientTransform*& pLocalTransforms) noexcept
{
    if (m_State.Version == std::numeric_limits<Uint64>::max())
    {
        LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    VERIFY_EXPR(m_State.LocalTransforms.size() == m_pSkeleton->GetDesc().JointCount);
    pLocalTransforms = m_State.LocalTransforms.data();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSkeletonPoseImpl::EndLegacyAnimationUpdate(Bool UpdateGlobals) noexcept
{
    m_State.GlobalTransformsDirty = true;
    return UpdateGlobals ?
        UpdateGlobalTransforms() :
        RADIENT_STATUS_OK;
}

} // namespace Diligent
