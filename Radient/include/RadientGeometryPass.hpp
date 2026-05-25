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

#include "RadientDrawList.hpp"
#include "RadientFrameRenderTargets.hpp"
#include "RadientRenderResourceCache.hpp"

#include "GLTFLoader.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>

namespace Diligent
{

/// Mesh geometry render pass used by shadow and forward rendering stages.
class RadientGeometryPass
{
public:
    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IDeviceContext* pContext, const RadientFrameRenderTargets& Targets);
    RADIENT_STATUS Execute(IRenderDevice*               pDevice,
                           IDeviceContext*              pContext,
                           const RadientDrawList&       DrawList,
                           RadientRenderResourceCache&  ResourceCache,
                           const RadientRenderAttribs&  Attribs,
                           const RadientFrameRenderTargets& Targets);

private:
    RADIENT_STATUS CreateRenderer(IRenderDevice* pDevice,
                                  IDeviceContext* pContext,
                                  TEXTURE_FORMAT RTVFormat,
                                  TEXTURE_FORMAT DSVFormat);
    void InitializeResourceCacheUseInfo();

private:
    std::unique_ptr<GLTF_PBR_Renderer> m_pGLTFRenderer;
    RefCntAutoPtr<IBuffer>             m_pFrameAttribsCB;

    GLTF_PBR_Renderer::RenderInfo            m_RenderInfo;
    GLTF_PBR_Renderer::ResourceCacheUseInfo  m_CacheUseInfo;
    GLTF_PBR_Renderer::ResourceCacheBindings m_CacheBindings;

    GLTF::ModelTransforms m_Transforms;
    GLTF::ModelTransforms m_PrevTransforms;

    TEXTURE_FORMAT m_RTVFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT m_DSVFormat = TEX_FORMAT_UNKNOWN;
    Uint32         m_FrameIndex = 0;
};

} // namespace Diligent
