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

bool IsValidEntityFlags(RADIENT_ENTITY_FLAGS Flags)
{
    return (Flags & ~RADIENT_ENTITY_FLAGS_ALL) == RADIENT_ENTITY_FLAG_NONE;
}

} // namespace


RadientSceneState::MeshComponentStorage::MeshComponentStorage() = default;

RadientSceneState::MeshComponentStorage::MeshComponentStorage(MeshComponentStorage&& Rhs) noexcept :
    Component{Rhs.Component},
    MeshURI{std::move(Rhs.MeshURI)}
{
    FixupURI();
}

void RadientSceneState::MeshComponentStorage::Assign(const RadientMeshComponent& Mesh)
{
    Component = Mesh;
    MeshURI   = Mesh.Mesh.URI != nullptr ? Mesh.Mesh.URI : "";
    FixupURI();
}

void RadientSceneState::MeshComponentStorage::FixupURI()
{
    // Component.Mesh.URI points into MeshURI and must be repaired after move.
    Component.Mesh.URI = Component.Mesh.URI != nullptr ? MeshURI.c_str() : nullptr;
}

RadientSceneState::RadientSceneState() :
    m_Desc{}
{
}

RadientSceneState::RadientSceneState(const RadientSceneDesc& Desc) :
    m_Name{Desc.Name != nullptr ? Desc.Name : ""},
    m_Desc{Desc}
{
    m_Desc.Name = m_Name.c_str();
}

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

RADIENT_STATUS RadientSceneState::GetEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Visible = False;
        return RADIENT_STATUS_NOT_FOUND;
    }

    UpdateDerivedStatePathToRoot(E, DIRTY_FLAG_VISIBILITY);
    Visible = m_Registry.get<EffectiveVisibilityComponent>(E).Visible;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetCachedEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Visible = False;
        return RADIENT_STATUS_NOT_FOUND;
    }

    Visible = m_Registry.get<EffectiveVisibilityComponent>(E).Visible;
    return (m_DirtyFlags & DIRTY_FLAG_VISIBILITY) != DIRTY_FLAG_NONE ?
        RADIENT_STATUS_OUT_OF_DATE :
        RADIENT_STATUS_OK;
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
        return RADIENT_STATUS_OK;

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

