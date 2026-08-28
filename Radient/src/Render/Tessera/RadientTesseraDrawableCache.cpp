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

#include "Render/Tessera/RadientTesseraDrawableCache.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetStatus.hpp"
#include "Scene/RadientSceneImpl.hpp"

#include "Cast.hpp"
#include "DebugUtilities.hpp"

#include <algorithm>

namespace Diligent
{

namespace
{

PBR_Renderer::ALPHA_MODE ToPBRAlphaMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) noexcept
{
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_OPAQUE) == PBR_Renderer::ALPHA_MODE_OPAQUE, "Radient opaque surface mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_MASKED) == PBR_Renderer::ALPHA_MODE_MASK, "Radient masked surface mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT) == PBR_Renderer::ALPHA_MODE_BLEND, "Radient transparent surface mode must match PBR alpha mode");

    const auto AlphaMode = static_cast<PBR_Renderer::ALPHA_MODE>(SurfaceMode);
    if (AlphaMode < PBR_Renderer::ALPHA_MODE_OPAQUE ||
        AlphaMode >= PBR_Renderer::ALPHA_MODE_NUM_MODES)
    {
        UNEXPECTED("Unexpected Radient material surface mode");
        return PBR_Renderer::ALPHA_MODE_OPAQUE;
    }
    return AlphaMode;
}

class RadientAssetDrawableMeshProvider final : public IRadientDrawableMeshProvider
{
public:
    RadientDrawableMeshResolveResult GetDrawableMesh(IRadientMeshAsset* pMeshAsset) override final
    {
        if (pMeshAsset == nullptr)
            return {};

        const RadientDrawableMeshResolveResult Result =
            RadientAssetManagerImpl::GetDrawableMesh(pMeshAsset, true);
        return {Result.Status == RADIENT_STATUS_OK ? Result.pMesh : nullptr, Result.Status};
    }
};

IRadientDrawableMeshProvider& GetDefaultDrawableMeshProvider()
{
    static RadientAssetDrawableMeshProvider Provider;
    return Provider;
}

} // namespace

RadientTesseraDrawableCache::RadientTesseraDrawableCache(IRadientDrawableMeshProvider* pMeshProvider) :
    m_MeshProvider{pMeshProvider != nullptr ? *pMeshProvider : GetDefaultDrawableMeshProvider()}
{
}

RADIENT_STATUS RadientTesseraDrawableCache::SyncScene(
    const IRadientScene&                        Scene,
    const RadientTesseraMaterialResolveContext& MaterialResolveContext)
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
            [this, &MaterialResolveContext](const RadientSceneState::RenderableMeshChange& Change,
                                            const RadientSceneState::RenderableMesh*       pMesh) {
                if (pMesh != nullptr)
                {
                    ProcessRenderableMeshAddedOrUpdated(*pMesh, MaterialResolveContext);
                }
                else
                {
                    ProcessRenderableMeshRemoved(Change.Entity);
                }
            });
    }

    ResolvePendingRenderableMeshes(MaterialResolveContext);

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

RADIENT_STATUS RadientTesseraDrawableCache::PrepareSkinningData()
{
    RADIENT_STATUS Status     = RADIENT_STATUS_OK;
    bool           HasChanges = false;
    for (const SkinnedRenderable& Renderable : m_SkinnedRenderables)
    {
        VERIFY(Renderable.pSkinData != nullptr, "Skinned renderable list contains null skin data");
        if (Renderable.pSkinData == nullptr)
            continue;

        const RADIENT_STATUS SkinStatus = Renderable.pSkinData->Prepare();
        if (SkinStatus == RADIENT_STATUS_OK)
            HasChanges = true;
        else if (SkinStatus != RADIENT_STATUS_NO_CHANGE)
            Status = CombineDependencyStatus(Status, SkinStatus);
    }
    return Status == RADIENT_STATUS_OK && !HasChanges ? RADIENT_STATUS_NO_CHANGE : Status;
}

