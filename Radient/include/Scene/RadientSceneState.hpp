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

#include "RadientScene.h"
#include "FlagEnum.h"
#include "Scene/Components/RadientMaterialBindingsStorage.hpp"
#include "Scene/Components/RadientMeshComponentStorage.hpp"

#include "entt/entity/registry.hpp"
#include "entt/entity/storage.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

// RadientSceneState is not internally synchronized. Access from multiple threads must be externally synchronized.
// Enumeration callbacks must not mutate the scene or call methods that may update cached derived state.
// Renderable data passed to enumeration callbacks references registry-owned storage and is valid only
// for the duration of the callback.
class RadientSceneState
{
public:
    struct RenderableMesh
    {
        RadientEntityID                         Entity = InvalidRadientEntityID;
        const RadientMeshComponent&             Mesh;
        const RadientMeshRendererComponent&     Renderer;
        const RadientMaterialBindingsComponent* pMaterialBindings = nullptr;
        const RadientMatrix4x4&                 WorldMatrix;
        const Bool&                             EffectiveVisible;
    };

    struct RenderableLight
    {
        RadientEntityID              Entity = InvalidRadientEntityID;
        const RadientLightComponent& Light;
        const RadientMatrix4x4&      WorldMatrix;
        const Bool&                  EffectiveVisible;
    };

    enum class RenderableMeshChangeType
    {
        Added,
        Removed,
        Updated
    };

    struct RenderableMeshChange
    {
        RadientEntityID          Entity = InvalidRadientEntityID;
        RenderableMeshChangeType Type   = RenderableMeshChangeType::Updated;
    };

    enum class RenderableLightChangeType
    {
        Added,
        Removed,
        Updated
    };

    struct RenderableLightChange
    {
        RadientEntityID           Entity = InvalidRadientEntityID;
        RenderableLightChangeType Type   = RenderableLightChangeType::Updated;
    };

    struct RenderableChangeLogState
    {
        // Pending renderable changes are a delta log. When the log is cleared,
        // the base revision moves to the current scene revision; caches older
        // than this base can no longer safely consume incremental changes.
        RadientRevision MeshesBaseRevision = 0;
        RadientRevision LightsBaseRevision = 0;
    };

    RadientSceneState();
    explicit RadientSceneState(const RadientSceneDesc& Desc);

    // clang-format off
    RadientSceneState           (const RadientSceneState&) = delete;
    RadientSceneState& operator=(const RadientSceneState&) = delete;
    RadientSceneState           (RadientSceneState&&)      = delete;
    RadientSceneState& operator=(RadientSceneState&&)      = delete;
    // clang-format on

    const RadientSceneDesc& GetDesc() const;

