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

#include "Errors.hpp"
#include "ObjectBase.hpp"

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

class AnimationRegistryEntry
{
public:
    struct RemovedTarget
    {
        RadientEntityID Entity                 = InvalidRadientEntityID;
        size_t          EntityAssociationIndex = InvalidIndex;
    };

    explicit AnimationRegistryEntry(IRadientSkeletonAnimationAsset* pAnimation) :
        m_pAnimation{pAnimation}
    {
        VERIFY_EXPR(m_pAnimation != nullptr);
    }

    AnimationRegistryEntry() = delete;

    // clang-format off
    AnimationRegistryEntry           (const AnimationRegistryEntry&)     = delete;
    AnimationRegistryEntry& operator=(const AnimationRegistryEntry&)     = delete;
    AnimationRegistryEntry           (AnimationRegistryEntry&&) noexcept = default;
    AnimationRegistryEntry& operator=(AnimationRegistryEntry&&) noexcept = default;
    // clang-format on

    IRadientSkeletonAnimationAsset* GetAnimation() const noexcept
    {
        return m_pAnimation;
    }

    size_t GetTargetCount() const noexcept
    {
        VerifyInvariants();
        return m_Targets.size();
    }

    bool IsEmpty() const noexcept
    {
        return GetTargetCount() == 0;
    }

    size_t FindTarget(RadientEntityID Entity) const noexcept
    {
        const auto It = m_TargetIndices.find(Entity);
        if (It == m_TargetIndices.end())
            return InvalidIndex;

        VERIFY_EXPR(It->second < m_Targets.size());
        VERIFY_EXPR(m_Targets[It->second].Entity == Entity);
        return It->second;
    }

    void ReserveTargets(size_t TargetCount)
    {
        m_Targets.reserve(TargetCount);
        m_Poses.reserve(TargetCount);
        m_EntityAssociationIndices.reserve(TargetCount);
        m_TargetIndices.reserve(TargetCount);
    }

    void AddTarget(RadientEntityID                     Entity,
                   RefCntAutoPtr<IRadientSkeletonPose> pPose,
                   size_t                              EntityAssociationIndex);

    RemovedTarget RemoveTarget(size_t TargetIndex) noexcept;

    void SetEntityAssociationIndex(size_t TargetIndex, size_t EntityAssociationIndex) noexcept;
    void WritePublicEntry(RadientAnimationRegistryEntry& PublicEntry) const noexcept;

private:
    void VerifyInvariants() const noexcept;

private:
    // Retains the animation used as a raw key by the registry lookup maps.
    RefCntAutoPtr<IRadientSkeletonAnimationAsset> m_pAnimation;

    // Contiguous target array exposed through RadientAnimationRegistryEntry.
    std::vector<RadientAnimationTarget> m_Targets;

    // Retains the poses referenced by the borrowed pointers in m_Targets.
    std::vector<RefCntAutoPtr<IRadientSkeletonPose>> m_Poses;

    // Reciprocal index into EntityAssociations::Animations for O(1) removal.
    std::vector<size_t> m_EntityAssociationIndices;

    // Maps an entity to its m_Targets index for O(1) lookup and swap-erase repair.
    absl::flat_hash_map<RadientEntityID, size_t> m_TargetIndices;
};


void AnimationRegistryEntry::AddTarget(RadientEntityID                     Entity,
                                       RefCntAutoPtr<IRadientSkeletonPose> pPose,
                                       size_t                              EntityAssociationIndex)
{
    VerifyInvariants();
    VERIFY_EXPR(pPose != nullptr);
    VERIFY_EXPR(FindTarget(Entity) == InvalidIndex);

    const size_t TargetIndex = m_Targets.size();
    m_Targets.push_back({Entity, pPose});
    m_Poses.emplace_back(std::move(pPose));
    m_EntityAssociationIndices.push_back(EntityAssociationIndex);
    const auto InsertResult = m_TargetIndices.emplace(Entity, TargetIndex);
    VERIFY_EXPR(InsertResult.second);
    static_cast<void>(InsertResult);

    VerifyInvariants();
}