RADIENT_STATUS RadientSceneState::GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Matrix = {};
        return RADIENT_STATUS_NOT_FOUND;
    }

    UpdateDerivedStatePathToRoot(E, DIRTY_FLAG_TRANSFORM);
    Matrix = m_Registry.get<WorldTransformComponent>(E).Matrix;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetCachedWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
    {
        Matrix = {};
        return RADIENT_STATUS_NOT_FOUND;
    }

    Matrix = m_Registry.get<WorldTransformComponent>(E).Matrix;
    return (m_DirtyFlags & DIRTY_FLAG_TRANSFORM) != DIRTY_FLAG_NONE ?
        RADIENT_STATUS_OUT_OF_DATE :
        RADIENT_STATUS_OK;
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
            HasComponent = m_Registry.all_of<MeshComponentStorage>(E) ? True : False;
            break;

        case RADIENT_COMPONENT_TYPE_MESH_RENDERER:
            HasComponent = m_Registry.all_of<RadientMeshRendererComponent>(E) ? True : False;
            break;

        case RADIENT_COMPONENT_TYPE_LIGHT:
            HasComponent = m_Registry.all_of<RadientLightComponent>(E) ? True : False;
            break;

        default:
        {
            const CustomComponentStoresMapType::const_iterator It = m_CustomComponentStores.find(ComponentType);

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

    if (!IsValidEntityFlags(Desc.Flags))
        return RADIENT_STATUS_INVALID_ARGUMENT;

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

    if (Parent != entt::null)
    {
        m_Registry.get<HierarchyComponent>(E).Parent = Parent;
        m_Registry.get<HierarchyComponent>(Parent).Children.push_back(E);
    }

    MarkDirty(E, DIRTY_FLAGS_REQUIRING_PROPAGATION);
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
    if (m_DirtyEntities.empty())
        m_DirtyFlags = DIRTY_FLAG_NONE;
    Touch();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    if (!IsValidEntityFlags(Flags))
        return RADIENT_STATUS_INVALID_ARGUMENT;

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
        UpdateDerivedStatePathToRoot(E, DIRTY_FLAG_TRANSFORM);
        RadientMatrix4x4 LocalMatrix = m_Registry.get<WorldTransformComponent>(E).Matrix;

        if (NewParent != entt::null)
        {
            UpdateDerivedStatePathToRoot(NewParent, DIRTY_FLAG_TRANSFORM);

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
        VERIFY(std::find(Siblings.begin(), Siblings.end(), E) == Siblings.end(),
               "Entity is already listed as a child of the new parent");
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
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    MeshComponentStorage* pExistingMesh = m_Registry.try_get<MeshComponentStorage>(E);
    if (pExistingMesh != nullptr && pExistingMesh->Component == Mesh)
        return RADIENT_STATUS_NO_CHANGE;

    MeshComponentStorage& MeshStorage = pExistingMesh != nullptr ?
        *pExistingMesh :
        m_Registry.emplace<MeshComponentStorage>(E);

    MeshStorage.Assign(Mesh);
    Touch();
    return RADIENT_STATUS_OK;
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
            Removed = m_Registry.remove<MeshComponentStorage>(E) != 0;
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

                    CustomComponentStoresMapType::iterator StoreIt = m_CustomComponentStores.find(ComponentType);
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
    return RADIENT_STATUS_OK;
}

entt::entity RadientSceneState::FindEntity(RadientEntityID Entity) const
{
    const EntityMapType::const_iterator It = m_EntityMap.find(Entity);
    if (It == m_EntityMap.end())
        return entt::null;

    return m_Registry.valid(It->second) ? It->second : entt::null;
}

bool RadientSceneState::IsDescendant(entt::entity Entity, entt::entity PotentialAncestor) const
{
    if (!VerifyInternalEntity(Entity) || !VerifyInternalEntity(PotentialAncestor))
        return false;

    if (m_Registry.get<HierarchyComponent>(PotentialAncestor).Children.empty())
        return false;

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

bool RadientSceneState::VerifyInternalEntity(entt::entity Entity) const
{
    if (Entity == entt::null)
    {
        UNEXPECTED("Entity is null. This should never happen.");
        return false;
    }

    if (!m_Registry.valid(Entity))
    {
        UNEXPECTED("Entity is not valid. This should never happen.");
        return false;
    }

    return true;
}

template <typename ComponentType>
RADIENT_STATUS RadientSceneState::EmplaceOrReplaceComponent(RadientEntityID Entity, const ComponentType& Component)
{
    const entt::entity E = FindEntity(Entity);
    if (E == entt::null)
        return RADIENT_STATUS_NOT_FOUND;

    const ComponentType* pExistingComponent = m_Registry.try_get<ComponentType>(E);
    if (pExistingComponent != nullptr && *pExistingComponent == Component)
        return RADIENT_STATUS_NO_CHANGE;

    m_Registry.emplace_or_replace<ComponentType>(E, Component);
    Touch();
    return RADIENT_STATUS_OK;
}

void RadientSceneState::DetachFromParent(entt::entity Entity)
{
    if (!VerifyInternalEntity(Entity))
        return;

    HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Entity);
    if (Hierarchy.Parent == entt::null)
        return;

    const entt::entity Parent = Hierarchy.Parent;
    if (!VerifyInternalEntity(Parent))
        return;

    std::vector<entt::entity>& Siblings = m_Registry.get<HierarchyComponent>(Parent).Children;
    VERIFY(std::find(Siblings.begin(), Siblings.end(), Entity) != Siblings.end(),
           "Entity is not listed as a child of its parent");
    Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Entity), Siblings.end());

    Hierarchy.Parent = entt::null;
}

void RadientSceneState::DestroyEntitySubtree(entt::entity Entity)
{
    if (!VerifyInternalEntity(Entity))
        return;

    std::vector<DestroyWorkItem>& Stack = m_TmpDestroyStack;
    Stack.clear();
    Stack.push_back({Entity, 0});

    while (!Stack.empty())
    {
        DestroyWorkItem& Item = Stack.back();
        if (!VerifyInternalEntity(Item.Entity))
        {
            Stack.pop_back();
            continue;
        }

        const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(Item.Entity).Children;
        if (Item.NextChildIndex < Children.size())
        {
            const entt::entity Child = Children[Item.NextChildIndex++];
            if (VerifyInternalEntity(Child))
                Stack.push_back({Child, 0});
            continue;
        }

        const entt::entity Current = Item.Entity;
        Stack.pop_back();

        RemoveCustomComponents(Current);
        DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Current);
        RemoveFromDirtyList(Current, DirtyState);
        m_EntityMap.erase(m_Registry.get<EntityComponent>(Current).ID);
        m_Registry.destroy(Current);
    }

    Stack.clear();
}

