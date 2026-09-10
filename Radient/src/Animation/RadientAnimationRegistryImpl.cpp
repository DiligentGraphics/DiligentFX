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

#include "Animation/RadientAnimationRegistryImpl.hpp"

#include "Core/RadientValidation.hpp"
#include "Errors.hpp"
#include "ObjectBase.hpp"
#include "RadientScene.h"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

constexpr size_t InvalidIndex = std::numeric_limits<size_t>::max();

struct AnimationBindingEntityKey
{
    IRadientAnimationBinding* pBinding = nullptr;
    RadientEntityID           Entity   = InvalidRadientEntityID;

    friend bool operator==(const AnimationBindingEntityKey& Lhs,
                           const AnimationBindingEntityKey& Rhs) noexcept
    {
        return Lhs.pBinding == Rhs.pBinding && Lhs.Entity == Rhs.Entity;
    }

    template <typename HashState>
    friend HashState AbslHashValue(HashState                        State,
                                   const AnimationBindingEntityKey& Key)
    {
        return HashState::combine(std::move(State), Key.pBinding, Key.Entity);
    }
};

class AnimationRegistryEntry
{
public:
    struct RemovedAssociation
    {
        IRadientAnimationBinding* pBinding               = nullptr;
        RadientEntityID           Entity                 = InvalidRadientEntityID;
        size_t                    EntityAssociationIndex = InvalidIndex;
    };

    explicit AnimationRegistryEntry(IRadientAnimationClipAsset* pClip) :
        m_pClip{pClip}
    {
        VERIFY_EXPR(m_pClip != nullptr);
    }

    AnimationRegistryEntry() = delete;

    // clang-format off
    AnimationRegistryEntry           (const AnimationRegistryEntry&)     = delete;
    AnimationRegistryEntry& operator=(const AnimationRegistryEntry&)     = delete;
    AnimationRegistryEntry           (AnimationRegistryEntry&&) noexcept = default;
    AnimationRegistryEntry& operator=(AnimationRegistryEntry&&) noexcept = default;
    // clang-format on

    IRadientAnimationClipAsset* GetClip() const noexcept
    {
        return m_pClip;
    }

    size_t GetAssociationCount() const noexcept
    {
        VerifyInvariants();
        return m_Associations.size();
    }

    size_t GetBindingCount() const noexcept
    {
        VerifyInvariants();
        return m_BindingPointers.size();
    }

    bool IsEmpty() const noexcept
    {
        return GetAssociationCount() == 0;
    }

    size_t FindAssociation(IRadientAnimationBinding* pBinding,
                           RadientEntityID           Entity) const noexcept
    {
        const auto It = m_AssociationIndices.find(AnimationBindingEntityKey{pBinding, Entity});
        if (It == m_AssociationIndices.end())
            return InvalidIndex;

        VERIFY_EXPR(It->second < m_Associations.size());
        VERIFY_EXPR(m_Associations[It->second].pBinding == pBinding);
        VERIFY_EXPR(m_Associations[It->second].Entity == Entity);
        return It->second;
    }

    size_t FindBinding(IRadientAnimationBinding* pBinding) const noexcept
    {
        const auto It = m_BindingIndices.find(pBinding);
        if (It == m_BindingIndices.end())
            return InvalidIndex;

        VERIFY_EXPR(It->second < m_BindingPointers.size());
        VERIFY_EXPR(m_BindingPointers[It->second] == pBinding);
        return It->second;
    }

    void Reserve(size_t AssociationCount, size_t BindingCount)
    {
        m_Associations.reserve(AssociationCount);
        m_AssociationIndices.reserve(AssociationCount);
        m_Bindings.reserve(BindingCount);
        m_BindingAssociationCounts.reserve(BindingCount);
        m_BindingIndices.reserve(BindingCount);

        // This public projection is reserved last. Once it moves, all later
        // operations are capacity-backed insertions of non-throwing value
        // types, so a failed reserve cannot leave a published pointer stale.
        m_BindingPointers.reserve(BindingCount);
    }

    void AddAssociation(IRadientAnimationBinding* pBinding,
                        RadientEntityID           Entity,
                        size_t                    EntityAssociationIndex);

