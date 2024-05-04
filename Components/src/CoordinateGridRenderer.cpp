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

#include "imgui.h"
#include "ImGuiUtils.hpp"

#include "CoordinateGridRenderer.hpp"
#include "GraphicsUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "PostFXRenderTechnique.hpp"
#include "GraphicsTypesX.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/Common/public/CoordinateGridStructures.fxh"

} // namespace HLSL

CoordinateGridRenderer::CoordinateGridRenderer(IRenderDevice* pDevice) :
    m_pRenderAttribs{std::make_unique<HLSL::CoordinateGridAttribs>()}
{
    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, sizeof(HLSL::CoordinateGridAttribs), "CoordinateGridRenderer::ConstantBuffer", &pBuffer, USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, m_pRenderAttribs.get());
    m_Resources.Insert(RESOURCE_IDENTIFIER_SETTINGS_CONSTANT_BUFFER, pBuffer);
}

void CoordinateGridRenderer::Render(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pColorRTV != nullptr, "RenderAttribs.pColorRTV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDepthSRV != nullptr, "RenderAttribs.pDepthSRV must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorRTV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_DEPTH, RenderAttribs.pDepthSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "CoordinateGridRenderer"};

    if (RenderAttribs.pCameraAttribsCB == nullptr)
    {
        DEV_CHECK_ERR(RenderAttribs.pCamera != nullptr, "RenderAttribs.pCurrCamera must not be null");

        if (!m_Resources[RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER])
        {
            RefCntAutoPtr<IBuffer> pBuffer;
            CreateUniformBuffer(RenderAttribs.pDevice, sizeof(HLSL::CameraAttribs), "CoordinateGridRenderer::CameraAttibsConstantBuffer", &pBuffer);
            m_Resources.Insert(RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER, pBuffer);
        }

        MapHelper<HLSL::CameraAttribs> CameraAttibs{RenderAttribs.pDeviceContext, m_Resources[RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER], MAP_WRITE, MAP_FLAG_DISCARD};
        *CameraAttibs = *RenderAttribs.pCamera;
    }
    else
    {
        m_Resources.Insert(RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER, RenderAttribs.pCameraAttribsCB);
    }

    if (memcmp(RenderAttribs.pAttribs, m_pRenderAttribs.get(), sizeof(HLSL::CoordinateGridAttribs)) != 0)
    {
        memcpy(m_pRenderAttribs.get(), RenderAttribs.pAttribs, sizeof(HLSL::CoordinateGridAttribs));
        RenderAttribs.pDeviceContext->UpdateBuffer(m_Resources[RESOURCE_IDENTIFIER_SETTINGS_CONSTANT_BUFFER].AsBuffer(), 0, sizeof(HLSL::CoordinateGridAttribs), RenderAttribs.pAttribs, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    RenderGridAxes(RenderAttribs);

    // Release references to input resources
    for (Uint32 ResourceIdx = 0; ResourceIdx <= RESOURCE_IDENTIFIER_INPUT_LAST; ++ResourceIdx)
        m_Resources[ResourceIdx].Release();
}

bool CoordinateGridRenderer::UpdateUI(HLSL::CoordinateGridAttribs& Attribs, CoordinateGridRenderer::FEATURE_FLAGS& FeatureFlags)
{
    bool ActiveAxisX = (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_X) != 0;
    bool ActiveAxisY = (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_Y) != 0;
    bool ActiveAxisZ = (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_Z) != 0;

    bool ActivePlaneYZ = (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_YZ) != 0;
    bool ActivePlaneXZ = (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_XZ) != 0;
    bool ActivePlaneXY = (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_XY) != 0;

    bool AttribsChanged = false;

    ImGui::Text("Axes");
    ImGui::SameLine();

    if (ImGui::Checkbox("X", &ActiveAxisX))
        AttribsChanged = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("Y", &ActiveAxisY))
        AttribsChanged = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("Z", &ActiveAxisZ))
        AttribsChanged = true;

    ImGui::Text("Planes");
    ImGui::SameLine();

    if (ImGui::Checkbox("YZ", &ActivePlaneYZ))
        AttribsChanged = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("XZ", &ActivePlaneXZ))
        AttribsChanged = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("XY", &ActivePlaneXY))
        AttribsChanged = true;

    if (ImGui::SliderFloat("YZ Scale", &Attribs.GridScale[0], 0.01f, 10.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("XZ Scale", &Attribs.GridScale[1], 0.01f, 10.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("XY Scale", &Attribs.GridScale[2], 0.01f, 10.0))
        AttribsChanged = true;


    auto SubdivisionSlider = [](const char* Name, float& Subdivision) {
        int SubdivisionInt = static_cast<int>(Subdivision);
        if (ImGui::SliderInt(Name, &SubdivisionInt, 2, 10))
        {
            Subdivision = static_cast<float>(SubdivisionInt);
            return true;
        }
        return false;
    };
    if (SubdivisionSlider("YZ Subdivision", Attribs.GridSubdivision[0]))
        AttribsChanged = true;
    if (SubdivisionSlider("XZ Subdivision", Attribs.GridSubdivision[1]))
        AttribsChanged = true;
    if (SubdivisionSlider("XY Subdivision", Attribs.GridSubdivision[2]))
        AttribsChanged = true;

    if (ImGui::ColorEdit3("X Axis Color", Attribs.XAxisColor.Data()))
        AttribsChanged = true;
    if (ImGui::ColorEdit3("Y Axis Color", Attribs.YAxisColor.Data()))
        AttribsChanged = true;
    if (ImGui::ColorEdit3("Z Axis Color", Attribs.ZAxisColor.Data()))
        AttribsChanged = true;

    if (ImGui::SliderFloat("X Axis Width", &Attribs.XAxisWidth, 0.5f, 10.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Y Axis Width", &Attribs.YAxisWidth, 0.5f, 10.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Z Axis Width", &Attribs.ZAxisWidth, 0.5f, 10.0))
        AttribsChanged = true;

    if (ImGui::SliderFloat("Grid Min Cell Size", &Attribs.GridMinCellSize, 0.0001f, 1.0, "%.4f", ImGuiSliderFlags_Logarithmic))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Grid Min Cell Width", &Attribs.GridMinCellWidth, 1.0f, 10.0))
        AttribsChanged = true;
    if (ImGui::SliderFloat("Grid Line Width", &Attribs.GridLineWidth, 1.0f, 5.0))
        AttribsChanged = true;

    if (ImGui::ColorEdit3("Grid Major Color", Attribs.GridMajorColor.Data()))
        AttribsChanged = true;
    if (ImGui::ColorEdit3("Grid Minor Color", Attribs.GridMinorColor.Data()))
        AttribsChanged = true;

    auto ResetStateFeatureMask = [](FEATURE_FLAGS& FeatureFlags, FEATURE_FLAGS Flag, bool State) {
        if (State)
            FeatureFlags = static_cast<FEATURE_FLAGS>(FeatureFlags | Flag);
        else
            FeatureFlags = static_cast<FEATURE_FLAGS>(FeatureFlags & ~Flag);
    };

    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_AXIS_X, ActiveAxisX);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_AXIS_Y, ActiveAxisY);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_AXIS_Z, ActiveAxisZ);

    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_PLANE_YZ, ActivePlaneYZ);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_PLANE_XZ, ActivePlaneXZ);
    ResetStateFeatureMask(FeatureFlags, FEATURE_FLAG_RENDER_PLANE_XY, ActivePlaneXY);

    return AttribsChanged;
}

