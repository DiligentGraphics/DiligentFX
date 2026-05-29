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

#include "DebugUtilities.hpp"
#include "GLTFLoader.hpp"

#include <cstring>
#include <exception>
#include <utility>

namespace Diligent
{

namespace
{

constexpr Uint64 RadientDefaultIndexBufferSize       = 16ull * 1024ull * 1024ull;
constexpr Uint64 RadientDefaultMaxIndexBufferSize    = 256ull * 1024ull * 1024ull;
constexpr Uint32 RadientDefaultVertexPoolSize        = 1024u * 1024u;
constexpr Uint32 RadientDefaultTextureAtlasSize      = 4096u;
constexpr Uint32 RadientDefaultTextureAtlasSlices    = 1u;
constexpr Uint32 RadientDefaultTextureAtlasMaxSlices = 2048u;

GLTF::ResourceManager::CreateInfo CreateResourceManagerInfo()
{
    GLTF::ResourceManager::CreateInfo CreateInfo;

    CreateInfo.IndexAllocatorCI.Desc.Name      = "Radient index pool";
    CreateInfo.IndexAllocatorCI.Desc.Size      = RadientDefaultIndexBufferSize;
    CreateInfo.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    CreateInfo.IndexAllocatorCI.ExpansionSize  = static_cast<Uint32>(RadientDefaultIndexBufferSize);
    CreateInfo.IndexAllocatorCI.MaxSize        = RadientDefaultMaxIndexBufferSize;

    CreateInfo.DefaultPoolDesc.Name        = "Radient vertex pool";
    CreateInfo.DefaultPoolDesc.VertexCount = RadientDefaultVertexPoolSize;
    CreateInfo.DefaultPoolDesc.Usage       = USAGE_DEFAULT;
    CreateInfo.DefaultPoolDesc.Mode        = BUFFER_MODE_UNDEFINED;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient texture atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = RadientDefaultTextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = RadientDefaultTextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = RadientDefaultTextureAtlasSlices;
    CreateInfo.DefaultAtlasDesc.Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    CreateInfo.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    CreateInfo.DefaultAtlasDesc.MaxSliceCount  = RadientDefaultTextureAtlasMaxSlices;

    return CreateInfo;
}

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

RADIENT_STATUS RadientRenderResourceCache::Prepare(IRenderDevice*  pDevice,
                                                   IDeviceContext* pContext)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_OK;

    if (m_pDevice != pDevice)
    {
        Reset();
        CreateResources(pDevice, pContext);
    }

    if (m_pUploadManager != nullptr && pContext != nullptr)
        m_pUploadManager->RenderThreadUpdate(pContext);

    if (m_pResourceManager != nullptr && pContext != nullptr)
        m_pResourceManager->UpdateAllResources(pDevice, pContext);