    RemovedAssociation RemoveAssociation(size_t AssociationIndex) noexcept;

    void SetEntityAssociationIndex(size_t AssociationIndex, size_t EntityAssociationIndex) noexcept;
    void WritePublicEntry(RadientAnimationRegistryEntry& PublicEntry) const noexcept;

private:
    struct EntityAssociation
    {
        IRadientAnimationBinding* pBinding               = nullptr;
        RadientEntityID           Entity                 = InvalidRadientEntityID;
        size_t                    EntityAssociationIndex = InvalidIndex;
    };

    void RemoveBinding(size_t BindingIndex) noexcept;
    void VerifyInvariants() const noexcept;

private:
    // Retains the clip used as a raw key by the registry lookup maps.
    RefCntAutoPtr<IRadientAnimationClipAsset> m_pClip;

    // One private association per registered binding/entity pair. The binding
    // is retained by its corresponding unique target below.
    std::vector<EntityAssociation> m_Associations;

    // Maps a binding/entity pair to its m_Associations index for O(1) lookup
    // and swap-erase repair.
    absl::flat_hash_map<AnimationBindingEntityKey, size_t> m_AssociationIndices;

    // Contiguous unique-binding array exposed through the public entry.
    std::vector<IRadientAnimationBinding*> m_BindingPointers;

    // Retains the bindings referenced by m_BindingPointers.
    std::vector<RefCntAutoPtr<IRadientAnimationBinding>> m_Bindings;

    // Number of entity associations represented by each unique binding.
    std::vector<size_t> m_BindingAssociationCounts;

    // Maps a retained binding identity to its m_BindingPointers index.
    absl::flat_hash_map<IRadientAnimationBinding*, size_t> m_BindingIndices;
};


void AnimationRegistryEntry::AddAssociation(IRadientAnimationBinding* pBinding,
                                            RadientEntityID           Entity,
                                            size_t                    EntityAssociationIndex)
{
    VerifyInvariants();
    VERIFY_EXPR(pBinding != nullptr);
    VERIFY_EXPR(pBinding->GetClip() == m_pClip);
    VERIFY_EXPR(FindAssociation(pBinding, Entity) == InvalidIndex);

    const size_t BindingIndex = FindBinding(pBinding);
    if (BindingIndex == InvalidIndex)
    {
        const size_t NewBindingIndex = m_BindingPointers.size();
        m_BindingPointers.push_back(pBinding);
        m_Bindings.emplace_back(pBinding);
        m_BindingAssociationCounts.push_back(1);
        const auto InsertResult = m_BindingIndices.emplace(pBinding, NewBindingIndex);
        VERIFY_EXPR(InsertResult.second);
        static_cast<void>(InsertResult);
    }
    else
    {
        ++m_BindingAssociationCounts[BindingIndex];
    }

    const size_t AssociationIndex = m_Associations.size();
    m_Associations.push_back({pBinding, Entity, EntityAssociationIndex});
    const auto InsertResult = m_AssociationIndices.emplace(AnimationBindingEntityKey{pBinding, Entity}, AssociationIndex);
    VERIFY_EXPR(InsertResult.second);
    static_cast<void>(InsertResult);

    VerifyInvariants();
}

