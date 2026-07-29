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

#include "RadientAssets.h"
#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "RadientAssetCache.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace Diligent
{

struct IGPUUploadManager;
struct IRenderDevice;
struct IThreadPool;
struct ITextureView;
struct ITextureLoader;
struct IRadientAssetResolver;

namespace GLTF
{
class ResourceManager;
} // namespace GLTF

class TexturePayloadImpl;
class RadientTextureSource;
class RadientTextureAssetManager;

using RadientTextureAssetManagerSharedPtr = std::shared_ptr<RadientTextureAssetManager>;

enum class RadientTextureViewType : Uint8
{
    Linear,
    SRGB,
    Count
};

/// Stable identity of the logical resource and typed view used by a texture binding.
struct RadientTextureBindingIdentity
{
    /// Unique device-object identifier for a standalone texture. Zero denotes
    /// an atlas binding: within one resource manager, ViewFormat uniquely
    /// identifies the corresponding typed atlas SRV.
    Int32 StandaloneResourceId = 0;

    /// Typed SRV format. This distinguishes, for example, linear and sRGB
    /// bindings that refer to the same underlying resource.
    TEXTURE_FORMAT ViewFormat = TEX_FORMAT_UNKNOWN;

    explicit operator bool() const noexcept
    {
        return ViewFormat != TEX_FORMAT_UNKNOWN;
    }

    bool operator==(const RadientTextureBindingIdentity& Rhs) const noexcept
    {
        return StandaloneResourceId == Rhs.StandaloneResourceId && ViewFormat == Rhs.ViewFormat;
    }

    bool operator!=(const RadientTextureBindingIdentity& Rhs) const noexcept
    {
        return !(*this == Rhs);
    }

    struct Hasher
    {
        size_t operator()(const RadientTextureBindingIdentity& Identity) const noexcept;
    };
};

struct RadientTextureAssetManagerStats
{
    // Total number of texture loads that are still active in any stage. A load
    // is counted until it either fails, reuses an existing payload, completes an
    // immediate texture creation path, or all scheduled copy-command callbacks finish.
    Uint32 PendingTextureLoads = 0;

    // Number of texture source loads currently queued or running on the worker
    // thread. This covers source key creation, cache lookup, loader creation,
    // and upload scheduling preparation, but not render-thread copy-command callbacks.
    Uint32 PendingTextureSourceLoads = 0;

    // Number of scheduled upload-manager callbacks that have not yet reported
    // whether the copy command was enqueued. This does not track GPU completion.
    Uint32 PendingCopyCommandEnqueueCallbacks = 0;
};

class RadientTextureAssetManager final : public std::enable_shared_from_this<RadientTextureAssetManager>
{
public:
    struct CreateInfo
    {
        IRenderDevice*         pDevice          = nullptr;
        GLTF::ResourceManager* pResourceManager = nullptr;
        IGPUUploadManager*     pUploadManager   = nullptr;
        IRadientAssetResolver* pAssetResolver   = nullptr;
    };

    ~RadientTextureAssetManager();

    static RadientTextureAssetManagerSharedPtr Create(const CreateInfo& CI);

    RadientTextureAssetManagerStats GetStats() const noexcept;

    RADIENT_STATUS LoadTexture(IThreadPool&                  ThreadPool,
                               const RadientTextureLoadInfo& LoadInfo,
                               IRadientTextureAsset**        ppTexture);

    static RADIENT_STATUS RejectTextureLoad(const RadientTextureLoadInfo& LoadInfo);

    // Returns the requested typed texture SRV if the texture GPU resource
    // status is OK (i.e., all required copy commands were enqueued), or
    // nullptr otherwise. Linear is the default interpretation for callers
    // such as environment-map rendering.
    // This method must not race with render-thread operations that may access
    // the texture.
    static ITextureView* GetTextureSRV(IRadientTextureAsset*  pTextureAsset,
                                       RadientTextureViewType ViewType = RadientTextureViewType::Linear);

    // Returns the stable resource identity and resolved typed view format used
    // by the requested texture view. This method is thread-safe and may be
    // called from worker threads after the GPU resource status becomes OK.
    static RadientTextureBindingIdentity GetTextureBindingIdentity(
        IRadientTextureAsset*  pTextureAsset,
        RadientTextureViewType ViewType = RadientTextureViewType::Linear);

    // Reports texture source loading status. OK means the source image was
    // decoded/loaded, but does not imply that GPU resources exist.
    static RADIENT_STATUS GetLoadStatus(IRadientAsset* pTextureAsset);

    // Reports texture GPU resource status. OK means all required copy commands
    // were enqueued, but does not imply GPU completion. NO_GPU_DATA means the
    // source loaded successfully without a GPU backend.
    static RADIENT_STATUS GetGPUResourceStatus(IRadientAsset* pTextureAsset);

    static const TexturePayloadImpl* GetTexturePayload(IRadientTextureAsset* pTextureAsset);

    // Sets atlas texture coordinates. Returns true when the texture storage
    // placement is known and the values were set, or false if storage has not
    // been created yet. This does not imply that texture data has been uploaded.
    static bool ApplyTextureAtlasAttribs(IRadientTextureAsset*                 pTexture,
                                         GLTF::Material::TextureShaderAttribs& Attribs);

private:
    explicit RadientTextureAssetManager(const CreateInfo& CI) noexcept;

    struct AtomicStats
    {
        RadientTextureAssetManagerStats GetSnapshot() const noexcept;

        std::atomic<Uint32> PendingTextureLoads{0};
        std::atomic<Uint32> PendingTextureSourceLoads{0};
        std::atomic<Uint32> PendingCopyCommandEnqueueCallbacks{0};
    };

    RADIENT_STATUS ScheduleTextureGPUUpload(GLTF::ResourceManager& ResourceManager,
                                            IGPUUploadManager&     UploadManager,
                                            IRadientTextureAsset&  TextureAsset,
                                            ITextureLoader&        Loader,
                                            const std::string&     TextureCacheKey);

    ASYNC_TASK_STATUS LoadTextureFromSource(IRadientTextureAsset& TextureAsset,
                                            RadientTextureSource  TextureSource);

    RefCntAutoPtr<IRenderDevice>          m_pDevice;
    RefCntAutoPtr<IRadientAssetResolver>  m_pAssetResolver;
    RefCntWeakPtr<GLTF::ResourceManager>  m_WeakResourceManager;
    RefCntWeakPtr<IGPUUploadManager>      m_WeakUploadManager;
    RadientAssetCache<TexturePayloadImpl> m_TextureCache;

    AtomicStats m_Stats;
};

} // namespace Diligent
