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

#include "RadientSceneRenderCache.hpp"

#include "RadientSceneImpl.hpp"
#include "RadientSceneState.hpp"

#include "Cast.hpp"

namespace Diligent
{

RADIENT_STATUS RadientSceneRenderCache::SyncScene(IRadientScene& Scene)
{
    const RadientRevision SceneRevision = Scene.GetRevision();
    if (m_SceneRevision == SceneRevision)
        return RADIENT_STATUS_NO_CHANGE;

    m_DrawList.Clear();

    RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(&Scene);
    if (pSceneImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS Status = pSceneImpl->GetState().EnumerateRenderableMeshes(
        [this](const RadientSceneState::RenderableMesh& Mesh) {
            if (!Mesh.EffectiveVisible)
                return;

            RadientDrawItem Item{};
            Item.Entity         = Mesh.Entity;
            Item.Mesh           = Mesh.Mesh.Mesh;
            Item.WorldMatrix    = Mesh.WorldMatrix;
            Item.VisibilityMask = Mesh.Renderer.VisibilityMask;
            m_DrawList.Add(Item);
        });
    if (RADIENT_FAILED(Status))
        return Status;

    m_SceneRevision = SceneRevision;

    return Status;
}

const RadientDrawList& RadientSceneRenderCache::GetDrawList() const
{
    return m_DrawList;
}

RadientRevision RadientSceneRenderCache::GetSceneRevision() const
{
    return m_SceneRevision;
}

} // namespace Diligent
