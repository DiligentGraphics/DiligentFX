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

#include "Tasks/HnProcessSelectionTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnTokens.hpp"
#include "HnRenderParam.hpp"
#include "HnShaderSourceFactory.hpp"

#include "DebugUtilities.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace HLSL
{

#include "../shaders/HnClosestSelectedLocation.fxh"

} // namespace HLSL

namespace USD
{

HnProcessSelectionTask::HnProcessSelectionTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnProcessSelectionTask::~HnProcessSelectionTask()
{
}

void HnProcessSelectionTask::Sync(pxr::HdSceneDelegate* Delegate,
                                  pxr::HdTaskContext*   TaskCtx,
                                  pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnProcessSelectionTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
            m_NumJFIterations = static_cast<Uint32>(std::ceil(std::log2(std::max(Params.MaximumDistance, 1.f)))) + 1;

            (*TaskCtx)[HnRenderResourceTokens->suspendSuperSampling] = pxr::VtValue{true};
        }
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnProcessSelectionTask::PrepareTechniques(TEXTURE_FORMAT RTVFormat)
{
    if (m_InitTech.IsDirty || (m_InitTech.PSO && m_InitTech.PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat))
        m_InitTech.PSO.Release();
    if (m_UpdateTech.IsDirty || (m_UpdateTech.PSO && m_UpdateTech.PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFormat))
        m_UpdateTech.PSO.Release();

    if (m_InitTech.PSO && m_UpdateTech.PSO)
        return;

    try
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());

        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate->GetDevice(), RenderDelegate->GetRenderStateCache()};

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        auto pHnFxCompoundSourceFactory     = HnShaderSourceFactory::CreateHnFxCompoundFactory();
        ShaderCI.pShaderSourceStreamFactory = pHnFxCompoundSourceFactory;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Full-screen Triangle VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "FullScreenTriangleVS";
            ShaderCI.FilePath   = "FullScreenTriangleVS.fx";

            pVS = Device.CreateShader(ShaderCI); // Throws an exception in case of error
        }

        PipelineResourceLayoutDescX ResourceLauout;
        ResourceLauout
            .SetDefaultVariableType(SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
            .AddVariable(SHADER_TYPE_PIXEL, "cbConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        GraphicsPipelineStateCreateInfoX PsoCI;
        PsoCI
            .AddRenderTarget(RTVFormat)
            .AddShader(pVS)
            .SetResourceLayout(ResourceLauout)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        if (!m_InitTech.PSO)
        {
            RefCntAutoPtr<IShader> pPS;
            {
                ShaderCI.Desc       = {"Init Closest Selected Location PS", SHADER_TYPE_PIXEL, true};
                ShaderCI.EntryPoint = "main";
                ShaderCI.FilePath   = "HnInitClosestSelectedLocation.psh";

                pPS = Device.CreateShader(ShaderCI); // Throws an exception in case of error
            }

            PsoCI
                .SetName("Init closest selection")
                .AddShader(pPS);
            m_InitTech.PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws an exception in case of error
            m_InitTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbConstants")->Set(m_ConstantsCB);
            m_InitTech.IsDirty = false;
        }

        if (!m_UpdateTech.PSO)
        {
            RefCntAutoPtr<IShader> pPS;
            {
                ShaderCI.Desc       = {"Update Closest Selected Location PS", SHADER_TYPE_PIXEL, true};
                ShaderCI.EntryPoint = "main";
                ShaderCI.FilePath   = "HnUpdateClosestSelectedLocation.psh";

                pPS = Device.CreateShader(ShaderCI); // Throws an exception in case of error
            }

            PsoCI
                .SetName("Update closest selection")
                .AddShader(pPS);
            m_UpdateTech.PSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws an exception in case of error
            m_UpdateTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbConstants")->Set(m_ConstantsCB);
            m_UpdateTech.IsDirty = false;
        }
    }
    catch (const std::runtime_error& err)
    {
        LOG_ERROR_MESSAGE("Failed to initialize techniques: ", err.what());
    }
}

