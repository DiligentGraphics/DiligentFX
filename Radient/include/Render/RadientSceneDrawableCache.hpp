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

#include "Render/RadientDrawList.hpp"
#include "Render/RadientLightList.hpp"
#include "RadientScene.h"
#include "Scene/RadientSceneState.hpp"

#include "GLTFLoader.hpp"
#include "PBR_Renderer.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

/// Per-frame drawable state owned by the scene and referenced by stable drawable slots.
struct RadientDrawableFrameData
{
    const RadientMatrix4x4* pWorldMatrix      = nullptr;
    const Bool*             pEffectiveVisible = nullptr;
};

struct RadientDrawablePrimitive
{
    Uint32 FirstIndex  = 0;
    Uint32 IndexCount  = 0;
    Uint32 FirstVertex = 0;
    Uint32 VertexCount = 0;
    Uint32 MaterialId  = 0;

    bool HasIndices() const
    {
        return IndexCount > 0;
    }
};

/// Renderer-facing primitive data addressed by a stable drawable ID.
struct RadientDrawableSlot
{
    bool   Alive      = false;
    bool   InDrawList = false;
    Uint32 Generation = 0;

    RadientEntityID Entity = InvalidRadientEntityID;

    const RadientMeshRendererComponent* pRenderer = nullptr;
    RadientDrawableFrameData            FrameData;

    RadientDrawablePrimitive   Primitive;
    GLTF::Material::ALPHA_MODE AlphaMode = GLTF::Material::ALPHA_MODE_OPAQUE;

    const GLTF::Material* pMaterial = nullptr;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 FirstIndexLocation = 0;
    Uint32 BaseVertex         = 0;

    size_t DrawListIndex = ~size_t{0};
};

enum class RadientDrawableChangeType
{
    Added,
    Removed,
    Updated
};

struct RadientDrawableChange
{
    RadientDrawableID         DrawableID = InvalidRadientDrawableID;
    RadientDrawableChangeType Type       = RadientDrawableChangeType::Updated;
};

/// Converts Radient scene state into renderer-facing render data.
class RadientSceneDrawableCache
{
public:
    RADIENT_STATUS SyncScene(const IRadientScene& Scene);

    const RadientDrawLists& GetDrawLists() const
    {
        return m_DrawLists;
    }

    const RadientDrawList& GetDrawList(GLTF::Material::ALPHA_MODE AlphaMode) const
    {
        return m_DrawLists.GetDrawList(AlphaMode);
    }

    const std::vector<RadientDrawableChange>& GetDrawableChanges() const
    {
        return m_DrawableChanges;
    }

    const RadientLightList& GetLightList() const
    {
        return m_LightList;
    }

    const RadientSceneRevisions& GetSceneRevisions() const
    {
        return m_SceneRevisions;
    }

    const RadientDrawableSlot* GetDrawableSlot(RadientDrawableID DrawableID) const;

private:
    struct RenderableRecord
    {
        RadientEntityID Entity = InvalidRadientEntityID;

        RadientMeshComponent Mesh;

        const RadientMeshRendererComponent* pRenderer         = nullptr;
        const RadientMatrix4x4*             pWorldMatrix      = nullptr;
        const Bool*                         pEffectiveVisible = nullptr;

        bool PendingResolution = false;

        std::vector<RadientDrawableID> DrawableIDs;
    };

    void ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh);
    void ProcessRenderableMeshRemoved(RadientEntityID Entity);
    void ResolvePendingRenderableMeshes();

    bool TryExpandRenderable(RenderableRecord& Record);

    RadientDrawableID AllocateDrawableID();

    void FreeDrawableID(RadientDrawableID DrawableID);
    void AddDrawableToDrawList(RadientDrawableID DrawableID);
    void RemoveDrawableFromDrawList(RadientDrawableID DrawableID);
    void RemoveRenderableDrawables(RenderableRecord& Record);
    void AddPendingResolution(RenderableRecord& Record);
    void RecordDrawableChange(RadientDrawableID DrawableID, RadientDrawableChangeType Type);

private:
    std::unordered_map<RadientEntityID, RenderableRecord> m_Renderables;

    // Geometry passes cache pointers to drawable slots; deque keeps existing slot
    // addresses stable when new drawable IDs append more slots.
    std::deque<RadientDrawableSlot>    m_DrawableSlots;
    std::vector<RadientDrawableID>     m_FreeDrawableIDs;
    std::vector<RadientEntityID>       m_PendingRenderableEntities;
    std::vector<RadientDrawableChange> m_DrawableChanges;

    RadientDrawLists      m_DrawLists;
    RadientLightList      m_LightList;
    RadientSceneRevisions m_SceneRevisions;
};

} // namespace Diligent