AnimationRegistryEntry::RemovedAssociation AnimationRegistryEntry::RemoveAssociation(size_t AssociationIndex) noexcept
{
    VerifyInvariants();
    VERIFY_EXPR(AssociationIndex < m_Associations.size());

    const EntityAssociation  Association = m_Associations[AssociationIndex];
    const RemovedAssociation Removed{
        Association.pBinding,
        Association.Entity,
        Association.EntityAssociationIndex,
    };

    const size_t NumErasedAssociations =
        m_AssociationIndices.erase(AnimationBindingEntityKey{Removed.pBinding, Removed.Entity});
    VERIFY_EXPR(NumErasedAssociations == 1);
    static_cast<void>(NumErasedAssociations);

    const size_t LastAssociation = m_Associations.size() - 1;
    if (AssociationIndex != LastAssociation)
    {
        m_Associations[AssociationIndex] = m_Associations[LastAssociation];

        const EntityAssociation& MovedAssociation   = m_Associations[AssociationIndex];
        auto                     MovedAssociationIt = m_AssociationIndices.find(
            AnimationBindingEntityKey{MovedAssociation.pBinding, MovedAssociation.Entity});
        VERIFY_EXPR(MovedAssociationIt != m_AssociationIndices.end());
        MovedAssociationIt->second = AssociationIndex;
    }
    m_Associations.pop_back();

    const size_t BindingIndex = FindBinding(Association.pBinding);
    VERIFY_EXPR(BindingIndex != InvalidIndex);
    VERIFY_EXPR(m_BindingAssociationCounts[BindingIndex] > 0);
    if (--m_BindingAssociationCounts[BindingIndex] == 0)
        RemoveBinding(BindingIndex);

    VerifyInvariants();
    return Removed;
}

void AnimationRegistryEntry::SetEntityAssociationIndex(size_t AssociationIndex, size_t EntityAssociationIndex) noexcept
{
    VerifyInvariants();
    VERIFY_EXPR(AssociationIndex < m_Associations.size());
    m_Associations[AssociationIndex].EntityAssociationIndex = EntityAssociationIndex;
}

void AnimationRegistryEntry::RemoveBinding(size_t BindingIndex) noexcept
{
    VERIFY_EXPR(BindingIndex < m_BindingPointers.size());
    VERIFY_EXPR(m_BindingAssociationCounts[BindingIndex] == 0);

    IRadientAnimationBinding* const pRemovedBinding   = m_BindingPointers[BindingIndex];
    const size_t                    NumErasedBindings = m_BindingIndices.erase(pRemovedBinding);
    VERIFY_EXPR(NumErasedBindings == 1);
    static_cast<void>(NumErasedBindings);

    const size_t LastBinding = m_BindingPointers.size() - 1;
    if (BindingIndex != LastBinding)
    {
        m_BindingPointers[BindingIndex]          = m_BindingPointers[LastBinding];
        m_Bindings[BindingIndex]                 = std::move(m_Bindings[LastBinding]);
        m_BindingAssociationCounts[BindingIndex] = m_BindingAssociationCounts[LastBinding];

        auto MovedBindingIt = m_BindingIndices.find(m_BindingPointers[BindingIndex]);
        VERIFY_EXPR(MovedBindingIt != m_BindingIndices.end());
        MovedBindingIt->second = BindingIndex;
    }
    m_BindingPointers.pop_back();
    m_Bindings.pop_back();
    m_BindingAssociationCounts.pop_back();
}

void AnimationRegistryEntry::WritePublicEntry(RadientAnimationRegistryEntry& PublicEntry) const noexcept
{
    VerifyInvariants();
    VERIFY_EXPR(m_BindingPointers.size() <= (std::numeric_limits<Uint32>::max)());
    PublicEntry.pClip        = m_pClip;
    PublicEntry.ppBindings   = m_BindingPointers.empty() ? nullptr : m_BindingPointers.data();
    PublicEntry.BindingCount = static_cast<Uint32>(m_BindingPointers.size());
}

void AnimationRegistryEntry::VerifyInvariants() const noexcept
{
    VERIFY_EXPR(m_pClip != nullptr);
    VERIFY_EXPR(m_Associations.size() == m_AssociationIndices.size());
    VERIFY_EXPR(m_BindingPointers.size() == m_Bindings.size());
    VERIFY_EXPR(m_BindingPointers.size() == m_BindingAssociationCounts.size());
    VERIFY_EXPR(m_BindingPointers.size() == m_BindingIndices.size());
    VERIFY_EXPR(m_Associations.empty() == m_BindingPointers.empty());
}


class RadientAnimationRegistryImpl final : public ObjectBase<IRadientAnimationRegistry>
{
public:
    using TBase = ObjectBase<IRadientAnimationRegistry>;

    RadientAnimationRegistryImpl(IReferenceCounters* pRefCounters, IRadientScene* pScene) :
        TBase{pRefCounters},
        m_pScene{pScene}
    {
        VERIFY_EXPR(m_pScene != nullptr);
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationRegistry, TBase)

