/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "Tasks/HnRenderBoundBoxTask.hpp"

#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnRenderParam.hpp"
#include "HnTokens.hpp"
#include "GfTypeConversions.hpp"

#include "BoundBoxRenderer.hpp"
#include "DebugUtilities.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace USD
{

HnRenderBoundBoxTask::HnRenderBoundBoxTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderBoundBoxTask::~HnRenderBoundBoxTask()
{
}

void HnRenderBoundBoxTask::Sync(pxr::HdSceneDelegate* Delegate,
                                pxr::HdTaskContext*   TaskCtx,
                                pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        if (GetTaskParams(Delegate, m_Params))
        {
            (*TaskCtx)[HnRenderResourceTokens->suspendSuperSampling] = pxr::VtValue{true};
        }

        if (!GetTaskParameter(Delegate, HnTokens->renderPassName, m_RenderPassName))
        {
            UNEXPECTED("Render pass ID is not set");
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

static std::string GetBoundBoxPSMain(bool IsGL)
{
    static_assert(HnFrameRenderTargets::GBUFFER_TARGET_COUNT == 7, "Did you change the number of G-buffer targets? You may need to update the code below.");

    std::stringstream ss;
    ss << R"(
void main(in BoundBoxVSOutput VSOut,
)";
    ss << "          out float4 Color     : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR << ',' << std::endl
       << "          out float4 MotionVec : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR;

    if (IsGL)
    {
        // Normally, bound box shader does not need to write to anything but
        // color and motion vector targets.
        // However, in OpenGL this somehow results in color output also being
        // written to the MeshID target. To work around this issue, we use a
        // custom shader that writes 0.
        ss << ',' << std::endl
           << "          out float4 MeshId    : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_MESH_ID << ',' << std::endl
           << "          out float4 Normal    : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_NORMAL << ',' << std::endl
           << "          out float4 BaseColor : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR << ',' << std::endl
           << "          out float4 Material  : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL << ',' << std::endl
           << "          out float4 IBL       : SV_Target" << HnFrameRenderTargets::GBUFFER_TARGET_IBL;
    }

    ss << R"()
{
    BoundBoxOutput BoundBox = GetBoundBoxOutput(VSOut);

    // Write zero alpha as if bound box was fully transparent.
    // In particular, this disables SSR. 
    Color = float4(BoundBox.Color.rgb, 0.0);

    MotionVec = float4(BoundBox.MotionVector, 0.0, 1.0);
)";

    if (IsGL)
    {
        ss << R"(
    MeshId    = float4(0.0, 0.0, 0.0, 1.0);
    Normal    = float4(0.0, 0.0, 1.0, 1.0);
    BaseColor = float4(0.0, 0.0, 0.0, 0.0);
    Material  = float4(0.0, 0.0, 0.0, 0.0);
    IBL       = float4(0.0, 0.0, 0.0, 0.0);
)";
    }

    ss << "}\n";

    return ss.str();
}

void HnRenderBoundBoxTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                   pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex    = RenderIndex;
    m_RenderBoundBox = false;

    HnRenderDelegate*    pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const HnRenderParam* pRenderParam    = static_cast<const HnRenderParam*>(pRenderDelegate->GetRenderParam());
    if (pRenderParam == nullptr)
    {
        UNEXPECTED("Render param is not set");
        return;
    }

    const pxr::SdfPath& SelectedPrimId = pRenderParam->GetSelectedPrimId();
    if (SelectedPrimId.IsEmpty())
    {
        return;
    }

    pxr::HdSceneDelegate* SceneDelegate = m_RenderIndex->GetSceneDelegateForRprim(SelectedPrimId);
    if (SceneDelegate == nullptr)
    {
        return;
    }

    const pxr::GfRange3d PrimExtent = SceneDelegate->GetExtent(SelectedPrimId);
    if (PrimExtent.IsEmpty())
    {
        return;
    }

    pxr::GfMatrix4d Transform = SceneDelegate->GetTransform(SelectedPrimId);
    pxr::GfVec3d    Scale     = PrimExtent.GetMax() - PrimExtent.GetMin();
    pxr::GfVec3d    Offset    = PrimExtent.GetMin();

    float4x4 BoundBoxTransform =
        float4x4::Scale(ToVector3<float>(Scale)) *
        float4x4::Translation(ToVector3<float>(Offset)) *
        ToMatrix4x4<float>(Transform);

    if (!m_BoundBoxRenderer)
    {
        if (HnRenderPassState* RenderPassState = GetRenderPassState(TaskCtx, m_RenderPassName))
        {
            BoundBoxRenderer::CreateInfo BoundBoxRndrCI;
            BoundBoxRndrCI.pDevice          = pRenderDelegate->GetDevice();
            BoundBoxRndrCI.pCameraAttribsCB = pRenderDelegate->GetFrameAttribsCB();
            BoundBoxRndrCI.NumRenderTargets = RenderPassState->GetNumRenderTargets();
            for (Uint32 rt = 0; rt < BoundBoxRndrCI.NumRenderTargets; ++rt)
                BoundBoxRndrCI.RTVFormats[rt] = RenderPassState->GetRenderTargetFormat(rt);
            BoundBoxRndrCI.DSVFormat = RenderPassState->GetDepthStencilFormat();

            const std::string PSMain    = GetBoundBoxPSMain(BoundBoxRndrCI.pDevice->GetDeviceInfo().IsGLDevice());
            BoundBoxRndrCI.PSMainSource = PSMain.c_str();

            m_BoundBoxRenderer = std::make_unique<BoundBoxRenderer>(BoundBoxRndrCI);
        }
        else
        {
            UNEXPECTED("Render pass state is not set in the task context");
        }
    }

    BoundBoxRenderer::RenderAttribs Attribs;
    Attribs.Color                = &m_Params.Color;
    Attribs.BoundBoxTransform    = &BoundBoxTransform;
    Attribs.PatternLength        = m_Params.PatternLength;
    Attribs.PatternMask          = m_Params.PatternMask;
    Attribs.ComputeMotionVectors = true;
    m_BoundBoxRenderer->Prepare(pRenderDelegate->GetDeviceContext(), Attribs);

    m_RenderBoundBox = true;
}

void HnRenderBoundBoxTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (!m_BoundBoxRenderer || !m_RenderBoundBox)
        return;

    HnRenderDelegate* pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pContext        = pRenderDelegate->GetDeviceContext();

    if (HnRenderPassState* RenderPassState = GetRenderPassState(TaskCtx, m_RenderPassName))
    {
        RenderPassState->Commit(pContext);
    }
    else
    {
        UNEXPECTED("Render pass state is not set in the task context");
        return;
    }

    ScopedDebugGroup DebugGroup{pContext, "Bounding Box"};
    m_BoundBoxRenderer->Render(pContext);
}

} // namespace USD

} // namespace Diligent
