/*
 *  Copyright 2025 Diligent Graphics LLC
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

#include "DepthRangeCalculator.hpp"

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderMacroHelper.hpp"
#include "GraphicsTypesX.hpp"
#include "RenderStateCache.hpp"

namespace Diligent
{

static constexpr Uint32 kThreadGroupSize = 8;

namespace HLSL
{

namespace
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/Common/private/ComputeDepthRangeStructs.fxh"

static_assert(sizeof(DepthRangeCalculator::DepthRange) == sizeof(DepthRangeI), "DepthRange and DepthRangeI must have the same size.");

static_assert(offsetof(DepthRangeI, iSceneNearZ) == offsetof(DepthRangeCalculator::DepthRange, fSceneNearZ),
              "Order of members in the DepthRange struct does not match the order of members in the DepthRangeI struct.");
static_assert(offsetof(DepthRangeI, iSceneFarZ) == offsetof(DepthRangeCalculator::DepthRange, fSceneFarZ),
              "Order of members in the DepthRange struct does not match the order of members in the DepthRangeI struct.");
static_assert(offsetof(DepthRangeI, iSceneNearDepth) == offsetof(DepthRangeCalculator::DepthRange, fSceneNearDepth),
              "Order of members in the DepthRange struct does not match the order of members in the DepthRangeI struct.");
static_assert(offsetof(DepthRangeI, iSceneFarDepth) == offsetof(DepthRangeCalculator::DepthRange, fSceneFarDepth),
              "Order of members in the DepthRange struct does not match the order of members in the DepthRangeI struct.");

static_assert(offsetof(CameraAttribs, fSceneNearZ) == offsetof(CameraAttribs, fSceneNearZ) + offsetof(DepthRangeI, iSceneNearZ),
              "Order of members in the DepthRange struct does not match the order of members in the CameraAttribs struct.");
static_assert(offsetof(CameraAttribs, fSceneFarZ) == offsetof(CameraAttribs, fSceneNearZ) + offsetof(DepthRangeI, iSceneFarZ),
              "Order of members in the DepthRange struct does not match the order of members in the CameraAttribs struct.");
static_assert(offsetof(CameraAttribs, fSceneNearDepth) == offsetof(CameraAttribs, fSceneNearZ) + offsetof(DepthRangeI, iSceneNearDepth),
              "Order of members in the DepthRange struct does not match the order of members in the CameraAttribs struct.");
static_assert(offsetof(CameraAttribs, fSceneFarDepth) == offsetof(CameraAttribs, fSceneNearZ) + offsetof(DepthRangeI, iSceneFarDepth),
              "Order of members in the DepthRange struct does not match the order of members in the CameraAttribs struct.");

} // namespace

} // namespace HLSL

DepthRangeCalculator::DepthRangeCalculator(const CreateInfo& CI) :
    m_pDevice{CI.pDevice}
{
    RenderDeviceWithCache_N Device{CI.pDevice, CI.pStateCache};

    // Create the signature
    {
        constexpr WebGPUResourceAttribs WGPUDepthMap{WEB_GPU_BINDING_TYPE_UNFILTERABLE_FLOAT_TEXTURE, RESOURCE_DIM_TEX_2D};

        PipelineResourceSignatureDescX SignDesc{
            {
                // clang-format off
                {SHADER_TYPE_COMPUTE, "cbCameraAttribs", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                {SHADER_TYPE_COMPUTE, "g_Depth",         1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, PIPELINE_RESOURCE_FLAG_NONE, WGPUDepthMap},
                {SHADER_TYPE_COMPUTE, "g_DepthRange",    1, SHADER_RESOURCE_TYPE_BUFFER_UAV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                // clang-format on
            },
            {},
        };
        m_Signature = Device.CreatePipelineResourceSignature(SignDesc);
        VERIFY_EXPR(m_Signature);
    }

    {
        ShaderCreateInfo ShaderCI{
            "ClearDepthRange.csh",
            &DiligentFXShaderSourceStreamFactory::GetInstance(),
            "main",
            {},
            SHADER_SOURCE_LANGUAGE_HLSL,
            {"Clear depth range CS", SHADER_TYPE_COMPUTE, true},
        };
        ShaderCI.CompileFlags = CI.PackMatrixRowMajor ? SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR : SHADER_COMPILE_FLAG_NONE;

        RefCntAutoPtr<IShader> pCS = Device.CreateShader(ShaderCI);
        if (!pCS)
        {
            LOG_FATAL_ERROR_AND_THROW("Failed to create clear depth range compute shader");
        }

        ComputePipelineStateCreateInfoX PsoCI{"Clear Depth Range"};
        PsoCI.AddSignature(m_Signature);
        PsoCI.AddShader(pCS);

        m_ClearDepthRangePSO = Device.CreateComputePipelineState(PsoCI);
        if (!m_ClearDepthRangePSO)
        {
            LOG_FATAL_ERROR_AND_THROW("Failed to create clear depth range PSO");
        }
    }

    {
        ShaderMacroHelper Macros;
        Macros.Add("THREAD_GROUP_SIZE", static_cast<int>(kThreadGroupSize));

        ShaderCreateInfo ShaderCI{
            "ComputeDepthRange.csh",
            &DiligentFXShaderSourceStreamFactory::GetInstance(),
            "main",
            Macros,
            SHADER_SOURCE_LANGUAGE_HLSL,
            {"Compute depth range CS", SHADER_TYPE_COMPUTE, true},
        };
        ShaderCI.CompileFlags = CI.PackMatrixRowMajor ? SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR : SHADER_COMPILE_FLAG_NONE;

        RefCntAutoPtr<IShader> pCS = Device.CreateShader(ShaderCI);
        if (!pCS)
        {
            LOG_FATAL_ERROR_AND_THROW("Failed to create depth range compute shader");
        }

        ComputePipelineStateCreateInfoX PsoCI{"Compute Depth Range"};
        PsoCI.AddSignature(m_Signature);
        PsoCI.AddShader(pCS);

        m_ComputeDepthRangePSO = Device.CreateComputePipelineState(PsoCI);
        if (!m_ComputeDepthRangePSO)
        {
            LOG_FATAL_ERROR_AND_THROW("Failed to create compute depth range PSO");
        }
    }

    if (CI.ReadBackData)
    {
        m_DepthRangeReadBackQueue = std::make_unique<DepthRangeReadBackQueueType>(CI.pDevice);
    }

    {
        BufferDesc DepthRangeDesc;
        DepthRangeDesc.Name              = "Depth Range";
        DepthRangeDesc.Size              = sizeof(HLSL::DepthRangeI);
        DepthRangeDesc.BindFlags         = BIND_UNORDERED_ACCESS;
        DepthRangeDesc.Usage             = USAGE_DEFAULT;
        DepthRangeDesc.ElementByteStride = sizeof(HLSL::DepthRangeI);
        DepthRangeDesc.Mode              = BUFFER_MODE_STRUCTURED;

        RefCntAutoPtr<IBuffer> pDepthRangeBuffer;
        CI.pDevice->CreateBuffer(DepthRangeDesc, nullptr, &m_DepthRangeBuff);
        VERIFY_EXPR(m_DepthRangeBuff);
    }
}

RefCntAutoPtr<IShaderResourceBinding> DepthRangeCalculator::CreateSRB(ITextureView* pDepthBufferSRV,
                                                                      IBuffer*      pCameraAttribs)
{
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
    m_Signature->CreateShaderResourceBinding(&pSRB, true);
    ShaderResourceVariableX{pSRB, SHADER_TYPE_COMPUTE, "g_Depth"}.Set(pDepthBufferSRV);
    ShaderResourceVariableX{pSRB, SHADER_TYPE_COMPUTE, "cbCameraAttribs"}.Set(pCameraAttribs);
    ShaderResourceVariableX{pSRB, SHADER_TYPE_COMPUTE, "g_DepthRange"}.Set(m_DepthRangeBuff->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    return pSRB;
}

void DepthRangeCalculator::PollReadBackQueue(IDeviceContext* pCtx)
{
    while (RefCntAutoPtr<IBuffer> pStagingBuff = m_DepthRangeReadBackQueue->GetFirstCompleted())
    {
        {
            // We waited for the fence, so the texture data should be available.
            // However, mapping the texture on AMD with the MAP_FLAG_DO_NOT_WAIT flag
            // still returns null.
            MAP_FLAGS MapFlags = m_pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11 ?
                MAP_FLAG_NONE :
                MAP_FLAG_DO_NOT_WAIT;

            void* pMappedData = nullptr;
            pCtx->MapBuffer(pStagingBuff, MAP_READ, MapFlags, pMappedData);
            if (pMappedData != nullptr)
            {
                memcpy(&m_DepthRange, pMappedData, sizeof(m_DepthRange));
                pCtx->UnmapBuffer(pStagingBuff, MAP_READ);
            }
            else
            {
                UNEXPECTED("Mapped data pointer is null");
            }
        }
        m_DepthRangeReadBackQueue->Recycle(std::move(pStagingBuff));
    }
}

const DepthRangeCalculator::DepthRange& DepthRangeCalculator::GetDepthRange(IDeviceContext* pCtx)
{
    if (pCtx != nullptr)
    {
        PollReadBackQueue(pCtx);
    }

    return m_DepthRange;
}

void DepthRangeCalculator::ComputeRange(const ComputeRangeAttribs& Attribs)
{
    IDeviceContext* pCtx = Attribs.pContext;
    if (pCtx == nullptr)
    {
        UNEXPECTED("Context must not be null");
        return;
    }
    if (Attribs.pSRB == nullptr)
    {
        UNEXPECTED("SRB must not be null");
        return;
    }

    pCtx->SetPipelineState(m_ClearDepthRangePSO);
    pCtx->CommitShaderResources(Attribs.pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->DispatchCompute({1, 1});

    pCtx->SetPipelineState(m_ComputeDepthRangePSO);
    // Each thread group processes 2x2 pixels
    DispatchComputeAttribs DispatchAttribs{
        (Attribs.Width + (kThreadGroupSize * 2 - 1)) / (kThreadGroupSize * 2),
        (Attribs.Height + (kThreadGroupSize * 2 - 1)) / (kThreadGroupSize * 2),
    };

    pCtx->DispatchCompute(DispatchAttribs);

    if (m_DepthRangeReadBackQueue)
    {
        PollReadBackQueue(pCtx);

        RefCntAutoPtr<IBuffer> pStagingBuff = m_DepthRangeReadBackQueue->GetRecycled();
        if (!pStagingBuff)
        {
            BufferDesc StagingBuffDesc;
            StagingBuffDesc.Name           = "Staging depth range";
            StagingBuffDesc.Size           = sizeof(HLSL::DepthRangeI);
            StagingBuffDesc.Usage          = USAGE_STAGING;
            StagingBuffDesc.BindFlags      = BIND_NONE;
            StagingBuffDesc.CPUAccessFlags = CPU_ACCESS_READ;

            m_pDevice->CreateBuffer(StagingBuffDesc, nullptr, &pStagingBuff);
            VERIFY_EXPR(pStagingBuff);
        }

        pCtx->CopyBuffer(m_DepthRangeBuff, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                         pStagingBuff, 0, sizeof(HLSL::DepthRangeI), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_DepthRangeReadBackQueue->Enqueue(pCtx, std::move(pStagingBuff));
    }
}

} // namespace Diligent