    virtual IRadientScene* DILIGENT_CALL_TYPE GetScene() const override final
    {
        return m_pScene;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE AddAnimationBinding(IRadientAnimationBinding* pBinding,
                                                                  const RadientEntityID*    pEntities,
                                                                  Uint32                    EntityCount) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationBinding(IRadientAnimationBinding* pBinding,
                                                                     const RadientEntityID*    pEntities,
                                                                     Uint32                    EntityCount) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveEntity(RadientEntityID Entity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationClip(IRadientAnimationClipAsset* pClip) override final;

    virtual const RadientAnimationRegistryState& DILIGENT_CALL_TYPE GetState() const override final
    {
        return m_State;
    }

private:
    struct EntityAssociations
    {
        // Reverse entity-to-binding index used by RemoveEntity(). Every raw
        // pointer is retained by its corresponding AnimationRegistryEntry.
        std::vector<IRadientAnimationBinding*> Bindings;
    };

    size_t FindEntry(IRadientAnimationClipAsset* pClip) const noexcept;
    void   UpdatePublicEntry(size_t EntryIndex) noexcept;
    void   RemoveAssociation(size_t EntryIndex, size_t AssociationIndex) noexcept;
    void   RemoveEntry(size_t EntryIndex) noexcept;
    void   PublishMutation() noexcept;

private:
    RefCntAutoPtr<IRadientScene> m_pScene;

    // Dense storage indexed by m_EntryIndices and maintained with swap-erase.
    std::vector<AnimationRegistryEntry> m_Entries;

    // Public projections parallel to m_Entries; m_State points at this array.
    std::vector<RadientAnimationRegistryEntry> m_PublicEntries;

    // Maps retained clip identities to their dense m_Entries index.
    absl::flat_hash_map<IRadientAnimationClipAsset*, size_t> m_EntryIndices;

    // Finds every binding associated with an entity without scanning entries.
    absl::flat_hash_map<RadientEntityID, EntityAssociations> m_EntityAssociations;

    // Published state whose entry pointer aliases m_PublicEntries.
    RadientAnimationRegistryState m_State;
};

size_t RadientAnimationRegistryImpl::FindEntry(IRadientAnimationClipAsset* pClip) const noexcept
{
    const auto It = m_EntryIndices.find(pClip);
    if (It == m_EntryIndices.end())
        return InvalidIndex;

    VERIFY_EXPR(It->second < m_Entries.size());
    VERIFY_EXPR(m_Entries[It->second].GetClip() == pClip);
    return It->second;
}

void RadientAnimationRegistryImpl::UpdatePublicEntry(size_t EntryIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());
    VERIFY_EXPR(EntryIndex < m_PublicEntries.size());

    const AnimationRegistryEntry& EntryData = m_Entries[EntryIndex];
    EntryData.WritePublicEntry(m_PublicEntries[EntryIndex]);
}

void RadientAnimationRegistryImpl::RemoveAssociation(size_t EntryIndex, size_t AssociationIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());

    AnimationRegistryEntry&                          EntryData = m_Entries[EntryIndex];
    const AnimationRegistryEntry::RemovedAssociation Removed   = EntryData.RemoveAssociation(AssociationIndex);

    auto EntityIt = m_EntityAssociations.find(Removed.Entity);
    VERIFY_EXPR(EntityIt != m_EntityAssociations.end());
    std::vector<IRadientAnimationBinding*>& Bindings = EntityIt->second.Bindings;
    VERIFY_EXPR(Removed.EntityAssociationIndex < Bindings.size());
    VERIFY_EXPR(Bindings[Removed.EntityAssociationIndex] == Removed.pBinding);

