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

/// Renderer-facing primitive data addressed by a stable drawable ID.
struct RadientDrawableSlot
{
    static constexpr size_t InvalidDrawListIndex = ~size_t{0};

    RadientEntityID Entity = InvalidRadientEntityID;

    Uint32 Generation = 0;
    bool   IsIndexed  = false;
    Uint8  AlphaMode  = GLTF::Material::ALPHA_MODE_OPAQUE;

    const RadientMeshRendererComponent* pRenderer         = nullptr;
    const RadientMatrix4x4*             pWorldMatrix      = nullptr;
    const Bool*                         pEffectiveVisible = nullptr;

    const GLTF::Material* pMaterial = nullptr;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 FirstIndexLocation = 0;
    Uint32 BaseVertex         = 0;
    Uint32 FirstElement       = 0;
    Uint32 ElementCount       = 0;

    size_t DrawListIndex = InvalidDrawListIndex;

    bool IsValid() const
    {
        return Entity != InvalidRadientEntityID;
    }

    bool IsInDrawList() const
    {
        return DrawListIndex != InvalidDrawListIndex;
    }
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

enum class RadientLightChangeType
{
    Added,
    Removed,
    Updated
};

struct RadientLightChange
{
    RadientEntityID        Entity = InvalidRadientEntityID;
    RADIENT_LIGHT_TYPE     Type   = RADIENT_LIGHT_TYPE_DIRECTIONAL;
    RadientLightChangeType Change = RadientLightChangeType::Updated;
};

enum class RadientDrawableMeshStatus
{
    Ready,
    Pending,
    Failed
};

struct RadientDrawableMesh
{
    const GLTF::Model* pModel = nullptr;
    const GLTF::Mesh*  pMesh  = nullptr;

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 FirstIndexLocation = 0;
    Uint32 BaseVertex         = 0;
};

class IRadientDrawableMeshProvider
{
public:
    virtual ~IRadientDrawableMeshProvider() = default;

    virtual RadientDrawableMeshStatus GetDrawableMesh(IRadientMeshAsset*   pMesh,
                                                      RadientDrawableMesh& Mesh) = 0;
};

/// Converts Radient scene state into renderer-facing render data.
class RadientSceneDrawableCache
{
public:
    explicit RadientSceneDrawableCache(IRadientDrawableMeshProvider* pMeshProvider = nullptr);

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

    const RadientLightLists& GetLightList() const
    {
        return m_LightLists;
    }

    const RadientLightList& GetLightList(RADIENT_LIGHT_TYPE Type) const
    {
        return m_LightLists.GetLightList(Type);
    }

    const std::vector<RadientLightChange>& GetLightChanges() const
    {
        return m_LightChanges;
    }

    const RadientSceneRevisions& GetSceneRevisions() const
    {
        return m_SceneRevisions;
    }

    const RadientDrawableSlot* GetDrawableSlot(RadientDrawableID DrawableID) const;

private:
    struct RenderableRecord
    {
        RadientMeshComponent Mesh;

        const RadientMeshRendererComponent* pRenderer         = nullptr;
        const RadientMatrix4x4*             pWorldMatrix      = nullptr;
        const Bool*                         pEffectiveVisible = nullptr;

        bool PendingResolution = false;

        std::vector<RadientDrawableID> DrawableIDs;
    };

    struct LightRecord
    {
        static constexpr size_t InvalidListIndex = ~size_t{0};

        const RadientLightComponent* pLight            = nullptr;
        const RadientMatrix4x4*      pWorldMatrix      = nullptr;
        const Bool*                  pEffectiveVisible = nullptr;

        RADIENT_LIGHT_TYPE Type      = RADIENT_LIGHT_TYPE_DIRECTIONAL;
        size_t             ListIndex = InvalidListIndex;

        bool IsInLightList() const
        {
            return ListIndex != InvalidListIndex;
        }
    };

    void ProcessRenderableMeshAddedOrUpdated(const RadientSceneState::RenderableMesh& Mesh);
    void ProcessRenderableMeshRemoved(RadientEntityID Entity);
    void ResolvePendingRenderableMeshes();

    void ProcessRenderableLightAddedOrUpdated(const RadientSceneState::RenderableLight& Light);
    void ProcessRenderableLightRemoved(RadientEntityID Entity);

    bool TryExpandRenderable(RadientEntityID Entity, RenderableRecord& Record);

    RadientDrawableID AllocateDrawableID();

    void FreeDrawableID(RadientDrawableID DrawableID);
    void AddDrawableToDrawList(RadientDrawableID DrawableID);
    void RemoveDrawableFromDrawList(RadientDrawableID DrawableID);
    void RemoveRenderableDrawables(RenderableRecord& Record);
    void AddPendingResolution(RadientEntityID Entity, RenderableRecord& Record);
    void RecordDrawableChange(RadientDrawableID DrawableID, RadientDrawableChangeType Type);
    void AddLightToList(RadientEntityID Entity, LightRecord& Record);
    void RemoveLightFromList(RadientEntityID Entity, LightRecord& Record);
    void RecordLightChange(RadientEntityID Entity, RADIENT_LIGHT_TYPE Type, RadientLightChangeType Change);

private:
    IRadientDrawableMeshProvider& m_MeshProvider;

    std::unordered_map<RadientEntityID, RenderableRecord> m_Renderables;
    std::unordered_map<RadientEntityID, LightRecord>      m_Lights;

    // Geometry passes cache pointers to drawable slots; deque keeps existing slot
    // addresses stable when new drawable IDs append more slots.
    std::deque<RadientDrawableSlot>    m_DrawableSlots;
    std::vector<RadientDrawableID>     m_FreeDrawableIDs;
    std::vector<RadientEntityID>       m_PendingRenderableEntities;
    std::vector<RadientEntityID>       m_PendingRenderableEntitiesScratch;
    std::vector<RadientDrawableChange> m_DrawableChanges;
    std::vector<RadientLightChange>    m_LightChanges;

    RadientDrawLists      m_DrawLists;
    RadientLightLists     m_LightLists;
    RadientSceneRevisions m_SceneRevisions;
};

} // namespace Diligent
