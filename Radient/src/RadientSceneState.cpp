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

#include "RadientSceneState.hpp"

#include "DebugUtilities.hpp"
#include "RadientMath.hpp"

#include <algorithm>
#include <utility>

namespace Diligent
{

DEFINE_FLAG_ENUM_OPERATORS(RadientSceneState::DIRTY_FLAGS);

namespace
{

bool IsBuiltInComponentType(const RadientComponentTypeID ComponentType)
{
    return (ComponentType == RADIENT_COMPONENT_TYPE_TRANSFORM ||
            ComponentType == RADIENT_COMPONENT_TYPE_CAMERA ||
            ComponentType == RADIENT_COMPONENT_TYPE_MESH ||
            ComponentType == RADIENT_COMPONENT_TYPE_MESH_RENDERER ||
            ComponentType == RADIENT_COMPONENT_TYPE_LIGHT);
}

} // namespace

const RadientSceneDesc& RadientSceneState::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientSceneState::IsEntityAlive(RadientEntityID Entity) const
{
    return FindEntity(Entity) != entt::null ? RADIENT_STATUS_OK : RADIENT_STATUS_NOT_FOUND;
}

RADIENT_STATUS RadientSceneState::GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Flags = RADIENT_ENTITY_FLAG_NONE;
        return RADIENT_STATUS_NOT_FOUND;
    }

    Flags = m_Registry.get<EntityStateComponent>(E).Flags;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetEntityOwnVisibility(RadientEntityID Entity, Bool& Visible) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Visible = False;
        return RADIENT_STATUS_NOT_FOUND;
    }

    Visible = (m_Registry.get<EntityStateComponent>(E).Flags & RADIENT_ENTITY_FLAG_VISIBLE) != 0 ? True : False;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Visible = False;
        return RADIENT_STATUS_NOT_FOUND;
    }

#ifdef DILIGENT_DEVELOPMENT
    const DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(E);
    if ((DirtyState.Flags & DIRTY_FLAG_VISIBILITY) != DIRTY_FLAG_NONE)
    {
        LOG_WARNING_MESSAGE("Radient scene entity ", Entity,
                            " effective visibility is dirty. Call CommitChanges() before querying effective visibility to get up-to-date value.");
    }
#endif

    Visible = m_Registry.get<EffectiveVisibilityComponent>(E).Visible;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetParent(RadientEntityID Entity, RadientEntityID& Parent) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Parent = InvalidRadientEntityID;
        return RADIENT_STATUS_NOT_FOUND;
    }

    const entt::entity ParentEntity = m_Registry.get<HierarchyComponent>(E).Parent;

    Parent = ParentEntity != entt::null ?
        m_Registry.get<EntityComponent>(ParentEntity).ID :
        InvalidRadientEntityID;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetChildCount(RadientEntityID Entity, Uint32& ChildCount) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        ChildCount = 0;
        return RADIENT_STATUS_NOT_FOUND;
    }

    ChildCount = static_cast<Uint32>(m_Registry.get<HierarchyComponent>(E).Children.size());
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren, Uint32& NumChildrenWritten) const
{
    NumChildrenWritten = 0;

    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    if (pChildren == nullptr && ChildCount != 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (ChildCount == 0)
        return RADIENT_STATUS_OK;

    const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(E).Children;
    if (StartChild >= Children.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    NumChildrenWritten = std::min(ChildCount, static_cast<Uint32>(Children.size() - StartChild));
    for (Uint32 ChildIndex = 0; ChildIndex < NumChildrenWritten; ++ChildIndex)
    {
        const entt::entity Child = Children[StartChild + ChildIndex];
        VERIFY(m_Registry.valid(Child), "Child entity is not alive");
        pChildren[ChildIndex] = m_Registry.get<EntityComponent>(Child).ID;
    }
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Transform = {};
        return RADIENT_STATUS_NOT_FOUND;
    }

    Transform = m_Registry.get<LocalTransformComponent>(E).Transform;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Matrix = {};
        return RADIENT_STATUS_NOT_FOUND;
    }

#ifdef DILIGENT_DEVELOPMENT
    const DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(E);
    if ((DirtyState.Flags & DIRTY_FLAG_TRANSFORM) != DIRTY_FLAG_NONE)
    {
        LOG_WARNING_MESSAGE("Radient scene entity ", Entity,
                            " world matrix is dirty. Call CommitChanges() before querying world matrix to get up-to-date value.");
    }
#endif

    Matrix = m_Registry.get<WorldTransformComponent>(E).Matrix;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType, Bool& HasComponent) const
{
    HasComponent = False;

    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    if (ComponentType == InvalidRadientComponentTypeID)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    switch (ComponentType)
    {
        case RADIENT_COMPONENT_TYPE_TRANSFORM:
            HasComponent = True;
            break;

        case RADIENT_COMPONENT_TYPE_CAMERA:
            HasComponent = m_Registry.all_of<RadientCameraComponent>(E) ? True : False;
            break;

        case RADIENT_COMPONENT_TYPE_MESH:
            HasComponent = m_Registry.all_of<RadientMeshComponent>(E) ? True : False;
            break;

        case RADIENT_COMPONENT_TYPE_MESH_RENDERER:
            HasComponent = m_Registry.all_of<RadientMeshRendererComponent>(E) ? True : False;
            break;

        case RADIENT_COMPONENT_TYPE_LIGHT:
            HasComponent = m_Registry.all_of<RadientLightComponent>(E) ? True : False;
            break;

        default:
        {
            const std::unordered_map<RadientComponentTypeID, CustomComponentStore>::const_iterator It = m_CustomComponentStores.find(ComponentType);

            HasComponent = It != m_CustomComponentStores.end() && It->second.contains(E) ? True : False;
            break;
        }
    }

    return RADIENT_STATUS_OK;
}