    if (pContext != nullptr)
    {
        for (auto& GLTFResourceIt : m_GLTFResources)
        {
            GLTFResource& Resource = GLTFResourceIt.second;
            PrepareGLTFResource(Resource, pDevice, pContext);
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientRenderResourceCache::EnsureGLTFLoaded(const RadientAssetReference& Model,
                                                            IRenderDevice*               pDevice,
                                                            IDeviceContext*              pContext)
{
    if (Model.URI == nullptr || *Model.URI == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (m_pDevice != pDevice)
    {
        Reset();
        CreateResources(pDevice, pContext);
    }

    if (m_pResourceManager == nullptr || m_pUploadManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const Char*          pSourceURI   = nullptr;
    const RADIENT_STATUS SourceStatus = m_pAssetManager->GetGLTFSourceURI(Model, pSourceURI);
    if (RADIENT_FAILED(SourceStatus))
        return SourceStatus;

    const std::string CacheKey = MakeAssetCacheKey(Model);
    GLTFResource&     Resource = m_GLTFResources[CacheKey];
    if (Resource.pModel == nullptr)
    {
        Resource.SourceURI = pSourceURI;

        GLTF::ModelCreateInfo ModelCI;
        ModelCI.FileName         = Resource.SourceURI.c_str();
        ModelCI.pResourceManager = m_pResourceManager;
        ModelCI.pUploadMgr       = m_pUploadManager;

        try
        {
            Resource.pModel = std::make_unique<GLTF::Model>(pDevice, pContext, ModelCI);
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", Resource.SourceURI, "': ", Error.what());
            m_GLTFResources.erase(CacheKey);
            return RADIENT_STATUS_INVALID_OPERATION;
        }
        catch (...)
        {
            LOG_ERROR_MESSAGE("Failed to load Radient GLTF asset '", Resource.SourceURI, "'");
            m_GLTFResources.erase(CacheKey);
            return RADIENT_STATUS_INVALID_OPERATION;
        }
    }

    return PrepareGLTFResource(Resource, pDevice, pContext);
}

const RadientRenderMesh* RadientRenderResourceCache::ResolveMesh(const RadientAssetReference& Mesh,
                                                                 IRenderDevice*               pDevice,
                                                                 IDeviceContext*              pContext)
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

    const RADIENT_STATUS LoadStatus = EnsureGLTFLoaded(Record.SourceModel, pDevice, pContext);
    if (RADIENT_FAILED(LoadStatus) || LoadStatus != RADIENT_STATUS_OK)
    {
        if (RADIENT_FAILED(LoadStatus))
            Record.State = MeshResource::STATE::Failed;
        return nullptr;
    }

    const std::string                                       ModelCacheKey = MakeAssetCacheKey(Record.SourceModel);
    std::unordered_map<std::string, GLTFResource>::iterator ResourceIt    = m_GLTFResources.find(ModelCacheKey);
    if (ResourceIt == m_GLTFResources.end() || ResourceIt->second.pModel == nullptr)
    {
        Record.State = MeshResource::STATE::Failed;
        return nullptr;
    }

    RadientRenderMesh    RenderMesh;
    const RADIENT_STATUS BuildStatus = BuildMeshResource(*ResourceIt->second.pModel, Record.SourceMeshIndex, RenderMesh);
    if (RADIENT_FAILED(BuildStatus))
    {
        Record.State = MeshResource::STATE::Failed;
        return nullptr;
    }

    Record.Mesh  = std::move(RenderMesh);
    Record.State = MeshResource::STATE::Ready;
    return &Record.Mesh;
}

IGPUUploadManager* RadientRenderResourceCache::GetUploadManager() const
{
    return m_pUploadManager;
}

GLTF::ResourceManager* RadientRenderResourceCache::GetResourceManager() const
{
    return m_pResourceManager;
}

void RadientRenderResourceCache::Reset()
{
    m_MeshResources.clear();
    m_GLTFResources.clear();
    m_pUploadManager.Release();
    m_pResourceManager.Release();
    m_pDevice.Release();
}

void RadientRenderResourceCache::CreateResources(IRenderDevice*  pDevice,
                                                 IDeviceContext* pContext)
{
    m_pDevice = pDevice;

    GPUUploadManagerCreateInfo UploadCI;
    UploadCI.pDevice  = pDevice;
    UploadCI.pContext = pContext;
    CreateGPUUploadManager(UploadCI, &m_pUploadManager);

    const GLTF::ResourceManager::CreateInfo ResourceCI = CreateResourceManagerInfo();
    m_pResourceManager                                 = GLTF::ResourceManager::Create(pDevice, ResourceCI);
}

RADIENT_STATUS RadientRenderResourceCache::PrepareGLTFResource(GLTFResource&   Resource,
                                                               IRenderDevice*  pDevice,
                                                               IDeviceContext* pContext)
{
    if (Resource.pModel == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (pContext == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    return Resource.pModel->PrepareGPUResources(pDevice, pContext) ?
        RADIENT_STATUS_OK :
        RADIENT_STATUS_OUT_OF_DATE;
}

} // namespace Diligent
