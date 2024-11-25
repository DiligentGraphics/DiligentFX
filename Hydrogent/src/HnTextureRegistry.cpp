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

HnTextureRegistry::TextureHandle::TextureHandle(HnTextureRegistry& Registry, Uint32 Id) noexcept :
    m_Registry{Registry.shared_from_this()},
    m_TextureId{Id}
{}

HnTextureRegistry::TextureHandle::~TextureHandle()
{
    if (auto Registry = m_Registry.lock())
    {
        Registry->OnDestroyHandle(*this);
    }
    else
    {
        UNEXPECTED("All texture handles must be destroyed before the registry is destroyed.");
    }
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
    // Note that we can't wait for async tasks here because the shared pointer to the registry is already expired,
    // and the texture handles will not be able to access the registry to decrement the data size counters.
    VERIFY(m_AsyncTasks.empty(), "There are still async tasks pending. Call WaitForAsyncTasks() before destroying the registry.");

    VERIFY(m_AtlasDataSize == 0 && m_SeparateTexDataSize == 0,
           "Atlas data size and/or Separate texture data size are non-zero. "
           "This either indicates that data size is not properly tracked or that some texture handles are still alive. "
           "There should be no texture handles alive at this point because all prims should have been destroyed by now and because we "
           "waited for all async tasks to finish.");
}

