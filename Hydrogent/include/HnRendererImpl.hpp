/*
 *  Copyright 2023 Diligent Graphics LLC
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
#include <vector>
#include <array>

#include "HnRenderer.hpp"

#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.hpp"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/ObjectBase.hpp"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/GPUCompletionAwaitQueue.hpp"

#include "Tasks/HnTaskController.hpp"

#include "pxr/usd/usd/stage.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/usdImaging/usdImaging/delegate.h"


namespace Diligent
{

namespace USD
{

class HnRenderDelegate;
class HnMesh;
class HnMaterial;
class HnTaskController;

class HnRendererImpl final : public ObjectBase<IHnRenderer>
{
public:
    using TBase = ObjectBase<IHnRenderer>;

    // {B8E2E916-B4E6-4C1E-A2DD-78FCD763F43E}
    static constexpr INTERFACE_ID IID_Impl =
        {0xb8e2e916, 0xb4e6, 0x4c1e, {0xa2, 0xdd, 0x78, 0xfc, 0xd7, 0x63, 0xf4, 0x3e}};

    HnRendererImpl(IReferenceCounters*         pRefCounters,
                   IRenderDevice*              pDevice,
                   IDeviceContext*             pContext,
                   const HnRendererCreateInfo& CI);
    ~HnRendererImpl();

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_HnRenderer, IID_Impl, TBase)

    virtual void LoadUSDStage(pxr::UsdStageRefPtr& Stage) override final;
    virtual void Update() override final;
    virtual void SetParams(const HnRenderParams& Params) override final;
    virtual void Draw(IDeviceContext* pCtx, const HnDrawAttribs& Attribs) override final;
    virtual void SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV) override final;

    virtual const pxr::SdfPath* QueryPrimId(IDeviceContext* pCtx, Uint32 X, Uint32 Y) override final;

private:
    void DestroyStageResources();

private:
    RenderDeviceWithCache_N       m_Device;
    RefCntAutoPtr<IDeviceContext> m_Context;

    RefCntAutoPtr<IBuffer> m_CameraAttribsCB;
    RefCntAutoPtr<IBuffer> m_LightAttribsCB;

    const bool m_ConvertOutputToSRGB;

    pxr::UsdStageRefPtr m_Stage;

    std::unique_ptr<HnRenderDelegate>        m_RenderDelegate;
    std::unique_ptr<pxr::HdRenderIndex>      m_RenderIndex;
    std::unique_ptr<pxr::UsdImagingDelegate> m_ImagingDelegate;
    std::unique_ptr<HnTaskController>        m_TaskController;

    pxr::HdEngine m_Engine;

    static constexpr TEXTURE_FORMAT ColorBufferFormat = TEX_FORMAT_R11G11B10_FLOAT;
    static constexpr TEXTURE_FORMAT MeshIdFormat      = TEX_FORMAT_R32_FLOAT;
    static constexpr TEXTURE_FORMAT DepthFormat       = TEX_FORMAT_D32_FLOAT;

    GPUCompletionAwaitQueue<RefCntAutoPtr<ITexture>> m_MeshIdReadBackQueue;

    HnRenderParams m_RenderParams;
    bool           m_RenderParamsChanged = true;

    pxr::SdfPath m_FinalColorTargetId;
};

} // namespace USD

} // namespace Diligent