AnimationRegistryEntry::RemovedTarget AnimationRegistryEntry::RemoveTarget(size_t TargetIndex) noexcept
{
    VerifyInvariants();
    VERIFY_EXPR(TargetIndex < m_Targets.size());

    const RemovedTarget Removed{
        m_Targets[TargetIndex].Entity,
        m_EntityAssociationIndices[TargetIndex],
    };

    const size_t NumErasedTargets = m_TargetIndices.erase(Removed.Entity);
    VERIFY_EXPR(NumErasedTargets == 1);
    static_cast<void>(NumErasedTargets);

    const size_t LastTarget = m_Targets.size() - 1;
    if (TargetIndex != LastTarget)
    {
        m_Targets[TargetIndex]                  = m_Targets[LastTarget];
        m_Poses[TargetIndex]                    = std::move(m_Poses[LastTarget]);
        m_EntityAssociationIndices[TargetIndex] = m_EntityAssociationIndices[LastTarget];

        auto MovedTargetIt = m_TargetIndices.find(m_Targets[TargetIndex].Entity);
        VERIFY_EXPR(MovedTargetIt != m_TargetIndices.end());
        MovedTargetIt->second = TargetIndex;
    }
    m_Targets.pop_back();
    m_Poses.pop_back();
    m_EntityAssociationIndices.pop_back();

    VerifyInvariants();
    return Removed;
}

void AnimationRegistryEntry::SetEntityAssociationIndex(size_t TargetIndex, size_t EntityAssociationIndex) noexcept
{
    VerifyInvariants();
    VERIFY_EXPR(TargetIndex < m_EntityAssociationIndices.size());
    m_EntityAssociationIndices[TargetIndex] = EntityAssociationIndex;
}

void AnimationRegistryEntry::WritePublicEntry(RadientAnimationRegistryEntry& PublicEntry) const noexcept
{
    VerifyInvariants();
    PublicEntry.pAnimation  = m_pAnimation;
    PublicEntry.pTargets    = m_Targets.empty() ? nullptr : m_Targets.data();
    PublicEntry.TargetCount = static_cast<Uint32>(m_Targets.size());
}

void AnimationRegistryEntry::VerifyInvariants() const noexcept
{
    VERIFY_EXPR(m_pAnimation != nullptr);
    VERIFY_EXPR(m_Targets.size() == m_Poses.size());
    VERIFY_EXPR(m_Targets.size() == m_EntityAssociationIndices.size());
    VERIFY_EXPR(m_Targets.size() == m_TargetIndices.size());
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE AddAnimatedEntities(IRadientSkeletonAnimationAsset* pAnimation,
                                                                  const RadientEntityID*          pEntities,
                                                                  Uint32                          EntityCount) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimatedEntities(IRadientSkeletonAnimationAsset* pAnimation,
                                                                     const RadientEntityID*          pEntities,
                                                                     Uint32                          EntityCount) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveEntity(RadientEntityID Entity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimation(IRadientSkeletonAnimationAsset* pAnimation) override final;

    virtual const RadientAnimationRegistryState& DILIGENT_CALL_TYPE GetState() const override final
    {
        return m_State;
    }

private:
    struct EntityAssociations
    {
        // Reverse entity-to-animation index used by RemoveEntity(). Every raw
        // pointer is retained by its corresponding AnimationRegistryEntry.
        std::vector<IRadientSkeletonAnimationAsset*> Animations;
    };

    size_t FindEntry(IRadientSkeletonAnimationAsset* pAnimation) const noexcept;
    void   UpdatePublicEntry(size_t EntryIndex) noexcept;
    void   RemoveTarget(size_t EntryIndex, size_t TargetIndex) noexcept;
    void   RemoveEntry(size_t EntryIndex) noexcept;
    void   PublishMutation() noexcept;

private:
    RefCntAutoPtr<IRadientScene> m_pScene;

    // Dense storage indexed by m_EntryIndices and maintained with swap-erase.
    std::vector<AnimationRegistryEntry> m_Entries;

    // Public projections parallel to m_Entries; m_State points at this array.
    std::vector<RadientAnimationRegistryEntry> m_PublicEntries;

    // Maps retained animation identities to their dense m_Entries index.
    absl::flat_hash_map<IRadientSkeletonAnimationAsset*, size_t> m_EntryIndices;

    // Finds every animation associated with an entity without scanning entries.
    absl::flat_hash_map<RadientEntityID, EntityAssociations> m_EntityAssociations;

    // Published state whose entry pointer aliases m_PublicEntries.
    RadientAnimationRegistryState m_State;
};

size_t RadientAnimationRegistryImpl::FindEntry(IRadientSkeletonAnimationAsset* pAnimation) const noexcept
{
    const auto It = m_EntryIndices.find(pAnimation);
    if (It == m_EntryIndices.end())
        return InvalidIndex;

    VERIFY_EXPR(It->second < m_Entries.size());
    VERIFY_EXPR(m_Entries[It->second].GetAnimation() == pAnimation);
    return It->second;
}

void RadientAnimationRegistryImpl::UpdatePublicEntry(size_t EntryIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());
    VERIFY_EXPR(EntryIndex < m_PublicEntries.size());

    const AnimationRegistryEntry& EntryData = m_Entries[EntryIndex];
    EntryData.WritePublicEntry(m_PublicEntries[EntryIndex]);
}

