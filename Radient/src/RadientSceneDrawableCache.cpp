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

#include "RadientSceneDrawableCache.hpp"

#include "RadientAssetManagerImpl.hpp"
#include "RadientSceneImpl.hpp"

#include "Cast.hpp"
#include "DebugUtilities.hpp"
#include "GLTFLoader.hpp"

#include <algorithm>
#include <cstring>

namespace Diligent
{

namespace
{

bool IsSameMeshAsset(const RadientMeshComponent& Mesh0,
                     const RadientMeshComponent& Mesh1)
{
    return Mesh0.pMesh == Mesh1.pMesh;
}

PBR_Renderer::PSO_FLAGS GetVertexAttribFlags(const GLTF::Model& Model)
{
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_NONE;
    for (Uint32 AttribIndex = 0; AttribIndex < Model.GetNumVertexAttributes(); ++AttribIndex)
    {
        if (!Model.IsVertexAttributeEnabled(AttribIndex))
            continue;

        const GLTF::VertexAttributeDesc& Attrib = Model.GetVertexAttribute(AttribIndex);
        if (std::strcmp(Attrib.Name, GLTF::NormalAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord0AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord1AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
        else if (std::strcmp(Attrib.Name, GLTF::JointsAttributeName) == 0)
        {
            // Radient skinning is not wired yet; keep the pass on the rigid path.
        }
        else if (std::strcmp(Attrib.Name, GLTF::VertexColorAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
        else if (std::strcmp(Attrib.Name, GLTF::TangentAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    }

    return Flags;
}

RadientDrawablePrimitive ConvertPrimitive(const GLTF::Primitive& Primitive)
{
    RadientDrawablePrimitive Result;
    Result.FirstIndex  = Primitive.FirstIndex;
    Result.IndexCount  = Primitive.IndexCount;
    Result.FirstVertex = Primitive.FirstVertex;
    Result.VertexCount = Primitive.VertexCount;
    Result.MaterialId  = Primitive.MaterialId;
    return Result;
}

enum class MeshResolveStatus
{
    Ready,
    Pending,
    Failed
};

struct ResolvedMesh
{
    const GLTF::Model* pModel = nullptr;
    const GLTF::Mesh*  pMesh  = nullptr;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE;
    Uint32                  FirstIndexLocation = 0;
    Uint32                  BaseVertex         = 0;
};

MeshResolveStatus ResolveMesh(RadientAssetManagerImpl* pAssetManager,
                              IRadientMeshAsset*       pMeshAsset,
                              ResolvedMesh&            Mesh)
{
    Mesh = {};

    if (pAssetManager == nullptr || pMeshAsset == nullptr)
        return MeshResolveStatus::Failed;

    RefCntAutoPtr<IRadientSceneAsset> pSourceModel;
    Uint32                            SourceMeshIndex = ~0u;
    const RADIENT_STATUS              SourceStatus    = pAssetManager->GetMeshGLTFSource(pMeshAsset, &pSourceModel, SourceMeshIndex);
    if (RADIENT_FAILED(SourceStatus))
        return MeshResolveStatus::Failed;

    const RADIENT_STATUS LoadStatus = pAssetManager->GetGLTFLoadStatus(pSourceModel);
    if (RADIENT_FAILED(LoadStatus))
        return MeshResolveStatus::Failed;

    if (LoadStatus != RADIENT_STATUS_OK)
        return MeshResolveStatus::Pending;

    const GLTF::Model* pModel = pAssetManager->GetGLTFModel(pSourceModel, true);
    if (pModel == nullptr)
        return MeshResolveStatus::Pending;

    if (SourceMeshIndex >= pModel->Meshes.size())
        return MeshResolveStatus::Failed;

    Mesh.pModel             = pModel;
    Mesh.pMesh              = &pModel->Meshes[SourceMeshIndex];
    Mesh.VertexAttribFlags  = GetVertexAttribFlags(*pModel);
    Mesh.FirstIndexLocation = pModel->GetFirstIndexLocation();
    Mesh.BaseVertex         = pModel->GetBaseVertex();

    return MeshResolveStatus::Ready;
}

} // namespace

RADIENT_STATUS RadientSceneDrawableCache::SyncScene(IRadientScene&            Scene,
                                                    RadientAssetManagerImpl* pAssetManager)
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

    RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(&Scene);
    if (pSceneImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientSceneState& State = pSceneImpl->GetState();

    RADIENT_STATUS Status = RADIENT_STATUS_NO_CHANGE;
    if (UpdateRenderables)
    {
        Status = State.EnumerateRenderableMeshChanges(
            [this, pAssetManager](const RadientSceneState::RenderableMeshChange& Change,
                                  const RadientSceneState::RenderableMesh*       pMesh) {
                if (pMesh == nullptr)
                {
                    ProcessRenderableMeshRemoved(Change.Entity);
                    return;
                }

                ProcessRenderableMeshAddedOrUpdated(*pMesh, pAssetManager);
            });
        if (RADIENT_FAILED(Status))
            return Status;

        State.ClearRenderableMeshChanges();
    }

    ResolvePendingRenderableMeshes(pAssetManager);

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

void RadientSceneDrawableCache::ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh,
                                                                    RadientAssetManagerImpl*                 pAssetManager)
{
    RenderableRecord& Record = m_Renderables[Mesh.Entity];

    const bool IsNewRecord = Record.Entity == InvalidRadientEntityID;
    const bool MeshChanged = !IsNewRecord && !IsSameMeshAsset(Record.Mesh, Mesh.Mesh);

    if (IsNewRecord)
        Record.Entity = Mesh.Entity;

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
        TryExpandRenderable(Record, pAssetManager);
        return;
    }

    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
    {
        VERIFY(DrawableID < m_DrawableSlots.size(), "Invalid drawable ID in renderable record");
        RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
        VERIFY(Slot.Alive, "Renderable record references a dead drawable slot");

        Slot.pRenderer = Record.pRenderer;
        Slot.FrameData = {Record.pWorldMatrix, Record.pEffectiveVisible};
        RecordDrawableChange(DrawableID, RadientDrawableChangeType::Updated);
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

void RadientSceneDrawableCache::ResolvePendingRenderableMeshes(RadientAssetManagerImpl* pAssetManager)
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
        TryExpandRenderable(Record, pAssetManager);
    }
}

bool RadientSceneDrawableCache::TryExpandRenderable(RenderableRecord&   Record,
                                                    RadientAssetManagerImpl* pAssetManager)
{
    ResolvedMesh Mesh;
    switch (ResolveMesh(pAssetManager, Record.Mesh.pMesh, Mesh))
    {
        case MeshResolveStatus::Ready:
            break;

        case MeshResolveStatus::Pending:
            AddPendingResolution(Record);
            return false;

        case MeshResolveStatus::Failed:
            return false;
    }

    Record.PendingResolution = false;
    RemoveRenderableDrawables(Record);

    Record.DrawableIDs.reserve(Mesh.pMesh->Primitives.size());
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < Mesh.pMesh->Primitives.size(); ++PrimitiveIndex)
    {
        const RadientDrawablePrimitive Primitive = ConvertPrimitive(Mesh.pMesh->Primitives[PrimitiveIndex]);
        if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
            continue;

        if (Primitive.MaterialId >= Mesh.pModel->Materials.size())
            continue;

        const GLTF::Material* pMaterial = &Mesh.pModel->Materials[Primitive.MaterialId];

        const RadientDrawableID DrawableID = AllocateDrawableID();
        RadientDrawableSlot&    Slot       = m_DrawableSlots[DrawableID];

        Slot.Entity             = Record.Entity;
        Slot.pRenderer          = Record.pRenderer;
        Slot.FrameData          = {Record.pWorldMatrix, Record.pEffectiveVisible};
        Slot.Primitive          = Primitive;
        Slot.pMaterial          = pMaterial;
        Slot.VertexAttribFlags  = Mesh.VertexAttribFlags;
        Slot.FirstIndexLocation = Mesh.FirstIndexLocation;
        Slot.BaseVertex         = Mesh.BaseVertex;
        Slot.AlphaMode          = static_cast<GLTF::Material::ALPHA_MODE>(pMaterial->Attribs.AlphaMode);

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
    Slot.Alive                      = true;
    Slot.Generation                 = Generation;

    return DrawableID;
}

void RadientSceneDrawableCache::FreeDrawableID(RadientDrawableID DrawableID)
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

    RecordDrawableChange(DrawableID, RadientDrawableChangeType::Removed);
    m_FreeDrawableIDs.push_back(DrawableID);
}

void RadientSceneDrawableCache::AddDrawableToDrawList(RadientDrawableID DrawableID)
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

void RadientSceneDrawableCache::RemoveDrawableFromDrawList(RadientDrawableID DrawableID)
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

void RadientSceneDrawableCache::RemoveRenderableDrawables(RenderableRecord& Record)
{
    for (const RadientDrawableID DrawableID : Record.DrawableIDs)
        FreeDrawableID(DrawableID);
    Record.DrawableIDs.clear();
}

void RadientSceneDrawableCache::AddPendingResolution(RenderableRecord& Record)
{
    if (Record.PendingResolution)
        return;

    Record.PendingResolution = true;
    m_PendingRenderableEntities.push_back(Record.Entity);
}

void RadientSceneDrawableCache::RecordDrawableChange(RadientDrawableID DrawableID, RadientDrawableChangeType Type)
{
    if (DrawableID == InvalidRadientDrawableID)
        return;

    m_DrawableChanges.push_back({DrawableID, Type});
}

const RadientDrawLists& RadientSceneDrawableCache::GetDrawLists() const
{
    return m_DrawLists;
}

const RadientDrawList& RadientSceneDrawableCache::GetDrawList(GLTF::Material::ALPHA_MODE AlphaMode) const
{
    return m_DrawLists.GetDrawList(AlphaMode);
}

const std::vector<RadientDrawableChange>& RadientSceneDrawableCache::GetDrawableChanges() const
{
    return m_DrawableChanges;
}

const RadientDrawableSlot* RadientSceneDrawableCache::GetDrawableSlot(RadientDrawableID DrawableID) const
{
    if (DrawableID >= m_DrawableSlots.size())
        return nullptr;

    const RadientDrawableSlot& Slot = m_DrawableSlots[DrawableID];
    return Slot.Alive ? &Slot : nullptr;
}

const RadientLightList& RadientSceneDrawableCache::GetLightList() const
{
    return m_LightList;
}

const RadientSceneRevisions& RadientSceneDrawableCache::GetSceneRevisions() const
{
    return m_SceneRevisions;
}

} // namespace Diligent