void HnProcessSelectionTask::PrepareSRBs(const HnFrameRenderTargets& Targets)
{
    if (Targets.ClosestSelectedLocationRTV[0] == nullptr || Targets.ClosestSelectedLocationRTV[1] == nullptr || Targets.SelectionDepthDSV == nullptr)
    {
        UNEXPECTED("Closest selected location render targets are not initialized");
        return;
    }
    ITextureView* pSelectionDepthSRV = Targets.SelectionDepthDSV->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (pSelectionDepthSRV == nullptr)
    {
        UNEXPECTED("Selection depth SRV is null");
        return;
    }

    std::array<ITextureView*, 2> ClosestSelectedLocationSRVs =
        {
            Targets.ClosestSelectedLocationRTV[0]->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
            Targets.ClosestSelectedLocationRTV[1]->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
        };
    if (ClosestSelectedLocationSRVs[0] == nullptr || ClosestSelectedLocationSRVs[1] == nullptr)
    {
        UNEXPECTED("Closest selected location SRV is null");
        return;
    }

    if (!m_InitTech.PSO || !m_UpdateTech.PSO)
    {
        UNEXPECTED("PSOs are not initialized");
        return;
    }

    if (m_InitTech.SRB && m_InitTech.Vars)
    {
        if (m_InitTech.Vars.SelectionDepth->Get() != pSelectionDepthSRV)
        {
            m_InitTech.SRB.Release();
            m_InitTech.Vars = {};
        }
    }

    if (!m_InitTech.SRB)
    {
        m_InitTech.PSO->CreateShaderResourceBinding(&m_InitTech.SRB, true);
        VERIFY_EXPR(m_InitTech.SRB);
        m_InitTech.Vars.SelectionDepth = m_InitTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SelectionDepth");
        VERIFY_EXPR(m_InitTech.Vars);

        if (m_InitTech.Vars.SelectionDepth != nullptr)
            m_InitTech.Vars.SelectionDepth->Set(pSelectionDepthSRV);
    }

    for (size_t i = 0; i < 2; ++i)
    {
        auto& Res = m_UpdateTech.Res[i];
        if (Res.SRB && Res.Vars)
        {
            if (Res.Vars.SrcClosestLocation->Get() != ClosestSelectedLocationSRVs[i])
            {
                Res.SRB.Release();
                Res.Vars = {};
            }
        }

        if (!Res.SRB)
        {
            m_UpdateTech.PSO->CreateShaderResourceBinding(&Res.SRB, true);
            VERIFY_EXPR(Res.SRB);
            Res.Vars.SrcClosestLocation = Res.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SrcClosestLocation");
            VERIFY_EXPR(Res.Vars);

            if (Res.Vars.SrcClosestLocation != nullptr)
                Res.Vars.SrcClosestLocation->Set(ClosestSelectedLocationSRVs[i]);
        }
    }
}

void HnProcessSelectionTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                     pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    if (!m_ConstantsCB)
    {
        CreateUniformBuffer(RenderDelegate->GetDevice(), sizeof(HLSL::ClosestSelectedLocationConstants), "Closest selected location constants", &m_ConstantsCB);
        VERIFY(m_ConstantsCB, "Failed to create constants CB");
    }

    if (const HnFrameRenderTargets* Targets = GetFrameRenderTargets(TaskCtx))
    {
        PrepareTechniques(Targets->ClosestSelectedLocationRTV[0]->GetDesc().Format);
        PrepareSRBs(*Targets);

        const pxr::TfToken& FinalTarget = (m_NumJFIterations % 2 == 0) ?
            HnRenderResourceTokens->closestSelectedLocation0Target :
            HnRenderResourceTokens->closestSelectedLocation1Target;

        (*TaskCtx)[HnRenderResourceTokens->closestSelectedLocationFinalTarget] = (*TaskCtx)[FinalTarget];
    }
    else
    {
        UNEXPECTED("Frame render targets are not set in the task context");
    }

    if (HnRenderParam* pRenderParam = static_cast<HnRenderParam*>(RenderDelegate->GetRenderParam()))
    {
        if (pRenderParam->GetSelectedPrimId() != m_SelectedPrimId)
        {
            m_SelectedPrimId = pRenderParam->GetSelectedPrimId();
            // For now, reset TAA when selection changes
            (*TaskCtx)[HnRenderResourceTokens->taaReset] = pxr::VtValue{true};
        }
    }
}

void HnProcessSelectionTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is not initialized");
        return;
    }

    const HnFrameRenderTargets* Targets = GetFrameRenderTargets(TaskCtx);
    if (Targets == nullptr)
    {
        UNEXPECTED("Fraame render targets is not set in the task context");
        return;
    }

    if (!m_InitTech || !m_UpdateTech)
    {
        UNEXPECTED("Render techniques are not initialized");
        return;
    }

    HnRenderDelegate* pRenderDelegate = static_cast<HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    IDeviceContext*   pCtx            = pRenderDelegate->GetDeviceContext();

    ScopedDebugGroup DebugGroup{pCtx, "Process Selection"};

    if (m_SelectedPrimId.IsEmpty())
    {
        ITextureView* pFinalRTV = Targets->ClosestSelectedLocationRTV[m_NumJFIterations % 2];
        pCtx->SetRenderTargets(1, &pFinalRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->ClearRenderTarget(pFinalRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }

    {
        float BackgroundDepth = 1.f;
        if (!GetTaskContextData(TaskCtx, HnRenderResourceTokens->backgroundDepth, BackgroundDepth))
        {
            UNEXPECTED("Background depth is not set in the task context");
        }

        MapHelper<HLSL::ClosestSelectedLocationConstants> Constants{pCtx, m_ConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        Constants->ClearDepth = BackgroundDepth;
    }

    ITextureView* ClosestSelectedLocationRTVs[] = {Targets->ClosestSelectedLocationRTV[0], Targets->ClosestSelectedLocationRTV[1]};
    pCtx->SetRenderTargets(1, ClosestSelectedLocationRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->SetPipelineState(m_InitTech.PSO);
    pCtx->CommitShaderResources(m_InitTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});

    for (Uint32 i = 0; i < m_NumJFIterations; ++i)
    {
        {
            MapHelper<HLSL::ClosestSelectedLocationConstants> Constants{pCtx, m_ConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
            Constants->SampleRange = static_cast<float>(1 << (m_NumJFIterations - 1 - i));
        }

        pCtx->SetRenderTargets(1, ClosestSelectedLocationRTVs + (i + 1) % 2, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->SetPipelineState(m_UpdateTech.PSO);
        pCtx->CommitShaderResources(m_UpdateTech.Res[i % 2].SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
    }
}

} // namespace USD

} // namespace Diligent