RadientRevision RadientSceneState::GetRevision() const
{
    return m_Revision;
}

RADIENT_STATUS RadientSceneState::CreateEntity(const RadientEntityDesc& Desc, RadientEntityID& Entity)
{
    Entity = InvalidRadientEntityID;

    entt::entity Parent = entt::null;
    if (Desc.Parent != InvalidRadientEntityID)
    {
        Parent = FindEntity(Desc.Parent);
        if (Parent == entt::null)
            return RADIENT_STATUS_NOT_FOUND;
    }

    Entity               = m_NextEntityID++;
    const entt::entity E = m_Registry.create();

    m_Registry.emplace<EntityComponent>(E, EntityComponent{Entity, Desc.Name != nullptr ? Desc.Name : ""});
    m_Registry.emplace<EntityStateComponent>(E, EntityStateComponent{Desc.Flags});
    m_Registry.emplace<HierarchyComponent>(E);
    m_Registry.emplace<LocalTransformComponent>(E, LocalTransformComponent{Desc.Transform});
    m_Registry.emplace<WorldTransformComponent>(E);
    m_Registry.emplace<EffectiveVisibilityComponent>(E);
    m_Registry.emplace<DirtyStateComponent>(E);

    m_EntityMap.emplace(Entity, E);
    MarkDirty(E, DIRTY_FLAGS_REQUIRING_PROPAGATION);

    if (Parent != entt::null)
    {
        m_Registry.get<HierarchyComponent>(E).Parent = Parent;
        m_Registry.get<HierarchyComponent>(Parent).Children.push_back(E);
    }

    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::DestroyEntity(RadientEntityID Entity)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    DetachFromParent(E);
    DestroyEntitySubtree(E);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    EntityStateComponent& State = m_Registry.get<EntityStateComponent>(E);
    if (State.Flags == Flags)
        return RADIENT_STATUS_NO_CHANGE;

    const Bool VisibilityChanged =
        ((State.Flags & RADIENT_ENTITY_FLAG_VISIBLE) != (Flags & RADIENT_ENTITY_FLAG_VISIBLE)) ? True : False;

    State.Flags = Flags;
    if (VisibilityChanged)
        MarkDirty(E, DIRTY_FLAG_VISIBILITY);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetEntityOwnVisibility(RadientEntityID Entity, Bool Visible)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    EntityStateComponent& State = m_Registry.get<EntityStateComponent>(E);
    RADIENT_ENTITY_FLAGS  Flags = State.Flags;
    Flags                       = Visible ? (Flags | RADIENT_ENTITY_FLAG_VISIBLE) : (Flags & ~RADIENT_ENTITY_FLAG_VISIBLE);

    if (Flags == State.Flags)
        return RADIENT_STATUS_NO_CHANGE;

    State.Flags = Flags;
    MarkDirty(E, DIRTY_FLAG_VISIBILITY);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    entt::entity NewParent = entt::null;
    if (Parent != InvalidRadientEntityID)
    {
        NewParent = FindEntity(Parent);
        if (NewParent == entt::null)
            return RADIENT_STATUS_NOT_FOUND;

        if (NewParent == E || IsDescendant(NewParent, E))
            return RADIENT_STATUS_INVALID_OPERATION;
    }

    HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(E);
    if (Hierarchy.Parent == NewParent)
        return RADIENT_STATUS_NO_CHANGE;

    RadientTransform LocalTransform = m_Registry.get<LocalTransformComponent>(E).Transform;
    if (KeepWorldTransform)
    {
        UpdateDerivedState(E);
        RadientMatrix4x4 LocalMatrix = m_Registry.get<WorldTransformComponent>(E).Matrix;

        if (NewParent != entt::null)
        {
            UpdateDerivedState(NewParent);

            RadientMatrix4x4 ParentWorldInverse;
            if (!RadientMath::TryInverseMatrix(m_Registry.get<WorldTransformComponent>(NewParent).Matrix, ParentWorldInverse))
                return RADIENT_STATUS_INVALID_OPERATION;

            LocalMatrix = RadientMath::MultiplyMatrices(LocalMatrix, ParentWorldInverse);
        }

        LocalTransform = RadientMath::MatrixToTransform(LocalMatrix);
    }

    DetachFromParent(E);

    Hierarchy.Parent = NewParent;
    if (NewParent != entt::null)
    {
        std::vector<entt::entity>& Siblings = m_Registry.get<HierarchyComponent>(NewParent).Children;
        if (std::find(Siblings.begin(), Siblings.end(), E) == Siblings.end())
            Siblings.push_back(E);
    }

    m_Registry.get<LocalTransformComponent>(E).Transform = LocalTransform;
    MarkDirty(E, DIRTY_FLAGS_REQUIRING_PROPAGATION);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    LocalTransformComponent& LocalTransform = m_Registry.get<LocalTransformComponent>(E);
    if (LocalTransform.Transform == Transform)
        return RADIENT_STATUS_NO_CHANGE;

    LocalTransform.Transform = Transform;
    MarkDirty(E, DIRTY_FLAG_TRANSFORM);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera)
{
    return EmplaceOrReplaceComponent(Entity, Camera);
}

