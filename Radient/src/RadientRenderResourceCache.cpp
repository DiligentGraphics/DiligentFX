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

#include <exception>

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

    const Char* pSourceURI = nullptr;
    const RADIENT_STATUS SourceStatus = m_pAssetManager->GetGLTFSourceURI(Model, pSourceURI);
    if (RADIENT_FAILED(SourceStatus))
        return SourceStatus;

    const std::string CacheKey = MakeAssetCacheKey(Model);
    GLTFResource& Resource = m_GLTFResources[CacheKey];
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

const GLTF::Model* RadientRenderResourceCache::GetGLTFModel(const RadientAssetReference& Model) const
{
    if (Model.URI == nullptr || *Model.URI == 0)
        return nullptr;

    const std::string CacheKey = MakeAssetCacheKey(Model);
    std::unordered_map<std::string, GLTFResource>::const_iterator ResourceIt = m_GLTFResources.find(CacheKey);
    return ResourceIt != m_GLTFResources.end() ? ResourceIt->second.pModel.get() : nullptr;
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

RADIENT_STATUS RadientRenderResourceCache::PrepareGLTFResource(GLTFResource&  Resource,
                                                               IRenderDevice* pDevice,
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
