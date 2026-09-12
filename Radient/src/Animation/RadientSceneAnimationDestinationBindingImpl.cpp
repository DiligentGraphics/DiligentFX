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
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "HashUtils.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <algorithm>
#include <type_traits>
#include <vector>

namespace Diligent
{

/// Compiles a scene destination's packed playback plan and discards all temporary state.
class RadientSceneAnimationDestinationBindingImpl::Builder final
{
public:
    explicit Builder(RadientSceneAnimationDestinationBindingImpl& Binding) noexcept :
        m_Binding{Binding}
    {}

    RADIENT_STATUS Initialize(const RadientAnimationPropertyBindingDesc* pProperties,
                              Uint32                                     PropertyCount,
                              RadientAnimationResolvedPropertyDesc*      pResolvedProperties);

private:
    /// Construction-only range used to reject overlaps within one storage address space.
    struct BoundRange
    {
        Uint32            StorageIndex  = 0;
        OutputStorageKind OutputStorage = OutputStorageKind::Direct;
        size_t            Offset        = 0;
        size_t            Size          = 0;
    };

    /// Identifies one entity/storage pair in the construction-time interning map.
    struct StorageKey
    {
        Uint32             EntityIndex = 0;
        const StorageDesc* pStorage    = nullptr;

        bool operator==(const StorageKey& Rhs) const noexcept
        {
            return EntityIndex == Rhs.EntityIndex && pStorage == Rhs.pStorage;
        }
    };

    struct StorageKeyHash
    {
        size_t operator()(const StorageKey& Key) const noexcept
        {
            return ComputeHash(Key.EntityIndex, Key.pStorage);
        }
    };

    /// Resolves and appends one supported property to the construction plan.
    RADIENT_STATUS AddProperty(const RadientAnimationPropertyBindingDesc& Property,
                               RadientAnimationResolvedPropertyDesc&      ResolvedProperty);

    /// Interns a destination entity and validates that it currently exists.
    RADIENT_STATUS GetOrAddEntity(RadientEntityID Entity, Uint32& EntityIndex);

    /// Interns one entity/storage pair and validates that the component exists.
    RADIENT_STATUS GetOrAddStorageGroup(Uint32 EntityIndex, const StorageDesc& Storage, Uint32& StorageIndex);

    /// Validates output ranges and publishes the completed playback plan.
    RADIENT_STATUS Finalize();

    /// Copies the completed construction plan into one exact-size allocation.
    void PackRuntimeData() const;

    // Binding receiving the completed playback plan.
    RadientSceneAnimationDestinationBindingImpl& m_Binding;

