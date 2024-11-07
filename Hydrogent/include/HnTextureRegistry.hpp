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

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/types.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/ObjectsRegistry.hpp"
#include "../../../DiligentTools/TextureLoader/interface/TextureLoader.h"

#include "HnTextureUtils.hpp"

namespace Diligent
{

struct ITextureAtlasSuballocation;

namespace GLTF
{
class ResourceManager;
}

namespace USD
{

struct HnTextureIdentifier;

class HnTextureRegistry final : public std::enable_shared_from_this<HnTextureRegistry>
{
public:
    struct CreateInfo
    {
        IRenderDevice*             pDevice          = nullptr;
        IThreadPool*               pThreadPool      = nullptr;
        GLTF::ResourceManager*     pResourceManager = nullptr;
        TEXTURE_LOAD_COMPRESS_MODE CompressMode     = TEXTURE_LOAD_COMPRESS_MODE_NONE;
        Uint64                     LoadBudget       = 0;
    };

    HnTextureRegistry(const CreateInfo& CI);
    ~HnTextureRegistry();

    void Commit(IDeviceContext* pContext);

    class TextureHandle
    {
    public:
        TextureHandle(HnTextureRegistry& Registry, Uint32 Id) noexcept;
        ~TextureHandle();

        bool IsInitialized() const noexcept
        {
            return m_IsInitialized.load();
        }
        bool IsLoaded() const noexcept
        {
            return IsInitialized() && (m_pTexture || m_pAtlasSuballocation);
        }

        Uint32 GetId() const
        {
            VERIFY(IsInitialized(), "Reading texture Id is not safe before the handle is initialized");
            return m_TextureId;
        }

        ITexture* GetTexture() const
        {
            VERIFY(IsInitialized(), "Reading texture is not safe before the handle is initialized");
            return m_pTexture;
        }

        ITextureAtlasSuballocation* GetAtlasSuballocation() const
        {
            VERIFY(IsInitialized(), "Reading texture atlas suballocation is not safe before the handle is initialized");
            return m_pAtlasSuballocation;
        }

    private:
        friend HnTextureRegistry;

        void SetAtlasSuballocation(ITextureAtlasSuballocation* pSuballocation);

        void Initialize(IRenderDevice*                  pDevice,
                        IDeviceContext*                 pContext,
                        ITextureLoader*                 pLoader,
                        const pxr::HdSamplerParameters& SamplerParams);

    private:
        RefCntAutoPtr<ITexture>                   m_pTexture;
        RefCntAutoPtr<ITextureAtlasSuballocation> m_pAtlasSuballocation;

        std::weak_ptr<HnTextureRegistry> m_Registry;

        // Texture ID used for bindless access
        const Uint32 m_TextureId;

        std::atomic<bool> m_IsInitialized{false};

        size_t LoaderMemorySize = 0;

        // Texture data size in bytes
        Uint64 DataSize = 0;
    };

    using TextureHandleSharedPtr = std::shared_ptr<TextureHandle>;

    TextureHandleSharedPtr Allocate(const HnTextureIdentifier&      TexId,
                                    TEXTURE_FORMAT                  Format,
                                    const pxr::HdSamplerParameters& SamplerParams,
                                    bool                            IsAsync);

    using CreateTextureLoaderCallbackType = std::function<HnLoadTextureResult(Int64, size_t)>;

    // Allocates texture handle for the specified texture file path.
    // If the texture is not loaded, calls CreateLoader() to create the texture loader.
    TextureHandleSharedPtr Allocate(const pxr::TfToken&             FilePath,
                                    const TextureComponentMapping&  Swizzle,
                                    const pxr::HdSamplerParameters& SamplerParams,
                                    bool                            IsAsync,
                                    CreateTextureLoaderCallbackType CreateLoader);

    TextureHandleSharedPtr Get(const pxr::TfToken& Path)
    {
        return m_Cache.Get(Path);
    }

    /// Returns the texture registry storage version.
    ///
    /// \remarks    The storage version is incremented every time a new texture is created
    ///             or dynamic texture atlas version changes.
    ///
    ///             The storage version is not incremented when the texture data is updated.
    Uint32 GetStorageVersion() const;

    /// Returns the texture registry data version.
    ///
    /// \remarks    The data version is incremented every time a texture is loaded or updated.
    Uint32 GetDataVersion() const;

    template <typename HandlerType>
    void ProcessTextures(HandlerType&& Handler)
    {
        m_Cache.ProcessElements(Handler);
    }

    TEXTURE_LOAD_COMPRESS_MODE GetCompressMode() const { return m_CompressMode; }

    Int32 GetNumTexturesLoading() const { return m_NumTexturesLoading.load(); }

    void WaitForAsyncTasks();

    struct UsageStats
    {
        Uint32 NumTexturesLoading  = 0;
        Uint64 LoadingTexDataSize  = 0;
        Uint64 AtlasDataSize       = 0;
        Uint64 SeparateTexDataSize = 0;
    };
    UsageStats GetUsageStats() const;

private:
    HN_LOAD_TEXTURE_STATUS LoadTexture(const pxr::TfToken              Key,
                                       const pxr::TfToken&             FilePath,
                                       const pxr::HdSamplerParameters& SamplerParams,
                                       Int64                           MemoryBudget,
                                       CreateTextureLoaderCallbackType CreateLoader,
                                       std::shared_ptr<TextureHandle>  TexHandle);

    void OnDestroyHandle(const TextureHandle& Handle);

private:
    RefCntAutoPtr<IRenderDevice>     m_pDevice;
    RefCntAutoPtr<IThreadPool>       m_pThreadPool;
    GLTF::ResourceManager* const     m_pResourceManager;
    const TEXTURE_LOAD_COMPRESS_MODE m_CompressMode;
    const Int64                      m_LoadBudget;

    ObjectsRegistry<pxr::TfToken, TextureHandleSharedPtr, pxr::TfToken::HashFunctor> m_Cache;

    struct PendingTextureInfo
    {
        RefCntAutoPtr<ITextureLoader> pLoader;
        pxr::HdSamplerParameters      SamplerParams;
        TextureHandleSharedPtr        Handle;

        void InitHandle(IRenderDevice* pDevice, IDeviceContext* pContext);
    };

    using PendingTexturesMapType = std::unordered_map<pxr::TfToken, PendingTextureInfo, pxr::TfToken::HashFunctor>;
    std::mutex             m_PendingTexturesMtx;
    PendingTexturesMapType m_PendingTextures;
    PendingTexturesMapType m_WIPPendingTextures;

    std::mutex                             m_AsyncTasksMtx;
    std::vector<RefCntAutoPtr<IAsyncTask>> m_AsyncTasks;

    std::mutex          m_RecycledTextureIdsMtx;
    std::vector<Uint32> m_RecycledTextureIds;

    std::atomic<Uint32> m_NextTextureId{0};
    std::atomic<Int32>  m_NumTexturesLoading{0};
    std::atomic<Uint32> m_StorageVersion{0};
    std::atomic<Uint32> m_DataVersion{0};
    std::atomic<Int64>  m_AtlasDataSize{0};
    std::atomic<Int64>  m_SeparateTexDataSize{0};
};

} // namespace USD

} // namespace Diligent