void RadientSceneState::RemoveCustomComponents(entt::entity Entity)
{
    if (!VerifyInternalEntity(Entity))
        return;

    CustomComponentIndexComponent* pIndex = m_Registry.try_get<CustomComponentIndexComponent>(Entity);
    if (pIndex == nullptr)
        return;

    for (const RadientComponentTypeID ComponentType : pIndex->ComponentTypes)
    {
        CustomComponentStoresMapType::iterator It = m_CustomComponentStores.find(ComponentType);
        if (It != m_CustomComponentStores.end())
        {
            It->second.remove(Entity);
            if (It->second.empty())
                m_CustomComponentStores.erase(It);
        }
    }

    m_Registry.remove<CustomComponentIndexComponent>(Entity);
}

RadientSceneState::DIRTY_FLAGS RadientSceneState::MarkDirty(entt::entity Entity, DIRTY_FLAGS Flags, bool AddToDirtyList)
{
    if (Flags == DIRTY_FLAG_NONE)
        return DIRTY_FLAG_NONE;

    if (!VerifyInternalEntity(Entity))
        return DIRTY_FLAG_NONE;

    DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);

    // m_DirtyEntities tracks only nodes where dirtiness was introduced directly by a scene edit or a lazy
    // path repair. Descendants dirtied by propagation are intentionally not inserted there, otherwise commit
    // would have to filter a much larger worklist and could revisit the same subtree many times.
    if (AddToDirtyList)
    {
        if (!DirtyState.IsInDirtyList())
        {
            DirtyState.DirtyListIndex = m_DirtyEntities.size();
            m_DirtyEntities.push_back(Entity);
        }
        else
        {
            VERIFY(DirtyState.DirtyListIndex < m_DirtyEntities.size() &&
                       m_DirtyEntities[DirtyState.DirtyListIndex] == Entity,
                   "Dirty list index points to the wrong entity");
        }
    }

    const DIRTY_FLAGS AddedFlags = Flags & ~DirtyState.Flags;
    DirtyState.Flags |= AddedFlags;
    m_DirtyFlags |= Flags;

    return AddedFlags;
}

void RadientSceneState::ClearDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags)
{
    if (Flags == DIRTY_FLAG_NONE)
        return;

    if (!VerifyInternalEntity(Entity))
        return;

    DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
    DirtyState.Flags &= ~Flags;

    if (DirtyState.Flags == DIRTY_FLAG_NONE)
    {
        RemoveFromDirtyList(Entity, DirtyState);
    }
}

void RadientSceneState::RemoveFromDirtyList(entt::entity Entity, DirtyStateComponent& DirtyState)
{
    if (!DirtyState.IsInDirtyList())
        return;

    VERIFY(DirtyState.DirtyListIndex < m_DirtyEntities.size() &&
               m_DirtyEntities[DirtyState.DirtyListIndex] == Entity,
           "Dirty list index points to the wrong entity");

    const size_t       RemovedIndex = DirtyState.DirtyListIndex;
    const entt::entity LastEntity   = m_DirtyEntities.back();

    if (RemovedIndex + 1 < m_DirtyEntities.size())
    {
        m_DirtyEntities[RemovedIndex] = LastEntity;
        m_Registry.get<DirtyStateComponent>(LastEntity).DirtyListIndex = RemovedIndex;
    }

    m_DirtyEntities.pop_back();
    DirtyState.DirtyListIndex = InvalidDirtyListIndex;
}

