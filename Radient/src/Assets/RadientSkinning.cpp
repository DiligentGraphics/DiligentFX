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
#include "Assets/RadientAssetURI.hpp"
#include "Math/RadientMath.hpp"

#include "RadientSkinning.h"

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

static_assert(std::is_trivially_copyable<RadientTransform>::value,
              "RadientTransform must support byte-wise copying");
static_assert(std::is_trivially_copyable<RadientMatrix4x4>::value,
              "RadientMatrix4x4 must support byte-wise copying");

bool IsFinite(const RadientTransform& Transform) noexcept
{
    return RadientMath::IsFinite(Transform.Position) &&
        RadientMath::IsFinite(Transform.Rotation) &&
        RadientMath::IsFinite(Transform.Scale);
}

bool IsFinite(const RadientMatrix4x4& Matrix) noexcept
{
    for (Float32 Value : Matrix.Data)
    {
        if (!RadientMath::IsFinite(Value))
            return false;
    }
    return true;
}

bool IsValidRange(Uint32 First, Uint32 Count, Uint32 Total) noexcept
{
    return First <= Total && Count <= Total - First;
}

RADIENT_STATUS ValidateSkeletonDesc(const RadientSkeletonDesc& Desc,
                                    std::vector<Uint32>&       EvaluationOrder)
{
    if (Desc.JointCount == 0)
    {
        LOG_ERROR_MESSAGE("A Radient skeleton must contain at least one joint");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.pJoints == nullptr)
    {
        LOG_ERROR_MESSAGE("Radient skeleton joint data must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const RadientSkeletonJointDesc& Joint = Desc.pJoints[JointIndex];
        if (Joint.ParentJointIndex != InvalidRadientJointIndex &&
            Joint.ParentJointIndex >= Desc.JointCount)
        {
            LOG_ERROR_MESSAGE("Skeleton joint ", JointIndex, " references invalid parent joint ", Joint.ParentJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (!IsFinite(Joint.LocalRestTransform))
        {
            LOG_ERROR_MESSAGE("Skeleton joint ", JointIndex, " has a non-finite local rest transform");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    std::vector<Uint32> FirstChild(Desc.JointCount, InvalidRadientJointIndex);
    std::vector<Uint32> NextSibling(Desc.JointCount, InvalidRadientJointIndex);
    EvaluationOrder.clear();
    EvaluationOrder.reserve(Desc.JointCount);
    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const Uint32 ParentIndex = Desc.pJoints[JointIndex].ParentJointIndex;
        if (ParentIndex == InvalidRadientJointIndex)
        {
            EvaluationOrder.push_back(JointIndex);
        }
        else
        {
            NextSibling[JointIndex] = FirstChild[ParentIndex];
            FirstChild[ParentIndex] = JointIndex;
        }
    }

    for (size_t OrderIndex = 0; OrderIndex < EvaluationOrder.size(); ++OrderIndex)
    {
        const Uint32 ParentIndex = EvaluationOrder[OrderIndex];
        for (Uint32 ChildIndex = FirstChild[ParentIndex];
             ChildIndex != InvalidRadientJointIndex;
             ChildIndex = NextSibling[ChildIndex])
        {
            EvaluationOrder.push_back(ChildIndex);
        }
    }

    if (EvaluationOrder.size() != Desc.JointCount)
    {
        LOG_ERROR_MESSAGE("Skeleton hierarchy contains a cycle");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateSkinDesc(const RadientSkinDesc& Desc)
{
    if (Desc.pSkeleton == nullptr)
    {
        LOG_ERROR_MESSAGE("A Radient skin must reference a skeleton");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.JointCount == 0)
    {
        LOG_ERROR_MESSAGE("A Radient skin must contain at least one joint");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.pJoints == nullptr)
    {
        LOG_ERROR_MESSAGE("Radient skin joint data must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32 SkeletonJointCount = Desc.pSkeleton->GetDesc().JointCount;
    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const RadientSkinJointBindingDesc& Joint = Desc.pJoints[JointIndex];
        if (Joint.SkeletonJointIndex >= SkeletonJointCount)
        {
            LOG_ERROR_MESSAGE("Skin joint ", JointIndex, " references invalid skeleton joint ", Joint.SkeletonJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (!IsFinite(Joint.InverseBindMatrix))
        {
            LOG_ERROR_MESSAGE("Skin joint ", JointIndex, " has a non-finite inverse-bind matrix");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

class RadientSkeletonAssetImpl;
class RadientSkeletonPoseImpl;
class RadientSkeletonPoseWriterImpl;

struct SkeletonPoseState
{
    std::vector<RadientTransform> LocalTransforms;
    std::vector<RadientMatrix4x4> GlobalMatrices;
    Uint64                        Version = 1;
    bool                          GlobalTransformsDirty = false;
};

class RadientSkeletonAssetImpl final : public ObjectBase<IRadientSkeletonAsset>
{
public:
    using TBase = ObjectBase<IRadientSkeletonAsset>;

    RadientSkeletonAssetImpl(IReferenceCounters*        pRefCounters,
                             const RadientSkeletonDesc& Desc,
                             std::vector<Uint32>&&      EvaluationOrder) :
        TBase{pRefCounters},
        m_URI{MakeRadientAssetURI("skeleton")},
        m_Name{Desc.Name != nullptr ? Desc.Name : ""},
        m_EvaluationOrder{std::move(EvaluationOrder)}
    {
        m_Reference.URI     = m_URI.c_str();
        m_Reference.Version = 1;

        m_JointNames.resize(Desc.JointCount);
        m_Joints.resize(Desc.JointCount);
        for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
        {
            if (Desc.pJoints[JointIndex].Name != nullptr)
                m_JointNames[JointIndex] = Desc.pJoints[JointIndex].Name;
            else
                m_JointNames[JointIndex] = std::string{"Joint "} + std::to_string(JointIndex);
        }

        for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
        {
            m_Joints[JointIndex]                    = Desc.pJoints[JointIndex];
            m_Joints[JointIndex].Name               = m_JointNames[JointIndex].c_str();
            m_Joints[JointIndex].LocalRestTransform = RadientMath::NormalizeTransform(Desc.pJoints[JointIndex].LocalRestTransform);
        }

        m_Desc.Name       = m_Name.c_str();
        m_Desc.pJoints    = m_Joints.data();
        m_Desc.JointCount = static_cast<Uint32>(m_Joints.size());
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSkeletonAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientSkeletonAsset*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_SKELETON;
    }

    virtual const RadientSkeletonDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Desc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreatePose(IRadientSkeletonPose** ppPose) override final;

    const std::vector<RadientSkeletonJointDesc>& GetJoints() const noexcept
    {
        return m_Joints;
    }

    const std::vector<Uint32>& GetEvaluationOrder() const noexcept
    {
        return m_EvaluationOrder;
    }

private:
    const std::string                     m_URI;
    RadientAssetReference                 m_Reference;
    const std::string                     m_Name;
    std::vector<std::string>              m_JointNames;
    std::vector<RadientSkeletonJointDesc> m_Joints;
    RadientSkeletonDesc                   m_Desc;
    const std::vector<Uint32>             m_EvaluationOrder;
};

class RadientSkinAssetImpl final : public ObjectBase<IRadientSkinAsset>
{
public:
    using TBase = ObjectBase<IRadientSkinAsset>;

    RadientSkinAssetImpl(IReferenceCounters*    pRefCounters,
                         const RadientSkinDesc& Desc) :
        TBase{pRefCounters},
        m_URI{MakeRadientAssetURI("skin")},
        m_Name{Desc.Name != nullptr ? Desc.Name : ""},
        m_pSkeleton{Desc.pSkeleton},
        m_Joints{Desc.pJoints, Desc.pJoints + Desc.JointCount}
    {
        m_Reference.URI     = m_URI.c_str();
        m_Reference.Version = 1;

        m_Desc.Name       = m_Name.c_str();
        m_Desc.pSkeleton  = m_pSkeleton;
        m_Desc.pJoints    = m_Joints.data();
        m_Desc.JointCount = static_cast<Uint32>(m_Joints.size());
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSkinAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientSkinAsset*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_SKIN;
    }

    virtual const RadientSkinDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Desc;
    }

private:
    const std::string                              m_URI;
    RadientAssetReference                          m_Reference;
    const std::string                              m_Name;
    const RefCntAutoPtr<IRadientSkeletonAsset>     m_pSkeleton;
    const std::vector<RadientSkinJointBindingDesc> m_Joints;
    RadientSkinDesc                                m_Desc;
};

class RadientSkeletonPoseImpl final : public ObjectBase<IRadientSkeletonPose>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPose>;

    RadientSkeletonPoseImpl(IReferenceCounters*       pRefCounters,
                            RadientSkeletonAssetImpl* pSkeleton) :
        TBase{pRefCounters},
        m_pSkeleton{pSkeleton}
    {
        VERIFY_EXPR(m_pSkeleton != nullptr);

        m_State.LocalTransforms.reserve(m_pSkeleton->GetJoints().size());
        for (const RadientSkeletonJointDesc& Joint : m_pSkeleton->GetJoints())
            m_State.LocalTransforms.push_back(Joint.LocalRestTransform);
        m_State.GlobalMatrices.resize(m_State.LocalTransforms.size());
        ComputeGlobalMatrices();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPose, TBase)

    virtual IRadientSkeletonAsset* DILIGENT_CALL_TYPE GetSkeleton() const override final
    {
        return m_pSkeleton;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_State.Version;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointLocalTransforms(Uint32            FirstJoint,
                                                                      Uint32            JointCount,
                                                                      RadientTransform* pTransforms) const override final
    {
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_State.LocalTransforms.size())) ||
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointGlobalMatrices(Uint32            FirstJoint,
                                                                     Uint32            JointCount,
                                                                     RadientMatrix4x4* pMatrices) const override final
    {
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_State.GlobalMatrices.size())) ||
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE UpdateGlobalTransforms() override final
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientSkeletonPoseWriter** ppWriter) override final;

private:
    friend class RadientSkeletonPoseWriterImpl;

    const std::vector<RadientTransform>& GetLocalTransforms() const noexcept
    {
        return m_State.LocalTransforms;
    }

    RADIENT_STATUS ApplyLocalTransforms(const std::vector<RadientTransform>& LocalTransforms,
                                        Bool                                UpdateGlobals) noexcept
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

    void ComputeGlobalMatrices() noexcept
    {
        VERIFY_EXPR(m_State.GlobalMatrices.size() == m_State.LocalTransforms.size());
        const std::vector<RadientSkeletonJointDesc>& Joints = m_pSkeleton->GetJoints();
        for (Uint32 JointIndex : m_pSkeleton->GetEvaluationOrder())
        {
            const RadientMatrix4x4 LocalMatrix = RadientMath::TransformToMatrix(m_State.LocalTransforms[JointIndex]);
            const Uint32           ParentIndex = Joints[JointIndex].ParentJointIndex;
            m_State.GlobalMatrices[JointIndex] = ParentIndex == InvalidRadientJointIndex ?
                LocalMatrix :
                RadientMath::MultiplyMatrices(LocalMatrix, m_State.GlobalMatrices[ParentIndex]);
        }
    }

private:
    const RefCntAutoPtr<RadientSkeletonAssetImpl> m_pSkeleton;
    SkeletonPoseState                             m_State;
};

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
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_LocalTransforms.size())) ||
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
        const std::vector<RadientSkeletonJointDesc>& Joints =
            static_cast<RadientSkeletonAssetImpl*>(m_pPose->GetSkeleton())->GetJoints();

        bool Changed = false;
        for (Uint32 JointIndex = 0; JointIndex < Joints.size(); ++JointIndex)
        {
            if (m_LocalTransforms[JointIndex] != Joints[JointIndex].LocalRestTransform)
            {
                m_LocalTransforms[JointIndex] = Joints[JointIndex].LocalRestTransform;
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

RADIENT_STATUS RadientSkeletonAssetImpl::CreatePose(IRadientSkeletonPose** ppPose)
{
    if (ppPose == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppPose == nullptr, "Output skeleton pose pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppPose = nullptr;

    try
    {
        RefCntAutoPtr<RadientSkeletonPoseImpl> pPose{MakeNewRCObj<RadientSkeletonPoseImpl>()(this)};
        *ppPose = pPose.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton pose: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

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

} // namespace

RADIENT_STATUS RadientAssetManagerImpl::CreateSkeleton(const RadientSkeletonDesc& SkeletonDesc,
                                                       IRadientSkeletonAsset**    ppSkeleton)
{
    if (ppSkeleton == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppSkeleton == nullptr, "Output skeleton pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppSkeleton = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        std::vector<Uint32>  EvaluationOrder;
        const RADIENT_STATUS ValidationStatus = ValidateSkeletonDesc(SkeletonDesc, EvaluationOrder);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        RefCntAutoPtr<RadientSkeletonAssetImpl> pSkeleton{
            MakeNewRCObj<RadientSkeletonAssetImpl>()(SkeletonDesc, std::move(EvaluationOrder))};
        *ppSkeleton = pSkeleton.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientAssetManagerImpl::CreateSkin(const RadientSkinDesc& SkinDesc,
                                                   IRadientSkinAsset**    ppSkin)
{
    if (ppSkin == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppSkin == nullptr, "Output skin pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppSkin = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateSkinDesc(SkinDesc);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        RefCntAutoPtr<RadientSkinAssetImpl> pSkin{MakeNewRCObj<RadientSkinAssetImpl>()(SkinDesc)};
        *ppSkin = pSkin.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skin: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