void RadientTesseraDrawableCache::ProcessRenderableMeshAddedOrUpdated(
    const RadientSceneState::RenderableMesh&    Mesh,
    const RadientTesseraMaterialResolveContext& MaterialResolveContext)
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
        m_PendingMaterialData.erase(Mesh.Entity);

        Record.pMesh             = Mesh.Mesh.pMesh;
        Record.PendingResolution = false;
    }

    Record.pRenderer         = &Mesh.Renderer;
    Record.pWorldMatrix      = &Mesh.WorldMatrix;
    Record.pEffectiveVisible = &Mesh.EffectiveVisible;

    UpdateRenderableSkin(Mesh.Entity, Record, Mesh.pSkin);

    if (Record.DrawableIDs.empty())
    {
        TryExpandRenderable(Mesh.Entity, Record, MaterialResolveContext);
    }
    else
    {
        RadientTesseraSkinData* const pSkinData = GetRenderableSkinData(Mesh.Entity, Record);
        for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        {
            VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID in renderable record");
            RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
            VERIFY(Slot.IsValid(), "Renderable record references an invalid drawable slot");

            Slot.pRenderer         = Record.pRenderer;
            Slot.pWorldMatrix      = Record.pWorldMatrix;
            Slot.pEffectiveVisible = Record.pEffectiveVisible;
            Slot.pSkinData         = pSkinData;
            RecordDrawableChange(DrawableID, RadientDrawableChangeType::Updated);
        }
    }
}

void RadientTesseraDrawableCache::ProcessRenderableMeshRemoved(RadientEntityID Entity)
{
    RenderableMap::iterator It = m_Renderables.find(Entity);
    if (It == m_Renderables.end())
        return;

    RemoveRenderableDrawables(It->second);
    m_PendingMaterialData.erase(Entity);
    RemoveRenderableSkin(Entity, It->second);
    m_Renderables.erase(It);
}

void RadientTesseraDrawableCache::UpdateRenderableSkin(RadientEntityID             Entity,
                                                       RenderableRecord&           Record,
                                                       const RadientSkinComponent* pSkin)
{
    if (pSkin == nullptr)
    {
        RemoveRenderableSkin(Entity, Record);
        return;
    }

    RadientTesseraSkinData* const pCurrentSkinData = GetRenderableSkinData(Entity, Record);
    if (pCurrentSkinData != nullptr &&
        pCurrentSkinData->Matches(pSkin->pSkin, pSkin->pPose))
    {
        return;
    }

    std::unique_ptr<RadientTesseraSkinData> pSkinData =
        std::make_unique<RadientTesseraSkinData>(pSkin->pSkin, pSkin->pPose);

    if (Record.SkinListIndex == InvalidSkinListIndex)
    {
        const size_t SkinListIndex = m_SkinnedRenderables.size();
        m_SkinnedRenderables.push_back({Entity, std::move(pSkinData)});
        Record.SkinListIndex = SkinListIndex;
    }
    else
    {
        VERIFY(Record.SkinListIndex < m_SkinnedRenderables.size(), "Renderable skin list index is invalid");
        if (Record.SkinListIndex >= m_SkinnedRenderables.size())
            return;

        SkinnedRenderable& Renderable = m_SkinnedRenderables[Record.SkinListIndex];
        VERIFY(Renderable.Entity == Entity, "Renderable skin list index references a different entity");
        if (Renderable.Entity != Entity)
            return;

        Renderable.pSkinData = std::move(pSkinData);
    }
}

void RadientTesseraDrawableCache::RemoveRenderableSkin(RadientEntityID   Entity,
                                                       RenderableRecord& Record)
{
    if (Record.SkinListIndex == InvalidSkinListIndex)
        return;

    const size_t RemovedIndex = Record.SkinListIndex;
    VERIFY(RemovedIndex < m_SkinnedRenderables.size(), "Renderable skin list index is invalid");
    if (RemovedIndex >= m_SkinnedRenderables.size())
    {
        Record.SkinListIndex = InvalidSkinListIndex;
        return;
    }

    VERIFY(m_SkinnedRenderables[RemovedIndex].Entity == Entity, "Renderable skin list index references a different entity");
    if (m_SkinnedRenderables[RemovedIndex].Entity != Entity)
        return;

    const size_t LastIndex = m_SkinnedRenderables.size() - 1;
    if (RemovedIndex != LastIndex)
    {
        const RadientEntityID MovedEntity  = m_SkinnedRenderables.back().Entity;
        m_SkinnedRenderables[RemovedIndex] = std::move(m_SkinnedRenderables.back());

        RenderableMap::iterator MovedIt = m_Renderables.find(MovedEntity);
        VERIFY(MovedIt != m_Renderables.end(), "Skinned renderable list references an entity missing from the renderable records");
        if (MovedIt != m_Renderables.end())
            MovedIt->second.SkinListIndex = RemovedIndex;
    }

    m_SkinnedRenderables.pop_back();
    Record.SkinListIndex = InvalidSkinListIndex;
}

