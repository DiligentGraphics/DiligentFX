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

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
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

enum class SkeletonPoseAnimationComponent : Uint8
{
    Translation,
    Rotation,
    Scale,
};

struct SkeletonPoseAnimationBindingEntry
{
    Uint32                         JointIndex = InvalidRadientJointIndex;
    SkeletonPoseAnimationComponent Component  = SkeletonPoseAnimationComponent::Translation;
};

RADIENT_STATUS ResolveAnimationProperty(
    const RadientAnimationPropertyBindingDesc& Property,
    Uint32                                     JointCount,
    SkeletonPoseAnimationBindingEntry&         Entry,
    RADIENT_ANIMATION_VALUE_SEMANTIC&          Semantic) noexcept
{
    if (Property.Schema == InvalidRadientAnimationSchemaID ||
        Property.Property == InvalidRadientAnimationPropertyID ||
        Property.Value.Type <= RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN ||
        Property.Value.Type >= RADIENT_ANIMATION_VALUE_TYPE_COUNT ||
        Property.Value.ArraySize == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (Property.Schema != RadientNodeAnimationSchemaID)
        return RADIENT_STATUS_UNSUPPORTED;

    if (Property.DestinationElement >= JointCount)
        return RADIENT_STATUS_NOT_FOUND;

    Entry.JointIndex = static_cast<Uint32>(Property.DestinationElement);
    switch (Property.Property)
    {
        case RadientNodeTranslationProperty:
            Entry.Component = SkeletonPoseAnimationComponent::Translation;
            Semantic        = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
            if (Property.Value.Type != RADIENT_ANIMATION_VALUE_TYPE_FLOAT3 ||
                Property.Value.ArraySize != 1 ||
                Property.FirstArrayElement != 0)
            {
                return RADIENT_STATUS_UNSUPPORTED;
            }
            return RADIENT_STATUS_OK;

        case RadientNodeRotationProperty:
            Entry.Component = SkeletonPoseAnimationComponent::Rotation;
            Semantic        = RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION;
            if (Property.Value.Type != RADIENT_ANIMATION_VALUE_TYPE_FLOAT4 ||
                Property.Value.ArraySize != 1 ||
                Property.FirstArrayElement != 0)
            {
                return RADIENT_STATUS_UNSUPPORTED;
            }
            return RADIENT_STATUS_OK;

        case RadientNodeScaleProperty:
            Entry.Component = SkeletonPoseAnimationComponent::Scale;
            Semantic        = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
            if (Property.Value.Type != RADIENT_ANIMATION_VALUE_TYPE_FLOAT3 ||
                Property.Value.ArraySize != 1 ||
                Property.FirstArrayElement != 0)
            {
                return RADIENT_STATUS_UNSUPPORTED;
            }
            return RADIENT_STATUS_OK;

        default:
            return RADIENT_STATUS_UNSUPPORTED;
    }
}

} // namespace