    // Interns public entity IDs into Entities.
    absl::flat_hash_map<RadientEntityID, Uint32> m_EntityIndices;
    // Interns entity/component pairs into StorageGroups.
    absl::flat_hash_map<StorageKey, Uint32, StorageKeyHash> m_StorageIndices;
    // Runtime records copied into the packed allocation.
    std::vector<BindingEntity> m_Entities;
    std::vector<StorageGroup>  m_StorageGroups;
    // Storage groups that need one post-write callback.
    std::vector<Uint32> m_FinalizeStorageIndices;
    // Accepted properties in evaluator output order.
    std::vector<BoundOutput> m_BoundOutputs;
    // Address ranges retained only for overlap validation.
    std::vector<BoundRange> m_BoundRanges;
    // Scene revisions contributed without inspecting sampled values.
    RadientSceneState::CHANGE_FLAGS m_UnconditionalChangeFlags = RadientSceneState::CHANGE_FLAG_NONE;
};

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
    Builder Build{*this};
    return Build.Initialize(pProperties, PropertyCount, pResolvedProperties);
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Builder::Initialize(
    const RadientAnimationPropertyBindingDesc* pProperties,
    Uint32                                     PropertyCount,
    RadientAnimationResolvedPropertyDesc*      pResolvedProperties)
{
    VERIFY_EXPR(pProperties != nullptr);
    VERIFY_EXPR(PropertyCount != 0);
    VERIFY_EXPR(pResolvedProperties != nullptr);

    for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        pResolvedProperties[PropertyIndex] = {};

    constexpr Uint64 PackedBytesPerProperty =
        sizeof(BindingEntity) + sizeof(StorageGroup) + sizeof(Uint32) + sizeof(BoundOutput) + sizeof(void*);
    constexpr Uint64 MaximumAlignmentOverhead =
        (alignof(BindingEntity) - 1u) +
        (alignof(StorageGroup) - 1u) +
        (alignof(Uint32) - 1u) +
        (alignof(BoundOutput) - 1u) +
        (alignof(void*) - 1u) +
        (sizeof(void*) - 1u);
    Uint64 MaximumPackedArraySize = 0;
    if (!RadientValidation::CheckedMultiply(PropertyCount, PackedBytesPerProperty, MaximumPackedArraySize) ||
        !RadientValidation::IsSumRepresentable<size_t>(MaximumPackedArraySize, MaximumAlignmentOverhead))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    m_EntityIndices.reserve(PropertyCount);
    m_StorageIndices.reserve(PropertyCount);
    m_Entities.reserve(PropertyCount);
    m_StorageGroups.reserve(PropertyCount);
    m_FinalizeStorageIndices.reserve(PropertyCount);
    m_BoundOutputs.reserve(PropertyCount);
    m_BoundRanges.reserve(PropertyCount);

    for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
    {
        const RADIENT_STATUS Status = AddProperty(
            pProperties[PropertyIndex], pResolvedProperties[PropertyIndex]);
        if (Status == RADIENT_STATUS_UNSUPPORTED)
            continue;
        if (Status != RADIENT_STATUS_OK)
            return Status;
    }

    if (m_BoundOutputs.empty())
        return RADIENT_STATUS_UNSUPPORTED;

    return Finalize();
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Builder::GetOrAddEntity(
    RadientEntityID Entity,
    Uint32&         EntityIndex)
{
    const auto InsertResult = m_EntityIndices.try_emplace(
        Entity, static_cast<Uint32>(m_Entities.size()));
    EntityIndex = InsertResult.first->second;
    if (!InsertResult.second)
        return RADIENT_STATUS_OK;

    const entt::entity ResolvedEntity = m_Binding.m_State.FindEntity(Entity);
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

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Builder::GetOrAddStorageGroup(
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
    if (Storage.Acquire(m_Binding, Entity.ResolvedEntity) == nullptr)
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

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Builder::AddProperty(
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

    const size_t Offset           = pPropertyDesc->Offset + static_cast<size_t>(RelativeOffset);
    const size_t Size             = static_cast<size_t>(OutputSize);
    const bool   IsDirectStorage  = pPropertyDesc->OutputStorage == OutputStorageKind::Direct;
    const size_t StorageSize      = IsDirectStorage ?
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
    ResolvedProperty.Semantic = pPropertyDesc->Semantic;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::Builder::Finalize()
{
    // Sorting makes overlapping ranges adjacent within each storage address space.
    std::sort(m_BoundRanges.begin(), m_BoundRanges.end(),
              [](const Builder::BoundRange& Lhs, const Builder::BoundRange& Rhs) {
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

    PackRuntimeData();
    return RADIENT_STATUS_OK;
}

void RadientSceneAnimationDestinationBindingImpl::Builder::PackRuntimeData() const
{
    static_assert(std::is_trivially_destructible<BindingEntity>::value,
                  "Packed binding entities must be trivially destructible");
    static_assert(std::is_trivially_destructible<StorageGroup>::value,
                  "Packed storage groups must be trivially destructible");
    static_assert(std::is_trivially_destructible<BoundOutput>::value,
                  "Packed bound outputs must be trivially destructible");

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<BindingEntity>(m_Entities.size());
    Allocator.AddSpace<StorageGroup>(m_StorageGroups.size());
    Allocator.AddSpace<Uint32>(m_FinalizeStorageIndices.size());
    Allocator.AddSpace<BoundOutput>(m_BoundOutputs.size());
    Allocator.AddSpace<void*>(m_BoundOutputs.size());
    Allocator.Reserve();

    const size_t                        MemorySize = Allocator.GetReservedSize();
    decltype(m_Binding.m_RuntimeMemory) RuntimeMemory{
        Allocator.ReleaseOwnership(),
        STDDeleterRawMem<void>{GetRawAllocator()}};

    FixedLinearAllocator Writer{RuntimeMemory.get(), MemorySize};
    BindingEntity* const pEntities               = Writer.CopyArray(m_Entities.data(), m_Entities.size());
    StorageGroup* const  pStorageGroups          = Writer.CopyArray(m_StorageGroups.data(), m_StorageGroups.size());
    Uint32* const        pFinalizeStorageIndices = Writer.CopyArray(
        m_FinalizeStorageIndices.data(), m_FinalizeStorageIndices.size());
    BoundOutput* const pBoundOutputs = Writer.CopyArray(m_BoundOutputs.data(), m_BoundOutputs.size());
    void** const       ppOutputs     = Writer.ConstructArray<void*>(m_BoundOutputs.size());
    VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());

    m_Binding.m_pEntities                = pEntities;
    m_Binding.m_pStorageGroups           = pStorageGroups;
    m_Binding.m_pFinalizeStorageIndices  = pFinalizeStorageIndices;
    m_Binding.m_pBoundOutputs            = pBoundOutputs;
    m_Binding.m_ppOutputs                = ppOutputs;
    m_Binding.m_EntityCount              = static_cast<Uint32>(m_Entities.size());
    m_Binding.m_StorageGroupCount        = static_cast<Uint32>(m_StorageGroups.size());
    m_Binding.m_FinalizeStorageCount     = static_cast<Uint32>(m_FinalizeStorageIndices.size());
    m_Binding.m_OutputCount              = static_cast<Uint32>(m_BoundOutputs.size());
    m_Binding.m_UnconditionalChangeFlags = m_UnconditionalChangeFlags;
    m_Binding.m_RuntimeMemory            = std::move(RuntimeMemory);
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::BeginUpdate(void* const** ppOutputs)
{
    if (ppOutputs == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppOutputs = nullptr;

    VERIFY_EXPR(m_RuntimeMemory != nullptr);
    if (m_RuntimeMemory == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    // Scene edits can invalidate both ECS handles and packed component addresses.
    for (Uint32 EntityIndex = 0; EntityIndex < m_EntityCount; ++EntityIndex)
    {
        BindingEntity& BindingEntity = m_pEntities[EntityIndex];
        BindingEntity.ResolvedEntity = m_State.FindEntity(BindingEntity.Entity);
        if (BindingEntity.ResolvedEntity == entt::null)
            return RADIENT_STATUS_NOT_FOUND;
    }

    for (Uint32 StorageIndex = 0; StorageIndex < m_StorageGroupCount; ++StorageIndex)
    {
        StorageGroup& Storage = m_pStorageGroups[StorageIndex];
        VERIFY_EXPR(Storage.EntityIndex < m_EntityCount);
        VERIFY_EXPR(Storage.pStorage != nullptr);
        VERIFY_EXPR(Storage.pStorage->Acquire != nullptr);
        Storage.pDirectStorage = Storage.pStorage->Acquire(
            *this, m_pEntities[Storage.EntityIndex].ResolvedEntity);
        if (Storage.pDirectStorage == nullptr)
            return RADIENT_STATUS_NOT_FOUND;
    }

    // Expose addresses only after every required entity and component was reacquired.
    for (Uint32 OutputIndex = 0; OutputIndex < m_OutputCount; ++OutputIndex)
    {
        const BoundOutput& Output = m_pBoundOutputs[OutputIndex];
        VERIFY_EXPR(Output.StorageIndex < m_StorageGroupCount);
        StorageGroup& Storage = m_pStorageGroups[Output.StorageIndex];
        void*         pBase   = Output.OutputStorage == OutputStorageKind::Direct ?
            Storage.pDirectStorage :
            static_cast<void*>(Storage.StagedStorage);
        VERIFY_EXPR(pBase != nullptr);
        m_ppOutputs[OutputIndex] = static_cast<Uint8*>(pBase) + Output.Offset;
    }

    *ppOutputs = m_ppOutputs;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneAnimationDestinationBindingImpl::EndUpdate(Bool UpdateDerivedState)
{
    VERIFY_EXPR(m_RuntimeMemory != nullptr);
    if (m_RuntimeMemory == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientSceneState::CHANGE_FLAGS ChangeFlags = m_UnconditionalChangeFlags;

    // Each affected entity/component pair is finalized at most once per update.
    for (Uint32 FinalizeIndex = 0; FinalizeIndex < m_FinalizeStorageCount; ++FinalizeIndex)
    {
        const Uint32 StorageIndex = m_pFinalizeStorageIndices[FinalizeIndex];
        VERIFY_EXPR(StorageIndex < m_StorageGroupCount);
        StorageGroup& Storage = m_pStorageGroups[StorageIndex];
        VERIFY_EXPR(Storage.pStorage->Finalize != nullptr);
        VERIFY_EXPR(Storage.EntityIndex < m_EntityCount);
        ChangeFlags |= Storage.pStorage->Finalize(
            *this, m_pEntities[Storage.EntityIndex].ResolvedEntity, Storage);
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