RADIENT_STATUS RadientSceneState::SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh)
{
    return EmplaceOrReplaceComponent(Entity, Mesh);
}

RADIENT_STATUS RadientSceneState::SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer)
{
    return EmplaceOrReplaceComponent(Entity, Renderer);
}

RADIENT_STATUS RadientSceneState::SetLight(RadientEntityID Entity, const RadientLightComponent& Light)
{
    return EmplaceOrReplaceComponent(Entity, Light);
}

RADIENT_STATUS RadientSceneState::SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    if (Component.ComponentType == InvalidRadientComponentTypeID ||
        (Component.pData == nullptr && Component.DataSize != 0) ||
        IsBuiltInComponentType(Component.ComponentType))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    CustomComponentStorage CustomComponent;
    CustomComponent.Name    = Component.Name != nullptr ? Component.Name : "";
    CustomComponent.Schema  = Component.Schema != nullptr ? Component.Schema : "";
    CustomComponent.Version = Component.Version;

    if (Component.pData != nullptr && Component.DataSize != 0)
    {
        const Uint8* pData = static_cast<const Uint8*>(Component.pData);
        CustomComponent.Data.assign(pData, pData + Component.DataSize);
    }

    CustomComponentStore& Store = m_CustomComponentStores[Component.ComponentType];
    if (Store.contains(E))
    {
#ifdef DILIGENT_DEBUG
        const CustomComponentIndexComponent* pIndex = m_Registry.try_get<CustomComponentIndexComponent>(E);
        VERIFY(pIndex != nullptr &&
                   std::find(pIndex->ComponentTypes.begin(), pIndex->ComponentTypes.end(), Component.ComponentType) != pIndex->ComponentTypes.end(),
               "Custom component index is missing component type ", Component.ComponentType);
#endif
        Store.get(E) = std::move(CustomComponent);
    }
    else
    {
        Store.emplace(E, std::move(CustomComponent));

        CustomComponentIndexComponent& Index = m_Registry.get_or_emplace<CustomComponentIndexComponent>(E);
        if (std::find(Index.ComponentTypes.begin(), Index.ComponentTypes.end(), Component.ComponentType) == Index.ComponentTypes.end())
            Index.ComponentTypes.push_back(Component.ComponentType);
    }

    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::RemoveComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    if (ComponentType == InvalidRadientComponentTypeID)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    bool Removed = false;
    switch (ComponentType)
    {
        case RADIENT_COMPONENT_TYPE_TRANSFORM:
            return RADIENT_STATUS_INVALID_OPERATION;

        case RADIENT_COMPONENT_TYPE_CAMERA:
            Removed = m_Registry.remove<RadientCameraComponent>(E) != 0;
            break;

        case RADIENT_COMPONENT_TYPE_MESH:
            Removed = m_Registry.remove<RadientMeshComponent>(E) != 0;
            break;

        case RADIENT_COMPONENT_TYPE_MESH_RENDERER:
            Removed = m_Registry.remove<RadientMeshRendererComponent>(E) != 0;
            break;

        case RADIENT_COMPONENT_TYPE_LIGHT:
            Removed = m_Registry.remove<RadientLightComponent>(E) != 0;
            break;

        default:
        {
            CustomComponentIndexComponent* pIndex = m_Registry.try_get<CustomComponentIndexComponent>(E);
            if (pIndex != nullptr)
            {
                std::vector<RadientComponentTypeID>::iterator ComponentIt = std::find(pIndex->ComponentTypes.begin(), pIndex->ComponentTypes.end(), ComponentType);
                if (ComponentIt != pIndex->ComponentTypes.end())
                {
                    pIndex->ComponentTypes.erase(ComponentIt);
                    if (pIndex->ComponentTypes.empty())
                        m_Registry.remove<CustomComponentIndexComponent>(E);

                    std::unordered_map<RadientComponentTypeID, CustomComponentStore>::iterator StoreIt = m_CustomComponentStores.find(ComponentType);
                    VERIFY(StoreIt != m_CustomComponentStores.end() && StoreIt->second.contains(E),
                           "Custom component store is missing component type ", ComponentType, " listed in the entity index");
                    if (StoreIt != m_CustomComponentStores.end())
                    {
                        StoreIt->second.remove(E);
                        if (StoreIt->second.empty())
                            m_CustomComponentStores.erase(StoreIt);
                    }

                    Removed = true;
                }
            }
            break;
        }
    }

    if (Removed)
    {
        Touch();
        return RADIENT_STATUS_OK;
    }

    return RADIENT_STATUS_NO_CHANGE;
}