class RadientSkeletonPoseAnimationDestinationBindingImpl final : public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    RadientSkeletonPoseAnimationDestinationBindingImpl(
        IReferenceCounters*                            pRefCounters,
        IRadientAnimationDestination*                  pDestination,
        RadientSkeletonPoseImpl&                       Pose,
        std::vector<SkeletonPoseAnimationBindingEntry> Entries) :
        TBase{pRefCounters},
        m_pDestination{pDestination},
        m_Pose{Pose},
        m_Entries{std::move(Entries)}
    {
        VERIFY_EXPR(m_pDestination != nullptr);
        VERIFY_EXPR(!m_Entries.empty());
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ApplyProperties(
        const RadientAnimationApplyInfo& Info) override final
    {
        if (Info.UpdateCount != static_cast<Uint32>(m_Entries.size()) ||
            Info.pUpdates == nullptr)
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (m_Pose.m_State.Version == std::numeric_limits<Uint64>::max())
        {
            LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        for (Uint32 PropertyIndex = 0; PropertyIndex < Info.UpdateCount; ++PropertyIndex)
        {
            const SkeletonPoseAnimationBindingEntry&  Entry     = m_Entries[PropertyIndex];
            const RadientAnimationPropertyUpdateDesc& Update    = Info.pUpdates[PropertyIndex];
            RadientTransform&                         Transform = m_Pose.m_State.LocalTransforms[Entry.JointIndex];
            VERIFY_EXPR(Update.pValue != nullptr);
            switch (Entry.Component)
            {
                case SkeletonPoseAnimationComponent::Translation:
                    VERIFY_EXPR(Update.ValueDataSize == sizeof(Transform.Position));
                    std::memcpy(&Transform.Position, Update.pValue, sizeof(Transform.Position));
                    break;

                case SkeletonPoseAnimationComponent::Rotation:
                    VERIFY_EXPR(Update.ValueDataSize == sizeof(Transform.Rotation));
                    std::memcpy(&Transform.Rotation, Update.pValue, sizeof(Transform.Rotation));
                    break;

                case SkeletonPoseAnimationComponent::Scale:
                    VERIFY_EXPR(Update.ValueDataSize == sizeof(Transform.Scale));
                    std::memcpy(&Transform.Scale, Update.pValue, sizeof(Transform.Scale));
                    break;
            }
        }

        m_Pose.m_State.GlobalTransformsDirty = true;
        return Info.UpdateDerivedState ?
            m_Pose.UpdateGlobalTransforms() :
            RADIENT_STATUS_OK;
    }

private:
    const RefCntAutoPtr<IRadientAnimationDestination>    m_pDestination;
    RadientSkeletonPoseImpl&                             m_Pose;
    const std::vector<SkeletonPoseAnimationBindingEntry> m_Entries;
};

void RadientSkeletonPoseAnimationDestinationImpl::QueryInterface(
    const INTERFACE_ID& IID,
    IObject**           ppInterface)
{
    m_Pose.QueryInterface(IID, ppInterface);
}

ReferenceCounterValueType RadientSkeletonPoseAnimationDestinationImpl::AddRef()
{
    return m_Pose.AddRef();
}

ReferenceCounterValueType RadientSkeletonPoseAnimationDestinationImpl::Release()
{
    return m_Pose.Release();
}

IReferenceCounters* RadientSkeletonPoseAnimationDestinationImpl::GetReferenceCounters() const
{
    return m_Pose.GetReferenceCounters();
}

RADIENT_STATUS RadientSkeletonPoseAnimationDestinationImpl::CreateBinding(
    const RadientAnimationPropertyBindingDesc* pProperties,
    Uint32                                     PropertyCount,
    RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
    IRadientAnimationDestinationBinding**      ppBinding)
{
    if (ppBinding == nullptr || *ppBinding != nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (!RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationPropertyBindingDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationResolvedPropertyDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(SkeletonPoseAnimationBindingEntry)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RADIENT_ANIMATION_VALUE_SEMANTIC)))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    try
    {
        std::vector<SkeletonPoseAnimationBindingEntry> Entries(PropertyCount);
        std::vector<RADIENT_ANIMATION_VALUE_SEMANTIC>  Semantics(PropertyCount);
        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            const RADIENT_STATUS Status = ResolveAnimationProperty(
                pProperties[PropertyIndex],
                static_cast<Uint32>(m_Pose.m_State.LocalTransforms.size()),
                Entries[PropertyIndex],
                Semantics[PropertyIndex]);
            if (Status != RADIENT_STATUS_OK)
                return Status;
        }

        std::vector<SkeletonPoseAnimationBindingEntry> SortedEntries = Entries;
        std::sort(
            SortedEntries.begin(),
            SortedEntries.end(),
            [](const SkeletonPoseAnimationBindingEntry& Lhs, const SkeletonPoseAnimationBindingEntry& Rhs) {
                return Lhs.JointIndex != Rhs.JointIndex ?
                    Lhs.JointIndex < Rhs.JointIndex :
                    Lhs.Component < Rhs.Component;
            });
        for (size_t EntryIndex = 1; EntryIndex < SortedEntries.size(); ++EntryIndex)
        {
            const SkeletonPoseAnimationBindingEntry& Previous = SortedEntries[EntryIndex - 1];
            const SkeletonPoseAnimationBindingEntry& Current  = SortedEntries[EntryIndex];
            if (Previous.JointIndex == Current.JointIndex &&
                Previous.Component == Current.Component)
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }

        RefCntAutoPtr<RadientSkeletonPoseAnimationDestinationBindingImpl> pBinding{
            MakeNewRCObj<RadientSkeletonPoseAnimationDestinationBindingImpl>()(
                static_cast<IRadientAnimationDestination*>(this),
                m_Pose,
                std::move(Entries))};

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
            pResolvedProperties[PropertyIndex].Semantic = Semantics[PropertyIndex];

        *ppBinding = pBinding.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a skeleton-pose animation binding: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a skeleton-pose animation binding due to an unknown error");
        return RADIENT_STATUS_FAILED;
    }
}

RadientSkeletonPoseImpl::RadientSkeletonPoseImpl(IReferenceCounters*    pRefCounters,
                                                 IRadientSkeletonAsset* pSkeleton,
                                                 const Uint32*          pEvaluationOrder) :
    TBase{pRefCounters},
    m_pSkeleton{pSkeleton},
    m_pEvaluationOrder{pEvaluationOrder},
    m_AnimationDestination{*this}
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

void RadientSkeletonPoseImpl::QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface)
{
    if (ppInterface == nullptr)
        return;

    if (IID == IID_RadientSkeletonPose)
    {
        *ppInterface = static_cast<IRadientSkeletonPose*>(this);
        (*ppInterface)->AddRef();
    }
    else if (IID == IID_RadientAnimationDestination)
    {
        *ppInterface = static_cast<IRadientAnimationDestination*>(&m_AnimationDestination);
        (*ppInterface)->AddRef();
    }
    else
    {
        TBase::QueryInterface(IID, ppInterface);
    }
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