RadientTesseraSkinData* RadientTesseraDrawableCache::GetRenderableSkinData(
    RadientEntityID         Entity,
    const RenderableRecord& Record) const
{
    if (Record.SkinListIndex == InvalidSkinListIndex)
        return nullptr;

    VERIFY(Record.SkinListIndex < m_SkinnedRenderables.size(), "Renderable skin list index is invalid");
    if (Record.SkinListIndex >= m_SkinnedRenderables.size())
        return nullptr;

    const SkinnedRenderable& Renderable = m_SkinnedRenderables[Record.SkinListIndex];
    VERIFY(Renderable.Entity == Entity, "Renderable skin list index references a different entity");
    return Renderable.Entity == Entity ? Renderable.pSkinData.get() : nullptr;
}

void RadientTesseraDrawableCache::ProcessRenderableLightAddedOrUpdated(const RadientSceneState::RenderableLight& Light)
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

void RadientTesseraDrawableCache::ProcessRenderableLightRemoved(RadientEntityID Entity)
{
    LightMap::iterator It = m_Lights.find(Entity);
    if (It == m_Lights.end())
        return;

    RemoveLightFromList(Entity, It->second);
    m_Lights.erase(It);
}

void RadientTesseraDrawableCache::ResolvePendingRenderableMeshes(
    const RadientTesseraMaterialResolveContext& MaterialResolveContext)
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
        TryExpandRenderable(Entity, Record, MaterialResolveContext);
    }
    m_PendingRenderableEntitiesScratch.clear();
}

