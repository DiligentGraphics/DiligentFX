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

#include "Animation/RadientSceneAnimationDestinationBindingImpl.hpp"
#include "Animation/RadientAnimationValueType.hpp"
#include "Core/RadientValidation.hpp"

#include "DebugUtilities.hpp"

#include <algorithm>

namespace Diligent
{

RadientSceneAnimationDestinationBindingImpl::RadientSceneAnimationDestinationBindingImpl(
    IReferenceCounters*           pRefCounters,
    IRadientAnimationDestination* pDestination,
    RadientSceneState&            State) :
    TBase{pRefCounters},
    m_pDestination{pDestination},
    m_State{State}
{
    VERIFY_EXPR(m_pDestination != nullptr);
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Initialize(
    const RadientAnimationPropertyBindingDesc* pProperties,
    Uint32                                     PropertyCount,
    RadientAnimationResolvedPropertyDesc*      pResolvedProperties)
{
    VERIFY_EXPR(pProperties != nullptr);
    VERIFY_EXPR(PropertyCount != 0);
    VERIFY_EXPR(pResolvedProperties != nullptr);

    for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        pResolvedProperties[PropertyIndex] = {};

    m_EntityIndices.reserve(PropertyCount);
    m_StorageIndices.reserve(PropertyCount);
    m_Entities.reserve(PropertyCount);
    m_StorageGroups.reserve(PropertyCount);
    m_FinalizeStorageIndices.reserve(PropertyCount);
    m_BoundOutputs.reserve(PropertyCount);
    m_BoundRanges.reserve(PropertyCount);
    m_Outputs.reserve(PropertyCount);

    for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
    {
        const RADIENT_STATUS Status = AddProperty(
            pProperties[PropertyIndex], pResolvedProperties[PropertyIndex]);
        if (Status == RADIENT_STATUS_UNSUPPORTED)
            continue;
        if (Status != RADIENT_STATUS_OK)
            return Status;
    }

    if (m_Outputs.empty())
        return RADIENT_STATUS_UNSUPPORTED;

    return FinalizeInitialization();
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::GetOrAddEntity(
    RadientEntityID Entity,
    Uint32&         EntityIndex)
{
    const auto InsertResult = m_EntityIndices.try_emplace(
        Entity, static_cast<Uint32>(m_Entities.size()));
    EntityIndex = InsertResult.first->second;
    if (!InsertResult.second)
        return RADIENT_STATUS_OK;

    const entt::entity ResolvedEntity = m_State.FindEntity(Entity);
    if (ResolvedEntity == entt::null)
    {
        m_EntityIndices.erase(InsertResult.first);
        return RADIENT_STATUS_NOT_FOUND;
    }

    BindingEntity& BindingEntity = m_Entities.emplace_back();
    BindingEntity.Entity         = Entity;
    BindingEntity.ResolvedEntity = ResolvedEntity;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::GetOrAddStorageGroup(
    Uint32             EntityIndex,
    const StorageDesc& Storage,
    Uint32&            StorageIndex)
{
    const StorageKey Key{EntityIndex, &Storage};
    const auto       InsertResult = m_StorageIndices.try_emplace(
        Key, static_cast<Uint32>(m_StorageGroups.size()));
    StorageIndex = InsertResult.first->second;
    if (!InsertResult.second)
        return RADIENT_STATUS_OK;

    VERIFY_EXPR(EntityIndex < m_Entities.size());
    const BindingEntity& Entity = m_Entities[EntityIndex];
    VERIFY_EXPR(Entity.ResolvedEntity != entt::null);
    VERIFY_EXPR(Storage.Acquire != nullptr);
    if (Storage.Acquire(*this, Entity.ResolvedEntity) == nullptr)
    {
        m_StorageIndices.erase(InsertResult.first);
        return RADIENT_STATUS_NOT_FOUND;
    }

    StorageGroup& Group = m_StorageGroups.emplace_back();
    Group.pStorage      = &Storage;
    Group.EntityIndex   = EntityIndex;
    if (Storage.Finalize != nullptr)
        m_FinalizeStorageIndices.push_back(StorageIndex);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::AddProperty(
    const RadientAnimationPropertyBindingDesc& Property,
    RadientAnimationResolvedPropertyDesc&      ResolvedProperty)
{
    if (Property.Schema == InvalidRadientAnimationSchemaID ||
        Property.DestinationElement == InvalidRadientAnimationDestinationElement ||
        Property.Property == InvalidRadientAnimationPropertyID ||
        Property.Value.Type <= RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN ||
        Property.Value.Type >= RADIENT_ANIMATION_VALUE_TYPE_COUNT ||
        Property.Value.ArraySize == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const PropertyDesc* pPropertyDesc = FindProperty(Property.Schema, Property.Property);
    if (pPropertyDesc == nullptr)
        return RADIENT_STATUS_UNSUPPORTED;

    if (Property.Value.Type != pPropertyDesc->Value.Type ||
        !RadientValidation::IsValidSubrange(Property.FirstArrayElement,
                                            Property.Value.ArraySize,
                                            pPropertyDesc->Value.ArraySize))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const AnimationValueTypeInfo TypeInfo = GetAnimationValueTypeInfo(Property.Value.Type);
    VERIFY_EXPR(TypeInfo.NativeSize != 0);
    VERIFY_EXPR(TypeInfo.NativeAlignment != 0);

    Uint64 RelativeOffset = 0;
    Uint64 OutputSize     = 0;
    if (!RadientValidation::CheckedMultiply(Property.FirstArrayElement, TypeInfo.NativeSize, RelativeOffset) ||
        !RadientValidation::CheckedMultiply(Property.Value.ArraySize, TypeInfo.NativeSize, OutputSize) ||
        !RadientValidation::IsAddressableSize(RelativeOffset) ||
        !RadientValidation::IsAddressableSize(OutputSize))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (!RadientValidation::IsSumRepresentable<size_t>(pPropertyDesc->Offset, RelativeOffset))
    {
        UNEXPECTED("Invalid scene animation property descriptor");
        return RADIENT_STATUS_FAILED;
    }

    const size_t Offset          = pPropertyDesc->Offset + static_cast<size_t>(RelativeOffset);
    const size_t Size            = static_cast<size_t>(OutputSize);
    const bool   IsDirectStorage = pPropertyDesc->OutputStorage == OutputStorageKind::Direct;
    const size_t StorageSize     = IsDirectStorage ?
        pPropertyDesc->Storage.DirectStorageSize :
        StagedStorageSize;
    const size_t StorageAlignment = IsDirectStorage ?
        pPropertyDesc->Storage.DirectStorageAlignment :
        StagedStorageAlignment;
    if (pPropertyDesc->Offset > StorageSize ||
        static_cast<size_t>(RelativeOffset) > StorageSize - pPropertyDesc->Offset ||
        Size > StorageSize - Offset ||
        StorageAlignment < TypeInfo.NativeAlignment ||
        Offset % TypeInfo.NativeAlignment != 0)
    {
        UNEXPECTED("Invalid scene animation property descriptor");
        return RADIENT_STATUS_FAILED;
    }

    Uint32               EntityIndex  = 0;
    const RADIENT_STATUS EntityStatus = GetOrAddEntity(
        static_cast<RadientEntityID>(Property.DestinationElement), EntityIndex);
    if (EntityStatus != RADIENT_STATUS_OK)
        return EntityStatus;

    Uint32               StorageIndex  = 0;
    const RADIENT_STATUS StorageStatus = GetOrAddStorageGroup(
        EntityIndex, pPropertyDesc->Storage, StorageIndex);
    if (StorageStatus != RADIENT_STATUS_OK)
        return StorageStatus;

    VERIFY_EXPR(StorageIndex < m_StorageGroups.size());
    StorageGroup& Storage = m_StorageGroups[StorageIndex];
    Storage.FinalizeFlags |= pPropertyDesc->FinalizeFlags;
    m_UnconditionalChangeFlags |= pPropertyDesc->ChangeFlags;

    m_BoundOutputs.push_back({StorageIndex, pPropertyDesc->OutputStorage, Offset});
    m_BoundRanges.push_back({StorageIndex, pPropertyDesc->OutputStorage, Offset, Size});
    m_Outputs.push_back(nullptr);
    ResolvedProperty.Semantic = pPropertyDesc->Semantic;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::FinalizeInitialization()
{
    // Sorting makes overlapping ranges adjacent within each storage address space.
    std::sort(m_BoundRanges.begin(), m_BoundRanges.end(),
              [](const BoundRange& Lhs, const BoundRange& Rhs) {
                  if (Lhs.StorageIndex != Rhs.StorageIndex)
                      return Lhs.StorageIndex < Rhs.StorageIndex;
                  if (Lhs.OutputStorage != Rhs.OutputStorage)
                      return Lhs.OutputStorage < Rhs.OutputStorage;
                  return Lhs.Offset < Rhs.Offset;
              });

    size_t ActiveRangeEnd = m_BoundRanges.empty() ?
        0 :
        m_BoundRanges.front().Offset + m_BoundRanges.front().Size;
    for (size_t RangeIndex = 1; RangeIndex < m_BoundRanges.size(); ++RangeIndex)
    {
        const BoundRange& Previous = m_BoundRanges[RangeIndex - 1];
        const BoundRange& Current  = m_BoundRanges[RangeIndex];
        if (Current.StorageIndex == Previous.StorageIndex &&
            Current.OutputStorage == Previous.OutputStorage)
        {
            if (Current.Offset < ActiveRangeEnd)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            ActiveRangeEnd = (std::max)(ActiveRangeEnd, Current.Offset + Current.Size);
        }
        else
        {
            ActiveRangeEnd = Current.Offset + Current.Size;
        }
    }

    // Construction is complete; retain only the compact playback plan.
    decltype(m_EntityIndices){}.swap(m_EntityIndices);
    decltype(m_StorageIndices){}.swap(m_StorageIndices);
    decltype(m_BoundRanges){}.swap(m_BoundRanges);
    m_Entities.shrink_to_fit();
    m_StorageGroups.shrink_to_fit();
    m_FinalizeStorageIndices.shrink_to_fit();
    m_BoundOutputs.shrink_to_fit();
    m_Outputs.shrink_to_fit();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::BeginUpdate(void* const** ppOutputs)
{
    if (ppOutputs == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppOutputs = nullptr;

    // Scene edits can invalidate both ECS handles and packed component addresses.
    for (BindingEntity& BindingEntity : m_Entities)
    {
        BindingEntity.ResolvedEntity = m_State.FindEntity(BindingEntity.Entity);
        if (BindingEntity.ResolvedEntity == entt::null)
            return RADIENT_STATUS_NOT_FOUND;
    }

    for (StorageGroup& Storage : m_StorageGroups)
    {
        VERIFY_EXPR(Storage.EntityIndex < m_Entities.size());
        VERIFY_EXPR(Storage.pStorage != nullptr);
        VERIFY_EXPR(Storage.pStorage->Acquire != nullptr);
        Storage.pDirectStorage = Storage.pStorage->Acquire(
            *this, m_Entities[Storage.EntityIndex].ResolvedEntity);
        if (Storage.pDirectStorage == nullptr)
            return RADIENT_STATUS_NOT_FOUND;
    }

    // Expose addresses only after every required entity and component was reacquired.
    VERIFY_EXPR(m_BoundOutputs.size() == m_Outputs.size());
    for (size_t OutputIndex = 0; OutputIndex < m_BoundOutputs.size(); ++OutputIndex)
    {
        const BoundOutput& Output = m_BoundOutputs[OutputIndex];
        VERIFY_EXPR(Output.StorageIndex < m_StorageGroups.size());
        StorageGroup& Storage = m_StorageGroups[Output.StorageIndex];
        void*         pBase   = Output.OutputStorage == OutputStorageKind::Direct ?
            Storage.pDirectStorage :
            static_cast<void*>(Storage.StagedStorage);
        VERIFY_EXPR(pBase != nullptr);
        m_Outputs[OutputIndex] = static_cast<Uint8*>(pBase) + Output.Offset;
    }

    *ppOutputs = m_Outputs.data();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::EndUpdate(Bool UpdateDerivedState)
{
    RadientSceneState::CHANGE_FLAGS ChangeFlags = m_UnconditionalChangeFlags;

    // Each affected entity/component pair is finalized at most once per update.
    for (Uint32 StorageIndex : m_FinalizeStorageIndices)
    {
        VERIFY_EXPR(StorageIndex < m_StorageGroups.size());
        StorageGroup& Storage = m_StorageGroups[StorageIndex];
        VERIFY_EXPR(Storage.pStorage->Finalize != nullptr);
        VERIFY_EXPR(Storage.EntityIndex < m_Entities.size());
        ChangeFlags |= Storage.pStorage->Finalize(
            *this, m_Entities[Storage.EntityIndex].ResolvedEntity, Storage);
    }

    if (ChangeFlags != RadientSceneState::CHANGE_FLAG_NONE)
        m_State.Touch(ChangeFlags);

    if (UpdateDerivedState)
    {
        const RADIENT_STATUS Status = m_State.CommitChanges();
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
