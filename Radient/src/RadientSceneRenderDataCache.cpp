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

#include "RadientSceneRenderDataCache.hpp"

#include "RadientSceneImpl.hpp"

#include "Cast.hpp"
#include "DebugUtilities.hpp"

#include <algorithm>

namespace Diligent
{

namespace
{

bool IsSameMeshAsset(const RadientMeshComponent& Mesh0,
                     const RadientMeshComponent& Mesh1)
{
    return Mesh0.Mesh == Mesh1.Mesh;
}

} // namespace

RADIENT_STATUS RadientSceneRenderDataCache::SyncScene(IRadientScene&              Scene,
                                                      RadientRenderResourceCache& ResourceCache,
                                                      IRenderDevice*              pDevice,
                                                      IDeviceContext*             pContext)
{
    const RadientSceneRevisions& SceneRevisions = Scene.GetSceneRevisions();
    if (m_SceneRevisions == SceneRevisions && m_PendingRenderableEntities.empty())
        return RADIENT_STATUS_NO_CHANGE;

    const bool UpdateRenderables =
        m_SceneRevisions.Drawables != SceneRevisions.Drawables;
    const bool UpdateLightList =
        m_SceneRevisions.Lights != SceneRevisions.Lights ||
        m_SceneRevisions.Visibility != SceneRevisions.Visibility;

    RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(&Scene);
    if (pSceneImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientSceneState& State = pSceneImpl->GetState();

    RADIENT_STATUS Status = RADIENT_STATUS_NO_CHANGE;
    if (UpdateRenderables)
    {
        Status = State.EnumerateRenderableMeshChanges(
            [this, &ResourceCache, pDevice, pContext](const RadientSceneState::RenderableMeshChange& Change,
                                                      const RadientSceneState::RenderableMesh*       pMesh) {
                if (pMesh == nullptr)
                {
                    ProcessRenderableMeshRemoved(Change.Entity);
                    return;
                }

                ProcessRenderableMeshAddedOrUpdated(*pMesh, ResourceCache, pDevice, pContext);
            });
        if (RADIENT_FAILED(Status))
            return Status;

        State.ClearRenderableMeshChanges();
    }

    ResolvePendingRenderableMeshes(ResourceCache, pDevice, pContext);

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

void RadientSceneRenderDataCache::ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh,
                                                                      RadientRenderResourceCache&              ResourceCache,
                                                                      IRenderDevice*                           pDevice,
                                                                      IDeviceContext*                          pContext)
{
    RenderableRecord& Record = m_Renderables[Mesh.Entity];

    const bool IsNewRecord = Record.Entity == InvalidRadientEntityID;
    const bool MeshChanged = !IsNewRecord && !IsSameMeshAsset(Record.Mesh, Mesh.Mesh);

    if (IsNewRecord)
        Record.Entity = Mesh.Entity;

    if (IsNewRecord || MeshChanged)
    {
        RemoveRenderableDrawables(Record);
        Record.MeshURI       = Mesh.Mesh.Mesh.URI != nullptr ? Mesh.Mesh.Mesh.URI : "";
        Record.Mesh          = Mesh.Mesh;
        Record.Mesh.Mesh.URI = Record.MeshURI.empty() ? nullptr : Record.MeshURI.c_str();
    }

    Record.pRenderer         = &Mesh.Renderer;
    Record.pWorldMatrix      = &Mesh.WorldMatrix;
    Record.pEffectiveVisible = &Mesh.EffectiveVisible;

    if (Record.DrawableIDs.empty())
    {
        TryExpandRenderable(Record, ResourceCache, pDevice, pContext);
        return;
    }

    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
    {
        VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID in renderable record");
        RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
        VERIFY(Slot.Alive, "Renderable record references a dead drawable slot");

        Slot.pRenderer = Record.pRenderer;
        Slot.FrameData = {Record.pWorldMatrix, Record.pEffectiveVisible};
    }
}

void RadientSceneRenderDataCache::ProcessRenderableMeshRemoved(RadientEntityID Entity)
{
    std::unordered_map<RadientEntityID, RenderableRecord>::iterator It = m_Renderables.find(Entity);
    if (It == m_Renderables.end())
        return;

    RemoveRenderableDrawables(It->second);
    m_Renderables.erase(It);
}

void RadientSceneRenderDataCache::ResolvePendingRenderableMeshes(RadientRenderResourceCache& ResourceCache,
                                                                 IRenderDevice*              pDevice,
                                                                 IDeviceContext*             pContext)
{
    std::vector<RadientEntityID> Pending;
    Pending.swap(m_PendingRenderableEntities);

    for (const RadientEntityID Entity : Pending)
    {
        std::unordered_map<RadientEntityID, RenderableRecord>::iterator It = m_Renderables.find(Entity);
        if (It == m_Renderables.end())
            continue;

        RenderableRecord& Record = It->second;
        if (!Record.PendingResolution || !Record.DrawableIDs.empty())
            continue;

        Record.PendingResolution = false;
        TryExpandRenderable(Record, ResourceCache, pDevice, pContext);
    }
}

bool RadientSceneRenderDataCache::TryExpandRenderable(RenderableRecord&           Record,
                                                      RadientRenderResourceCache& ResourceCache,
                                                      IRenderDevice*              pDevice,
                                                      IDeviceContext*             pContext)
{
    const RadientRenderMesh* pMesh = ResourceCache.ResolveMesh(Record.Mesh.Mesh, pDevice, pContext);
    if (pMesh == nullptr)
    {
        AddPendingResolution(Record);
        return false;
    }

    Record.PendingResolution = false;
    RemoveRenderableDrawables(Record);

    Record.DrawableIDs.reserve(pMesh->Primitives.size());
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < pMesh->Primitives.size(); ++PrimitiveIndex)
    {
        const RadientRenderMeshPrimitive& Primitive = pMesh->Primitives[PrimitiveIndex];
        if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
            continue;

        if (Primitive.MaterialId >= pMesh->Materials.size())
            continue;

        const GLTF::Material* pMaterial = pMesh->Materials[Primitive.MaterialId];
        if (pMaterial == nullptr)
            continue;

        const RadientDrawableID DrawableID = AllocateDrawableID();
        RadientDrawableSlot&    Slot       = m_DrawableSlots[DrawableID];

        Slot.Entity         = Record.Entity;
        Slot.PrimitiveIndex = PrimitiveIndex;
        Slot.pRenderer      = Record.pRenderer;
        Slot.FrameData      = {Record.pWorldMatrix, Record.pEffectiveVisible};
        Slot.pMesh          = pMesh;
        Slot.pPrimitive     = &Primitive;
        Slot.pMaterial      = pMaterial;
        Slot.AlphaMode      = static_cast<GLTF::Material::ALPHA_MODE>(pMaterial->Attribs.AlphaMode);

        Record.DrawableIDs.push_back(DrawableID);
        AddDrawableToDrawList(DrawableID);
    }

    return true;
}

RadientDrawableID RadientSceneRenderDataCache::AllocateDrawableID()
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
    Slot.Alive                      = true;
    Slot.Generation                 = Generation;

