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

#include "RadientRenderPipeline.hpp"

namespace Diligent
{

namespace
{

GLTF::Material::ALPHA_MODE GetMaterialAlphaMode(const GLTF::Material& Material)
{
    switch (Material.Attribs.AlphaMode)
    {
        case GLTF::Material::ALPHA_MODE_OPAQUE:
        case GLTF::Material::ALPHA_MODE_MASK:
        case GLTF::Material::ALPHA_MODE_BLEND:
            return static_cast<GLTF::Material::ALPHA_MODE>(Material.Attribs.AlphaMode);

        default:
            return GLTF::Material::ALPHA_MODE_OPAQUE;
    }
}

} // namespace

RadientRenderPipeline::RadientRenderPipeline(IRadientBackend*         pBackend,
                                             RadientAssetManagerImpl* pAssetManager) :
    m_pBackend{pBackend},
    m_ResourceCache{
        pAssetManager,
        pBackend != nullptr ? pBackend->GetNativeDevice() : nullptr}
{
}

RadientRenderPipeline::~RadientRenderPipeline()
{
}

void RadientRenderPipeline::PrepareDrawList(IRenderDevice*  pDevice,
                                            IDeviceContext* pContext)
{
    m_DrawLists.Clear();

    const RadientRenderableMeshList& RenderableMeshes = m_SceneDataCache.GetRenderableMeshes();
    if (RenderableMeshes.IsEmpty())
        return;

    for (const RadientRenderableMeshItem& MeshItem : RenderableMeshes.GetItems())
    {
        const RadientRenderMesh* pMesh = m_ResourceCache.ResolveMesh(MeshItem.Mesh.Mesh, pDevice, pContext);
        if (pMesh == nullptr)
            continue;

        for (const RadientRenderMeshPrimitive& Primitive : pMesh->Primitives)
        {
            if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
                continue;

            if (Primitive.MaterialId >= pMesh->Materials.size())
                continue;

            const GLTF::Material* pMaterial = pMesh->Materials[Primitive.MaterialId];
            if (pMaterial == nullptr)
                continue;

            m_DrawLists.Add(GetMaterialAlphaMode(*pMaterial),
                            MeshItem.Entity,
                            MeshItem.Renderer,
                            MeshItem.WorldMatrix,
                            *pMesh,
                            Primitive,
                            *pMaterial);
        }
    }
}

RADIENT_STATUS RadientRenderPipeline::Render(const RadientRenderAttribs& Attribs)
{
    if (m_pBackend == nullptr ||
        Attribs.pScene == nullptr ||
        Attribs.pRenderTarget == nullptr)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RADIENT_STATUS SyncStatus = m_SceneDataCache.SyncScene(*Attribs.pScene);
    if (RADIENT_FAILED(SyncStatus))
        return SyncStatus;

    IRenderDevice*  pDevice  = m_pBackend->GetNativeDevice();
    IDeviceContext* pContext = Attribs.pDeviceContext != nullptr ?
        Attribs.pDeviceContext :
        m_pBackend->GetNativeImmediateContext();

    const RADIENT_STATUS TargetStatus = m_FrameTargets.Prepare(pDevice, *Attribs.pRenderTarget);
    if (RADIENT_FAILED(TargetStatus))
        return TargetStatus;

    // Remote execution and headless local tests use the same public renderer object.
    // The concrete command serialization/GPU execution will be plugged in behind this pipeline.
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    RADIENT_STATUS Status = m_ResourceCache.Prepare(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_GeometryRenderer.Prepare(pDevice, pContext);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_ForwardPass.Prepare(m_GeometryRenderer, pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    Status = m_PostProcessPipeline.Prepare(pDevice, pContext, m_FrameTargets);
    if (RADIENT_FAILED(Status))
        return Status;

    PrepareDrawList(pDevice, pContext);

    if (!m_DrawLists.IsEmpty())
    {
        Status = m_GeometryRenderer.BeginFrame(pDevice,
                                               pContext,
                                               m_SceneDataCache.GetLightList(),
                                               m_ResourceCache.GetResourceManager(),
                                               Attribs,
                                               m_FrameTargets);
        if (RADIENT_FAILED(Status))
            return Status;

        const std::array<GLTF::Material::ALPHA_MODE, 3> AlphaModes =
            {
                GLTF::Material::ALPHA_MODE_OPAQUE,
                GLTF::Material::ALPHA_MODE_MASK,
                GLTF::Material::ALPHA_MODE_BLEND,
            };

        for (const GLTF::Material::ALPHA_MODE AlphaMode : AlphaModes)
        {
            Status = m_ForwardPass.Execute(m_GeometryRenderer,
                                           pDevice,
                                           pContext,
                                           m_DrawLists.GetDrawList(AlphaMode),
                                           m_FrameTargets);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        m_GeometryRenderer.EndFrame();
    }

    return m_PostProcessPipeline.Execute(pContext, m_FrameTargets);
}

} // namespace Diligent
