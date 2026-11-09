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

#include "BrowserNative.h"
#include "RefCntAutoPtr.hpp"

#if WEBGPU_SUPPORTED
#    include "EngineFactoryWebGPU.h"
#    include <webgpu/webgpu.h>
#    include <emscripten/html5.h>

extern "C" void RadientRegisterCanvas();
#endif

namespace Diligent
{

RADIENT_STATUS CreateRadientBrowserEngine(const RadientEngineCreateInfo& CreateInfo, IRadientEngine** ppEngine, Bool EnableCapture)
{
    if (ppEngine == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppEngine = nullptr;

#if WEBGPU_SUPPORTED
    // A module-local Emscripten event target avoids temporary selector strings
    // and does not require the application to give its canvas a DOM id.
    RadientRegisterCanvas();
    static constexpr const char* CanvasTarget = "!radient-canvas";
    int                          Width        = 0;
    int                          Height       = 0;
    if (emscripten_get_canvas_element_size(CanvasTarget, &Width, &Height) != EMSCRIPTEN_RESULT_SUCCESS || Width <= 0 || Height <= 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    IEngineFactoryWebGPU* const   pFactory = GetEngineFactoryWebGPU();
    RefCntAutoPtr<IRenderDevice>  pDevice;
    RefCntAutoPtr<IDeviceContext> pContext;
    // Use the same asynchronous JavaScript device initialization and native
    // attachment path as Diligent's Emscripten samples.
    WGPUDevice             Device   = emscripten_webgpu_get_device();
    WGPUInstance           Instance = wgpuCreateInstance(nullptr);
    EngineWebGPUCreateInfo EngineCI;
    pFactory->AttachToWebGPUDevice(Instance, nullptr, Device, EngineCI, &pDevice, &pContext);
    if (!pDevice || !pContext)
        return RADIENT_STATUS_FAILED;

    SwapChainDesc SCDesc;
    SCDesc.Width  = static_cast<Uint32>(Width);
    SCDesc.Height = static_cast<Uint32>(Height);
    if (EnableCapture)
        SCDesc.Usage |= SWAP_CHAIN_USAGE_COPY_SOURCE;
    RefCntAutoPtr<ISwapChain> pSwapChain;
    pFactory->CreateSwapChainWebGPU(pDevice, pContext, SCDesc, NativeWindow{CanvasTarget}, &pSwapChain);
    if (!pSwapChain)
        return RADIENT_STATUS_FAILED;

    RadientEngineCreateInfo CI   = CreateInfo;
    CI.Backend.pDevice           = pDevice;
    CI.Backend.pImmediateContext = pContext;
    CI.Backend.pSwapChain        = pSwapChain;
    return CreateRadientEngine(CI, ppEngine);
#else
    return RADIENT_STATUS_UNSUPPORTED;
#endif
}

RADIENT_STATUS CreateRadientCanvasRenderTarget(IRadientEngine* pEngine, IRadientRenderer* pRenderer, IRadientRenderTarget** ppTarget)
{
    if (ppTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppTarget = nullptr;
    if (pEngine == nullptr || pRenderer == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientBackend> pBackend;
    RADIENT_STATUS                 Status = pEngine->GetBackend(&pBackend);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientRenderTargetDesc Desc;
    Desc.Name       = "Radient canvas";
    Desc.pSwapChain = pBackend->GetNativeSwapChain();
    if (Desc.pSwapChain == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;
    return pRenderer->CreateRenderTarget(Desc, ppTarget);
}

RADIENT_STATUS ResizeRadientCanvas(IRadientEngine* pEngine, Uint32 Width, Uint32 Height)
{
    if (pEngine == nullptr || Width == 0 || Height == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    RefCntAutoPtr<IRadientBackend> pBackend;
    RADIENT_STATUS                 Status = pEngine->GetBackend(&pBackend);
    if (RADIENT_FAILED(Status))
        return Status;
    ISwapChain* const pSwapChain = pBackend->GetNativeSwapChain();
    if (pSwapChain == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;
    pSwapChain->Resize(Width, Height);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS PresentRadientCanvas(IRadientEngine* pEngine)
{
    if (pEngine == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    RefCntAutoPtr<IRadientBackend> pBackend;
    RADIENT_STATUS                 Status = pEngine->GetBackend(&pBackend);
    if (RADIENT_FAILED(Status))
        return Status;
    ISwapChain* const pSwapChain = pBackend->GetNativeSwapChain();
    if (pSwapChain == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;
    pSwapChain->Present();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS StopRadientBrowserEngine(IRadientEngine* pEngine)
{
    if (pEngine == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    RefCntAutoPtr<IRadientBackend> pBackend;
    RADIENT_STATUS                 Status = pEngine->GetBackend(&pBackend);
    if (RADIENT_FAILED(Status))
        return Status;
    RefCntAutoPtr<IRadientAssetManager> pAssets;
    Status = pEngine->GetAssetManager(&pAssets);
    if (RADIENT_FAILED(Status))
        return Status;
    return pAssets->Stop(pBackend->GetNativeImmediateContext());
}

} // namespace Diligent
