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

#include "entt/entity/registry.hpp"
#include "entt/entity/storage.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Diligent
{

class RadientSceneState
{
public:
    RadientSceneState();
    explicit RadientSceneState(const RadientSceneDesc& Desc);

    const RadientSceneDesc& GetDesc() const;

    RADIENT_STATUS  IsEntityAlive(RadientEntityID Entity) const;
    RADIENT_STATUS  GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const;
    RADIENT_STATUS  GetEntityOwnVisibility(RadientEntityID Entity, Bool& Visible) const;
    RADIENT_STATUS  GetEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible);
    RADIENT_STATUS  GetParent(RadientEntityID Entity, RadientEntityID& Parent) const;
    RADIENT_STATUS  GetChildCount(RadientEntityID Entity, Uint32& ChildCount) const;
    RADIENT_STATUS  GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren, Uint32& NumChildrenWritten) const;
    RADIENT_STATUS  GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const;
    RADIENT_STATUS  GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix);
    RADIENT_STATUS  HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType, Bool& HasComponent) const;
    RadientRevision GetRevision() const;

    RADIENT_STATUS CreateEntity(const RadientEntityDesc& Desc, RadientEntityID& Entity);
    RADIENT_STATUS DestroyEntity(RadientEntityID Entity);

    RADIENT_STATUS SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags);
    RADIENT_STATUS SetEntityOwnVisibility(RadientEntityID Entity, Bool Visible);
    RADIENT_STATUS SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform);
    RADIENT_STATUS SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform);
    RADIENT_STATUS SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera);
    RADIENT_STATUS SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh);
    RADIENT_STATUS SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer);
    RADIENT_STATUS SetLight(RadientEntityID Entity, const RadientLightComponent& Light);
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

    struct DirtyStateComponent
    {
        DIRTY_FLAGS Flags = DIRTY_FLAG_NONE;
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

    template <typename ComponentType>
    RADIENT_STATUS EmplaceOrReplaceComponent(RadientEntityID Entity, const ComponentType& Component);

    entt::entity FindEntity(RadientEntityID Entity) const;
    bool         IsDescendant(entt::entity Entity, entt::entity PotentialAncestor) const;
    bool         VerifyInternalEntity(entt::entity Entity) const;
    void         DetachFromParent(entt::entity Entity);
    void         DestroyEntitySubtree(entt::entity Entity);
    void         RemoveCustomComponents(entt::entity Entity);
    DIRTY_FLAGS  MarkDirty(entt::entity Entity, DIRTY_FLAGS Flags);
    void         ClearDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags);
    void         PropagateDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags);
    void         MarkChildrenDirtyExcept(entt::entity Entity, DIRTY_FLAGS Flags, entt::entity ExcludedChild);
    void         UpdateDirtyEntities();
    void         UpdateDerivedState(entt::entity Entity, DIRTY_FLAGS Flags);
    void         UpdateEntityDerivedState(entt::entity Entity, DIRTY_FLAGS Flags);
    void         Touch();

    const std::string m_Name;
    RadientSceneDesc  m_Desc;

    entt::registry                                                   m_Registry;
    std::unordered_map<RadientEntityID, entt::entity>                m_EntityMap;
    std::unordered_map<RadientComponentTypeID, CustomComponentStore> m_CustomComponentStores;
    RadientEntityID                                                  m_NextEntityID = 1;
    RadientRevision                                                  m_Revision     = 0;

    // Entities with any dirty flag currently set. MarkDirty/ClearDirtyFlags keep this set in sync.
    std::unordered_set<entt::entity> m_DirtyEntities;
    // Reused snapshot buffer; commit cannot iterate m_DirtyEntities directly because processing mutates it.
    std::vector<entt::entity> m_DirtyEntityBuffer;
    // Reused path scratch buffer for lazy parent-to-child derived-state updates.
    std::vector<entt::entity> m_TmpEntityBuffer;
};

} // namespace Diligent