    RADIENT_STATUS IsEntityAlive(RadientEntityID Entity) const;
    RADIENT_STATUS GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const;
    RADIENT_STATUS GetEntityOwnVisibility(RadientEntityID Entity, Bool& Visible) const;
    RADIENT_STATUS GetEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible);
    RADIENT_STATUS GetCachedEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible) const;
    RADIENT_STATUS GetParent(RadientEntityID Entity, RadientEntityID& Parent) const;
    RADIENT_STATUS GetChildCount(RadientEntityID Entity, Uint32& ChildCount) const;
    RADIENT_STATUS GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren, Uint32& NumChildrenWritten) const;
    RADIENT_STATUS GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const;
    RADIENT_STATUS GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix);
    RADIENT_STATUS GetCachedWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const;
    RADIENT_STATUS GetCamera(RadientEntityID Entity, RadientCameraComponent& Camera) const;
    RADIENT_STATUS HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType, Bool& HasComponent) const;

    const RadientEnvironmentDesc& GetEnvironment() const;

    const RadientSceneRevisions&    GetSceneRevisions() const;
    const RenderableChangeLogState& GetRenderableChangeLogState() const;

    // Callback receives RenderableMesh by value, but its members reference registry-owned component data.
    template <typename CallbackType>
    RADIENT_STATUS EnumerateRenderableMeshes(CallbackType&& Callback) const;

    // The renderable mesh pointer is valid only during the callback and is null for removed renderables.
    template <typename CallbackType>
    void EnumerateRenderableMeshChanges(CallbackType&& Callback) const;

    // Callback receives RenderableLight by value, but its members reference registry-owned component data.
    template <typename CallbackType>
    RADIENT_STATUS EnumerateRenderableLights(CallbackType&& Callback) const;

    // The renderable light pointer is valid only during the callback and is null for removed lights.
    template <typename CallbackType>
    void EnumerateRenderableLightChanges(CallbackType&& Callback) const;

    void ClearRenderableChanges();
    void ClearRenderableMeshChanges();
    void ClearRenderableLightChanges();

    RADIENT_STATUS CreateEntity(const RadientEntityDesc& Desc, RadientEntityID& Entity);
    RADIENT_STATUS DestroyEntity(RadientEntityID Entity);

    RADIENT_STATUS SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags);
    RADIENT_STATUS SetEntityOwnVisibility(RadientEntityID Entity, Bool Visible);
    RADIENT_STATUS SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform);
    RADIENT_STATUS SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform);
    RADIENT_STATUS SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera);
    RADIENT_STATUS SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh);
    RADIENT_STATUS SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer);
    RADIENT_STATUS SetMaterialBindings(RadientEntityID Entity, const RadientMaterialBindingsComponent& Bindings);
    RADIENT_STATUS SetLight(RadientEntityID Entity, const RadientLightComponent& Light);
    RADIENT_STATUS SetEnvironment(const RadientEnvironmentDesc& Environment);
    RADIENT_STATUS SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component);
    RADIENT_STATUS RemoveComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType);
    RADIENT_STATUS CommitChanges();