void RadientAnimationRegistryImpl::RemoveTarget(size_t EntryIndex, size_t TargetIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());

    AnimationRegistryEntry&                     EntryData = m_Entries[EntryIndex];
    const AnimationRegistryEntry::RemovedTarget Removed   = EntryData.RemoveTarget(TargetIndex);

    auto EntityIt = m_EntityAssociations.find(Removed.Entity);
    VERIFY_EXPR(EntityIt != m_EntityAssociations.end());
    std::vector<IRadientSkeletonAnimationAsset*>& Animations = EntityIt->second.Animations;
    VERIFY_EXPR(Removed.EntityAssociationIndex < Animations.size());
    VERIFY_EXPR(Animations[Removed.EntityAssociationIndex] == EntryData.GetAnimation());

    const size_t LastEntityAssociation = Animations.size() - 1;
    if (Removed.EntityAssociationIndex != LastEntityAssociation)
    {
        IRadientSkeletonAnimationAsset* const pMovedAnimation = Animations[LastEntityAssociation];
        Animations[Removed.EntityAssociationIndex]            = pMovedAnimation;

        const size_t MovedEntryIndex = FindEntry(pMovedAnimation);
        VERIFY_EXPR(MovedEntryIndex != InvalidIndex);
        AnimationRegistryEntry& MovedEntry       = m_Entries[MovedEntryIndex];
        const size_t            MovedTargetIndex = MovedEntry.FindTarget(Removed.Entity);
        VERIFY_EXPR(MovedTargetIndex != InvalidIndex);
        MovedEntry.SetEntityAssociationIndex(MovedTargetIndex, Removed.EntityAssociationIndex);
    }
    Animations.pop_back();
    if (Animations.empty())
        m_EntityAssociations.erase(EntityIt);
}