// Propagate directly tracked dirty flags down the subtree. Propagation is a marking pass only: it does not add
// descendants to m_DirtyEntities, but it does update their DirtyStateComponent.Flags so they are included in the
// subtree update. Propagation stops when a subtree already has all requested flags, so overlapping dirty roots do not
// repeatedly walk the same descendants.
void RadientSceneState::PropagateDirtyFlags(entt::entity Entity, DIRTY_FLAGS Flags)
{
    if (Flags == DIRTY_FLAG_NONE)
        return;

    if (!VerifyInternalEntity(Entity))
        return;

    std::vector<DirtyWorkItem>& Stack = m_TmpDirtyWorkItems;
    Stack.clear();
    Stack.push_back({Entity, Flags});

    while (!Stack.empty())
    {
        const DirtyWorkItem Item = Stack.back();
        Stack.pop_back();

        if (!VerifyInternalEntity(Item.Entity))
            continue;

        const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(Item.Entity).Children;
        for (const entt::entity Child : Children)
        {
            // Propagation is a marking pass only. Newly dirtied descendants inherit the subset of flags that was
            // not already present, and traversal stops as soon as a subtree already has all requested flags. This
            // keeps overlapping dirty roots from repeatedly walking the same descendants.
            constexpr bool    AddToDirtyList = false;
            const DIRTY_FLAGS AddedFlags     = MarkDirty(Child, Item.Flags, AddToDirtyList) & DIRTY_FLAGS_REQUIRING_PROPAGATION;
            if (AddedFlags != DIRTY_FLAG_NONE)
                Stack.push_back({Child, AddedFlags});
        }
    }

    Stack.clear();
}

// Mark all children dirty except the excluded child.
void RadientSceneState::MarkChildrenDirtyExcept(entt::entity Entity, DIRTY_FLAGS Flags, entt::entity ExcludedChild)
{
    if (Flags == DIRTY_FLAG_NONE)
        return;

    if (!VerifyInternalEntity(Entity))
        return;

    const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(Entity).Children;
    for (const entt::entity Child : Children)
    {
        if (Child == ExcludedChild)
            continue;

        // The excluded child is already repaired by the active path update; siblings remain invalidated.
        MarkDirty(Child, Flags);
    }
}

void RadientSceneState::UpdateDirtyEntities()
{
    if (m_DirtyEntities.empty())
    {
        m_DirtyFlags = DIRTY_FLAG_NONE;
        return;
    }

    // Commit updates dirty derived state in three phases:
    // 1. Propagate directly tracked dirty flags down affected subtrees without adding descendants to
    //    m_DirtyEntities. Propagation stops when a subtree already has the requested flags.
    // 2. Select only the highest directly tracked dirty roots. Descendants of another dirty node are skipped
    //    because the ancestor's subtree update will reach them.
    // 3. Update each selected subtree top-down. Parent state is repaired before child state, so each entity can
    //    use cached parent world transform and effective visibility without walking back to the root.

    // Commit is optimized for batch updates. It does not repair each originally dirty entity by walking up to
    // the root. Instead, it first expands dirty flags down from the directly edited nodes. Propagation marks
    // descendants dirty without adding them to m_DirtyEntities, so this pass can iterate the worklist directly.
    for (const entt::entity Entity : m_DirtyEntities)
    {
        if (!VerifyInternalEntity(Entity))
            continue;

        const DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
        VERIFY(DirtyState.IsInDirtyList(), "Dirty entity is not marked as being in the dirty list");

        const DIRTY_FLAGS Flags = DirtyState.Flags & DIRTY_FLAGS_REQUIRING_PROPAGATION;
        PropagateDirtyFlags(Entity, Flags);
    }

    // After propagation, every affected descendant has the inherited dirty flags. To update the scene in linear
    // time, keep only the highest original dirty roots: if an original dirty entity has a dirty parent, it will
    // be reached by the parent's subtree traversal. Checking the immediate parent is enough because propagation
    // makes the whole path from a dirty ancestor to this node dirty.
    std::vector<entt::entity>& DirtyRoots = m_TmpEntityBuffer;
    DirtyRoots.clear();
    DirtyRoots.reserve(m_DirtyEntities.size());

    for (const entt::entity Entity : m_DirtyEntities)
    {
        if (!VerifyInternalEntity(Entity))
            continue;

        const DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
        VERIFY(DirtyState.IsInDirtyList(), "Dirty entity is not marked as being in the dirty list");

        const DIRTY_FLAGS Flags = DirtyState.Flags & DIRTY_FLAGS_REQUIRING_PROPAGATION;
        if (Flags == DIRTY_FLAG_NONE)
            continue;

        const entt::entity Parent = m_Registry.get<HierarchyComponent>(Entity).Parent;
        if (Parent != entt::null)
        {
            if (!VerifyInternalEntity(Parent))
                continue;

            const DIRTY_FLAGS ParentFlags = m_Registry.get<DirtyStateComponent>(Parent).Flags & DIRTY_FLAGS_REQUIRING_PROPAGATION;
            if (ParentFlags != DIRTY_FLAG_NONE)
                continue;
        }

        DirtyRoots.push_back(Entity);
    }

    // Each selected root is updated top-down. Parents are repaired before children, so the direct per-entity
    // recompute can use cached parent world transform and effective visibility without doing an upward walk.
    for (const entt::entity Entity : DirtyRoots)
    {
        if (!VerifyInternalEntity(Entity))
            continue;

        const DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Entity);
        VERIFY(DirtyState.IsInDirtyList(), "Dirty root is not in the dirty list");

        const DIRTY_FLAGS Flags = DirtyState.Flags & DIRTY_FLAGS_REQUIRING_PROPAGATION;
        if (Flags != DIRTY_FLAG_NONE)
            UpdateDirtySubtree(Entity, DIRTY_FLAG_NONE);
    }

    VERIFY(m_DirtyEntities.empty(), "All dirty entities should have been processed and cleared at this point");
    m_DirtyEntities.clear();
    m_DirtyFlags = DIRTY_FLAG_NONE;
    DirtyRoots.clear();
}