    const size_t LastEntityAssociation = Bindings.size() - 1;
    if (Removed.EntityAssociationIndex != LastEntityAssociation)
    {
        IRadientAnimationBinding* const pMovedBinding = Bindings[LastEntityAssociation];
        Bindings[Removed.EntityAssociationIndex]      = pMovedBinding;

        const size_t MovedEntryIndex = FindEntry(pMovedBinding->GetClip());
        VERIFY_EXPR(MovedEntryIndex != InvalidIndex);
        AnimationRegistryEntry& MovedEntry            = m_Entries[MovedEntryIndex];
        const size_t            MovedAssociationIndex = MovedEntry.FindAssociation(pMovedBinding, Removed.Entity);
        VERIFY_EXPR(MovedAssociationIndex != InvalidIndex);
        MovedEntry.SetEntityAssociationIndex(MovedAssociationIndex, Removed.EntityAssociationIndex);
    }
    Bindings.pop_back();
    if (Bindings.empty())
        m_EntityAssociations.erase(EntityIt);
}

void RadientAnimationRegistryImpl::RemoveEntry(size_t EntryIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());
    VERIFY_EXPR(m_Entries.size() == m_PublicEntries.size());
    VERIFY_EXPR(m_Entries[EntryIndex].IsEmpty());

    IRadientAnimationClipAsset* const pRemovedClip     = m_Entries[EntryIndex].GetClip();
    const size_t                      NumErasedEntries = m_EntryIndices.erase(pRemovedClip);
    VERIFY_EXPR(NumErasedEntries == 1);
    static_cast<void>(NumErasedEntries);

    const size_t LastEntry = m_Entries.size() - 1;
    if (EntryIndex != LastEntry)
    {
        m_Entries[EntryIndex] = std::move(m_Entries[LastEntry]);

        auto MovedEntryIt = m_EntryIndices.find(m_Entries[EntryIndex].GetClip());
        VERIFY_EXPR(MovedEntryIt != m_EntryIndices.end());
        MovedEntryIt->second = EntryIndex;
    }
    m_Entries.pop_back();
    m_PublicEntries.pop_back();

    if (EntryIndex < m_Entries.size())
        UpdatePublicEntry(EntryIndex);
}

void RadientAnimationRegistryImpl::PublishMutation() noexcept
{
    VERIFY_EXPR(m_Entries.size() == m_PublicEntries.size());
    VERIFY_EXPR(m_Entries.size() == m_EntryIndices.size());
    VERIFY_EXPR(m_PublicEntries.size() <= (std::numeric_limits<Uint32>::max)());

    ++m_State.Revision;
    m_State.pEntries   = m_PublicEntries.empty() ? nullptr : m_PublicEntries.data();
    m_State.EntryCount = static_cast<Uint32>(m_PublicEntries.size());
}

