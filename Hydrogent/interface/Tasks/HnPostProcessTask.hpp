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

#pragma once

#include <memory>

#include "HnTask.hpp"

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

class VectorFieldRenderer;
class ScreenSpaceReflection;

namespace USD
{

class HnRenderPassState;
struct HnFramebufferTargets;

struct HnPostProcessTaskParams
{
    bool ConvertOutputToSRGB = false;

    float4 SelectionColor = float4{0.75f, 0.75f, 0.25f, 0.5f};

    float SelectionOutlineWidth = 4.0f;

    /// Desaturation factor for unselected objects
    float NonselectionDesaturationFactor = 0.0f;

    // Tone mappig attribs
    int   ToneMappingMode     = 0;     // TONE_MAPPING_MODE enum
    float MiddleGray          = 0.18f; // Middle gray luminance
    float WhitePoint          = 3.0f;  // White point luminance
    float LuminanceSaturation = 1.0;   // Luminance saturation factor
    float AverageLogLum       = 0.3f;  // Average log luminance of the scene

    constexpr bool operator==(const HnPostProcessTaskParams& rhs) const
    {
        // clang-format off
        return ConvertOutputToSRGB == rhs.ConvertOutputToSRGB &&
               SelectionColor      == rhs.SelectionColor &&
               SelectionOutlineWidth == rhs.SelectionOutlineWidth &&
               NonselectionDesaturationFactor == rhs.NonselectionDesaturationFactor &&
               ToneMappingMode     == rhs.ToneMappingMode &&
               MiddleGray          == rhs.MiddleGray &&
               WhitePoint          == rhs.WhitePoint &&
               LuminanceSaturation == rhs.LuminanceSaturation &&
               AverageLogLum       == rhs.AverageLogLum;
        // clang-format on
    }

    constexpr bool operator!=(const HnPostProcessTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Performs post processing:
/// - Tone mapping
/// - Selection outline
/// - Converts output to sRGB, if needed
class HnPostProcessTask final : public HnTask
{
public:
    HnPostProcessTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnPostProcessTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void PreparePSO(TEXTURE_FORMAT RTVFormat);
    void PrepareSRB(const HnRenderPassState& RPState, ITextureView* ClosestSelectedLocationSRV);
    void CreateVectorFieldRenderer(TEXTURE_FORMAT RTVFormat);

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    HnPostProcessTaskParams m_Params;

    bool m_PsoIsDirty = true;

    RefCntAutoPtr<IPipelineState> m_PSO;
    RefCntAutoPtr<IBuffer>        m_PostProcessAttribsCB;

    std::unique_ptr<VectorFieldRenderer>   m_VectorFieldRenderer;
    std::unique_ptr<ScreenSpaceReflection> m_SSR;


    ITextureView*               m_FinalColorRTV = nullptr; // Set in Prepare()
    const HnFramebufferTargets* m_FBTargets     = nullptr; // Set in Prepare()
    float                       m_ClearDepth    = 1.f;     // Set in Prepare()

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    struct ShaderVariables
    {
        IShaderResourceVariable* Color                   = nullptr;
        IShaderResourceVariable* Depth                   = nullptr;
        IShaderResourceVariable* SelectionDepth          = nullptr;
        IShaderResourceVariable* ClosestSelectedLocation = nullptr;
        IShaderResourceVariable* SSR                     = nullptr;

        constexpr operator bool() const
        {
            return Color != nullptr && Depth != nullptr && SelectionDepth != nullptr && ClosestSelectedLocation;
        }
    };
    ShaderVariables m_ShaderVars;
};

} // namespace USD

} // namespace Diligent