RADIENT_STATUS RadientSceneState::CommitChanges()
{
    UpdateDirtyEntities();
    m_DirtyEntities.clear();
    return RADIENT_STATUS_OK;
}

entt::entity RadientSceneState::FindEntity(RadientEntityID Entity) const
{
    const std::unordered_map<RadientEntityID, entt::entity>::const_iterator It = m_EntityMap.find(Entity);
    if (It == m_EntityMap.end())
        return entt::null;

    return m_Registry.valid(It->second) ? It->second : entt::null;
}

bool RadientSceneState::IsDescendant(entt::entity Entity, entt::entity PotentialAncestor) const
{
    while (Entity != entt::null)
    {
        const HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Entity);
        if (Hierarchy.Parent == entt::null)
            return false;

        Entity = Hierarchy.Parent;
        if (Entity == PotentialAncestor)
            return true;
    }

    return false;
}

template <typename ComponentType>
RADIENT_STATUS RadientSceneState::EmplaceOrReplaceComponent(RadientEntityID Entity, const ComponentType& Component)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    m_Registry.emplace_or_replace<ComponentType>(E, Component);
    Touch();
    return RADIENT_STATUS_OK;
}

void RadientSceneState::DetachFromParent(entt::entity Entity)
{
    HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Entity);
    if (Hierarchy.Parent == entt::null)
        return;

    const entt::entity Parent = Hierarchy.Parent;
    if (m_Registry.valid(Parent))
    {
        std::vector<entt::entity>& Siblings = m_Registry.get<HierarchyComponent>(Parent).Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Entity), Siblings.end());
    }

    Hierarchy.Parent = entt::null;
}

void RadientSceneState::DestroyEntitySubtree(entt::entity Entity)
{
    const std::vector<entt::entity> Children = m_Registry.get<HierarchyComponent>(Entity).Children;
    for (const entt::entity Child : Children)
    {
        if (m_Registry.valid(Child))
            DestroyEntitySubtree(Child);
    }

    RemoveCustomComponents(Entity);
    m_EntityMap.erase(m_Registry.get<EntityComponent>(Entity).ID);
    m_Registry.destroy(Entity);
}