private:
    enum DIRTY_FLAGS : Uint32
    {
        DIRTY_FLAG_NONE = 0u,

        // Local transform changed; cached world transform must be recomputed.
        DIRTY_FLAG_TRANSFORM = 1u << 0u,

        // Own visibility changed; cached effective visibility must be recomputed.
        DIRTY_FLAG_VISIBILITY = 1u << 1u,

        // These flags affect descendants and must be propagated through the hierarchy.
        DIRTY_FLAGS_REQUIRING_PROPAGATION = DIRTY_FLAG_TRANSFORM | DIRTY_FLAG_VISIBILITY
    };
    DECLARE_FRIEND_FLAG_ENUM_OPERATORS(DIRTY_FLAGS);

    enum CHANGE_FLAGS : Uint32
    {
        CHANGE_FLAG_NONE = 0u,

        // Mesh, mesh renderer, or material bindings changed.
        CHANGE_FLAG_DRAWABLES = 1u << 0u,

        // Light component data changed.
        CHANGE_FLAG_LIGHTS = 1u << 1u,

        // Local transform or hierarchy changed.
        CHANGE_FLAG_TRANSFORMS = 1u << 2u,

        // Own visibility or hierarchy changed.
        CHANGE_FLAG_VISIBILITY = 1u << 3u,

        // Camera component data changed.
        CHANGE_FLAG_CAMERAS = 1u << 4u,

        // Scene environment data changed.
        CHANGE_FLAG_ENVIRONMENT = 1u << 5u,

        // Custom component data changed.
        CHANGE_FLAG_CUSTOM_COMPONENTS = 1u << 6u
    };
    DECLARE_FRIEND_FLAG_ENUM_OPERATORS(CHANGE_FLAGS);

    static constexpr size_t InvalidDirtyListIndex = static_cast<size_t>(-1);

    struct EntityComponent
    {
        RadientEntityID ID = InvalidRadientEntityID;
        std::string     Name;
    };

    struct EntityStateComponent
    {
        RADIENT_ENTITY_FLAGS Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    };

    struct HierarchyComponent
    {
        entt::entity              Parent = entt::null;
        std::vector<entt::entity> Children;
    };

    struct LocalTransformComponent
    {
        RadientTransform Transform;
    };

    struct WorldTransformComponent
    {
        RadientMatrix4x4 Matrix;
    };

    struct EffectiveVisibilityComponent
    {
        Bool Visible = True;
    };

    struct RenderableMeshStateComponent
    {
        bool IsRenderable = false;
    };

    struct PendingRenderableMeshChangeComponent
    {
        RenderableMeshChangeType Type = RenderableMeshChangeType::Updated;
    };

    struct PendingRenderableLightChangeComponent
    {
        RenderableLightChangeType Type = RenderableLightChangeType::Updated;
    };

    struct DirtyStateComponent
    {
        DIRTY_FLAGS Flags          = DIRTY_FLAG_NONE;
        size_t      DirtyListIndex = InvalidDirtyListIndex;

        bool IsInDirtyList() const
        {
            return DirtyListIndex != InvalidDirtyListIndex;
        }
    };

    struct DirtyWorkItem
    {
        entt::entity Entity = entt::null;
        DIRTY_FLAGS  Flags  = DIRTY_FLAG_NONE;
    };

    struct DestroyWorkItem
    {
        entt::entity Entity         = entt::null;
        size_t       NextChildIndex = 0;
    };

    struct CustomComponentStorage
    {
        std::string        Name;
        std::string        Schema;
        Uint32             Version = 0;
        std::vector<Uint8> Data;
    };

    struct CustomComponentIndexComponent
    {
        std::vector<RadientComponentTypeID> ComponentTypes;
    };

    using CustomComponentStore = entt::storage<CustomComponentStorage>;

    template <typename ComponentSourceType>
    RenderableMesh MakeRenderableMesh(entt::entity Entity, const ComponentSourceType& ComponentSource) const;
    template <typename ComponentSourceType>
    RenderableLight MakeRenderableLight(entt::entity Entity, const ComponentSourceType& ComponentSource) const;

    entt::entity FindEntity(RadientEntityID Entity) const;

    bool IsDescendant(entt::entity Entity, entt::entity PotentialAncestor) const;
    bool IsRenderableMeshEntity(entt::entity Entity) const;
    bool IsRenderableLightEntity(entt::entity Entity) const;

    void         DetachFromParent(entt::entity Entity);
    CHANGE_FLAGS DestroyEntitySubtree(entt::entity Entity);
    bool         RemoveCustomComponents(entt::entity Entity);
    void         RecordRenderableMeshChange(entt::entity Entity, RenderableMeshChangeType Type);
    void         RecordRenderableMeshUpdated(entt::entity Entity);
    bool         RecordRenderableMeshRemoved(entt::entity Entity);
    void         UpdateRenderableMeshState(entt::entity Entity);
    void         RecordRenderableLightChange(entt::entity Entity, RenderableLightChangeType Type);
    void         RecordRenderableLightUpdated(entt::entity Entity);
    bool         RecordRenderableLightRemoved(entt::entity Entity);
    DIRTY_FLAGS  MarkDirty(entt::entity Entity, DIRTY_FLAGS Flags, bool AddToDirtyList = true);
    void         ClearDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags);
    void         RemoveFromDirtyList(entt::entity Entity, DirtyStateComponent& DirtyState);
    void         PropagateDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags);
    void         MarkChildrenDirtyExcept(entt::entity Entity, DIRTY_FLAGS Flags, entt::entity ExcludedChild);
    void         UpdateDirtyEntities();
    void         UpdateDirtySubtree(entt::entity Entity, DIRTY_FLAGS InheritedFlags);
    void         UpdateDerivedStatePathToRoot(entt::entity Entity, DIRTY_FLAGS Flags);
    void         UpdateEntityDerivedState(entt::entity Entity, DIRTY_FLAGS Flags);
    void         Touch(CHANGE_FLAGS ChangeFlags = CHANGE_FLAG_NONE);

    const std::string m_Name;
    RadientSceneDesc  m_Desc;

    using CustomComponentStoresMapType = std::unordered_map<RadientComponentTypeID, CustomComponentStore>;
    using EntityMapType                = absl::flat_hash_map<RadientEntityID, entt::entity>;
    entt::registry                      m_Registry;
    EntityMapType                       m_EntityMap;
    CustomComponentStoresMapType        m_CustomComponentStores;
    RadientEntityID                     m_NextEntityID = 1;
    RadientSceneRevisions               m_SceneRevisions;
    RenderableChangeLogState            m_RenderableChangeLogState;
    RadientEnvironmentDesc              m_Environment;
    RefCntAutoPtr<IRadientTextureAsset> m_pEnvironmentMap;
    std::vector<RenderableMeshChange>   m_RemovedRenderableMeshChanges;
    std::vector<RenderableLightChange>  m_RemovedRenderableLightChanges;

    // Conservative scene-wide mask of derived states that may be dirty anywhere in the scene.
    DIRTY_FLAGS m_DirtyFlags = DIRTY_FLAG_NONE;

    // Entities where dirty state was introduced directly. DirtyStateComponent::DirtyListIndex stores the entity's
    // slot and prevents duplicate insertions. Removal uses swap-erase, so the list contains only live dirty entries.
    std::vector<entt::entity> m_DirtyEntities;

    // Reused path scratch buffer for lazy parent-to-child derived-state updates.
    std::vector<entt::entity> m_TmpEntityBuffer;

    // Reused stack for iterative post-order destruction.
    std::vector<DestroyWorkItem> m_TmpDestroyStack;

    // Reused stack for iterative dirty subtree traversal.
    std::vector<DirtyWorkItem> m_TmpDirtyWorkItems;
};

