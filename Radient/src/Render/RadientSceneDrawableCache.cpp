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

#include "Render/RadientSceneDrawableCache.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Scene/RadientSceneImpl.hpp"

#include "Cast.hpp"
#include "DebugUtilities.hpp"
#include "GLTFLoader.hpp"

#include <algorithm>

namespace Diligent
{

namespace
{

Uint8 CorrectMaterialAlphaMode(int AlphaMode)
{
    if (AlphaMode < GLTF::Material::ALPHA_MODE_OPAQUE || AlphaMode >= GLTF::Material::ALPHA_MODE_NUM_MODES)
        AlphaMode = GLTF::Material::ALPHA_MODE_OPAQUE;

    return static_cast<Uint8>(AlphaMode);
}

class RadientAssetDrawableMeshProvider final : public IRadientDrawableMeshProvider
{
public:
    RadientDrawableMeshStatus GetDrawableMesh(IRadientMeshAsset*   pMeshAsset,
                                              RadientDrawableMesh& Mesh) override final
    {
        Mesh = {};

        if (pMeshAsset == nullptr)
            return RadientDrawableMeshStatus::Failed;

        const RadientAssetManagerImpl::GLTFMeshResolveResult Result =
            RadientAssetManagerImpl::GetGLTFMesh(pMeshAsset, true);
        if (RADIENT_FAILED(Result.Status))
            return RadientDrawableMeshStatus::Failed;

        if (Result.Status != RADIENT_STATUS_OK)
            return RadientDrawableMeshStatus::Pending;

        VERIFY(Result.pModel != nullptr, "GLTF mesh resolve result has null model pointer. This should not happen when the status is RADIENT_STATUS_OK");
        VERIFY(Result.MeshIndex < Result.pModel->Meshes.size(), "GLTF mesh index (", Result.MeshIndex, ") is out of range for the model (", Result.pModel->Meshes.size(),
               " meshes). This should not happen when the status is RADIENT_STATUS_OK");

        Mesh.pModel             = Result.pModel;
        Mesh.pMesh              = &Result.pModel->Meshes[Result.MeshIndex];
        Mesh.VertexAttribFlags  = Result.VertexAttribFlags;
        Mesh.FirstIndexLocation = Result.pModel->GetFirstIndexLocation();
        Mesh.BaseVertex         = Result.pModel->GetBaseVertex();

        return RadientDrawableMeshStatus::Ready;
    }
};

IRadientDrawableMeshProvider& GetDefaultDrawableMeshProvider()
{
    static RadientAssetDrawableMeshProvider Provider;
    return Provider;
}

} // namespace

RadientSceneDrawableCache::RadientSceneDrawableCache(IRadientDrawableMeshProvider* pMeshProvider) :
    m_MeshProvider{pMeshProvider != nullptr ? *pMeshProvider : GetDefaultDrawableMeshProvider()}
{
}

RADIENT_STATUS RadientSceneDrawableCache::SyncScene(const IRadientScene& Scene)
{
    m_DrawableChanges.clear();
    m_LightChanges.clear();

    const RadientSceneRevisions& SceneRevisions = Scene.GetSceneRevisions();
    if (m_SceneRevisions == SceneRevisions && m_PendingRenderableEntities.empty())
        return RADIENT_STATUS_NO_CHANGE;

    const bool UpdateRenderables = (m_SceneRevisions.Drawables != SceneRevisions.Drawables);
    const bool UpdateLights      = (m_SceneRevisions.Lights != SceneRevisions.Lights);

    const RadientSceneImpl*                            pSceneImpl     = ClassPtrCast<const RadientSceneImpl>(&Scene);
    const RadientSceneState&                           State          = pSceneImpl->GetState();
    const RadientSceneState::RenderableChangeLogState& ChangeLogState = State.GetRenderableChangeLogState();

    // Scene state keeps renderable mesh/light changes as delta logs. Clearing a log
    // moves its base revision forward to the current scene revision. If this cache
    // is older than that base, the changes it needs have already been discarded and
    // an incremental sync would silently miss updates.
    if (UpdateRenderables && m_SceneRevisions.Drawables < ChangeLogState.MeshesBaseRevision)
    {
        LOG_ERROR_MESSAGE("Failed to sync Radient drawable cache: renderable mesh changes were cleared before the cache consumed them. "
                          "Cache drawable revision: ",
                          m_SceneRevisions.Drawables,
                          ", change log base revision: ",
                          ChangeLogState.MeshesBaseRevision,
                          ", scene drawable revision: ",
                          SceneRevisions.Drawables);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    if (UpdateLights && m_SceneRevisions.Lights < ChangeLogState.LightsBaseRevision)
    {
        LOG_ERROR_MESSAGE("Failed to sync Radient drawable cache: renderable light changes were cleared before the cache consumed them. "
                          "Cache light revision: ",
                          m_SceneRevisions.Lights,
                          ", change log base revision: ",
                          ChangeLogState.LightsBaseRevision,
                          ", scene light revision: ",
                          SceneRevisions.Lights);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    if (UpdateRenderables)
    {
        State.EnumerateRenderableMeshChanges(
            [this](const RadientSceneState::RenderableMeshChange& Change,
                   const RadientSceneState::RenderableMesh*       pMesh) {
                if (pMesh != nullptr)
                {
                    ProcessRenderableMeshAddedOrUpdated(*pMesh);
                }
                else
                {
                    ProcessRenderableMeshRemoved(Change.Entity);
                }
            });
    }

    ResolvePendingRenderableMeshes();

    if (UpdateLights)
    {
        State.EnumerateRenderableLightChanges(
            [this](const RadientSceneState::RenderableLightChange& Change,
                   const RadientSceneState::RenderableLight*       pLight) {
                if (pLight != nullptr)
                {
                    ProcessRenderableLightAddedOrUpdated(*pLight);
                }
                else
                {
                    ProcessRenderableLightRemoved(Change.Entity);
                }
            });
    }

    m_SceneRevisions = SceneRevisions;

    return RADIENT_STATUS_OK;
}

void RadientSceneDrawableCache::ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh)
{
    auto record_it = m_Renderables.find(Mesh.Entity);

    const bool IsNewRecord = (record_it == m_Renderables.end());
    if (IsNewRecord)
    {
        record_it = m_Renderables.emplace(Mesh.Entity, RenderableRecord{}).first;
    }
    RenderableRecord& Record      = record_it->second;
    const bool        MeshChanged = !IsNewRecord && (Record.pMesh != Mesh.Mesh.pMesh);

    if (IsNewRecord || MeshChanged)
    {
        RemoveRenderableDrawables(Record);

        Record.pMesh             = Mesh.Mesh.pMesh;
        Record.PendingResolution = false;
    }

    Record.pRenderer         = &Mesh.Renderer;
    Record.pWorldMatrix      = &Mesh.WorldMatrix;
    Record.pEffectiveVisible = &Mesh.EffectiveVisible;

    if (Record.DrawableIDs.empty())
    {
        TryExpandRenderable(Mesh.Entity, Record);
    }
    else
    {
        for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        {
            VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID in renderable record");
            RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
            VERIFY(Slot.IsValid(), "Renderable record references an invalid drawable slot");

            Slot.pRenderer         = Record.pRenderer;
            Slot.pWorldMatrix      = Record.pWorldMatrix;
            Slot.pEffectiveVisible = Record.pEffectiveVisible;
            RecordDrawableChange(DrawableID, RadientDrawableChangeType::Updated);
        }
    }
}

void RadientSceneDrawableCache::ProcessRenderableMeshRemoved(RadientEntityID Entity)
{
    RenderableMap::iterator It = m_Renderables.find(Entity);
    if (It == m_Renderables.end())
        return;

    RemoveRenderableDrawables(It->second);
    m_Renderables.erase(It);
}

void RadientSceneDrawableCache::ProcessRenderableLightAddedOrUpdated(const RadientSceneState::RenderableLight& Light)
{
    LightMap::iterator It = m_Lights.find(Light.Entity);

    const bool IsNewRecord = (It == m_Lights.end());
    const bool TypeChanged = !IsNewRecord && (It->second.Type != Light.Light.Type);
    const bool NeedsAdd    = IsNewRecord || TypeChanged;

    if (TypeChanged)
        RemoveLightFromList(Light.Entity, It->second);

    if (NeedsAdd)
    {
        const size_t            ListIndex = m_LightLists.Add(Light.Light.Type, Light.Entity, Light.Light, Light.WorldMatrix, Light.EffectiveVisible);
        const LightListLocation Location{Light.Light.Type, ListIndex};
        if (IsNewRecord)
            m_Lights.emplace(Light.Entity, Location);
        else
            It->second = Location;

        RecordLightChange(Light.Entity, Light.Light.Type, RadientLightChangeType::Added);
    }
    else
    {
        RecordLightChange(Light.Entity, It->second.Type, RadientLightChangeType::Updated);
    }
}

void RadientSceneDrawableCache::ProcessRenderableLightRemoved(RadientEntityID Entity)
{
    LightMap::iterator It = m_Lights.find(Entity);
    if (It == m_Lights.end())
        return;

    RemoveLightFromList(Entity, It->second);
    m_Lights.erase(It);
}

void RadientSceneDrawableCache::ResolvePendingRenderableMeshes()
{
    m_PendingRenderableEntitiesScratch.clear();
    m_PendingRenderableEntitiesScratch.swap(m_PendingRenderableEntities);

    for (const RadientEntityID Entity : m_PendingRenderableEntitiesScratch)
    {
        RenderableMap::iterator It = m_Renderables.find(Entity);
        if (It == m_Renderables.end())
        {
            // Pending renderable was removed.
            continue;
        }

        RenderableRecord& Record = It->second;
        if (!Record.PendingResolution || !Record.DrawableIDs.empty())
            continue;

        Record.PendingResolution = false;
        TryExpandRenderable(Entity, Record);
    }
    m_PendingRenderableEntitiesScratch.clear();
}

bool RadientSceneDrawableCache::TryExpandRenderable(RadientEntityID Entity, RenderableRecord& Record)
{
    RadientDrawableMesh Mesh;
    switch (m_MeshProvider.GetDrawableMesh(Record.pMesh, Mesh))
    {
        case RadientDrawableMeshStatus::Ready:
            break;

        case RadientDrawableMeshStatus::Pending:
            AddPendingResolution(Entity, Record);
            return false;

        case RadientDrawableMeshStatus::Failed:
            return false;
    }

    Record.PendingResolution = false;
    RemoveRenderableDrawables(Record);

    Record.DrawableIDs.reserve(Mesh.pMesh->Primitives.size());
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < Mesh.pMesh->Primitives.size(); ++PrimitiveIndex)
    {
        const GLTF::Primitive& Primitive = Mesh.pMesh->Primitives[PrimitiveIndex];

        const bool   IsIndexed    = Primitive.HasIndices();
        const Uint32 FirstElement = IsIndexed ? Primitive.FirstIndex : Primitive.FirstVertex;
        const Uint32 ElementCount = IsIndexed ? Primitive.IndexCount : Primitive.VertexCount;

        if (ElementCount == 0)
            continue;

        if (Primitive.MaterialId >= Mesh.pModel->Materials.size())
            continue;

        const GLTF::Material* pMaterial = &Mesh.pModel->Materials[Primitive.MaterialId];

        const RadientDrawableID DrawableID = AllocateDrawableID();
        RadientDrawableSlot&    Slot       = m_DrawableSlots[DrawableID];

        Slot.Entity             = Entity;
        Slot.pRenderer          = Record.pRenderer;
        Slot.pWorldMatrix       = Record.pWorldMatrix;
        Slot.pEffectiveVisible  = Record.pEffectiveVisible;
        Slot.IsIndexed          = IsIndexed;
        Slot.pMaterial          = pMaterial;
        Slot.VertexAttribFlags  = Mesh.VertexAttribFlags;
        Slot.FirstIndexLocation = Mesh.FirstIndexLocation;
        Slot.BaseVertex         = Mesh.BaseVertex;
        Slot.FirstElement       = FirstElement;
        Slot.ElementCount       = ElementCount;
        Slot.AlphaMode          = CorrectMaterialAlphaMode(pMaterial->Attribs.AlphaMode);

        Slot.DrawListIndex = m_DrawLists.Add(static_cast<GLTF::Material::ALPHA_MODE>(Slot.AlphaMode), DrawableID);
        Record.DrawableIDs.push_back(DrawableID);
        RecordDrawableChange(DrawableID, RadientDrawableChangeType::Added);
    }

    return true;
}

RadientDrawableID RadientSceneDrawableCache::AllocateDrawableID()
{
    RadientDrawableID DrawableID = InvalidRadientDrawableID;
    if (!m_FreeDrawableIDs.empty())
    {
        DrawableID = m_FreeDrawableIDs.back();
        m_FreeDrawableIDs.pop_back();
    }
    else
    {
        DrawableID = static_cast<RadientDrawableID>(m_DrawableSlots.size());
        m_DrawableSlots.emplace_back();
    }

    RadientDrawableSlot& Slot       = m_DrawableSlots[DrawableID];
    const Uint32         Generation = Slot.Generation + 1u;
    Slot                            = {};
    Slot.Generation                 = Generation;

    return DrawableID;
}

void RadientSceneDrawableCache::FreeDrawableID(RadientDrawableID DrawableID)
{
    if (DrawableID >= m_DrawableSlots.size())
    {
        UNEXPECTED("Trying to free an invalid drawable ID");
        return;
    }

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    VERIFY(Slot.IsValid(), "Trying to free an invalid drawable slot");
    VERIFY(Slot.IsInDrawList(), "Trying to free a drawable slot that is not in a draw list");

    // Remove the drawable from its draw list.
    const RadientDrawableID MovedDrawableID = m_DrawLists.RemoveAt(static_cast<GLTF::Material::ALPHA_MODE>(Slot.AlphaMode), Slot.DrawListIndex);
    if (MovedDrawableID != InvalidRadientDrawableID && MovedDrawableID != DrawableID)
    {
        VERIFY(MovedDrawableID < m_DrawableSlots.size(), "Draw list returned invalid moved drawable ID");
        RadientDrawableSlot& MovedSlot = m_DrawableSlots[MovedDrawableID];
        VERIFY(MovedSlot.IsInDrawList() && MovedSlot.AlphaMode == Slot.AlphaMode,
               "Moved drawable slot does not match the draw list it was moved inside");
        MovedSlot.DrawListIndex = Slot.DrawListIndex;
    }

    const Uint32 Generation = Slot.Generation + 1u;
    Slot                    = {};
    Slot.Generation         = Generation;

    RecordDrawableChange(DrawableID, RadientDrawableChangeType::Removed);
    m_FreeDrawableIDs.push_back(DrawableID);
}

void RadientSceneDrawableCache::RemoveRenderableDrawables(RenderableRecord& Record)
{
    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        FreeDrawableID(DrawableID);
    Record.DrawableIDs.clear();
}

void RadientSceneDrawableCache::AddPendingResolution(RadientEntityID Entity, RenderableRecord& Record)
{
    if (Record.PendingResolution)
        return;

    Record.PendingResolution = true;
    m_PendingRenderableEntities.push_back(Entity);
}

void RadientSceneDrawableCache::RecordDrawableChange(RadientDrawableID DrawableID, RadientDrawableChangeType Type)
{
    if (DrawableID == InvalidRadientDrawableID)
        return;

    m_DrawableChanges.push_back({DrawableID, Type});
}

void RadientSceneDrawableCache::RemoveLightFromList(RadientEntityID Entity, const LightListLocation& Location)
{
    const RADIENT_LIGHT_TYPE RemovedType = Location.Type;
    const RadientEntityID    MovedEntity = m_LightLists.RemoveAt(RemovedType, Location.Index);
    if (MovedEntity != InvalidRadientEntityID && MovedEntity != Entity)
    {
        LightMap::iterator MovedIt = m_Lights.find(MovedEntity);
        VERIFY(MovedIt != m_Lights.end(), "Light list returned moved entity that is missing from the light records");
        if (MovedIt != m_Lights.end())
            MovedIt->second.Index = Location.Index;
    }

    RecordLightChange(Entity, RemovedType, RadientLightChangeType::Removed);
}

void RadientSceneDrawableCache::RecordLightChange(RadientEntityID Entity, RADIENT_LIGHT_TYPE Type, RadientLightChangeType Change)
{
    if (Entity == InvalidRadientEntityID)
    {
        UNEXPECTED("Trying to record a light change for an invalid entity");
        return;
    }

    m_LightChanges.push_back({Entity, Type, Change});
}

} // namespace Diligent
