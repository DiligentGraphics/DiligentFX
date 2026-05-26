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
    m_LightList.Clear();

    RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(&Scene);
    if (pSceneImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS Status = pSceneImpl->GetState().EnumerateRenderableMeshes(
        [this](const RadientSceneState::RenderableMesh& Mesh) {
            if (!Mesh.EffectiveVisible)
                return;

            m_DrawList.Add(Mesh.Entity,
                           Mesh.Mesh,
                           Mesh.Renderer,
                           Mesh.pMaterialBindings,
                           Mesh.WorldMatrix);
        });
    if (RADIENT_FAILED(Status))
        return Status;

    const RADIENT_STATUS LightStatus = pSceneImpl->GetState().EnumerateRenderableLights(
        [this](const RadientSceneState::RenderableLight& Light) {
            if (!Light.EffectiveVisible)
                return;

            m_LightList.Add(Light.Entity, Light.Light, Light.WorldMatrix);
        });
    if (RADIENT_FAILED(LightStatus))
        return LightStatus;

    m_SceneRevision = SceneRevision;

    if (Status == RADIENT_STATUS_OUT_OF_DATE || LightStatus == RADIENT_STATUS_OUT_OF_DATE)
        return RADIENT_STATUS_OUT_OF_DATE;

    return Status;
}

const RadientDrawList& RadientSceneRenderCache::GetDrawList() const
{
    return m_DrawList;
}

const RadientLightList& RadientSceneRenderCache::GetLightList() const
{
    return m_LightList;
}

RadientRevision RadientSceneRenderCache::GetSceneRevision() const
{
    return m_SceneRevision;
}

} // namespace Diligent
