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
#include "HashUtils.hpp"

#include <algorithm>

namespace Diligent
{

size_t RadientTesseraDrawableCache::SkinDataKey::Hasher::operator()(const SkinDataKey& Key) const noexcept
{
    return ComputeHash(Key.pSkin, Key.pPose);
}

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

RadientTesseraDrawableCache::RadientTesseraDrawableCache(
    RadientTesseraBufferSuballocator& JointBuffer,
    IRadientDrawableMeshProvider*     pMeshProvider,
    Uint32                            MaxActiveMorphTargetCount) :
    m_MeshProvider{pMeshProvider != nullptr ? *pMeshProvider : GetDefaultDrawableMeshProvider()},
    m_JointBuffer{JointBuffer},
    m_MaxActiveMorphTargetCount{MaxActiveMorphTargetCount}
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
    const bool UpdateDeformationVisibility =
        UpdateRenderables || m_SceneRevisions.Visibility != SceneRevisions.Visibility;

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

    // Pending meshes can acquire geometry bindings without a scene revision change.
    if (UpdateDeformationVisibility || !m_DrawableChanges.empty())
        RebuildVisibleDeformationDataLists();

    m_SceneRevisions = SceneRevisions;

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraDrawableCache::PrepareSkinningData(RadientFrameID RenderFrameID, bool PackMatrixRowMajor)
{
    RADIENT_STATUS Status     = RADIENT_STATUS_OK;
    bool           HasChanges = false;
    for (RadientTesseraSkinData* pSkinData : m_VisibleSkinData)
    {
        VERIFY(pSkinData != nullptr, "Visible skin data list contains a null entry");
        if (pSkinData == nullptr)
            continue;

        const RADIENT_STATUS SkinStatus = pSkinData->Prepare(RenderFrameID, PackMatrixRowMajor);
        if (SkinStatus == RADIENT_STATUS_OK)
            HasChanges = true;
        else if (SkinStatus != RADIENT_STATUS_NO_CHANGE)
            Status = CombineDependencyStatus(Status, SkinStatus);
    }
    return Status == RADIENT_STATUS_OK && !HasChanges ? RADIENT_STATUS_NO_CHANGE : Status;
}

RADIENT_STATUS RadientTesseraDrawableCache::PrepareMorphTargetData(RadientFrameID RenderFrameID)
{
    RADIENT_STATUS Status     = RADIENT_STATUS_OK;
    bool           HasChanges = false;
    for (RadientTesseraMorphData* pMorphData : m_VisibleMorphData)
    {
        const RADIENT_STATUS MorphStatus = pMorphData->Prepare(RenderFrameID);
        if (MorphStatus == RADIENT_STATUS_OK)
            HasChanges = true;
        else if (MorphStatus != RADIENT_STATUS_NO_CHANGE)
            Status = CombineDependencyStatus(Status, MorphStatus);
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
    Record.pMorphWeights     = m_MaxActiveMorphTargetCount != 0 && Mesh.pMorph != nullptr ? Mesh.pMorph->pWeights : nullptr;

    UpdateRenderableSkin(Record, Mesh.pSkin);
    // A different weights object replaces the per-geometry attachments.
    const bool MorphChanged = Record.pMorphWeights != nullptr ?
        Record.pMorphState == nullptr || !Record.pMorphState->MorphData.Matches(Record.pMorphWeights) :
        Record.pMorphState != nullptr;
    if (MorphChanged)
    {
        RemoveRenderableDrawables(Record);
        RemoveRenderableMorph(Record);
    }
    if (Record.pMorphState != nullptr)
        Record.pMorphState->pEffectiveVisible = Record.pEffectiveVisible;

    if (Record.DrawableIDs.empty())
    {
        TryExpandRenderable(Mesh.Entity, Record, MaterialResolveContext);
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
            Slot.pSkinAttachment   = Record.pSkinAttachment.get();
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
    RemoveRenderableSkin(It->second);
    RemoveRenderableMorph(It->second);
    m_Renderables.erase(It);
}

void RadientTesseraDrawableCache::UpdateRenderableSkin(RenderableRecord&           Record,
                                                       const RadientSkinComponent* pSkin)
{
    if (pSkin == nullptr)
    {
        RemoveRenderableSkin(Record);
        return;
    }

    RadientTesseraSkinData* const pCurrentSkinData =
        Record.pSkinAttachment != nullptr ? &Record.pSkinAttachment->SkinData : nullptr;

    if (pCurrentSkinData == nullptr || !pCurrentSkinData->Matches(pSkin->pSkin, pSkin->pPose))
    {
        RemoveRenderableSkin(Record);

        const SkinDataKey Key{pSkin->pSkin, pSkin->pPose};
        auto [It, Inserted]       = m_SkinDataCache.try_emplace(Key);
        SkinDataCacheEntry& Entry = It->second;
        if (Inserted)
        {
            Entry.pSkinData = std::make_unique<RadientTesseraSkinData>(
                pSkin->pSkin,
                pSkin->pPose,
                m_JointBuffer);
        }

        VERIFY(Entry.pSkinData != nullptr, "Skin data cache contains a null entry");
        Record.pSkinAttachment = std::make_unique<RadientTesseraSkinAttachment>(
            RadientTesseraSkinAttachment{*Entry.pSkinData});
        Record.pSkinAttachment->CacheEntryAttachmentIndex = Entry.Attachments.size();
        Entry.Attachments.push_back(Record.pSkinAttachment.get());
    }

    Record.pSkinAttachment->pEffectiveVisible = Record.pEffectiveVisible;

    Record.pSkinAttachment->SkeletonToMeshTransform =
        pSkin->SkeletonToMeshTransform != RadientMatrix4x4{} ?
        std::optional<RadientMatrix4x4>{pSkin->SkeletonToMeshTransform} :
        std::optional<RadientMatrix4x4>{};
}

void RadientTesseraDrawableCache::RemoveRenderableSkin(RenderableRecord& Record)
{
    if (Record.pSkinAttachment == nullptr)
        return;

    RadientTesseraSkinAttachment& Attachment = *Record.pSkinAttachment;
    RadientTesseraSkinData&       SkinData   = Attachment.SkinData;
    const SkinDataKey             Key{SkinData.GetSkin(), SkinData.GetPose()};
    const auto                    It = m_SkinDataCache.find(Key);
    if (It == m_SkinDataCache.end() || It->second.pSkinData.get() != &SkinData)
    {
        UNEXPECTED("Renderable references skin data missing from the cache");
        Record.pSkinAttachment.reset();
        return;
    }

    SkinDataCacheEntry& Entry           = It->second;
    size_t              AttachmentIndex = Attachment.CacheEntryAttachmentIndex;
    if (AttachmentIndex >= Entry.Attachments.size() ||
        Entry.Attachments[AttachmentIndex] != &Attachment)
    {
        UNEXPECTED("Skin attachment has an invalid cache entry index");

        const auto AttachmentIt = std::find(Entry.Attachments.begin(), Entry.Attachments.end(), &Attachment);
        if (AttachmentIt == Entry.Attachments.end())
        {
            Record.pSkinAttachment.reset();
            return;
        }
        AttachmentIndex = static_cast<size_t>(AttachmentIt - Entry.Attachments.begin());
    }

    RadientTesseraSkinAttachment* const pMovedAttachment = Entry.Attachments.back();
    VERIFY(pMovedAttachment != nullptr, "Skin data cache contains a null attachment");
    Entry.Attachments[AttachmentIndex]          = pMovedAttachment;
    pMovedAttachment->CacheEntryAttachmentIndex = AttachmentIndex;
    Entry.Attachments.pop_back();

    if (Entry.Attachments.empty())
        m_SkinDataCache.erase(It);

    Record.pSkinAttachment.reset();
}

void RadientTesseraDrawableCache::InitializeRenderableMorph(RenderableRecord&          Record,
                                                            const RadientDrawableMesh& Mesh)
{
    if (Record.pMorphWeights == nullptr)
        return;
    if (Record.pMorphState == nullptr)
    {
        auto [It, Inserted]        = m_MorphDataCache.try_emplace(Record.pMorphWeights.RawPtr());
        MorphDataCacheEntry& Entry = It->second;
        if (Inserted)
        {
            Entry.pMorphData = std::make_unique<RadientTesseraMorphData>(
                Record.pMorphWeights,
                Mesh,
                m_MaxActiveMorphTargetCount);
        }

        VERIFY(Entry.pMorphData != nullptr, "Morph data cache contains a null entry");
        Record.pMorphState = std::make_unique<RenderableMorphState>(
            RenderableMorphState{*Entry.pMorphData});
        Record.pMorphState->CacheEntryIndex = Entry.Renderables.size();
        Entry.Renderables.push_back(Record.pMorphState.get());
    }

    Record.pMorphState->pEffectiveVisible = Record.pEffectiveVisible;
}

void RadientTesseraDrawableCache::RemoveRenderableMorph(RenderableRecord& Record)
{
    if (Record.pMorphState == nullptr)
        return;

    RenderableMorphState&    State     = *Record.pMorphState;
    RadientTesseraMorphData& MorphData = State.MorphData;
    const auto              It        = m_MorphDataCache.find(MorphData.GetWeights());
    if (It == m_MorphDataCache.end() || It->second.pMorphData.get() != &MorphData)
    {
        UNEXPECTED("Renderable references morph data missing from the cache");
        Record.pMorphState.reset();
        return;
    }

    MorphDataCacheEntry& Entry = It->second;
    size_t              Index = State.CacheEntryIndex;
    if (Index >= Entry.Renderables.size() || Entry.Renderables[Index] != &State)
    {
        UNEXPECTED("Morph renderable has an invalid cache entry index");

        const auto RenderableIt = std::find(Entry.Renderables.begin(), Entry.Renderables.end(), &State);
        if (RenderableIt == Entry.Renderables.end())
        {
            Record.pMorphState.reset();
            return;
        }
        Index = static_cast<size_t>(RenderableIt - Entry.Renderables.begin());
    }

    RenderableMorphState* const pMovedState = Entry.Renderables.back();
    VERIFY(pMovedState != nullptr, "Morph data cache contains a null renderable");
    Entry.Renderables[Index]    = pMovedState;
    pMovedState->CacheEntryIndex = Index;
    Entry.Renderables.pop_back();

    Record.pMorphState.reset();
    if (Entry.Renderables.empty())
        m_MorphDataCache.erase(It);
}

void RadientTesseraDrawableCache::RebuildVisibleDeformationDataLists()
{
    m_VisibleSkinData.clear();
    m_VisibleSkinData.reserve(m_SkinDataCache.size());

    for (const auto& CacheItem : m_SkinDataCache)
    {
        const SkinDataCacheEntry& Entry = CacheItem.second;
        VERIFY(Entry.pSkinData != nullptr, "Skin data cache contains a null entry");
        if (Entry.pSkinData == nullptr)
            continue;

        for (const RadientTesseraSkinAttachment* pAttachment : Entry.Attachments)
        {
            VERIFY(pAttachment != nullptr, "Skin data cache contains a null attachment");
            if (pAttachment != nullptr &&
                pAttachment->pEffectiveVisible != nullptr &&
                *pAttachment->pEffectiveVisible)
            {
                m_VisibleSkinData.push_back(Entry.pSkinData.get());
                break;
            }
        }
    }

    m_VisibleMorphData.clear();
    m_VisibleMorphData.reserve(m_MorphDataCache.size());

    for (const auto& CacheItem : m_MorphDataCache)
    {
        const MorphDataCacheEntry& Entry = CacheItem.second;
        VERIFY(Entry.pMorphData != nullptr, "Morph data cache contains a null entry");
        if (Entry.pMorphData == nullptr)
            continue;

        for (const RenderableMorphState* pState : Entry.Renderables)
        {
            VERIFY(pState != nullptr, "Morph data cache contains a null renderable");
            if (pState != nullptr &&
                pState->pEffectiveVisible != nullptr &&
                *pState->pEffectiveVisible)
            {
                m_VisibleMorphData.push_back(Entry.pMorphData.get());
                break;
            }
        }
    }
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

    InitializeRenderableMorph(Record, Mesh);
    if (Record.pMorphState != nullptr)
        Record.pMorphState->GeometryAttachments.resize(Mesh.Geometries.size());

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

        if (Record.pMorphState != nullptr && Geometry.pMorphTargetData != nullptr)
        {
            auto& Attachment = Record.pMorphState->GeometryAttachments[Primitive.GeometryIndex];
            if (!Attachment)
            {
                Attachment.emplace(RadientTesseraMorphAttachment{Record.pMorphState->MorphData, Primitive.GeometryIndex});
            }
            Slot.pMorphAttachment = &*Attachment;
        }

        Slot.Entity             = Entity;
        Slot.pRenderer          = Record.pRenderer;
        Slot.pWorldMatrix       = Record.pWorldMatrix;
        Slot.pEffectiveVisible  = Record.pEffectiveVisible;
        Slot.pSkinAttachment    = Record.pSkinAttachment.get();
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
    if (Record.pMorphState != nullptr)
        Record.pMorphState->GeometryAttachments.clear();
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