    return DrawableID;
}

void RadientSceneRenderDataCache::FreeDrawableID(RadientDrawableID DrawableID)
{
    VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID");
    if (DrawableID >= m_DrawableSlots.size())
        return;

    RemoveDrawableFromDrawList(DrawableID);

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    VERIFY(Slot.Alive, "Trying to free a dead drawable slot");

    const Uint32 Generation = Slot.Generation + 1u;
    Slot                    = {};
    Slot.Generation         = Generation;

    m_FreeDrawableIDs.push_back(DrawableID);
}

void RadientSceneRenderDataCache::AddDrawableToDrawList(RadientDrawableID DrawableID)
{
    VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID");
    if (DrawableID >= m_DrawableSlots.size())
        return;

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    VERIFY(Slot.Alive, "Trying to add a dead drawable slot to a draw list");

    if (Slot.InDrawList)
        return;

    Slot.DrawListIndex = m_DrawLists.Add(Slot.AlphaMode, DrawableID);
    Slot.InDrawList    = true;
}

void RadientSceneRenderDataCache::RemoveDrawableFromDrawList(RadientDrawableID DrawableID)
{
    VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID");
    if (DrawableID >= m_DrawableSlots.size())
        return;

    RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    if (!Slot.InDrawList)
        return;

    const RadientDrawableID MovedDrawableID = m_DrawLists.RemoveAt(Slot.AlphaMode, Slot.DrawListIndex);
    if (MovedDrawableID != InvalidRadientDrawableID && MovedDrawableID != DrawableID)
    {
        VERIFY(MovedDrawableID < m_DrawableSlots.size(), "Draw list returned invalid moved drawable ID");
        RadientDrawableSlot& MovedSlot = m_DrawableSlots[MovedDrawableID];
        VERIFY(MovedSlot.InDrawList && MovedSlot.AlphaMode == Slot.AlphaMode,
               "Moved drawable slot does not match the draw list it was moved inside");
        MovedSlot.DrawListIndex = Slot.DrawListIndex;
    }

    Slot.InDrawList    = false;
    Slot.DrawListIndex = ~size_t{0};
}

void RadientSceneRenderDataCache::RemoveRenderableDrawables(RenderableRecord& Record)
{
    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        FreeDrawableID(DrawableID);
    Record.DrawableIDs.clear();
}

void RadientSceneRenderDataCache::AddPendingResolution(RenderableRecord& Record)
{
    if (Record.PendingResolution)
        return;

    Record.PendingResolution = true;
    m_PendingRenderableEntities.push_back(Record.Entity);
}

const RadientDrawLists& RadientSceneRenderDataCache::GetDrawLists() const
{
    return m_DrawLists;
}

const RadientDrawList& RadientSceneRenderDataCache::GetDrawList(GLTF::Material::ALPHA_MODE AlphaMode) const
{
    return m_DrawLists.GetDrawList(AlphaMode);
}

const RadientDrawableSlot* RadientSceneRenderDataCache::GetDrawableSlot(RadientDrawableID DrawableID) const
{
    if (DrawableID >= m_DrawableSlots.size())
        return nullptr;

    const RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    return Slot.Alive ? &Slot : nullptr;
}

const RadientLightList& RadientSceneRenderDataCache::GetLightList() const
{
    return m_LightList;
}

const RadientSceneRevisions& RadientSceneRenderDataCache::GetSceneRevisions() const
{
    return m_SceneRevisions;
}

} // namespace Diligent
