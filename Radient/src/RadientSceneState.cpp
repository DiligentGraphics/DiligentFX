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

#include "RadientMath.hpp"

#include <algorithm>

namespace Diligent
{

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
    Visible = False;

    entt::entity Current = FindEntity(Entity);
    if (Current == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    while (Current != entt::null)
    {
        const EntityStateComponent& State = m_Registry.get<EntityStateComponent>(Current);
        if ((State.Flags & RADIENT_ENTITY_FLAG_VISIBLE) == 0)
            return RADIENT_STATUS_OK;

        const HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Current);
        if (Hierarchy.Parent == InvalidRadientEntityID)
        {
            Visible = True;
            return RADIENT_STATUS_OK;
        }

        Current = FindEntity(Hierarchy.Parent);
        if (Current == entt::null)
            return RADIENT_STATUS_NOT_FOUND;
    }

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

    Parent = m_Registry.get<HierarchyComponent>(E).Parent;
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

    const std::vector<RadientEntityID>& Children = m_Registry.get<HierarchyComponent>(E).Children;
    if (StartChild >= Children.size())
        return RADIENT_STATUS_OK;

    NumChildrenWritten = std::min(ChildCount, static_cast<Uint32>(Children.size() - StartChild));
    std::copy_n(Children.begin() + StartChild, NumChildrenWritten, pChildren);
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

    UpdateWorldMatrix(E);
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
            const CustomComponentSet* pCustomComponents = m_Registry.try_get<CustomComponentSet>(E);

            HasComponent = pCustomComponents != nullptr && pCustomComponents->Components.find(ComponentType) != pCustomComponents->Components.end() ? True : False;
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

    m_EntityMap.emplace(Entity, E);

    if (Parent != entt::null)
    {
        m_Registry.get<HierarchyComponent>(E).Parent = Desc.Parent;
        m_Registry.get<HierarchyComponent>(Parent).Children.push_back(Entity);
    }

    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::DestroyEntity(RadientEntityID Entity)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    const std::vector<RadientEntityID> Children = m_Registry.get<HierarchyComponent>(E).Children;
    for (const RadientEntityID Child : Children)
        DestroyEntity(Child);

    DetachFromParent(E);
    m_EntityMap.erase(Entity);
    m_Registry.destroy(E);
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

    State.Flags = Flags;
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
    if (Hierarchy.Parent == Parent)
        return RADIENT_STATUS_NO_CHANGE;

    RadientTransform LocalTransform = m_Registry.get<LocalTransformComponent>(E).Transform;
    if (KeepWorldTransform)
    {
        UpdateWorldMatrix(E);
        RadientMatrix4x4 LocalMatrix = m_Registry.get<WorldTransformComponent>(E).Matrix;

        if (NewParent != entt::null)
        {
            UpdateWorldMatrix(NewParent);

            RadientMatrix4x4 ParentWorldInverse;
            if (!RadientMath::TryInverseMatrix(m_Registry.get<WorldTransformComponent>(NewParent).Matrix, ParentWorldInverse))
                return RADIENT_STATUS_INVALID_OPERATION;

            LocalMatrix = RadientMath::MultiplyMatrices(LocalMatrix, ParentWorldInverse);
        }

        LocalTransform = RadientMath::MatrixToTransform(LocalMatrix);
    }

    DetachFromParent(E);

    Hierarchy.Parent = Parent;
    if (NewParent != entt::null)
    {
        std::vector<RadientEntityID>& Siblings = m_Registry.get<HierarchyComponent>(NewParent).Children;
        if (std::find(Siblings.begin(), Siblings.end(), Entity) == Siblings.end())
            Siblings.push_back(Entity);
    }

    m_Registry.get<LocalTransformComponent>(E).Transform = LocalTransform;
    MarkWorldMatrixDirty(E);
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    m_Registry.get<LocalTransformComponent>(E).Transform = Transform;
    MarkWorldMatrixDirty(E);
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

    std::unordered_map<RadientComponentTypeID, CustomComponentStorage>& CustomComponents = m_Registry.get_or_emplace<CustomComponentSet>(E).Components;
    CustomComponentStorage&                                             CustomComponent  = CustomComponents[Component.ComponentType];

    CustomComponent.Name    = Component.Name != nullptr ? Component.Name : "";
    CustomComponent.Schema  = Component.Schema != nullptr ? Component.Schema : "";
    CustomComponent.Version = Component.Version;
    CustomComponent.Data.clear();

    if (Component.pData != nullptr && Component.DataSize != 0)
    {
        const Uint8* pData = static_cast<const Uint8*>(Component.pData);
        CustomComponent.Data.assign(pData, pData + Component.DataSize);
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
            CustomComponentSet* pCustomComponents = m_Registry.try_get<CustomComponentSet>(E);
            if (pCustomComponents != nullptr)
            {
                Removed = pCustomComponents->Components.erase(ComponentType) != 0;
                if (pCustomComponents->Components.empty())
                    m_Registry.remove<CustomComponentSet>(E);
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
        if (Hierarchy.Parent == InvalidRadientEntityID)
            return false;

        Entity = FindEntity(Hierarchy.Parent);
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
    if (Hierarchy.Parent == InvalidRadientEntityID)
        return;

    const entt::entity Parent = FindEntity(Hierarchy.Parent);
    if (Parent != entt::null)
    {
        std::vector<RadientEntityID>& Siblings = m_Registry.get<HierarchyComponent>(Parent).Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), m_Registry.get<EntityComponent>(Entity).ID), Siblings.end());
    }

    Hierarchy.Parent = InvalidRadientEntityID;
}

void RadientSceneState::MarkWorldMatrixDirty(entt::entity Entity)
{
    m_Registry.get<WorldTransformComponent>(Entity).Dirty = true;

    const std::vector<RadientEntityID>& Children = m_Registry.get<HierarchyComponent>(Entity).Children;
    for (const RadientEntityID Child : Children)
    {
        const entt::entity ChildEntity = FindEntity(Child);
        if (ChildEntity != entt::null)
            MarkWorldMatrixDirty(ChildEntity);
    }
}

void RadientSceneState::UpdateWorldMatrix(entt::entity Entity) const
{
    WorldTransformComponent& WorldTransform = m_Registry.get<WorldTransformComponent>(Entity);
    if (!WorldTransform.Dirty)
        return;

    const LocalTransformComponent& LocalTransform = m_Registry.get<LocalTransformComponent>(Entity);
    const HierarchyComponent&      Hierarchy      = m_Registry.get<HierarchyComponent>(Entity);

    const RadientMatrix4x4 LocalMatrix = RadientMath::TransformToMatrix(LocalTransform.Transform);
    if (Hierarchy.Parent == InvalidRadientEntityID)
    {
        WorldTransform.Matrix = LocalMatrix;
    }
    else
    {
        const entt::entity Parent = FindEntity(Hierarchy.Parent);
        if (Parent != entt::null)
        {
            UpdateWorldMatrix(Parent);
            WorldTransform.Matrix = RadientMath::MultiplyMatrices(LocalMatrix, m_Registry.get<WorldTransformComponent>(Parent).Matrix);
        }
        else
        {
            WorldTransform.Matrix = LocalMatrix;
        }
    }

    WorldTransform.Dirty = false;
}

void RadientSceneState::Touch()
{
    ++m_Revision;
}

} // namespace Diligent