void CoordinateGridRenderer::AddShaderMacros(FEATURE_FLAGS FeatureFlags, ShaderMacroHelper& Macros)
{
    Macros.Add("COORDINATE_GRID_INVERTED_DEPTH", (FeatureFlags & FEATURE_FLAG_REVERSED_DEPTH) != 0);
    Macros.Add("COORDINATE_GRID_CONVERT_OUTPUT_TO_SRGB", (FeatureFlags & FEATURE_FLAG_CONVERT_TO_SRGB) != 0);

    Macros.Add("COORDINATE_GRID_AXIS_X", (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_X) != 0);
    Macros.Add("COORDINATE_GRID_AXIS_Y", (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_Y) != 0);
    Macros.Add("COORDINATE_GRID_AXIS_Z", (FeatureFlags & FEATURE_FLAG_RENDER_AXIS_Z) != 0);

    Macros.Add("COORDINATE_GRID_PLANE_YZ", (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_YZ) != 0);
    Macros.Add("COORDINATE_GRID_PLANE_XZ", (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_XZ) != 0);
    Macros.Add("COORDINATE_GRID_PLANE_XY", (FeatureFlags & FEATURE_FLAG_RENDER_PLANE_XY) != 0);
}

void CoordinateGridRenderer::RenderGridAxes(const RenderAttributes& RenderAttribs)
{
    auto& pPSO = GetPSO(RenderAttribs.FeatureFlags, RenderAttribs.pColorRTV->GetDesc().Format);
    if (!pPSO)
    {
        ShaderMacroHelper Macros;
        AddShaderMacros(RenderAttribs.FeatureFlags, Macros);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "CoordinateGridPS.psh", "ComputeGridAxesPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbGridAxesAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        GraphicsPipelineStateCreateInfoX PSOCreateInfo{"CoordinateGridRenderer::GridAxes"};
        PSOCreateInfo
            .AddShader(VS)
            .AddShader(PS)
            .AddRenderTarget(RenderAttribs.pColorRTV->GetDesc().Format)
            .SetResourceLayout(ResourceLayout)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetBlendDesc(BS_AlphaBlend)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        RenderAttribs.pDevice->CreatePipelineState(PSOCreateInfo, &pPSO);
    }

    if (!m_SRB)
    {
        ShaderResourceVariableX{pPSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER].AsBuffer());
        ShaderResourceVariableX{pPSO, SHADER_TYPE_PIXEL, "cbGridAxesAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_SETTINGS_CONSTANT_BUFFER].AsBuffer());
        pPSO->CreateShaderResourceBinding(&m_SRB, true);
    }

    ShaderResourceVariableX{m_SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());

    ITextureView* pRTVs[] = {
        RenderAttribs.pColorRTV};

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(pPSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

RefCntAutoPtr<IPipelineState>& CoordinateGridRenderer::GetPSO(FEATURE_FLAGS FeatureFlags, TEXTURE_FORMAT RTVFormat)
{
    auto Iter = m_PSOCache.find(PSOKey{FeatureFlags, RTVFormat});
    if (Iter != m_PSOCache.end())
        return Iter->second;

    auto Condition = m_PSOCache.emplace(PSOKey{FeatureFlags, RTVFormat}, RefCntAutoPtr<IPipelineState>{});
    return Condition.first->second;
}

} // namespace Diligent
