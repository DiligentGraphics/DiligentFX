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

namespace Diligent
{

struct IGPUUploadManager;
struct IRenderDevice;
struct IThreadPool;
struct ITextureView;
struct ITextureLoader;

namespace GLTF
{
class ResourceManager;
} // namespace GLTF

class TexturePayloadImpl;
class RadientTextureAssetManager;

using RadientTextureAssetManagerSharedPtr = std::shared_ptr<RadientTextureAssetManager>;

struct RadientTextureAssetManagerStats
{
    // Total number of texture loads that are still active in any stage. A load
    // is counted until it either fails, reuses an existing payload, completes an
    // immediate texture creation path, or all scheduled GPU upload callbacks finish.
    Uint32 PendingTextureLoads = 0;

    // Number of texture source loads currently queued or running on the worker
    // thread. This covers source key creation, cache lookup, loader creation,
    // and upload scheduling preparation, but not render-thread GPU upload callbacks.
    Uint32 PendingTextureSourceLoads = 0;

    // Number of worker-side upload scheduling operations still in progress.
    // This remains non-zero while the worker is enqueueing GPU upload callbacks.
    Uint32 PendingUploadScheduling = 0;

    // Number of scheduled GPU upload callbacks that have not reported completion.
    // The texture load is still pending while this value is non-zero.
    Uint32 PendingGPUUploads = 0;
};

class RadientTextureAssetManager final : public std::enable_shared_from_this<RadientTextureAssetManager>
{
public:
    struct CreateInfo
    {
        IRenderDevice*         pDevice          = nullptr;
        GLTF::ResourceManager* pResourceManager = nullptr;
        IGPUUploadManager*     pUploadManager   = nullptr;
    };

    ~RadientTextureAssetManager();

    static RadientTextureAssetManagerSharedPtr Create(const CreateInfo& CI);

    RadientTextureAssetManagerStats GetStats() const noexcept;

    RADIENT_STATUS LoadTexture(IThreadPool&                  ThreadPool,
                               const RadientTextureLoadInfo& LoadInfo,
                               IRadientTextureAsset**        ppTexture);

    static ITextureView* GetTextureSRV(IRadientTextureAsset* pTextureAsset);

    // Reports texture source loading and upload scheduling status. OK means
    // required copy commands were enqueued, but does not imply GPU completion.
    // Use GetTextureSRV() to retrieve the texture view.
    static RADIENT_STATUS            GetLoadStatus(IRadientAsset* pTextureAsset);
    static const TexturePayloadImpl* GetTexturePayload(IRadientTextureAsset* pTextureAsset);

    static bool ApplyTextureAtlasAttribs(IRadientTextureAsset*                 pTexture,
                                         GLTF::Material::TextureShaderAttribs& Attribs);

private:
    explicit RadientTextureAssetManager(const CreateInfo& CI) noexcept;

    struct AtomicStats
    {
        RadientTextureAssetManagerStats GetSnapshot() const noexcept;

        std::atomic<Uint32> PendingTextureLoads{0};
        std::atomic<Uint32> PendingTextureSourceLoads{0};
        std::atomic<Uint32> PendingUploadScheduling{0};
        std::atomic<Uint32> PendingGPUUploads{0};
    };

    RADIENT_STATUS ScheduleTextureGPUUpload(GLTF::ResourceManager& ResourceManager,
                                            IGPUUploadManager&     UploadManager,
                                            IRadientTextureAsset&  TextureAsset,
                                            ITextureLoader&        Loader);

    RefCntAutoPtr<IRenderDevice>          m_pDevice;
    RefCntWeakPtr<GLTF::ResourceManager>  m_WeakResourceManager;
    RefCntWeakPtr<IGPUUploadManager>      m_WeakUploadManager;
    RadientAssetCache<TexturePayloadImpl> m_TextureCache;
    AtomicStats                           m_Stats;
    std::atomic<RadientHandle>            m_NextAssetID{1};
};

} // namespace Diligent
