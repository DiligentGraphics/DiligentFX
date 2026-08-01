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

#include "Render/Tessera/Passes/RadientTesseraGeometryPass.hpp"
#include "Render/Tessera/Passes/RadientTesseraPostProcessPipeline.hpp"
#include "Render/Tessera/Passes/RadientTesseraSkyboxPass.hpp"
#include "Render/RadientRenderTechnique.hpp"
#include "Render/Tessera/RadientTesseraDrawableCache.hpp"
#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"

#include "RefCntAutoPtr.hpp"
#include "ThreadPool.h"

namespace Diligent
{

class RadientAssetManagerImpl;

/// Rasterization-based Radient render technique.
class RadientTesseraRenderTechnique final : public IRadientRenderTechnique
{
public:
    RadientTesseraRenderTechnique(IThreadPool*               pThreadPool,
                                  RadientAssetManagerImpl*   pAssetManager,
                                  const RadientRendererDesc& Desc);

    virtual RADIENT_STATUS SyncScene(const IRadientScene& Scene) override final;
    virtual RADIENT_STATUS PrepareFrame(const RadientRenderContext& Context) override final;
    virtual RADIENT_STATUS BeginFrame(const RadientRenderContext& Context) override final;
    virtual RADIENT_STATUS Render(const RadientRenderContext& Context) override final;
    virtual void           EndFrame(const RadientRenderContext& Context) override final;

private:
    RefCntAutoPtr<IThreadPool>             m_pThreadPool;
    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;

    RadientTesseraDrawableCache       m_DrawableCache;
    RadientFrameRenderTargets         m_FrameTargets;
    RadientTesseraGeometryRenderer    m_GeometryRenderer;
    RadientTesseraGeometryPass        m_ForwardPass;
    RadientTesseraSkyboxPass          m_SkyboxPass;
    RadientTesseraPostProcessPipeline m_PostProcessPipeline;

    RefCntAutoPtr<IShaderResourceBinding> m_pFrameSRB;
    bool                                  m_FrameActive = false;
};

} // namespace Diligent
