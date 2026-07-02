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

#include "Assets/RadientTextureAssetManager.hpp"
#include "GLTFResourceManager.hpp"
#include "RadientAssets.h"
#include "RefCntAutoPtr.hpp"

#include <vector>

namespace Threading
{
class Signal;
} // namespace Threading

namespace Diligent
{

struct IAsyncTask;
struct IDeviceContext;
struct IGPUUploadManager;
struct IRenderDevice;
struct IThreadPool;

namespace Testing
{

namespace RadientGPUTest
{

struct TestTextureParams
{
    Uint32 Width     = 64;
    Uint32 Height    = 64;
    Uint32 PixelSize = 4;
    // If zero, the helper uses tightly packed rows: Width * PixelSize.
    Uint32 Stride = 0;
};

// Generates deterministic RGBA8-like pixel data for texture upload tests.
std::vector<Uint8> MakeTexturePixels(Uint32                   Seed   = 0,
                                     const TestTextureParams& Params = {});

// Creates a Radient texture data view over caller-owned pixel storage.
RadientTextureData MakeTextureData(const std::vector<Uint8>& Pixels,
                                   const TestTextureParams&  Params = {});

// Wraps texture data into a memory-based texture load request.
RadientTextureLoadInfo MakeTextureDataLoadInfo(const RadientTextureData& TextureData);

// Enqueues a worker task that blocks until ReleaseWorker is triggered.
RefCntAutoPtr<IAsyncTask> BlockWorkerThread(IThreadPool&         ThreadPool,
                                            ::Threading::Signal& ReleaseWorker);

// Stops the associated thread pool when the guard leaves scope.
struct ThreadPoolStopGuard
{
    IThreadPool* pThreadPool = nullptr;

    ~ThreadPoolStopGuard();
};

// Builds a resource manager configuration with small test pools and atlas.
GLTF::ResourceManager::CreateInfo MakeResourceManagerCI(Uint32 TextureAtlasSize = 1024);

// Creates a GLTF resource manager from the standard GPU test configuration.
RefCntAutoPtr<GLTF::ResourceManager> CreateTestResourceManager(IRenderDevice* pDevice,
                                                               Uint32         TextureAtlasSize = 1024);

// Creates a GPU upload manager bound to the supplied device/context pair.
RefCntAutoPtr<IGPUUploadManager> CreateTestUploadManager(IRenderDevice*  pDevice,
                                                         IDeviceContext* pContext);

// Builds texture manager create info from already-created test dependencies.
RadientTextureAssetManager::CreateInfo MakeTextureManagerCI(IRenderDevice*         pDevice,
                                                            GLTF::ResourceManager* pResourceManager,
                                                            IGPUUploadManager*     pUploadManager);

// Creates a Radient texture manager with the supplied test dependencies.
RadientTextureAssetManagerSharedPtr CreateTextureManager(IRenderDevice*         pDevice,
                                                         GLTF::ResourceManager* pResourceManager,
                                                         IGPUUploadManager*     pUploadManager);

// Pumps one render-thread upload-manager tick and flushes the context.
void PumpUploadManager(IGPUUploadManager& UploadManager,
                       IDeviceContext&    Context);

// Pumps uploads until the texture exposes an SRV or the retry limit is reached.
void ProcessUploads(IGPUUploadManager&    UploadManager,
                    IDeviceContext&       Context,
                    IRadientTextureAsset& Texture);

// Returns true when the texture manager has no pending CPU-side load work.
bool IsTextureManagerIdle(const RadientTextureAssetManagerStats& Stats);

// Waits for the texture manager to become idle, pumping upload callbacks as needed.
bool WaitForTextureManagerIdle(const RadientTextureAssetManagerSharedPtr& Manager,
                               IGPUUploadManager&                         UploadManager,
                               IDeviceContext&                            Context);

// Waits until at least one texture upload callback is pending in the upload manager.
bool WaitForPendingCopyCommandEnqueueCallbacks(const RadientTextureAssetManagerSharedPtr& Manager);

// Checks whether asynchronous loading has either completed or is still in flight.
inline bool IsPendingOrOK(RADIENT_STATUS Status)
{
    return (Status == RADIENT_STATUS_PENDING ||
            Status == RADIENT_STATUS_OK);
}

} // namespace RadientGPUTest

} // namespace Testing

} // namespace Diligent
