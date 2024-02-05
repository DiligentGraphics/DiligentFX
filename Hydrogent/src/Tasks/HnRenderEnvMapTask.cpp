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

#include "Tasks/HnRenderEnvMapTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnTokens.hpp"

#include "EnvMapRenderer.hpp"
#include "USD_Renderer.hpp"

#include "DebugUtilities.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace USD
{

HnRenderEnvMapTask::HnRenderEnvMapTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderEnvMapTask::~HnRenderEnvMapTask()
{
}

void HnRenderEnvMapTask::Sync(pxr::HdSceneDelegate* Delegate,
                              pxr::HdTaskContext*   TaskCtx,
                              pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnRenderEnvMapTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

static constexpr char EnvMapPSMain[] = R"(
void main(in  float4 Pos       : SV_Position,
          in  float4 ClipPos   : CLIP_POS,
          out float4 Color     : SV_Target0,
          out float4 MotionVec : SV_Target2)
{
    SampleEnvMapOutput EnvMap = SampleEnvMap(ClipPos);

    Color     = EnvMap.Color;
    MotionVec = float4(EnvMap.MotionVector, 0.0, 1.0);
}
)";

static constexpr char EnvMapPSMainGL[] = R"(
void main(in  float4 Pos       : SV_Position,
          in  float4 ClipPos   : CLIP_POS,
          out float4 Color     : SV_Target0,
          out float4 MeshId    : SV_Target1,
          out float4 MotionVec : SV_Target2,
          out float4 Normal    : SV_Target3,
          out float4 BaseColor : SV_Target4,
          out float4 Material  : SV_Target5,
          out float4 IBL       : SV_Target6)
{
    SampleEnvMapOutput EnvMap = SampleEnvMap(ClipPos);

    Color     = EnvMap.Color;
    MeshId    = float4(0.0, 0.0, 0.0, 1.0);
    MotionVec = float4(EnvMap.MotionVector, 0.0, 1.0);
    Normal    = float4(0.0, 0.0, 0.0, 0.0);
    BaseColor = float4(0.0, 0.0, 0.0, 0.0);
    Material  = float4(0.0, 0.0, 0.0, 0.0);
    IBL       = float4(0.0, 0.0, 0.0, 0.0);
}
)";

void HnRenderEnvMapTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                 pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    if (!m_EnvMapRenderer)
    {
        std::shared_ptr<HnRenderPassState> RenderPassState = GetRenderPassState(TaskCtx);
        if (RenderPassState)
        {
            HnRenderDelegate* pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

            EnvMapRenderer::CreateInfo EnvMapRndrCI;
            EnvMapRndrCI.pDevice          = pRenderDelegate->GetDevice();
            EnvMapRndrCI.pCameraAttribsCB = pRenderDelegate->GetFrameAttribsCB();
            EnvMapRndrCI.NumRenderTargets = RenderPassState->GetNumRenderTargets();
            for (Uint32 rt = 0; rt < EnvMapRndrCI.NumRenderTargets; ++rt)
                EnvMapRndrCI.RTVFormats[rt] = RenderPassState->GetRenderTargetFormat(rt);
            EnvMapRndrCI.DSVFormat = RenderPassState->GetDepthStencilFormat();

            if (EnvMapRndrCI.pDevice->GetDeviceInfo().IsGLDevice())
            {
                // Normally, environment map shader does not need to write to anything but
                // color and motion vector targets.
                // However, in OpenGL this somehow results in color output also being
                // written to the MeshID target. To work around this issue, we use a
                // custom shader that writes 0.
                EnvMapRndrCI.PSMainSource = EnvMapPSMainGL;
            }
            else
            {
                EnvMapRndrCI.PSMainSource = EnvMapPSMain;
            }
            m_EnvMapRenderer = std::make_unique<EnvMapRenderer>(EnvMapRndrCI);
        }
        else
        {
            UNEXPECTED("Render pass state is not set");
        }
    }
}

void HnRenderEnvMapTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (!m_EnvMapRenderer)
        return;

    HnRenderDelegate* pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

    auto USDRenderer = pRenderDelegate->GetUSDRenderer();
    if (!USDRenderer)
    {
        UNEXPECTED("USD renderer is not initialized");
        return;
    }

    auto* pEnvMapSRV = USDRenderer->GetPrefilteredEnvMapSRV();
    if (pEnvMapSRV == nullptr)
        return;

    Diligent::HLSL::ToneMappingAttribs TMAttribs;
    // Tone mapping is performed in the post-processing pass
    TMAttribs.iToneMappingMode     = TONE_MAPPING_MODE_NONE;
    TMAttribs.bAutoExposure        = 0;
    TMAttribs.fMiddleGray          = 0.18f;
    TMAttribs.bLightAdaptation     = 0;
    TMAttribs.fWhitePoint          = 3.0f;
    TMAttribs.fLuminanceSaturation = 1.0;

    EnvMapRenderer::RenderAttribs EnvMapAttribs;
    EnvMapAttribs.pContext      = pRenderDelegate->GetDeviceContext();
    EnvMapAttribs.pEnvMap       = pEnvMapSRV;
    EnvMapAttribs.AverageLogLum = 0.3f;
    EnvMapAttribs.MipLevel      = 1;
    // We should write zero alpha to get correct alpha in the final image
    EnvMapAttribs.Alpha                = 0;
    EnvMapAttribs.ComputeMotionVectors = true;

    ScopedDebugGroup DebugGroup{EnvMapAttribs.pContext, "Render Environment Map"};

    m_EnvMapRenderer->Render(EnvMapAttribs, TMAttribs);
}

} // namespace USD

} // namespace Diligent