// Update the subtree with the given inherited dirty flags. This function is used by commit's top-down traversal,
// so the parent is already updated and Item.Flags carries the dirty state caused by ancestors. Update this node
// directly, then pass the effective dirty flags to children. A child may also have its own dirty flags (for example,
// the parent has a dirty visibility flag and the child has a dirty transform flag); the stack item combines both sets
// before updating it.
void RadientSceneState::UpdateDirtySubtree(entt::entity Entity, DIRTY_FLAGS InheritedFlags)
{
    InheritedFlags &= DIRTY_FLAGS_REQUIRING_PROPAGATION;

    if (!VerifyInternalEntity(Entity))
        return;

    std::vector<DirtyWorkItem>& Stack = m_TmpDirtyWorkItems;
    Stack.clear();
    Stack.push_back({Entity, InheritedFlags});

    while (!Stack.empty())
    {
        const DirtyWorkItem Item = Stack.back();
        Stack.pop_back();

        if (!VerifyInternalEntity(Item.Entity))
            continue;

        DirtyStateComponent& DirtyState = m_Registry.get<DirtyStateComponent>(Item.Entity);
        const DIRTY_FLAGS    Flags      = (DirtyState.Flags | Item.Flags) & DIRTY_FLAGS_REQUIRING_PROPAGATION;
        if (Flags == DIRTY_FLAG_NONE)
            continue;

        // This function is only used by commit's top-down traversal. The parent has already been updated, and
        // Item.Flags carries the dirty state caused by ancestors, so this node can be updated directly.
        UpdateEntityDerivedState(Item.Entity, Flags);
        ClearDirtyFlags(Item.Entity, Flags);

        // Pass the effective dirty flags to all children. A child may also have its own dirty flags; the stack
        // item combines both sets before updating it.
        const std::vector<entt::entity>& Children = m_Registry.get<HierarchyComponent>(Item.Entity).Children;
        for (const entt::entity Child : Children)
            Stack.push_back({Child, Flags});
    }

    Stack.clear();
}