void RadientSceneState::RemoveCustomComponents(entt::entity Entity)
{
    CustomComponentIndexComponent* pIndex = m_Registry.try_get<CustomComponentIndexComponent>(Entity);
    if (pIndex == nullptr)
        return;

    for (const RadientComponentTypeID ComponentType : pIndex->ComponentTypes)
    {
        std::unordered_map<RadientComponentTypeID, CustomComponentStore>::iterator It = m_CustomComponentStores.find(ComponentType);
        if (It != m_CustomComponentStores.end())
        {
            It->second.remove(Entity);
            if (It->second.empty())
                m_CustomComponentStores.erase(It);
        }
    }

    m_Registry.remove<CustomComponentIndexComponent>(Entity);
}

RadientSceneState::DIRTY_FLAGS RadientSceneState::MarkDirty(entt::entity Entity, DIRTY_FLAGS Flags)
{
    if (Flags == DIRTY_FLAG_NONE)
        return DIRTY_FLAG_NONE;

    DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
    const DIRTY_FLAGS    AddedFlags = Flags & ~DirtyState.Flags;
    if (DirtyState.Flags == DIRTY_FLAG_NONE)
        m_DirtyEntities.push_back(m_Registry.get<EntityComponent>(Entity).ID);

    DirtyState.Flags |= Flags;

    const DIRTY_FLAGS AddedPropagationFlags = AddedFlags & DIRTY_FLAGS_REQUIRING_PROPAGATION;
    if (AddedPropagationFlags != DIRTY_FLAG_NONE)
        PropagateDirtyFlags(Entity, AddedPropagationFlags);

    return AddedFlags;
}

void RadientSceneState::PropagateDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags)
{
    const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(Entity).Children;
    for (const entt::entity Child : Children)
    {
        if (!m_Registry.valid(Child))
            continue;

        MarkDirty(Child, Flags);
    }
}

void RadientSceneState::UpdateDirtyEntities()
{
    for (const RadientEntityID EntityID : m_DirtyEntities)
    {
        const entt::entity Entity = FindEntity(EntityID);
        if (Entity == entt::null)
            continue;

        UpdateDerivedState(Entity);
    }
}

void RadientSceneState::UpdateDerivedState(entt::entity Entity)
{
    DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
    if ((DirtyState.Flags & DIRTY_FLAGS_REQUIRING_PROPAGATION) == DIRTY_FLAG_NONE)
        return;

    const HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Entity);

    entt::entity                        Parent             = entt::null;
    const RadientMatrix4x4*             pParentWorldMatrix = nullptr;
    const EffectiveVisibilityComponent* pParentVisibility  = nullptr;
    if (Hierarchy.Parent != entt::null)
    {
        Parent = Hierarchy.Parent;
        if (m_Registry.valid(Parent))
        {
            UpdateDerivedState(Parent);
            pParentWorldMatrix = &m_Registry.get<WorldTransformComponent>(Parent).Matrix;
            pParentVisibility  = &m_Registry.get<EffectiveVisibilityComponent>(Parent);
        }
    }

    const DIRTY_FLAGS Flags = DirtyState.Flags;
    if ((Flags & DIRTY_FLAG_TRANSFORM) != DIRTY_FLAG_NONE)
    {
        const LocalTransformComponent& LocalTransform = m_Registry.get<LocalTransformComponent>(Entity);
        const RadientMatrix4x4         LocalMatrix    = RadientMath::TransformToMatrix(LocalTransform.Transform);

        WorldTransformComponent& WorldTransform = m_Registry.get<WorldTransformComponent>(Entity);
        WorldTransform.Matrix                   = pParentWorldMatrix != nullptr ?
                              RadientMath::MultiplyMatrices(LocalMatrix, *pParentWorldMatrix) :
                              LocalMatrix;
    }

    if ((Flags & DIRTY_FLAG_VISIBILITY) != DIRTY_FLAG_NONE)
    {
        const EntityStateComponent&   State      = m_Registry.get<EntityStateComponent>(Entity);
        EffectiveVisibilityComponent& Visibility = m_Registry.get<EffectiveVisibilityComponent>(Entity);

        const Bool OwnVisible = (State.Flags & RADIENT_ENTITY_FLAG_VISIBLE) != 0 ? True : False;
        Visibility.Visible    = OwnVisible && (pParentVisibility == nullptr || pParentVisibility->Visible) ? True : False;
    }

    DirtyState.Flags &= ~DIRTY_FLAGS_REQUIRING_PROPAGATION;
}

void RadientSceneState::Touch()
{
    ++m_Revision;
}

} // namespace Diligent
