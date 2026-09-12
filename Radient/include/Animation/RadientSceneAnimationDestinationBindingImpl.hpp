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

#include "RadientAnimation.h"
#include "Scene/RadientSceneState.hpp"

#include "HashUtils.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <cstddef>
#include <vector>

namespace Diligent
{

class RadientSceneAnimationDestinationImpl;

/// Compiled mapping from animation properties to scene ECS component storage.
/// Properties sharing an entity and component are acquired and finalized as one group.
class RadientSceneAnimationDestinationBindingImpl final :
    public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    /// Creates an empty binding associated with one scene destination.
    RadientSceneAnimationDestinationBindingImpl(IReferenceCounters*           pRefCounters,
                                                IRadientAnimationDestination* pDestination,
                                                RadientSceneState&            State);

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    /// Reacquires entities and relocatable component storage, then exposes writable outputs.
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE BeginUpdate(void* const** ppOutputs) override final;

    /// Finalizes grouped writes, records scene changes, and optionally updates derived state.
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE EndUpdate(Bool UpdateDerivedState) override final;

private:
    friend class RadientSceneAnimationDestinationImpl;

    /// Selects direct component storage or binding-owned staging storage for a sampler output.
    enum class OutputStorageKind : Uint8
    {
        Direct,
        Staged,
    };

    // Fixed arena for small staged values; descriptor validation enforces span and alignment.
    static constexpr size_t StagedStorageSize      = sizeof(Uint64);
    static constexpr size_t StagedStorageAlignment = alignof(Uint64);

    struct StorageGroup;

    /// Returns the current address of one component storage for an entity.
    using AcquireStorageFunc = void* (*)(RadientSceneAnimationDestinationBindingImpl& Binding,
                                         entt::entity                                 Entity) noexcept;

    /// Applies staged values and reports changes that depend on the sampled result.
    using FinalizeStorageFunc = RadientSceneState::CHANGE_FLAGS (*)(
        RadientSceneAnimationDestinationBindingImpl& Binding,
        entt::entity                                 Entity,
        StorageGroup&                                Storage);

    // Static adapter for one ECS storage type. Its address identifies the storage kind.
    struct StorageDesc
    {
        AcquireStorageFunc  Acquire                = nullptr;
        FinalizeStorageFunc Finalize               = nullptr;
        size_t              DirectStorageSize      = 0;
        size_t              DirectStorageAlignment = 0;
    };

    /// Orders the immutable property table by schema and property identifier.
    struct PropertyKey
    {
        RadientAnimationSchemaID   Schema   = InvalidRadientAnimationSchemaID;
        RadientAnimationPropertyID Property = InvalidRadientAnimationPropertyID;

        bool operator==(const PropertyKey& Rhs) const noexcept
        {
            return Schema == Rhs.Schema && Property == Rhs.Property;
        }

        bool operator<(const PropertyKey& Rhs) const noexcept
        {
            return Schema != Rhs.Schema ? Schema < Rhs.Schema : Property < Rhs.Property;
        }
    };

    // Catalog row describing a property's layout, output placement, and post-write effects.
    struct PropertyDesc
    {
        PropertyKey                      Key;
        RadientAnimationValueDesc        Value;
        RADIENT_ANIMATION_VALUE_SEMANTIC Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
        const StorageDesc&               Storage;
        OutputStorageKind                OutputStorage = OutputStorageKind::Direct;
        size_t                           Offset        = 0;
        RadientSceneState::CHANGE_FLAGS  ChangeFlags   = RadientSceneState::CHANGE_FLAG_NONE;
        Uint32                           FinalizeFlags = 0;
    };

    /// Keeps a stable public entity ID and its per-update ECS handle.
    struct BindingEntity
    {
        RadientEntityID Entity         = InvalidRadientEntityID;
        entt::entity    ResolvedEntity = entt::null;
    };

    // Runtime state shared by all properties using one entity/storage pair.
    struct StorageGroup
    {
        const StorageDesc* pStorage                                            = nullptr;
        void*              pDirectStorage                                      = nullptr;
        alignas(StagedStorageAlignment) Uint8 StagedStorage[StagedStorageSize] = {};
        Uint32 EntityIndex                                                     = 0;
        Uint32 FinalizeFlags                                                   = 0;
    };