void RadientSceneState::UpdateDerivedStatePathToRoot(entt::entity Entity, DIRTY_FLAGS Flags)
{
    Flags &= DIRTY_FLAGS_REQUIRING_PROPAGATION;
    if (Flags == DIRTY_FLAG_NONE)
        return;

    if (!VerifyInternalEntity(Entity))
        return;

    // If the scene has no pending dirtiness for the requested derived state, the cached value is valid and no
    // path walk is needed. m_DirtyFlags is conservative while the scene is dirty, so a set bit may cause extra
    // work, but a clear bit is always safe to trust.
    if ((m_DirtyFlags & Flags) == DIRTY_FLAG_NONE)
        return;

    std::vector<entt::entity>& Path = m_TmpEntityBuffer;
    Path.clear();

    // Lazy queries repair only the path needed by the query. Build the path from the requested entity up to the
    // root; it is consumed in reverse order so dirty ancestors are updated before their descendants.
    for (entt::entity Current = Entity; Current != entt::null;)
    {
        if (!VerifyInternalEntity(Current))
            break;

        Path.push_back(Current);
        Current = m_Registry.get<HierarchyComponent>(Current).Parent;
    }

    // Find the highest dirty ancestor on the path. Nodes above it are already clean for the requested flags, so
    // their cached values can be trusted; nodes below it inherit the repaired state as we walk back down.
    size_t FirstDirtyIndex = Path.size();
    for (size_t Index = Path.size(); Index > 0; --Index)
    {
        const size_t      PathIndex   = Index - 1;
        const DIRTY_FLAGS EntityFlags = m_Registry.get<DirtyStateComponent>(Path[PathIndex]).Flags & Flags;
        if (EntityFlags != DIRTY_FLAG_NONE)
        {
            FirstDirtyIndex = PathIndex;
            break;
        }
    }

    if (FirstDirtyIndex == Path.size())
        return;

    DIRTY_FLAGS ActiveFlags = DIRTY_FLAG_NONE;
    for (size_t Index = FirstDirtyIndex + 1; Index > 0; --Index)
    {
        const size_t       PathIndex   = Index - 1;
        const entt::entity Current     = Path[PathIndex];
        const DIRTY_FLAGS  EntityFlags = m_Registry.get<DirtyStateComponent>(Current).Flags & Flags;
        // Once a parent is dirty, the same flags are inherited by every descendant on this path.
        ActiveFlags |= EntityFlags;
        if (ActiveFlags == DIRTY_FLAG_NONE)
            continue;

        // Update this path node directly. Parent state is already valid because the loop walks from the highest
        // dirty ancestor down toward the originally requested entity.
        UpdateEntityDerivedState(Current, ActiveFlags);
        ClearDirtyFlags(Current, ActiveFlags);

        // Only the requested path is repaired. Off-path children inherit the parent's change and remain dirty so
        // a later query or CommitChanges() can update their subtrees.
        const entt::entity ExcludedChild = PathIndex > 0 ? Path[PathIndex - 1] : entt::null;
        MarkChildrenDirtyExcept(Current, ActiveFlags, ExcludedChild);
    }

    if (m_DirtyEntities.empty())
        m_DirtyFlags = DIRTY_FLAG_NONE;
}

void RadientSceneState::UpdateEntityDerivedState(entt::entity Entity, DIRTY_FLAGS Flags)
{
    Flags &= DIRTY_FLAGS_REQUIRING_PROPAGATION;
    if (Flags == DIRTY_FLAG_NONE)
        return;

    if (!VerifyInternalEntity(Entity))
        return;

    const HierarchyComponent& Hierarchy = m_Registry.get<HierarchyComponent>(Entity);

    entt::entity Parent = Hierarchy.Parent;
    if (Parent != entt::null)
    {
        if (!VerifyInternalEntity(Parent))
            return;

        // This low-level helper never walks the hierarchy. Both commit and lazy path repair must update parents
        // before calling it for a child.
        VERIFY((m_Registry.get<DirtyStateComponent>(Parent).Flags & Flags) == DIRTY_FLAG_NONE,
               "Parent derived state must be up to date before updating child derived state");
    }

    if ((Flags & DIRTY_FLAG_TRANSFORM) != DIRTY_FLAG_NONE)
    {
        const LocalTransformComponent& LocalTransform        = m_Registry.get<LocalTransformComponent>(Entity);
        const RadientMatrix4x4         LocalMatrix           = RadientMath::TransformToMatrix(LocalTransform.Transform);
        const WorldTransformComponent* pParentWorldTransform = (Parent != entt::null) ?
            &m_Registry.get<WorldTransformComponent>(Parent) :
            nullptr;

        WorldTransformComponent& WorldTransform = m_Registry.get<WorldTransformComponent>(Entity);

        WorldTransform.Matrix = pParentWorldTransform != nullptr ?
            RadientMath::MultiplyMatrices(LocalMatrix, pParentWorldTransform->Matrix) :
            LocalMatrix;
    }

    if ((Flags & DIRTY_FLAG_VISIBILITY) != DIRTY_FLAG_NONE)
    {
        const EntityStateComponent&         State             = m_Registry.get<EntityStateComponent>(Entity);
        EffectiveVisibilityComponent&       Visibility        = m_Registry.get<EffectiveVisibilityComponent>(Entity);
        const EffectiveVisibilityComponent* pParentVisibility = (Parent != entt::null) ?
            &m_Registry.get<EffectiveVisibilityComponent>(Parent) :
            nullptr;

        const Bool OwnVisible = (State.Flags & RADIENT_ENTITY_FLAG_VISIBLE) != 0 ? True : False;
        Visibility.Visible    = OwnVisible && (pParentVisibility == nullptr || pParentVisibility->Visible) ? True : False;
    }
}

void RadientSceneState::Touch()
{
    ++m_Revision;
}

} // namespace Diligent