DEFINE_FLAG_ENUM_OPERATORS(RadientSceneState::DIRTY_FLAGS);
DEFINE_FLAG_ENUM_OPERATORS(RadientSceneState::CHANGE_FLAGS);

template <typename ComponentSourceType>
inline RadientSceneState::RenderableMesh RadientSceneState::MakeRenderableMesh(entt::entity Entity, const ComponentSourceType& ComponentSource) const
{
    const EntityComponent&              EntityData       = ComponentSource.template get<const EntityComponent>(Entity);
    const MeshComponentStorage&         MeshStorage      = ComponentSource.template get<const MeshComponentStorage>(Entity);
    const RadientMeshRendererComponent& Renderer         = ComponentSource.template get<const RadientMeshRendererComponent>(Entity);
    const WorldTransformComponent&      WorldTransform   = ComponentSource.template get<const WorldTransformComponent>(Entity);
    const EffectiveVisibilityComponent& EffectiveVisible = ComponentSource.template get<const EffectiveVisibilityComponent>(Entity);

    const MaterialBindingsStorage* pMaterialBindings = m_Registry.try_get<MaterialBindingsStorage>(Entity);

    return RenderableMesh{
        EntityData.ID,
        MeshStorage.Component,
        Renderer,
        pMaterialBindings != nullptr ? &pMaterialBindings->Component : nullptr,
        WorldTransform.Matrix,
        EffectiveVisible.Visible};
}

template <typename ComponentSourceType>
inline RadientSceneState::RenderableLight RadientSceneState::MakeRenderableLight(entt::entity Entity, const ComponentSourceType& ComponentSource) const
{
    const EntityComponent&              EntityData       = ComponentSource.template get<const EntityComponent>(Entity);
    const RadientLightComponent&        Light            = ComponentSource.template get<const RadientLightComponent>(Entity);
    const WorldTransformComponent&      WorldTransform   = ComponentSource.template get<const WorldTransformComponent>(Entity);
    const EffectiveVisibilityComponent& EffectiveVisible = ComponentSource.template get<const EffectiveVisibilityComponent>(Entity);

    return RenderableLight{
        EntityData.ID,
        Light,
        WorldTransform.Matrix,
        EffectiveVisible.Visible};
}