    /// Locates one sampler output relative to its runtime storage group.
    struct BoundOutput
    {
        Uint32            StorageIndex  = 0;
        OutputStorageKind OutputStorage = OutputStorageKind::Direct;
        size_t            Offset        = 0;
    };

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

    /// Hashes the entity/storage identity used by the construction map.
    struct StorageKeyHash
    {
        size_t operator()(const StorageKey& Key) const noexcept
        {
            return ComputeHash(Key.EntityIndex, Key.pStorage);
        }
    };

    /// One-shot compilation of the supported subset and its compact playback plan.
    RADIENT_STATUS Initialize(const RadientAnimationPropertyBindingDesc* pProperties,
                              Uint32                                     PropertyCount,
                              RadientAnimationResolvedPropertyDesc*      pResolvedProperties);

    /// Resolves and appends one supported property to the compact output sequence.
    RADIENT_STATUS AddProperty(const RadientAnimationPropertyBindingDesc& Property,
                               RadientAnimationResolvedPropertyDesc&      ResolvedProperty);

    /// Validates output ranges and releases state used only during initialization.
    RADIENT_STATUS FinalizeInitialization();

    /// Interns a destination entity and validates that it currently exists.
    RADIENT_STATUS GetOrAddEntity(RadientEntityID Entity, Uint32& EntityIndex);

    /// Interns one entity/storage pair and validates that the component exists.
    RADIENT_STATUS GetOrAddStorageGroup(Uint32 EntityIndex, const StorageDesc& Storage, Uint32& StorageIndex);

    /// Looks up the declarative storage mapping for a public animation property.
    static const PropertyDesc* FindProperty(RadientAnimationSchemaID   Schema,
                                            RadientAnimationPropertyID Property) noexcept;

    /// Acquires a component that is present for every scene entity.
    template <typename ComponentType>
    static void* AcquireCoreStorage(RadientSceneAnimationDestinationBindingImpl& Binding,
                                    entt::entity                                 Entity) noexcept
    {
        return &Binding.m_State.m_CoreStorages.get<ComponentType>(Entity);
    }

    /// Acquires an optional component, returning null when the entity does not contain it.
    template <typename ComponentType>
    static void* AcquireOptionalStorage(RadientSceneAnimationDestinationBindingImpl& Binding,
                                        entt::entity                                 Entity) noexcept
    {
        return Binding.m_State.m_Registry.try_get<ComponentType>(Entity);
    }

    /// Applies staged node fields and marks affected derived node state dirty.
    static RadientSceneState::CHANGE_FLAGS FinalizeNodeStorage(
        RadientSceneAnimationDestinationBindingImpl& Binding,
        entt::entity                                 Entity,
        StorageGroup&                                Storage);

    /// Applies staged light fields and records one renderer-facing light update.
    static RadientSceneState::CHANGE_FLAGS FinalizeLightStorage(
        RadientSceneAnimationDestinationBindingImpl& Binding,
        entt::entity                                 Entity,
        StorageGroup&                                Storage);

    // Retaining the destination keeps its writer and referenced scene alive.
    const RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
    RadientSceneState&                                m_State;

    // Construction-only interning state.
    absl::flat_hash_map<RadientEntityID, Uint32>            m_EntityIndices;
    absl::flat_hash_map<StorageKey, Uint32, StorageKeyHash> m_StorageIndices;

    // Compiled entity, storage, and output plan. Bound ranges are construction-only.
    std::vector<BindingEntity> m_Entities;
    std::vector<StorageGroup>  m_StorageGroups;
    std::vector<Uint32>        m_FinalizeStorageIndices;
    std::vector<BoundOutput>   m_BoundOutputs;
    std::vector<BoundRange>    m_BoundRanges;
    std::vector<void*>         m_Outputs;

    // Revisions published for every completed update; finalizers add conditional changes.
    RadientSceneState::CHANGE_FLAGS m_UnconditionalChangeFlags = RadientSceneState::CHANGE_FLAG_NONE;
};

} // namespace Diligent
