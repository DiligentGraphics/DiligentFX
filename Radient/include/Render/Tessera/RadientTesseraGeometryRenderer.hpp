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

#include "Render/RadientFrameRenderTargets.hpp"
#include "Render/RadientLightList.hpp"
#include "Render/RadientMaterialSRBTable.hpp"
#include "Render/RadientPBRRenderer.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "RadientView.h"

#include "GLTFLoader.hpp"

#include <memory>

namespace Diligent
{

/// View-independent inputs used to populate Tessera PBR frame attributes.
struct RadientTesseraGeometryFrameAttribs
{
    const RadientEnvironmentDesc& Environment;
    IRadientScene*                pScene                = nullptr;
    RadientEntityID               Camera                = InvalidRadientEntityID;
    ITextureView*                 pPrefilteredEnvMapSRV = nullptr;
};

/// Shared Tessera renderer state used by geometry passes.
class RadientTesseraGeometryRenderer
{
public:
    RadientTesseraGeometryRenderer(Uint32                                MaterialTextureSlotCount,
                                   const RadientMaterialDefaultTextures& DefaultTextures) noexcept;

    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IDeviceContext* pContext);

    RADIENT_STATUS BeginFrame(IRenderDevice*                            pDevice,
                              IDeviceContext*                           pContext,
                              const RadientLightLists&                  LightList,
                              GLTF::ResourceManager*                    pResourceManager,
                              const RadientTesseraGeometryFrameAttribs& FrameAttribs,
                              const RadientFrameRenderTargets&          Targets);

    void EndFrame();

    RadientPBRRenderer*     GetRenderer() const { return m_pRenderer.get(); }
    PBR_Renderer::PSO_FLAGS GetBaseRenderFlags() const { return m_BaseRenderFlags; }

    RADIENT_STATUS AcquireMaterialSRB(
        const RadientMaterialRenderData&               MaterialData,
        PBR_Renderer::PSO_FLAGS                        PSOFlags,
        RadientMaterialSRBLease&                       Lease,
        PBR_Renderer::StaticShaderTextureIdsArrayType& ShaderTextureIds);

private:
    RADIENT_STATUS CreateRenderer(IRenderDevice* pDevice, IDeviceContext* pContext);

    void PrepareDefaultMaterialTextureBindings();

private:
    std::unique_ptr<RadientPBRRenderer> m_pRenderer;

    RadientMaterialSRBTable               m_MaterialSRBTable;
    RadientMaterialDefaultTextures        m_DefaultMaterialTextures;
    RadientMaterialDefaultTextureBindings m_DefaultMaterialTextureBindings;
    Uint32                                m_MaterialTextureSlotCount            = 8;
    bool                                  m_DefaultMaterialTextureBindingsReady = false;

    PBR_Renderer::PSO_FLAGS m_BaseRenderFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 m_FrameIndex = 0;
};

} // namespace Diligent