template <typename CallbackType>
RADIENT_STATUS RadientSceneState::EnumerateRenderableMeshes(CallbackType&& Callback) const
{
    auto View = m_Registry.view<const EntityComponent,
                                const MeshComponentStorage,
                                const RadientMeshRendererComponent,
                                const WorldTransformComponent,
                                const EffectiveVisibilityComponent>();

    for (const entt::entity Entity : View)
    {
        Callback(MakeRenderableMesh(Entity, View));
    }

    return (m_DirtyFlags & DIRTY_FLAGS_REQUIRING_PROPAGATION) != DIRTY_FLAG_NONE ?
        RADIENT_STATUS_OUT_OF_DATE :
        RADIENT_STATUS_OK;
}

template <typename CallbackType>
void RadientSceneState::EnumerateRenderableMeshChanges(CallbackType&& Callback) const
{
    for (const RenderableMeshChange& Change : m_RemovedRenderableMeshChanges)
        Callback(Change, static_cast<const RenderableMesh*>(nullptr));

    auto View = m_Registry.view<const EntityComponent, const PendingRenderableMeshChangeComponent>();
    for (const entt::entity Entity : View)
    {
        const EntityComponent&                      EntityData = View.get<const EntityComponent>(Entity);
        const PendingRenderableMeshChangeComponent& Pending    = View.get<const PendingRenderableMeshChangeComponent>(Entity);
        const RenderableMeshChange                  Change{EntityData.ID, Pending.Type};

        if (Pending.Type == RenderableMeshChangeType::Removed || !IsRenderableMeshEntity(Entity))
        {
            Callback(Change, static_cast<const RenderableMesh*>(nullptr));
            continue;
        }

        const RenderableMesh Mesh = MakeRenderableMesh(Entity, m_Registry);
        Callback(Change, &Mesh);
    }
}

template <typename CallbackType>
RADIENT_STATUS RadientSceneState::EnumerateRenderableLights(CallbackType&& Callback) const
{
    auto View = m_Registry.view<const EntityComponent,
                                const RadientLightComponent,
                                const WorldTransformComponent,
                                const EffectiveVisibilityComponent>();

    for (const entt::entity Entity : View)
    {
        Callback(MakeRenderableLight(Entity, View));
    }

    return (m_DirtyFlags & DIRTY_FLAGS_REQUIRING_PROPAGATION) != DIRTY_FLAG_NONE ?
        RADIENT_STATUS_OUT_OF_DATE :
        RADIENT_STATUS_OK;
}

template <typename CallbackType>
void RadientSceneState::EnumerateRenderableLightChanges(CallbackType&& Callback) const
{
    for (const RenderableLightChange& Change : m_RemovedRenderableLightChanges)
        Callback(Change, static_cast<const RenderableLight*>(nullptr));

    auto View = m_Registry.view<const EntityComponent, const PendingRenderableLightChangeComponent>();
    for (const entt::entity Entity : View)
    {
        const EntityComponent&                       EntityData = View.get<const EntityComponent>(Entity);
        const PendingRenderableLightChangeComponent& Pending    = View.get<const PendingRenderableLightChangeComponent>(Entity);
        const RenderableLightChange                  Change{EntityData.ID, Pending.Type};

        if (Pending.Type == RenderableLightChangeType::Removed || !IsRenderableLightEntity(Entity))
        {
            Callback(Change, static_cast<const RenderableLight*>(nullptr));
            continue;
        }

        const RenderableLight Light = MakeRenderableLight(Entity, m_Registry);
        Callback(Change, &Light);
    }
}

} // namespace Diligent
