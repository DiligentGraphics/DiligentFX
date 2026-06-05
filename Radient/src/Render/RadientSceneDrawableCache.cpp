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

bool IsSameMeshAsset(const RadientMeshComponent& Mesh0,
                     const RadientMeshComponent& Mesh1)
{
    return Mesh0.pMesh == Mesh1.pMesh;
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

    const RadientSceneRevisions& SceneRevisions = Scene.GetSceneRevisions();
    if (m_SceneRevisions == SceneRevisions && m_PendingRenderableEntities.empty())
        return RADIENT_STATUS_NO_CHANGE;

    const bool UpdateRenderables =
        m_SceneRevisions.Drawables != SceneRevisions.Drawables;
    const bool UpdateLightList =
        m_SceneRevisions.Lights != SceneRevisions.Lights ||
        m_SceneRevisions.Visibility != SceneRevisions.Visibility;

    const RadientSceneImpl*  pSceneImpl = ClassPtrCast<const RadientSceneImpl>(&Scene);
    const RadientSceneState& State      = pSceneImpl->GetState();

    RADIENT_STATUS Status = RADIENT_STATUS_NO_CHANGE;
    if (UpdateRenderables)
    {
        Status = State.EnumerateRenderableMeshChanges(
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
        if (RADIENT_FAILED(Status))
            return Status;
    }

    ResolvePendingRenderableMeshes();

    RADIENT_STATUS LightStatus = RADIENT_STATUS_NO_CHANGE;
    if (UpdateLightList)
    {
        m_LightList.Clear();

        LightStatus = State.EnumerateRenderableLights(
            [this](const RadientSceneState::RenderableLight& Light) {
                if (!Light.EffectiveVisible)
                    return;

                m_LightList.Add(Light.Entity, Light.Light, Light.WorldMatrix);
            });
        if (RADIENT_FAILED(LightStatus))
            return LightStatus;
    }

    m_SceneRevisions = SceneRevisions;

    if (Status == RADIENT_STATUS_OUT_OF_DATE || LightStatus == RADIENT_STATUS_OUT_OF_DATE)
        return RADIENT_STATUS_OUT_OF_DATE;

    return RADIENT_STATUS_OK;
}

void RadientSceneDrawableCache::ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh)
{
    auto record_it = m_Renderables.find(Mesh.Entity);

    const bool IsNewRecord = record_it == m_Renderables.end();
    if (IsNewRecord)
    {
        record_it = m_Renderables.emplace(Mesh.Entity, RenderableRecord{}).first;
    }
    RenderableRecord& Record      = record_it->second;
    const bool        MeshChanged = !IsNewRecord && !IsSameMeshAsset(Record.Mesh, Mesh.Mesh);

    if (IsNewRecord || MeshChanged)
    {
        RemoveRenderableDrawables(Record);

        Record.Mesh              = Mesh.Mesh;
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
    std::unordered_map<RadientEntityID, RenderableRecord>::iterator It = m_Renderables.find(Entity);
    if (It == m_Renderables.end())
        return;

    RemoveRenderableDrawables(It->second);
    m_Renderables.erase(It);
}

void RadientSceneDrawableCache::ResolvePendingRenderableMeshes()
{
    m_PendingRenderableEntitiesScratch.clear();
    m_PendingRenderableEntitiesScratch.swap(m_PendingRenderableEntities);

    for (const RadientEntityID Entity : m_PendingRenderableEntitiesScratch)
    {
        std::unordered_map<RadientEntityID, RenderableRecord>::iterator It = m_Renderables.find(Entity);
        if (It == m_Renderables.end())
            continue;

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
    switch (m_MeshProvider.GetDrawableMesh(Record.Mesh.pMesh, Mesh))
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
        Slot.AlphaMode          = static_cast<Uint8>(pMaterial->Attribs.AlphaMode);

        Record.DrawableIDs.push_back(DrawableID);
        AddDrawableToDrawList(DrawableID);
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

    RemoveDrawableFromDrawList(DrawableID);

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    VERIFY(Slot.IsValid(), "Trying to free an invalid drawable slot");

    const Uint32 Generation = Slot.Generation + 1u;
    Slot                    = {};
    Slot.Generation         = Generation;

    RecordDrawableChange(DrawableID, RadientDrawableChangeType::Removed);
    m_FreeDrawableIDs.push_back(DrawableID);
}

void RadientSceneDrawableCache::AddDrawableToDrawList(RadientDrawableID DrawableID)
{
    if (DrawableID >= m_DrawableSlots.size())
    {
        UNEXPECTED("Trying to add an invalid drawable ID to a draw list");
        return;
    }

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    VERIFY(Slot.IsValid(), "Trying to add an invalid drawable slot to a draw list");

    if (Slot.IsInDrawList())
    {
        UNEXPECTED("Trying to add a drawable slot that is already in a draw list");
        return;
    }

    Slot.DrawListIndex = m_DrawLists.Add(static_cast<GLTF::Material::ALPHA_MODE>(Slot.AlphaMode), DrawableID);
}

void RadientSceneDrawableCache::RemoveDrawableFromDrawList(RadientDrawableID DrawableID)
{
    if (DrawableID >= m_DrawableSlots.size())
    {
        UNEXPECTED("Trying to remove an invalid drawable ID from a draw list");
        return;
    }

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    if (!Slot.IsInDrawList())
    {
        UNEXPECTED("Trying to remove a drawable slot that is not in a draw list");
        return;
    }

    const RadientDrawableID MovedDrawableID = m_DrawLists.RemoveAt(static_cast<GLTF::Material::ALPHA_MODE>(Slot.AlphaMode), Slot.DrawListIndex);
    if (MovedDrawableID != InvalidRadientDrawableID && MovedDrawableID != DrawableID)
    {
        VERIFY(MovedDrawableID < m_DrawableSlots.size(), "Draw list returned invalid moved drawable ID");
        RadientDrawableSlot& MovedSlot = m_DrawableSlots[MovedDrawableID];
        VERIFY(MovedSlot.IsInDrawList() && MovedSlot.AlphaMode == Slot.AlphaMode,
               "Moved drawable slot does not match the draw list it was moved inside");
        MovedSlot.DrawListIndex = Slot.DrawListIndex;
    }

    Slot.DrawListIndex = RadientDrawableSlot::InvalidDrawListIndex;
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

const RadientDrawableSlot* RadientSceneDrawableCache::GetDrawableSlot(RadientDrawableID DrawableID) const
{
    if (DrawableID >= m_DrawableSlots.size())
        return nullptr;

    const RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    return Slot.IsValid() ? &Slot : nullptr;
}

} // namespace Diligent