void RadientAnimationRegistryImpl::RemoveEntry(size_t EntryIndex) noexcept
{
    VERIFY_EXPR(EntryIndex < m_Entries.size());
    VERIFY_EXPR(m_Entries.size() == m_PublicEntries.size());
    VERIFY_EXPR(m_Entries[EntryIndex].IsEmpty());

    IRadientSkeletonAnimationAsset* const pRemovedAnimation = m_Entries[EntryIndex].GetAnimation();
    const size_t                          NumErasedEntries  = m_EntryIndices.erase(pRemovedAnimation);
    VERIFY_EXPR(NumErasedEntries == 1);
    static_cast<void>(NumErasedEntries);

    const size_t LastEntry = m_Entries.size() - 1;
    if (EntryIndex != LastEntry)
    {
        m_Entries[EntryIndex] = std::move(m_Entries[LastEntry]);

        auto MovedEntryIt = m_EntryIndices.find(m_Entries[EntryIndex].GetAnimation());
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

    ++m_State.Revision;
    m_State.pEntries   = m_PublicEntries.empty() ? nullptr : m_PublicEntries.data();
    m_State.EntryCount = static_cast<Uint32>(m_PublicEntries.size());
}

RADIENT_STATUS RadientAnimationRegistryImpl::AddAnimatedEntities(IRadientSkeletonAnimationAsset* pAnimation,
                                                                 const RadientEntityID*          pEntities,
                                                                 Uint32                          EntityCount)
{
    if (pAnimation == nullptr || (EntityCount != 0 && pEntities == nullptr))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (EntityCount == 0)
        return RADIENT_STATUS_NO_CHANGE;

    struct PendingTarget
    {
        RadientEntityID                     Entity = InvalidRadientEntityID;
        RefCntAutoPtr<IRadientSkeletonPose> pPose;
    };

    std::vector<RadientEntityID> InsertedEntityAssociations;
    try
    {
        const size_t                  ExistingEntryIndex = FindEntry(pAnimation);
        const AnimationRegistryEntry* pExistingEntry     = ExistingEntryIndex != InvalidIndex ?
            &m_Entries[ExistingEntryIndex] :
            nullptr;

        absl::flat_hash_map<RadientEntityID, bool> BatchEntities;
        BatchEntities.reserve(EntityCount);

        std::vector<PendingTarget> PendingTargets;
        PendingTargets.reserve(EntityCount);

        IRadientSkeletonAsset* const pAnimationSkeleton = pAnimation->GetDesc().pSkeleton;
        if (pAnimationSkeleton == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        for (Uint32 EntityIndex = 0; EntityIndex < EntityCount; ++EntityIndex)
        {
            const RadientEntityID Entity = pEntities[EntityIndex];
            if ((pExistingEntry != nullptr && pExistingEntry->FindTarget(Entity) != InvalidIndex) ||
                !BatchEntities.emplace(Entity, true).second)
            {
                continue;
            }

            RadientSkinComponent Skin;
            const RADIENT_STATUS SkinStatus = m_pScene->GetSkin(Entity, Skin);
            if (SkinStatus != RADIENT_STATUS_OK)
                return SkinStatus;

            if (Skin.pSkin == nullptr || Skin.pPose == nullptr ||
                Skin.pSkin->GetDesc().pSkeleton != Skin.pPose->GetSkeleton())
            {
                UNEXPECTED("Scene returned invalid skin data for entity ", Entity);
                return RADIENT_STATUS_INVALID_OPERATION;
            }
            if (Skin.pPose->GetSkeleton() != pAnimationSkeleton)
                return RADIENT_STATUS_INVALID_ARGUMENT;

            PendingTargets.push_back({Entity, RefCntAutoPtr<IRadientSkeletonPose>{Skin.pPose}});
        }

        if (PendingTargets.empty())
            return RADIENT_STATUS_NO_CHANGE;

        // Reserve every auxiliary container before changing the published
        // target array. Adding each validated association below is then O(1).
        InsertedEntityAssociations.reserve(PendingTargets.size());
        m_EntityAssociations.reserve(m_EntityAssociations.size() + PendingTargets.size());
        for (const PendingTarget& Target : PendingTargets)
        {
            auto InsertResult = m_EntityAssociations.try_emplace(Target.Entity);
            if (InsertResult.second)
                InsertedEntityAssociations.push_back(Target.Entity);
            InsertResult.first->second.Animations.reserve(InsertResult.first->second.Animations.size() + 1);
        }

        AnimationRegistryEntry* pEntry = nullptr;
        if (pExistingEntry != nullptr)
        {
            pEntry                      = &m_Entries[ExistingEntryIndex];
            const size_t NewTargetCount = pEntry->GetTargetCount() + PendingTargets.size();
            pEntry->ReserveTargets(NewTargetCount);
        }
        else
        {
            AnimationRegistryEntry NewEntry{pAnimation};
            NewEntry.ReserveTargets(PendingTargets.size());

            m_EntryIndices.reserve(m_EntryIndices.size() + 1);
            m_Entries.reserve(m_Entries.size() + 1);
            m_PublicEntries.reserve(m_PublicEntries.size() + 1);

            const size_t NewEntryIndex = m_Entries.size();
            m_Entries.emplace_back(std::move(NewEntry));
            m_PublicEntries.emplace_back();
            m_EntryIndices.emplace(pAnimation, NewEntryIndex);
            pEntry = &m_Entries[NewEntryIndex];
        }

        const size_t EntryIndex = FindEntry(pAnimation);
        VERIFY_EXPR(EntryIndex != InvalidIndex);
        VERIFY_EXPR(pEntry == &m_Entries[EntryIndex]);

        for (PendingTarget& Target : PendingTargets)
        {
            auto EntityIt = m_EntityAssociations.find(Target.Entity);
            VERIFY_EXPR(EntityIt != m_EntityAssociations.end());

            const size_t EntityAssociationIndex = EntityIt->second.Animations.size();
            EntityIt->second.Animations.push_back(pAnimation);
            pEntry->AddTarget(Target.Entity, std::move(Target.pPose), EntityAssociationIndex);
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
            if (It != m_EntityAssociations.end() && It->second.Animations.empty())
                m_EntityAssociations.erase(It);
        }
        LOG_ERROR_MESSAGE("Failed to add animation registry targets: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        for (RadientEntityID Entity : InsertedEntityAssociations)
        {
            auto It = m_EntityAssociations.find(Entity);
            if (It != m_EntityAssociations.end() && It->second.Animations.empty())
                m_EntityAssociations.erase(It);
        }
        LOG_ERROR_MESSAGE("Failed to add animation registry targets");
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientAnimationRegistryImpl::RemoveAnimatedEntities(IRadientSkeletonAnimationAsset* pAnimation,
                                                                    const RadientEntityID*          pEntities,
                                                                    Uint32                          EntityCount)
{
    if (pAnimation == nullptr || (EntityCount != 0 && pEntities == nullptr))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (EntityCount == 0)
        return RADIENT_STATUS_NO_CHANGE;

    const size_t EntryIndex = FindEntry(pAnimation);
    if (EntryIndex == InvalidIndex)
        return RADIENT_STATUS_NO_CHANGE;

    bool Changed = false;
    for (Uint32 EntityIndex = 0; EntityIndex < EntityCount; ++EntityIndex)
    {
        const size_t TargetIndex = m_Entries[EntryIndex].FindTarget(pEntities[EntityIndex]);
        if (TargetIndex == InvalidIndex)
            continue;

        RemoveTarget(EntryIndex, TargetIndex);
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

    VERIFY_EXPR(!EntityIt->second.Animations.empty());
    while ((EntityIt = m_EntityAssociations.find(Entity)) != m_EntityAssociations.end())
    {
        VERIFY_EXPR(!EntityIt->second.Animations.empty());
        IRadientSkeletonAnimationAsset* const pAnimation = EntityIt->second.Animations.back();
        const size_t                          EntryIndex = FindEntry(pAnimation);
        VERIFY_EXPR(EntryIndex != InvalidIndex);

        AnimationRegistryEntry& EntryData   = m_Entries[EntryIndex];
        const size_t            TargetIndex = EntryData.FindTarget(Entity);
        VERIFY_EXPR(TargetIndex != InvalidIndex);

        RemoveTarget(EntryIndex, TargetIndex);
        if (EntryData.IsEmpty())
            RemoveEntry(EntryIndex);
        else
            UpdatePublicEntry(EntryIndex);
    }

    PublishMutation();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAnimationRegistryImpl::RemoveAnimation(IRadientSkeletonAnimationAsset* pAnimation)
{
    if (pAnimation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const size_t EntryIndex = FindEntry(pAnimation);
    if (EntryIndex == InvalidIndex)
        return RADIENT_STATUS_NO_CHANGE;

    while (!m_Entries[EntryIndex].IsEmpty())
        RemoveTarget(EntryIndex, m_Entries[EntryIndex].GetTargetCount() - 1);

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
