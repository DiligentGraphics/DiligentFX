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
#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientTypesX.hpp"

#include "GPUUploadManager.h"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"
#include "gtest/gtest-spi.h"

#include <chrono>
#include <cstddef>
#include <thread>

namespace Diligent
{

namespace Testing
{

namespace RadientGPUTest
{

namespace
{

static constexpr Uint32 TestVertexPoolSize        = 1024;
static constexpr Uint64 TestIndexBufferSize       = 1024 * 1024;
static constexpr Uint64 TestMorphTargetBufferSize = 1024 * 1024;
static constexpr Uint32 TestTextureAtlasMaxSlice  = 16;
static constexpr auto   TextureManagerWaitTimeout = std::chrono::seconds{10};

Uint32 GetTextureStride(const TestTextureParams& Params)
{
    return Params.Stride != 0 ? Params.Stride : Params.Width * Params.PixelSize;
}

} // namespace

RefCntAutoPtr<IRadientDataBlob> MakeTextureDataBlob(Uint32                   Seed,
                                                    const TestTextureParams& Params)
{
    const Uint32 Stride = GetTextureStride(Params);

    RadientDataBlobCreateInfo CI;
    CI.Size = Uint64{Stride} * Params.Height;
    RefCntAutoPtr<IRadientMutableDataBlob> pBlob;
    RADIENT_STATUS                         Status = CreateRadientMutableDataBlob(CI, &pBlob);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    void* pData = nullptr;
    Status      = pBlob->BeginWrite(&pData);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    auto* Pixels = static_cast<Uint8*>(pData);
    for (Uint32 y = 0; y < Params.Height; ++y)
    {
        for (Uint32 x = 0; x < Params.Width; ++x)
        {
            const size_t Offset = static_cast<size_t>(y) * Stride + x * Params.PixelSize;
            Pixels[Offset + 0]  = static_cast<Uint8>((x * 3 + y * 5 + Seed * 29) & 0xFF);
            Pixels[Offset + 1]  = static_cast<Uint8>((x * 11 + y * 7 + (x ^ y) + Seed * 31) & 0xFF);
            Pixels[Offset + 2]  = static_cast<Uint8>((x * y + x * 13 + y * 17 + Seed * 37) & 0xFF);
            Pixels[Offset + 3]  = static_cast<Uint8>(127 + ((x + y + Seed * 3) & 0x7F));
        }
    }

    Status = pBlob->EndWrite();
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    return pBlob;
}

RadientTextureDataX MakeTextureData(IRadientDataBlob*        pDataBlob,
                                    const TestTextureParams& Params)
{
    RadientTextureDataX TextureData{Params.Width, Params.Height, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    TextureData.AddMip(pDataBlob, 0, GetTextureStride(Params));
    return TextureData;
}

RadientTextureLoadInfoX MakeTextureDataLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfoX LoadInfo;
    LoadInfo.SetTextureData(TextureData).SetEncodedTextureSRGB(False);
    return LoadInfo;
}

ScopedWorkerBlocker::ScopedWorkerBlocker(IThreadPool& ThreadPool) :
    m_ThreadPool{ThreadPool}
{
    RefCntAutoPtr<IAsyncTask> pTask =
        EnqueueAsyncWork(
            &m_ThreadPool,
            [this](Uint32) //
            {
                m_ReleaseWorker.Wait();
                return ASYNC_TASK_STATUS_COMPLETE;
            });
    pTask->WaitUntilRunning();
}

ScopedWorkerBlocker::~ScopedWorkerBlocker()
{
    Release();
    m_ThreadPool.StopThreads();
}

void ScopedWorkerBlocker::Release()
{
    if (!m_ReleaseWorker.IsTriggered())
        m_ReleaseWorker.Trigger();
}

ThreadPoolStopGuard::~ThreadPoolStopGuard()
{
    if (pThreadPool != nullptr)
        pThreadPool->StopThreads();
}

UploadManagerStopGuard::~UploadManagerStopGuard()
{
    UploadManager.Stop(&Context);
}

TEST(RadientGPUTestHelpersTest, ReleasesBlockedWorkerOnFatalAssertion)
{
    // A fatal assertion must leave the scope without hanging or destroying
    // the signal while the worker is still waiting on it.
    EXPECT_FATAL_FAILURE(
        []() {
            RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
            ScopedWorkerBlocker        Blocker{*pThreadPool};
            FAIL() << "Intentional early exit before releasing the worker";
        }(),
        "Intentional early exit before releasing the worker");

    // Cleanup must also be safe after the test's normal release point.
    EXPECT_FATAL_FAILURE(
        []() {
            RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
            ScopedWorkerBlocker        Blocker{*pThreadPool};
            Blocker.Release();
            FAIL() << "Intentional early exit after releasing the worker";
        }(),
        "Intentional early exit after releasing the worker");
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

    CreateInfo.MorphTargetAllocatorCI.Desc.Name              = "Radient GPU test morph target buffer";
    CreateInfo.MorphTargetAllocatorCI.Desc.Size              = TestMorphTargetBufferSize;
    CreateInfo.MorphTargetAllocatorCI.Desc.Usage             = USAGE_DEFAULT;
    CreateInfo.MorphTargetAllocatorCI.Desc.BindFlags         = BIND_SHADER_RESOURCE;
    CreateInfo.MorphTargetAllocatorCI.Desc.Mode              = BUFFER_MODE_STRUCTURED;
    CreateInfo.MorphTargetAllocatorCI.Desc.ElementByteStride = sizeof(Float32);
    CreateInfo.MorphTargetAllocatorCI.ExpansionSize          = static_cast<Uint32>(TestMorphTargetBufferSize);
    CreateInfo.MorphTargetAllocatorCI.MaxSize                = TestMorphTargetBufferSize;

    CreateInfo.DefaultPoolDesc.Name        = "Radient GPU test vertex pool";
    CreateInfo.DefaultPoolDesc.VertexCount = TestVertexPoolSize;
    CreateInfo.DefaultPoolDesc.Usage       = USAGE_DEFAULT;
    CreateInfo.DefaultPoolDesc.Mode        = BUFFER_MODE_UNDEFINED;

    CreateInfo.DefaultAtlasDesc.Desc.Name      = "Radient GPU test texture atlas";
    CreateInfo.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    CreateInfo.DefaultAtlasDesc.Desc.Width     = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.Height    = TextureAtlasSize;
    CreateInfo.DefaultAtlasDesc.Desc.ArraySize = 1;
    CreateInfo.DefaultAtlasDesc.Desc.Format    = TEX_FORMAT_RGBA8_TYPELESS;
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

RADIENT_STATUS WaitForSceneGPUResources(RadientAssetManagerImpl& AssetManager,
                                        IRadientSceneAsset*      pScene,
                                        IRenderDevice*           pDevice,
                                        IDeviceContext*          pContext)
{
    const std::chrono::steady_clock::time_point Deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    do
    {
        const RADIENT_STATUS UpdateStatus = AssetManager.UpdateGPUResources(pDevice, pContext);
        if (RADIENT_FAILED(UpdateStatus))
            return UpdateStatus;
        pContext->Flush();
        pContext->FinishFrame();

        const RADIENT_STATUS Status = RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < Deadline);
    return RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene);
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

    const auto Deadline = std::chrono::steady_clock::now() + TextureManagerWaitTimeout;
    do
    {
        const RadientTextureAssetManagerStats Stats = Manager->GetStats();
        if (IsTextureManagerIdle(Stats))
            return true;

        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            PumpUploadManager(UploadManager, Context);

        // Avoid busy-waiting while asynchronous texture work is in progress.
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < Deadline);

    return IsTextureManagerIdle(Manager->GetStats());
}

bool WaitForPendingCopyCommandEnqueueCallbacks(const RadientTextureAssetManagerSharedPtr& Manager)
{
    using namespace std::chrono_literals;

    const auto Deadline = std::chrono::steady_clock::now() + TextureManagerWaitTimeout;
    do
    {
        const RadientTextureAssetManagerStats Stats = Manager->GetStats();
        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            return true;

        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < Deadline);

    return Manager->GetStats().PendingCopyCommandEnqueueCallbacks != 0;
}

} // namespace RadientGPUTest

} // namespace Testing

} // namespace Diligent