RADIENT_STATUS RadientAnimationRegistryImpl::AddAnimationBinding(IRadientAnimationBinding* pBinding,
                                                                 const RadientEntityID*    pEntities,
                                                                 Uint32                    EntityCount)
{
    if (pBinding == nullptr || (EntityCount != 0 && pEntities == nullptr))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (EntityCount == 0)
        return RADIENT_STATUS_NO_CHANGE;

    IRadientAnimationClipAsset* const pClip = pBinding->GetClip();
    if (pClip == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::vector<RadientEntityID> InsertedEntityAssociations;
    try
    {
        const size_t                  ExistingEntryIndex = FindEntry(pClip);
        AnimationRegistryEntry* const pExistingEntry     = ExistingEntryIndex != InvalidIndex ?
            &m_Entries[ExistingEntryIndex] :
            nullptr;

        absl::flat_hash_map<RadientEntityID, bool> BatchEntities;
        BatchEntities.reserve(EntityCount);

        std::vector<RadientEntityID> PendingAssociations;
        PendingAssociations.reserve(EntityCount);

        for (Uint32 EntityIndex = 0; EntityIndex < EntityCount; ++EntityIndex)
        {
            const RadientEntityID Entity = pEntities[EntityIndex];
            if ((pExistingEntry != nullptr && pExistingEntry->FindAssociation(pBinding, Entity) != InvalidIndex) ||
                !BatchEntities.emplace(Entity, true).second)
            {
                continue;
            }

            const RADIENT_STATUS EntityStatus = m_pScene->IsEntityAlive(Entity);
            if (EntityStatus != RADIENT_STATUS_OK)
                return EntityStatus;

            PendingAssociations.push_back(Entity);
        }

        if (PendingAssociations.empty())
            return RADIENT_STATUS_NO_CHANGE;

        size_t NewAssociationCount = PendingAssociations.size();
        size_t NewBindingCount     = 1;
        if (pExistingEntry != nullptr)
        {
            const size_t ExistingAssociationCount = pExistingEntry->GetAssociationCount();
            if (!RadientValidation::IsSumRepresentable<size_t>(
                    ExistingAssociationCount, PendingAssociations.size()))
            {
                return RADIENT_STATUS_FAILED;
            }
            NewAssociationCount = ExistingAssociationCount + PendingAssociations.size();
            NewBindingCount     = pExistingEntry->GetBindingCount();
            if (pExistingEntry->FindBinding(pBinding) == InvalidIndex)
            {
                if (NewBindingCount >= (std::numeric_limits<Uint32>::max)())
                    return RADIENT_STATUS_FAILED;
                ++NewBindingCount;
            }
        }
        else if (m_Entries.size() >= (std::numeric_limits<Uint32>::max)())
        {
            return RADIENT_STATUS_FAILED;
        }

        if (!RadientValidation::IsSumRepresentable<size_t>(
                m_EntityAssociations.size(), PendingAssociations.size()))
        {
            return RADIENT_STATUS_FAILED;
        }
        for (const RadientEntityID Entity : PendingAssociations)
        {
            const auto EntityIt = m_EntityAssociations.find(Entity);
            if (EntityIt != m_EntityAssociations.end() &&
                EntityIt->second.Bindings.size() == (std::numeric_limits<size_t>::max)())
            {
                return RADIENT_STATUS_FAILED;
            }
        }

        // Reserve every auxiliary container before changing a published
        // binding array. Adding each validated association below is then O(1).
        InsertedEntityAssociations.reserve(PendingAssociations.size());
        m_EntityAssociations.reserve(m_EntityAssociations.size() + PendingAssociations.size());
        for (const RadientEntityID Entity : PendingAssociations)
        {
            auto InsertResult = m_EntityAssociations.try_emplace(Entity);
            if (InsertResult.second)
                InsertedEntityAssociations.push_back(Entity);
            InsertResult.first->second.Bindings.reserve(InsertResult.first->second.Bindings.size() + 1);
        }

        AnimationRegistryEntry* pEntry = pExistingEntry;
        if (pEntry != nullptr)
        {
            pEntry->Reserve(NewAssociationCount, NewBindingCount);
        }
        else
        {
            AnimationRegistryEntry NewEntry{pClip};
            NewEntry.Reserve(PendingAssociations.size(), 1);

            m_EntryIndices.reserve(m_EntryIndices.size() + 1);
            m_Entries.reserve(m_Entries.size() + 1);

            // The public entry array is the final potentially relocating
            // reserve. All operations after it use preallocated storage and
            // non-throwing pointer/integer value types.
            m_PublicEntries.reserve(m_PublicEntries.size() + 1);

            const size_t NewEntryIndex = m_Entries.size();
            m_Entries.emplace_back(std::move(NewEntry));
            m_PublicEntries.emplace_back();
            m_EntryIndices.emplace(pClip, NewEntryIndex);
            pEntry = &m_Entries[NewEntryIndex];
        }

        const size_t EntryIndex = FindEntry(pClip);
        VERIFY_EXPR(EntryIndex != InvalidIndex);
        VERIFY_EXPR(pEntry == &m_Entries[EntryIndex]);

        for (const RadientEntityID Entity : PendingAssociations)
        {
            auto EntityIt = m_EntityAssociations.find(Entity);
            VERIFY_EXPR(EntityIt != m_EntityAssociations.end());

            const size_t EntityAssociationIndex = EntityIt->second.Bindings.size();
            EntityIt->second.Bindings.push_back(pBinding);
            pEntry->AddAssociation(pBinding, Entity, EntityAssociationIndex);
        }

        UpdatePublicEntry(EntryIndex);
        PublishMutation();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        for (RadientEntityID Entity : InsertedEntityAssociations)
        {
            auto It = m_EntityAssociations.find(Entity);
            if (It != m_EntityAssociations.end() && It->second.Bindings.empty())
                m_EntityAssociations.erase(It);
        }
        LOG_ERROR_MESSAGE("Failed to add animation registry bindings: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        for (RadientEntityID Entity : InsertedEntityAssociations)
        {
            auto It = m_EntityAssociations.find(Entity);
            if (It != m_EntityAssociations.end() && It->second.Bindings.empty())
                m_EntityAssociations.erase(It);
        }
        LOG_ERROR_MESSAGE("Failed to add animation registry bindings");
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientAnimationRegistryImpl::RemoveAnimationBinding(IRadientAnimationBinding* pBinding,
                                                                    const RadientEntityID*    pEntities,
                                                                    Uint32                    EntityCount)
{
    if (pBinding == nullptr || (EntityCount != 0 && pEntities == nullptr))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (EntityCount == 0)
        return RADIENT_STATUS_NO_CHANGE;

    IRadientAnimationClipAsset* const pClip = pBinding->GetClip();
    if (pClip == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const size_t EntryIndex = FindEntry(pClip);
    if (EntryIndex == InvalidIndex)
        return RADIENT_STATUS_NO_CHANGE;

    bool Changed = false;
    for (Uint32 EntityIndex = 0; EntityIndex < EntityCount; ++EntityIndex)
    {
        const size_t AssociationIndex = m_Entries[EntryIndex].FindAssociation(pBinding, pEntities[EntityIndex]);
        if (AssociationIndex == InvalidIndex)
            continue;

        RemoveAssociation(EntryIndex, AssociationIndex);
        Changed = true;
    }

    if (!Changed)
        return RADIENT_STATUS_NO_CHANGE;

    if (m_Entries[EntryIndex].IsEmpty())
        RemoveEntry(EntryIndex);
    else
        UpdatePublicEntry(EntryIndex);

    PublishMutation();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAnimationRegistryImpl::RemoveEntity(RadientEntityID Entity)
{
    auto EntityIt = m_EntityAssociations.find(Entity);
    if (EntityIt == m_EntityAssociations.end())
        return RADIENT_STATUS_NO_CHANGE;

    VERIFY_EXPR(!EntityIt->second.Bindings.empty());
    while ((EntityIt = m_EntityAssociations.find(Entity)) != m_EntityAssociations.end())
    {
        VERIFY_EXPR(!EntityIt->second.Bindings.empty());
        IRadientAnimationBinding* const pBinding   = EntityIt->second.Bindings.back();
        const size_t                    EntryIndex = FindEntry(pBinding->GetClip());
        VERIFY_EXPR(EntryIndex != InvalidIndex);

        AnimationRegistryEntry& EntryData        = m_Entries[EntryIndex];
        const size_t            AssociationIndex = EntryData.FindAssociation(pBinding, Entity);
        VERIFY_EXPR(AssociationIndex != InvalidIndex);

        RemoveAssociation(EntryIndex, AssociationIndex);
        if (EntryData.IsEmpty())
            RemoveEntry(EntryIndex);
        else
            UpdatePublicEntry(EntryIndex);
    }

    PublishMutation();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAnimationRegistryImpl::RemoveAnimationClip(IRadientAnimationClipAsset* pClip)
{
    if (pClip == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const size_t EntryIndex = FindEntry(pClip);
    if (EntryIndex == InvalidIndex)
        return RADIENT_STATUS_NO_CHANGE;

    while (!m_Entries[EntryIndex].IsEmpty())
        RemoveAssociation(EntryIndex, m_Entries[EntryIndex].GetAssociationCount() - 1);

    RemoveEntry(EntryIndex);
    PublishMutation();
    return RADIENT_STATUS_OK;
}

} // namespace

RefCntAutoPtr<IRadientAnimationRegistry> CreateRadientAnimationRegistry(IRadientScene* pScene)
{
    if (pScene == nullptr)
        return {};

    return RefCntAutoPtr<RadientAnimationRegistryImpl>{MakeNewRCObj<RadientAnimationRegistryImpl>()(pScene)};
}

} // namespace Diligent
