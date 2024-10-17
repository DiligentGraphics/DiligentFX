/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "HnTextureRegistry.hpp"
#include "HnTextureUtils.hpp"
#include "HnTypeConversions.hpp"
#include "GLTFResourceManager.hpp"
#include "USD_Renderer.hpp"
#include "HnTextureIdentifier.hpp"
#include "GraphicsAccessories.hpp"
#include "ThreadPool.hpp"

namespace Diligent
{

namespace USD
{

HnTextureRegistry::TextureHandle::TextureHandle(Uint32 Id) noexcept :
    m_TextureId{Id}
{}

HnTextureRegistry::TextureHandle::~TextureHandle()
{
}

HnTextureRegistry::HnTextureRegistry(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pThreadPool{CI.pThreadPool},
    m_pResourceManager{CI.pResourceManager},
    m_CompressMode{CI.CompressMode},
    m_LoadBudget{static_cast<Int64>(std::min(CI.LoadBudget, static_cast<Uint64>(INT64_MAX)))}
{
}

HnTextureRegistry::~HnTextureRegistry()
{
    if (m_pThreadPool)
    {
        VERIFY(m_NumTexturesLoading != 0 || m_LoadingTexDataSize == 0,
               "The number of loading textures is zero, but the loading data size is non-zero (", m_LoadingTexDataSize, ")");

        for (RefCntAutoPtr<IAsyncTask>& pTask : m_AsyncTasks)
        {
            pTask->Cancel();
        }
        // Only wait for our own tasks
        for (RefCntAutoPtr<IAsyncTask>& pTask : m_AsyncTasks)
        {
            pTask->WaitForCompletion();
        }
    }
}

void HnTextureRegistry::TextureHandle::Initialize(IRenderDevice*                  pDevice,
                                                  IDeviceContext*                 pContext,
                                                  ITextureLoader*                 pLoader,
                                                  const pxr::HdSamplerParameters& SamplerParams)
{
    VERIFY(!m_IsInitialized.load(), "Texture handle is already initialized");

    if (m_pAtlasSuballocation != nullptr)
    {
        VERIFY_EXPR(pContext != nullptr);

        IDynamicTextureAtlas*       pAtlas      = m_pAtlasSuballocation->GetAtlas();
        ITexture*                   pDstTex     = pAtlas->GetTexture();
        const TextureDesc&          AtlasDesc   = pAtlas->GetAtlasDesc();
        const TextureFormatAttribs& FmtAttribs  = GetTextureFormatAttribs(AtlasDesc.Format);
        const TextureData           UploadData  = pLoader->GetTextureData();
        const TextureDesc&          SrcDataDesc = pLoader->GetTextureDesc();
        const uint2&                Origin      = m_pAtlasSuballocation->GetOrigin();
        const Uint32                Slice       = m_pAtlasSuballocation->GetSlice();

        const Uint32 MipsToUpload = std::min(UploadData.NumSubresources, AtlasDesc.MipLevels);
        for (Uint32 mip = 0; mip < MipsToUpload; ++mip)
        {
            const TextureSubResData& LevelData = UploadData.pSubResources[mip];
            const MipLevelProperties MipProps  = GetMipLevelProperties(SrcDataDesc, mip);

            Box UpdateBox;
            UpdateBox.MinX = AlignDown(Origin.x >> mip, FmtAttribs.BlockWidth);
            UpdateBox.MaxX = AlignUp(UpdateBox.MinX + MipProps.LogicalWidth, FmtAttribs.BlockWidth);
            UpdateBox.MinY = AlignDown(Origin.y >> mip, FmtAttribs.BlockHeight);
            UpdateBox.MaxY = AlignUp(UpdateBox.MinY + MipProps.LogicalHeight, FmtAttribs.BlockHeight);
            pContext->UpdateTexture(pDstTex, mip, Slice, UpdateBox, LevelData, RESOURCE_STATE_TRANSITION_MODE_NONE, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        m_IsInitialized.store(true);
    }
    else
    {
        if (!m_pTexture)
        {
            if (pLoader->GetTextureDesc().Type == RESOURCE_DIM_TEX_2D)
            {
                TextureDesc TexDesc = pLoader->GetTextureDesc();
                // PBR Renderer expects 2D textures to be 2D array textures
                TexDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
                TexDesc.ArraySize = 1;

                TextureData InitData = pLoader->GetTextureData();
                pDevice->CreateTexture(TexDesc, &InitData, &m_pTexture);
            }
            else
            {
                pLoader->CreateTexture(pDevice, &m_pTexture);
            }

            if (m_pTexture)
            {
                const SamplerDesc SamDesc = HdSamplerParametersToSamplerDesc(SamplerParams);

                RefCntAutoPtr<ISampler> pSampler;
                pDevice->CreateSampler(SamDesc, &pSampler);
                VERIFY_EXPR(pSampler);
                m_pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(pSampler);
            }
            else
            {
                UNEXPECTED("Failed to create texture");
            }
        }

        if (pContext != nullptr)
        {
            if (m_pTexture)
            {
                StateTransitionDesc Barrier{m_pTexture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
                pContext->TransitionResourceStates(1, &Barrier);
            }

            // Set the IsInitialized flag even if the texture creation failed
            m_IsInitialized.store(true);
        }
    }
}

void HnTextureRegistry::TextureHandle::SetAtlasSuballocation(ITextureAtlasSuballocation* pSuballocation)
{
    VERIFY(m_pAtlasSuballocation == nullptr, "Atlas suballocation is already set");
    m_pAtlasSuballocation = pSuballocation;
}

void HnTextureRegistry::PendingTextureInfo::InitHandle(IRenderDevice* pDevice, IDeviceContext* pContext)
{
    VERIFY_EXPR(pDevice != nullptr);
    VERIFY_EXPR(pContext != nullptr);
    VERIFY_EXPR(Handle);

    Handle->Initialize(pDevice, pContext, pLoader, SamplerParams);
}

void HnTextureRegistry::Commit(IDeviceContext* pContext)
{
    // We can only process those textures for which the atlas has been updated,
    // so move them to a separate list before calling m_pResourceManager->UpdateTextures().
    {
        std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
        m_WIPPendingTextures.swap(m_PendingTextures);
    }

    if (m_pResourceManager)
    {
        m_pResourceManager->UpdateTextures(m_pDevice, pContext);
    }

    if (!m_WIPPendingTextures.empty())
    {
        for (auto& tex_it : m_WIPPendingTextures)
        {
            tex_it.second.InitHandle(m_pDevice, pContext);
            VERIFY_EXPR(tex_it.second.Handle->IsInitialized());
            if (tex_it.second.pLoader)
            {
                Uint64 TexDataSize = GetStagingTextureDataSize(tex_it.second.pLoader->GetTextureDesc());
                m_LoadingTexDataSize.fetch_add(-static_cast<Int64>(TexDataSize));
                VERIFY_EXPR(m_LoadingTexDataSize.load() >= 0);

                m_DataVersion.fetch_add(1);
            }
            if (tex_it.second.Handle->GetTexture())
            {
                m_StorageVersion.fetch_add(1);
            }
        }

        VERIFY_EXPR(m_NumTexturesLoading.load() >= static_cast<int>(m_WIPPendingTextures.size()));
        m_NumTexturesLoading.fetch_add(-static_cast<int>(m_WIPPendingTextures.size()));

        m_WIPPendingTextures.clear();
    }

    // Remove finished tasks from the list
    {
        std::lock_guard<std::mutex> Lock{m_AsyncTasksMtx};

        auto it = m_AsyncTasks.begin();
        while (it != m_AsyncTasks.end())
        {
            if ((*it)->IsFinished())
            {
                it = m_AsyncTasks.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void HnTextureRegistry::LoadTexture(const pxr::TfToken                             Key,
                                    const pxr::TfToken&                            FilePath,
                                    const pxr::HdSamplerParameters&                SamplerParams,
                                    std::function<RefCntAutoPtr<ITextureLoader>()> CreateLoader,
                                    std::shared_ptr<TextureHandle>                 TexHandle)
{
    RefCntAutoPtr<ITextureLoader> pLoader = CreateLoader();
    if (!pLoader)
    {
        LOG_ERROR_MESSAGE("Failed to create texture loader for texture ", FilePath);

        // Since we don't add the handle to the pending list, we need to decrement the counter here
        m_NumTexturesLoading.fetch_add(-1);

        // Set the m_IsInitialized flag or the handle will be stuck in the loading state
        TexHandle->m_IsInitialized.store(true);

        return;
    }

    const TextureDesc& TexDesc     = pLoader->GetTextureDesc();
    const Uint64       TexDataSize = GetStagingTextureDataSize(TexDesc);
    m_LoadingTexDataSize.fetch_add(static_cast<Int64>(TexDataSize));

    // Try to allocate texture in the atlas first
    RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;
    if (m_pResourceManager != nullptr)
    {
        const TextureDesc& AtlasDesc = m_pResourceManager->GetAtlasDesc(TexDesc.Format);
        if (TexDesc.Width <= AtlasDesc.Width && TexDesc.Height <= AtlasDesc.Height)
        {
            pAtlasSuballocation = m_pResourceManager->AllocateTextureSpace(TexDesc.Format, TexDesc.Width, TexDesc.Height);
            if (!pAtlasSuballocation)
            {
                LOG_ERROR_MESSAGE("Failed to allocate atlas region for texture ", FilePath);
            }
        }
        else
        {
            LOG_WARNING_MESSAGE("Texture ", FilePath, " is too large to fit into atlas (", TexDesc.Width, "x", TexDesc.Height, " vs ", AtlasDesc.Width, "x", AtlasDesc.Height, ")");
        }
    }

    if (pAtlasSuballocation)
    {
        TexHandle->SetAtlasSuballocation(pAtlasSuballocation);
    }
    else
    {
        // If the texture was not allocated in the atlas (because the atlas is not used or the texture is too large),
        // try to create it as a standalone texture.
        if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation)
        {
            TexHandle->Initialize(m_pDevice, nullptr, pLoader, SamplerParams);
        }
    }

    // Finish initialization in the main thread: we either need to upload the texture data to the atlas or
    // create the texture in the main thread if the device does not support multithreaded resource creation
    // and transition it to the shader resource state.
    {
        std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
        m_PendingTextures.emplace(Key, PendingTextureInfo{std::move(pLoader), SamplerParams, std::move(TexHandle)});
    }
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const pxr::TfToken&                            FilePath,
                                                                      const TextureComponentMapping&                 Swizzle,
                                                                      const pxr::HdSamplerParameters&                SamplerParams,
                                                                      bool                                           IsAsync,
                                                                      std::function<RefCntAutoPtr<ITextureLoader>()> CreateLoader)
{
    const pxr::TfToken Key{FilePath.GetString() + '.' + GetTextureComponentMappingString(Swizzle)};
    return m_Cache.Get(
        Key,
        [&]() {
            m_NumTexturesLoading.fetch_add(+1);

            std::shared_ptr<TextureHandle> TexHandle = std::make_shared<TextureHandle>(m_NextTextureId.fetch_add(1));

            if (IsAsync && m_pThreadPool)
            {
                // Load textures with lower priority to let e.g. the shaders compile first
                constexpr float Priority = -1;

                RefCntAutoPtr<IAsyncTask> pTask = EnqueueAsyncWork(
                    m_pThreadPool,
                    [=](Uint32 ThreadId) {
                        if (m_LoadBudget != 0 && m_LoadingTexDataSize.load() > m_LoadBudget)
                        {
                            // Reschedule the task
                            return ASYNC_TASK_STATUS_NOT_STARTED;
                        }

                        LoadTexture(Key, FilePath, SamplerParams, CreateLoader, TexHandle);
                        return ASYNC_TASK_STATUS_COMPLETE;
                    },
                    Priority);

                {
                    std::lock_guard<std::mutex> Lock{m_AsyncTasksMtx};
                    m_AsyncTasks.emplace_back(pTask);
                }
            }
            else
            {
                LoadTexture(Key, FilePath, SamplerParams, CreateLoader, TexHandle);
            }

            return TexHandle;
        });
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const HnTextureIdentifier&      TexId,
                                                                      TEXTURE_FORMAT                  Format,
                                                                      const pxr::HdSamplerParameters& SamplerParams,
                                                                      bool                            IsAsync)
{
    if (TexId.FilePath.IsEmpty())
    {
        UNEXPECTED("File path must not be empty");
        return {};
    }

    return Allocate(TexId.FilePath, TexId.SubtextureId.Swizzle, SamplerParams, IsAsync,
                    [TexId, Format, CompressMode = m_CompressMode]() {
                        TextureLoadInfo LoadInfo;
                        LoadInfo.Name   = TexId.FilePath.GetText();
                        LoadInfo.Format = Format;

                        // TODO: why do textures need to be flipped vertically?
                        LoadInfo.FlipVertically      = !TexId.SubtextureId.FlipVertically;
                        LoadInfo.IsSRGB              = TexId.SubtextureId.IsSRGB;
                        LoadInfo.PermultiplyAlpha    = TexId.SubtextureId.PremultiplyAlpha;
                        LoadInfo.Swizzle             = TexId.SubtextureId.Swizzle;
                        LoadInfo.CompressMode        = CompressMode;
                        LoadInfo.UniformImageClipDim = 32;

                        return CreateTextureLoaderFromSdfPath(TexId.FilePath.GetText(), LoadInfo);
                    });
}

Uint32 HnTextureRegistry::GetStorageVersion() const
{
    Uint32 Version = m_StorageVersion.load();
    if (m_pResourceManager != nullptr)
        Version += m_pResourceManager->GetTextureVersion();
    return Version;
}

Uint32 HnTextureRegistry::GetDataVersion() const
{
    return m_DataVersion.load();
}

} // namespace USD

} // namespace Diligent
