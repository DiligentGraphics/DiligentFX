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

#include "Animation/RadientSceneAnimationDestinationImpl.hpp"
#include "Animation/RadientNodeAnimationProperty.hpp"
#include "Core/RadientValidation.hpp"
#include "Scene/RadientSceneWriterImpl.hpp"
#include "Scene/RadientSceneState.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
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

#include <exception>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

struct SceneAnimationBindingEntry
{
    Uint32                    EntityIndex = 0;
    RadientNodeTransformField Field       = RadientNodeTransformField::Translation;
};

} // namespace

class RadientSceneAnimationDestinationBindingImpl final :
    public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    RadientSceneAnimationDestinationBindingImpl(IReferenceCounters*                     pRefCounters,
                                                IRadientAnimationDestination*           pDestination,
                                                RadientSceneState&                      State,
                                                std::vector<RadientEntityID>            Entities,
                                                std::vector<SceneAnimationBindingEntry> Entries) :
        TBase{pRefCounters},
        m_pDestination{pDestination},
        m_State{State},
        m_Entities{std::move(Entities)},
        m_Entries{std::move(Entries)},
        m_ResolvedEntities(m_Entities.size(), static_cast<entt::entity>(entt::null)),
        m_Transforms(m_Entities.size(), nullptr),
        m_Outputs(m_Entries.size(), nullptr)
    {
        VERIFY_EXPR(m_pDestination != nullptr);
        VERIFY_EXPR(!m_Entities.empty());
        VERIFY_EXPR(!m_Entries.empty());
        VERIFY_EXPR(!m_Outputs.empty());
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE BeginUpdate(void* const** ppOutputs) override final
    {
        if (ppOutputs == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppOutputs = nullptr;

        for (size_t EntityIndex = 0; EntityIndex < m_Entities.size(); ++EntityIndex)
        {
            const entt::entity Entity = m_State.FindEntity(m_Entities[EntityIndex]);
            if (Entity == entt::null)
                return RADIENT_STATUS_NOT_FOUND;

            m_ResolvedEntities[EntityIndex] = Entity;
            m_Transforms[EntityIndex]       = &m_State.m_CoreStorages
                                                   .get<RadientSceneState::LocalTransformComponent>(Entity)
                                                   .Transform;
        }

        for (size_t EntryIndex = 0; EntryIndex < m_Entries.size(); ++EntryIndex)
        {
            const SceneAnimationBindingEntry& Entry = m_Entries[EntryIndex];
            VERIFY_EXPR(Entry.EntityIndex < m_Transforms.size());
            VERIFY_EXPR(m_Transforms[Entry.EntityIndex] != nullptr);

            m_Outputs[EntryIndex] = GetRadientNodeTransformFieldAddress(
                *m_Transforms[Entry.EntityIndex], Entry.Field);
            VERIFY_EXPR(m_Outputs[EntryIndex] != nullptr);
        }

        *ppOutputs = m_Outputs.data();
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE EndUpdate(Bool UpdateDerivedState) override final
    {
        for (const entt::entity Entity : m_ResolvedEntities)
            m_State.MarkDirty(Entity, RadientSceneState::DIRTY_FLAG_TRANSFORM);

        m_State.Touch(RadientSceneState::CHANGE_FLAG_TRANSFORMS);

        if (UpdateDerivedState)
        {
            const RADIENT_STATUS Status = m_State.CommitChanges();
            if (RADIENT_FAILED(Status))
                return Status;
        }

        return RADIENT_STATUS_OK;
    }

private:
    const RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
    RadientSceneState&                                m_State;
    const std::vector<RadientEntityID>                m_Entities;
    const std::vector<SceneAnimationBindingEntry>     m_Entries;
    std::vector<entt::entity>                         m_ResolvedEntities;
    std::vector<RadientTransform*>                    m_Transforms;
    std::vector<void*>                                m_Outputs;
};

void RadientSceneAnimationDestinationImpl::QueryInterface(const INTERFACE_ID& IID,
                                                          IObject**           ppInterface)
{
    m_Writer.QueryInterface(IID, ppInterface);
}

ReferenceCounterValueType RadientSceneAnimationDestinationImpl::AddRef()
{
    return m_Writer.AddRef();
}

ReferenceCounterValueType RadientSceneAnimationDestinationImpl::Release()
{
    return m_Writer.Release();
}

IReferenceCounters* RadientSceneAnimationDestinationImpl::GetReferenceCounters() const
{
    return m_Writer.GetReferenceCounters();
}

RADIENT_STATUS RadientSceneAnimationDestinationImpl::CreateBinding(const RadientAnimationPropertyBindingDesc* pProperties,
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
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientEntityID)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(SceneAnimationBindingEntry)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(entt::entity)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientTransform*)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(void*)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(Uint8)))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        pResolvedProperties[PropertyIndex] = {};

    if (m_Writer.m_pState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        absl::flat_hash_map<RadientEntityID, Uint32> EntityIndices;
        std::vector<RadientEntityID>                 Entities;
        std::vector<Uint8>                           TransformFieldMasks;
        std::vector<SceneAnimationBindingEntry>      Entries;
        EntityIndices.reserve(PropertyCount);
        Entities.reserve(PropertyCount);
        TransformFieldMasks.reserve(PropertyCount);
        Entries.reserve(PropertyCount);

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            const RadientAnimationPropertyBindingDesc& Property = pProperties[PropertyIndex];
            RadientNodeAnimationPropertyResolution     Resolution;

            const RADIENT_STATUS Status = ResolveRadientNodeAnimationProperty(Property, Resolution);
            if (Status == RADIENT_STATUS_UNSUPPORTED)
                continue;
            if (Status != RADIENT_STATUS_OK)
                return Status;

            const RadientEntityID Entity = static_cast<RadientEntityID>(Property.DestinationElement);

            auto InsertResult = EntityIndices.try_emplace(Entity, static_cast<Uint32>(Entities.size()));
            if (InsertResult.second)
            {
                const RADIENT_STATUS EntityStatus = m_Writer.m_pState->IsEntityAlive(Entity);
                if (EntityStatus != RADIENT_STATUS_OK)
                    return EntityStatus;

                Entities.push_back(Entity);
                TransformFieldMasks.push_back(0);
            }

            const Uint32 EntityIndex = InsertResult.first->second;
            VERIFY_EXPR(EntityIndex < TransformFieldMasks.size());
            const Uint8 FieldMask = static_cast<Uint8>(Resolution.Field);
            if ((TransformFieldMasks[EntityIndex] & FieldMask) != 0)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            TransformFieldMasks[EntityIndex] = static_cast<Uint8>(TransformFieldMasks[EntityIndex] | FieldMask);

            SceneAnimationBindingEntry& Entry           = Entries.emplace_back();
            Entry.EntityIndex                           = EntityIndex;
            Entry.Field                                 = Resolution.Field;
            pResolvedProperties[PropertyIndex].Semantic = Resolution.Semantic;
        }

        if (Entries.empty())
            return RADIENT_STATUS_UNSUPPORTED;

        RefCntAutoPtr<RadientSceneAnimationDestinationBindingImpl> pBinding{
            MakeNewRCObj<RadientSceneAnimationDestinationBindingImpl>()(
                static_cast<IRadientAnimationDestination*>(this),
                *m_Writer.m_pState,
                std::move(Entities),
                std::move(Entries))};

        *ppBinding = pBinding.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a scene animation binding: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a scene animation binding due to an unknown error");
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
