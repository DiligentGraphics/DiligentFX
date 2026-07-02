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

#include "RadientGPUTestHelpers.hpp"

#include "GPUUploadManager.h"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include <chrono>
#include <thread>

namespace Diligent
{

namespace Testing
{

namespace RadientGPUTest
{

namespace
{

static constexpr Uint32 TestVertexPoolSize       = 1024;
static constexpr Uint64 TestIndexBufferSize      = 1024 * 1024;
static constexpr Uint32 TestTextureAtlasMaxSlice = 16;

Uint32 GetTextureStride(const TestTextureParams& Params)
{
    return Params.Stride != 0 ? Params.Stride : Params.Width * Params.PixelSize;
}

} // namespace

std::vector<Uint8> MakeTexturePixels(Uint32                   Seed,
                                     const TestTextureParams& Params)
{
    const Uint32 Stride = GetTextureStride(Params);

    std::vector<Uint8> Pixels(Stride * Params.Height);

    for (Uint32 y = 0; y < Params.Height; ++y)
    {
        for (Uint32 x = 0; x < Params.Width; ++x)
        {
            const Uint32 Offset = y * Stride + x * Params.PixelSize;
            Pixels[Offset + 0]  = static_cast<Uint8>((x * 3 + y * 5 + Seed * 29) & 0xFF);
            Pixels[Offset + 1]  = static_cast<Uint8>((x * 11 + y * 7 + (x ^ y) + Seed * 31) & 0xFF);
            Pixels[Offset + 2]  = static_cast<Uint8>((x * y + x * 13 + y * 17 + Seed * 37) & 0xFF);
            Pixels[Offset + 3]  = static_cast<Uint8>(127 + ((x + y + Seed * 3) & 0x7F));
        }
    }

    return Pixels;
}

RadientTextureData MakeTextureData(const std::vector<Uint8>& Pixels,
                                   const TestTextureParams&  Params)
{
    RadientTextureData TextureData{
        Params.Width,
        Params.Height,
        RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,
        Pixels.data(),
        GetTextureStride(Params),
    };
    return TextureData;
}

RadientTextureLoadInfo MakeTextureDataLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pTextureData = &TextureData;
    LoadInfo.IsSRGB       = False;
    return LoadInfo;
}

RefCntAutoPtr<IAsyncTask> BlockWorkerThread(IThreadPool&         ThreadPool,
                                            ::Threading::Signal& ReleaseWorker)
{
    RefCntAutoPtr<IAsyncTask> pTask =
        EnqueueAsyncWork(
            &ThreadPool,
            [&ReleaseWorker](Uint32) //
            {
                ReleaseWorker.Wait();
                return ASYNC_TASK_STATUS_COMPLETE;
            });
    pTask->WaitUntilRunning();
    return pTask;
}

ThreadPoolStopGuard::~ThreadPoolStopGuard()
{
    if (pThreadPool != nullptr)
        pThreadPool->StopThreads();
}

GLTF::ResourceManager::CreateInfo MakeResourceManagerCI(Uint32 TextureAtlasSize)
{
    GLTF::ResourceManager::CreateInfo CreateInfo;

    CreateInfo.IndexAllocatorCI.Desc.Name      = "Radient GPU test index pool";
    CreateInfo.IndexAllocatorCI.Desc.Size      = TestIndexBufferSize;
    CreateInfo.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    CreateInfo.IndexAllocatorCI.ExpansionSize  = static_cast<Uint32>(TestIndexBufferSize);
    CreateInfo.IndexAllocatorCI.MaxSize        = TestIndexBufferSize;

    CreateInfo.DefaultPoolDesc.Name        = "Radient GPU test vertex pool";
    CreateInfo.DefaultPoolDesc.VertexCount = TestVertexPoolSize;
    CreateInfo.DefaultPoolDesc.Usage       = USAGE_DEFAULT;
    CreateInfo.DefaultPoolDesc.Mode        = BUFFER_MODE_UNDEFINED;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient GPU test texture atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = 1;
    CreateInfo.DefaultAtlasDesc.Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    CreateInfo.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    CreateInfo.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    CreateInfo.DefaultAtlasDesc.MaxSliceCount  = TestTextureAtlasMaxSlice;
    CreateInfo.DefaultAtlasDesc.MinAlignment   = 1;

    return CreateInfo;
}

RefCntAutoPtr<GLTF::ResourceManager> CreateTestResourceManager(IRenderDevice* pDevice,
                                                               Uint32         TextureAtlasSize)
{
    return GLTF::ResourceManager::Create(pDevice, MakeResourceManagerCI(TextureAtlasSize));
}

RefCntAutoPtr<IGPUUploadManager> CreateTestUploadManager(IRenderDevice*  pDevice,
                                                         IDeviceContext* pContext)
{
    RefCntAutoPtr<IGPUUploadManager> pUploadManager;
    GPUUploadManagerCreateInfo       CreateInfo;
    CreateInfo.pDevice  = pDevice;
    CreateInfo.pContext = pContext;
    CreateGPUUploadManager(CreateInfo, &pUploadManager);
    return pUploadManager;
}

RadientTextureAssetManager::CreateInfo MakeTextureManagerCI(IRenderDevice*         pDevice,
                                                            GLTF::ResourceManager* pResourceManager,
                                                            IGPUUploadManager*     pUploadManager)
{
    RadientTextureAssetManager::CreateInfo CI;
    CI.pDevice          = pDevice;
    CI.pResourceManager = pResourceManager;
    CI.pUploadManager   = pUploadManager;
    return CI;
}

RadientTextureAssetManagerSharedPtr CreateTextureManager(IRenderDevice*         pDevice,
                                                         GLTF::ResourceManager* pResourceManager,
                                                         IGPUUploadManager*     pUploadManager)
{
    return RadientTextureAssetManager::Create(MakeTextureManagerCI(pDevice, pResourceManager, pUploadManager));
}

void PumpUploadManager(IGPUUploadManager& UploadManager,
                       IDeviceContext&    Context)
{
    UploadManager.RenderThreadUpdate(&Context);
    Context.Flush();
    Context.FinishFrame();
}

void ProcessUploads(IGPUUploadManager&    UploadManager,
                    IDeviceContext&       Context,
                    IRadientTextureAsset& Texture)
{
    for (Uint32 i = 0; i < 32 && RadientTextureAssetManager::GetTextureSRV(&Texture) == nullptr; ++i)
        PumpUploadManager(UploadManager, Context);
}

bool IsTextureManagerIdle(const RadientTextureAssetManagerStats& Stats)
{
    return Stats.PendingTextureLoads == 0 &&
        Stats.PendingTextureSourceLoads == 0 &&
        Stats.PendingCopyCommandEnqueueCallbacks == 0;
}

bool WaitForTextureManagerIdle(const RadientTextureAssetManagerSharedPtr& Manager,
                               IGPUUploadManager&                         UploadManager,
                               IDeviceContext&                            Context)
{
    using namespace std::chrono_literals;

    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientTextureAssetManagerStats Stats = Manager->GetStats();
        if (IsTextureManagerIdle(Stats))
            return true;

        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            PumpUploadManager(UploadManager, Context);
        else
            std::this_thread::sleep_for(1ms);
    }

    return IsTextureManagerIdle(Manager->GetStats());
}

bool WaitForPendingCopyCommandEnqueueCallbacks(const RadientTextureAssetManagerSharedPtr& Manager)
{
    using namespace std::chrono_literals;

    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientTextureAssetManagerStats Stats = Manager->GetStats();
        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            return true;

        std::this_thread::sleep_for(1ms);
    }

    return Manager->GetStats().PendingCopyCommandEnqueueCallbacks != 0;
}

} // namespace RadientGPUTest

} // namespace Testing

} // namespace Diligent