bool RadientTesseraDrawableCache::TryExpandRenderable(
    RadientEntityID                             Entity,
    RenderableRecord&                           Record,
    const RadientTesseraMaterialResolveContext& MaterialResolveContext)
{
    const RadientDrawableMeshResolveResult ResolveResult = m_MeshProvider.GetDrawableMesh(Record.pMesh);
    if (ResolveResult.Status == RADIENT_STATUS_PENDING)
    {
        AddPendingResolution(Entity, Record);
        return false;
    }

    if (ResolveResult.Status != RADIENT_STATUS_OK)
    {
        m_PendingMaterialData.erase(Entity);
        return false;
    }

    VERIFY(ResolveResult.pMesh != nullptr, "Drawable mesh provider returned ready status with null mesh data");
    if (ResolveResult.pMesh == nullptr)
    {
        m_PendingMaterialData.erase(Entity);
        return false;
    }

    const RadientDrawableMesh& Mesh = *ResolveResult.pMesh;

    std::vector<RadientTesseraMaterialDataMap::ValueHandle> MaterialData;
    if (MaterialResolveContext.pMaterialCache == nullptr)
    {
        AddPendingResolution(Entity, Record);
        return false;
    }

    bool HasPendingMaterial = false;
    MaterialData.resize(Mesh.Primitives.size());
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh.Primitives.size(); ++PrimitiveIndex)
    {
        const RadientDrawableMeshPrimitive& Primitive = Mesh.Primitives[PrimitiveIndex];
        if (Primitive.ElementCount == 0 || Primitive.pMaterialAsset == nullptr)
            continue;

        RadientTesseraMaterialResolveResult MaterialResult =
            MaterialResolveContext.pMaterialCache->Resolve(
                MaterialResolveContext.ThreadPool,
                Primitive.pMaterialAsset);
        if (!MaterialResult.Data)
            continue;

        const RADIENT_STATUS MaterialStatus = MaterialResult.Data->GetGPUResourceStatus();
        if (MaterialStatus == RADIENT_STATUS_PENDING)
            HasPendingMaterial = true;
        else if (MaterialStatus != RADIENT_STATUS_OK)
            continue;

        MaterialData[PrimitiveIndex] = std::move(MaterialResult.Data);
    }

    if (HasPendingMaterial)
    {
        // The material cache stores weak values. Retain the resolved handles
        // until the renderable is retried and they can move into drawable slots.
        m_PendingMaterialData[Entity] = std::move(MaterialData);
        AddPendingResolution(Entity, Record);
        return false;
    }

    Record.PendingResolution = false;
    m_PendingMaterialData.erase(Entity);
    RemoveRenderableDrawables(Record);

    Record.DrawableIDs.reserve(Mesh.Primitives.size());
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh.Primitives.size(); ++PrimitiveIndex)
    {
        const RadientDrawableMeshPrimitive& Primitive = Mesh.Primitives[PrimitiveIndex];
        if (Primitive.ElementCount == 0)
            continue;

        if (!MaterialData[PrimitiveIndex])
            continue;

        if (Primitive.GeometryIndex >= Mesh.Geometries.size())
            continue;

        const RadientDrawableMeshGeometry& Geometry = Mesh.Geometries[Primitive.GeometryIndex];
        if (Geometry.pVertexPool == nullptr)
            continue;

        const RadientDrawableID DrawableID = AllocateDrawableID();
        RadientDrawableSlot&    Slot       = m_DrawableSlots[DrawableID];

        Slot.Entity             = Entity;
        Slot.pRenderer          = Record.pRenderer;
        Slot.pWorldMatrix       = Record.pWorldMatrix;
        Slot.pEffectiveVisible  = Record.pEffectiveVisible;
        Slot.pSkinData          = GetRenderableSkinData(Entity, Record);
        Slot.IsIndexed          = Primitive.IsIndexed;
        Slot.MaterialData       = std::move(MaterialData[PrimitiveIndex]);
        Slot.pVertexPool        = Geometry.pVertexPool;
        Slot.VertexAttribFlags  = Geometry.VertexAttribFlags;
        Slot.FirstIndexLocation = Geometry.FirstIndexLocation;
        Slot.BaseVertex         = Geometry.BaseVertex;
        Slot.FirstElement       = Primitive.FirstElement;
        Slot.ElementCount       = Primitive.ElementCount;
        Slot.AlphaMode          = ToPBRAlphaMode(Slot.MaterialData->GetSurfaceMode());

        Slot.DrawListIndex = m_DrawLists.Add(Slot.AlphaMode, DrawableID);
        Record.DrawableIDs.push_back(DrawableID);
        RecordDrawableChange(DrawableID, RadientDrawableChangeType::Added);
    }

    return true;
}

RadientDrawableID RadientTesseraDrawableCache::AllocateDrawableID()
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

void RadientTesseraDrawableCache::FreeDrawableID(RadientDrawableID DrawableID)
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
    const RadientDrawableID MovedDrawableID = m_DrawLists.RemoveAt(Slot.AlphaMode, Slot.DrawListIndex);
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

void RadientTesseraDrawableCache::RemoveRenderableDrawables(RenderableRecord& Record)
{
    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        FreeDrawableID(DrawableID);
    Record.DrawableIDs.clear();
}

void RadientTesseraDrawableCache::AddPendingResolution(RadientEntityID Entity, RenderableRecord& Record)
{
    if (Record.PendingResolution)
        return;

    Record.PendingResolution = true;
    m_PendingRenderableEntities.push_back(Entity);
}

void RadientTesseraDrawableCache::RecordDrawableChange(RadientDrawableID DrawableID, RadientDrawableChangeType Type)
{
    if (DrawableID == InvalidRadientDrawableID)
        return;

    m_DrawableChanges.push_back({DrawableID, Type});
}

void RadientTesseraDrawableCache::RemoveLightFromList(RadientEntityID Entity, const LightListLocation& Location)
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

void RadientTesseraDrawableCache::RecordLightChange(RadientEntityID Entity, RADIENT_LIGHT_TYPE Type, RadientLightChangeType Change)
{
    if (Entity == InvalidRadientEntityID)
    {
        UNEXPECTED("Trying to record a light change for an invalid entity");
        return;
    }

    m_LightChanges.push_back({Entity, Type, Change});
}

} // namespace Diligent
