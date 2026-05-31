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

#include "RadientRenderResourceCache.hpp"

#include "GLTFLoader.hpp"

#include <cstring>
#include <utility>

namespace Diligent
{

namespace
{

std::string MakeAssetCacheKey(const RadientAssetReference& Asset)
{
    return std::string{Asset.URI} + "#" + std::to_string(Asset.Version);
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

RadientRenderMeshPrimitive ConvertPrimitive(const GLTF::Primitive& Primitive)
{
    RadientRenderMeshPrimitive Result;
    Result.FirstIndex  = Primitive.FirstIndex;
    Result.IndexCount  = Primitive.IndexCount;
    Result.FirstVertex = Primitive.FirstVertex;
    Result.VertexCount = Primitive.VertexCount;
    Result.MaterialId  = Primitive.MaterialId;
    return Result;
}

RADIENT_STATUS BuildMeshResource(const GLTF::Model& GLTFModel,
                                 Uint32             MeshIndex,
                                 RadientRenderMesh& Mesh)
{
    Mesh = {};

    if (MeshIndex >= GLTFModel.Meshes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const GLTF::Mesh& GLTFMesh = GLTFModel.Meshes[MeshIndex];

    Mesh.VertexAttribFlags  = GetVertexAttribFlags(GLTFModel);
    Mesh.FirstIndexLocation = GLTFModel.GetFirstIndexLocation();
    Mesh.BaseVertex         = GLTFModel.GetBaseVertex();

    Mesh.Primitives.reserve(GLTFMesh.Primitives.size());
    for (const GLTF::Primitive& Primitive : GLTFMesh.Primitives)
        Mesh.Primitives.emplace_back(ConvertPrimitive(Primitive));

    Mesh.Materials.reserve(GLTFModel.Materials.size());
    for (const GLTF::Material& Material : GLTFModel.Materials)
        Mesh.Materials.emplace_back(&Material);

    return RADIENT_STATUS_OK;
}

} // namespace

RadientRenderResourceCache::RadientRenderResourceCache(RadientAssetManagerImpl* pAssetManager) :
    m_pAssetManager{pAssetManager}
{
}

RadientRenderResourceCache::~RadientRenderResourceCache()
{
}

const RadientRenderMesh* RadientRenderResourceCache::ResolveMesh(const RadientAssetReference& Mesh)
{
    if (Mesh.URI == nullptr || *Mesh.URI == 0)
        return nullptr;

    const std::string MeshCacheKey = MakeAssetCacheKey(Mesh);
    MeshResource&     Record       = m_MeshResources[MeshCacheKey];

    switch (Record.State)
    {
        case MeshResource::STATE::Ready:
            return &Record.Mesh;

        case MeshResource::STATE::Failed:
            return nullptr;

        case MeshResource::STATE::NotRequested:
        case MeshResource::STATE::Loading:
            break;
    }

    if (m_pAssetManager == nullptr)
        return nullptr;

    if (Record.State == MeshResource::STATE::NotRequested)
    {
        RadientAssetReference SourceModel{};
        Uint32                SourceMeshIndex = ~0u;
        const RADIENT_STATUS  SourceStatus    = m_pAssetManager->GetMeshGLTFSource(Mesh, SourceModel, SourceMeshIndex);
        if (RADIENT_FAILED(SourceStatus))
        {
            Record.State = MeshResource::STATE::Failed;
            return nullptr;
        }

        Record.SourceModelURI  = SourceModel.URI != nullptr ? SourceModel.URI : "";
        Record.SourceModel     = SourceModel;
        Record.SourceModel.URI = Record.SourceModelURI.c_str();
        Record.SourceMeshIndex = SourceMeshIndex;
        Record.State           = MeshResource::STATE::Loading;
    }

    const RADIENT_STATUS LoadStatus = m_pAssetManager->GetGLTFLoadStatus(Record.SourceModel);
    if (RADIENT_FAILED(LoadStatus))
    {
        Record.State = MeshResource::STATE::Failed;
        return nullptr;
    }

    if (LoadStatus != RADIENT_STATUS_OK)
        return nullptr;

    const GLTF::Model* pModel = m_pAssetManager->GetGLTFModel(Record.SourceModel, true);
    if (pModel == nullptr)
        return nullptr;

    RadientRenderMesh    RenderMesh;
    const RADIENT_STATUS BuildStatus = BuildMeshResource(*pModel, Record.SourceMeshIndex, RenderMesh);
    if (RADIENT_FAILED(BuildStatus))
    {
        Record.State = MeshResource::STATE::Failed;
        return nullptr;
    }

    Record.Mesh  = std::move(RenderMesh);
    Record.State = MeshResource::STATE::Ready;
    return &Record.Mesh;
}

} // namespace Diligent