void HnTextureRegistry::WaitForAsyncTasks()
{
    if (m_pThreadPool)
    {
        for (RefCntAutoPtr<IAsyncTask>& pTask : m_AsyncTasks)
        {
            pTask->Cancel();
        }
        // Only wait for our own tasks
        for (RefCntAutoPtr<IAsyncTask>& pTask : m_AsyncTasks)
        {
            pTask->WaitForCompletion();
        }

        m_AsyncTasks.clear();
    }

    // Clear the pending textures list to release the shared pointers to the texture handles
    m_PendingTextures.clear();
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
            TextureHandle& Handle = *tex_it.second.Handle;
            VERIFY_EXPR(Handle.IsInitialized());

            m_DataVersion.fetch_add(1);

            if (Handle.GetTexture())
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

HN_LOAD_TEXTURE_STATUS HnTextureRegistry::LoadTexture(const pxr::TfToken              Key,
                                                      const pxr::TfToken&             FilePath,
                                                      const pxr::HdSamplerParameters& SamplerParams,
                                                      Int64                           MemoryBudget,
                                                      CreateTextureLoaderCallbackType CreateLoader,
                                                      std::shared_ptr<TextureHandle>  TexHandle)
{
    HnLoadTextureResult LoadResult = CreateLoader(MemoryBudget, TexHandle->LoaderMemorySize);
    if (!LoadResult)
    {
        // Save loader memory size so that next time we can check it up front
        TexHandle->LoaderMemorySize = LoadResult.LoaderMemorySize;

        if (LoadResult.LoadStatus != HN_LOAD_TEXTURE_STATUS_BUDGET_EXCEEDED)
        {
            LOG_ERROR_MESSAGE("Failed to create texture loader for texture ", FilePath);

            // Since we don't add the handle to the pending list, we need to decrement the counter here
            m_NumTexturesLoading.fetch_add(-1);

            // Set the m_IsInitialized flag or the handle will be stuck in the loading state
            TexHandle->m_IsInitialized.store(true);
        }

        return LoadResult.LoadStatus;
    }
    RefCntAutoPtr<ITextureLoader> pLoader = std::move(LoadResult.pLoader);

    const TextureDesc& TexDesc = pLoader->GetTextureDesc();
    TexHandle->DataSize        = GetStagingTextureDataSize(TexDesc);

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
        m_AtlasDataSize.fetch_add(static_cast<Int64>(TexHandle->DataSize));
    }
    else
    {
        // If the texture was not allocated in the atlas (because the atlas is not used or the texture is too large),
        // try to create it as a Separate texture.
        if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation)
        {
            TexHandle->Initialize(m_pDevice, nullptr, pLoader, SamplerParams);
        }
        m_SeparateTexDataSize.fetch_add(static_cast<Int64>(TexHandle->DataSize));
    }

    // Finish initialization in the main thread: we either need to upload the texture data to the atlas or
    // create the texture in the main thread if the device does not support multithreaded resource creation
    // and transition it to the shader resource state.
    {
        std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
        m_PendingTextures.emplace(Key, PendingTextureInfo{std::move(pLoader), SamplerParams, std::move(TexHandle)});
    }

    return LoadResult.LoadStatus;
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const pxr::TfToken&             FilePath,
                                                                      const TextureComponentMapping&  Swizzle,
                                                                      const pxr::HdSamplerParameters& SamplerParams,
                                                                      bool                            IsAsync,
                                                                      CreateTextureLoaderCallbackType CreateLoader)
{
    const pxr::TfToken Key{FilePath.GetString() + '.' + GetTextureComponentMappingString(Swizzle)};
    return m_Cache.Get(
        Key,
        [&]() {
            m_NumTexturesLoading.fetch_add(+1);

            Uint32 TextureId = 0;
            {
                std::lock_guard<std::mutex> Lock{m_RecycledTextureIdsMtx};
                if (!m_RecycledTextureIds.empty())
                {
                    TextureId = m_RecycledTextureIds.back();
                    m_RecycledTextureIds.pop_back();
                }
                else
                {
                    TextureId = m_NextTextureId.fetch_add(1);
                }
            }

            std::shared_ptr<TextureHandle> TexHandle = std::make_shared<TextureHandle>(*this, TextureId);

            if (IsAsync && m_pThreadPool)
            {
                // Load textures with lower priority to let e.g. the shaders compile first
                constexpr float Priority = -1;

                RefCntAutoPtr<IAsyncTask> pTask = EnqueueAsyncWork(
                    m_pThreadPool,
                    [=](Uint32 ThreadId) {
                        HN_LOAD_TEXTURE_STATUS LoadStatus = LoadTexture(Key, FilePath, SamplerParams, m_LoadBudget, CreateLoader, TexHandle);
                        return LoadStatus != HN_LOAD_TEXTURE_STATUS_BUDGET_EXCEEDED ?
                            ASYNC_TASK_STATUS_COMPLETE :
                            ASYNC_TASK_STATUS_NOT_STARTED; // Reschedule the task
                    },
                    Priority);

                {
                    std::lock_guard<std::mutex> Lock{m_AsyncTasksMtx};
                    m_AsyncTasks.emplace_back(pTask);
                }
            }
            else
            {
                LoadTexture(Key, FilePath, SamplerParams, 0, CreateLoader, TexHandle);
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
                    [TexId, Format, CompressMode = m_CompressMode](Int64 MemoryBudget, size_t LoaderMemorySize) {
                        TextureLoadInfo LoadInfo;
                        LoadInfo.Name                = TexId.FilePath.GetText();
                        LoadInfo.Format              = Format;
                        LoadInfo.FlipVertically      = TexId.SubtextureId.FlipVertically;
                        LoadInfo.IsSRGB              = TexId.SubtextureId.IsSRGB;
                        LoadInfo.PermultiplyAlpha    = TexId.SubtextureId.PremultiplyAlpha;
                        LoadInfo.Swizzle             = TexId.SubtextureId.Swizzle;
                        LoadInfo.CompressMode        = CompressMode;
                        LoadInfo.UniformImageClipDim = 32;

                        return LoadTextureFromSdfPath(TexId.FilePath.GetText(), LoadInfo, MemoryBudget, LoaderMemorySize);
                    });
}

Uint32 HnTextureRegistry::GetStorageVersion() const
{
    return m_StorageVersion.load();
}

Uint32 HnTextureRegistry::GetAtlasVersion() const
{
    return m_pResourceManager != nullptr ? m_pResourceManager->GetTextureVersion() : 0;
}

Uint32 HnTextureRegistry::GetDataVersion() const
{
    return m_DataVersion.load();
}

void HnTextureRegistry::OnDestroyHandle(const TextureHandle& Handle)
{
    {
        std::lock_guard<std::mutex> Guard{m_RecycledTextureIdsMtx};
        // Use m_TextureId since the handle may be uninitialized, which is totally
        // OK here, but GetId() would assert in this case.
        m_RecycledTextureIds.push_back(Handle.m_TextureId);
    }

    if (Handle.m_pAtlasSuballocation)
    {
        m_AtlasDataSize.fetch_add(-static_cast<Int64>(Handle.DataSize));
        VERIFY_EXPR(m_AtlasDataSize >= 0);
    }
    else
    {
        m_SeparateTexDataSize.fetch_add(-static_cast<Int64>(Handle.DataSize));
        VERIFY_EXPR(m_SeparateTexDataSize >= 0);
    }
}

HnTextureRegistry::UsageStats HnTextureRegistry::GetUsageStats() const
{
    UsageStats Stats;
    Stats.NumTexturesLoading  = m_NumTexturesLoading.load();
    Stats.LoadingTexDataSize  = GetTextureLoaderMemoryUsage();
    Stats.AtlasDataSize       = m_AtlasDataSize.load();
    Stats.SeparateTexDataSize = m_SeparateTexDataSize.load();
    return Stats;
}

} // namespace USD

} // namespace Diligent
