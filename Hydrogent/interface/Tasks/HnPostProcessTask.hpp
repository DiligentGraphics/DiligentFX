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
#include "HnTypes.hpp"

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineResourceSignature.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp"
#include "../../PBR/interface/PBR_Renderer.hpp"
#include "../../PostProcess/ScreenSpaceAmbientOcclusion/interface/ScreenSpaceAmbientOcclusion.hpp"
#include "../../PostProcess/ScreenSpaceReflection/interface/ScreenSpaceReflection.hpp"
#include "../../PostProcess/TemporalAntiAliasing/interface/TemporalAntiAliasing.hpp"
#include "../../PostProcess/Bloom/interface/Bloom.hpp"
#include "../../PostProcess/DepthOfField/interface/DepthOfField.hpp"
#include "../../Components/interface/CoordinateGridRenderer.hpp"

namespace Diligent
{

class PostFXContext;
class VectorFieldRenderer;

namespace HLSL
{
#include "../../../Shaders/Common/public/ShaderDefinitions.fxh"
#include "../../../Shaders/Common/public/CoordinateGridStructures.fxh"
#include "../../../Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
#include "../../../Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
#include "../../../Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
#include "../../../Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
#include "../../../Shaders/PostProcess/DepthOfField/public/DepthOfFieldStructures.fxh"
} // namespace HLSL

namespace USD
{

class HnRenderPassState;
struct HnFrameRenderTargets;
class HnRenderDelegate;

struct HnPostProcessTaskParams
{
    bool ConvertOutputToSRGB = false;

    float4 SelectionColor         = float4{1.000f, 0.675f, 0.250f, 0.5f};
    float4 OccludedSelectionColor = float4{0.375f, 0.375f, 0.125f, 0.5f};

    float SelectionOutlineWidth = 4.0f;

    /// Desaturation factor for unselected objects
    float NonselectionDesaturationFactor = 0.0f;

    // Tone mappig attribs
    int   ToneMappingMode     = 0;     // TONE_MAPPING_MODE enum
    float MiddleGray          = 0.18f; // Middle gray luminance
    float WhitePoint          = 3.0f;  // White point luminance
    float LuminanceSaturation = 1.0;   // Luminance saturation factor
    float AverageLogLum       = 0.3f;  // Average log luminance of the scene

    // Screen-space reflection scale.
    // 0 - disable SSR.
    float SSRScale = 1.f;

    // Screen-space ambient occlusion scale.
    // 0 - disable SSAO.
    float SSAOScale = 1.f;

    // Enable temporal anti-aliasing
    bool EnableTAA = false;

    // Enable depth of field
    bool EnableDOF = false;

    // Enable HDR bloom
    bool EnableBloom = false;

    ScreenSpaceReflection::FEATURE_FLAGS       SSRFeatureFlags   = ScreenSpaceReflection::FEATURE_FLAG_NONE;
    ScreenSpaceAmbientOcclusion::FEATURE_FLAGS SSAOFeatureFlags  = ScreenSpaceAmbientOcclusion::FEATURE_FLAG_NONE;
    TemporalAntiAliasing::FEATURE_FLAGS        TAAFeatureFlags   = TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER;
    DepthOfField::FEATURE_FLAGS                DOFFeatureFlags   = DepthOfField::FEATURE_FLAG_NONE;
    Bloom::FEATURE_FLAGS                       BloomFeatureFlags = Bloom::FEATURE_FLAG_NONE;
    CoordinateGridRenderer::FEATURE_FLAGS      GridFeatureFlags  = CoordinateGridRenderer::FEATURE_FLAG_NONE;

    // The number of frames to suspend temporal super-sampling when
    // rendering parameters change.
    Uint32 SuperSamplingSuspensionFrames = 8;

    HLSL::ScreenSpaceReflectionAttribs       SSR;
    HLSL::ScreenSpaceAmbientOcclusionAttribs SSAO;
    HLSL::TemporalAntiAliasingAttribs        TAA;
    HLSL::DepthOfFieldAttribs                DOF;
    HLSL::BloomAttribs                       Bloom;
    HLSL::CoordinateGridAttribs              Grid;

    constexpr HnPostProcessTaskParams() noexcept
    {
        SSR.MaxTraversalIntersections = 64;
        SSR.RoughnessChannel          = 0;
        SSR.IsRoughnessPerceptual     = true;
        SSR.RoughnessThreshold        = 0.4f;
    }

    constexpr bool operator==(const HnPostProcessTaskParams& rhs) const
    {
        // clang-format off
        return ConvertOutputToSRGB            == rhs.ConvertOutputToSRGB &&
               SelectionColor                 == rhs.SelectionColor &&
               OccludedSelectionColor         == rhs.OccludedSelectionColor &&
               SelectionOutlineWidth          == rhs.SelectionOutlineWidth &&
               NonselectionDesaturationFactor == rhs.NonselectionDesaturationFactor &&
               ToneMappingMode                == rhs.ToneMappingMode &&
               MiddleGray                     == rhs.MiddleGray &&
               WhitePoint                     == rhs.WhitePoint &&
               LuminanceSaturation            == rhs.LuminanceSaturation &&
               AverageLogLum                  == rhs.AverageLogLum &&
               SSRScale                       == rhs.SSRScale &&
               SSAOScale                      == rhs.SSAOScale &&
               EnableTAA                      == rhs.EnableTAA &&
               EnableDOF                      == rhs.EnableDOF &&
               EnableBloom					  == rhs.EnableBloom &&
               SSRFeatureFlags                == rhs.SSRFeatureFlags &&
               SSAOFeatureFlags               == rhs.SSAOFeatureFlags &&
               TAAFeatureFlags                == rhs.TAAFeatureFlags &&
               DOFFeatureFlags                == rhs.DOFFeatureFlags &&
               BloomFeatureFlags              == rhs.BloomFeatureFlags &&
               GridFeatureFlags               == rhs.GridFeatureFlags &&
               SuperSamplingSuspensionFrames  == rhs.SuperSamplingSuspensionFrames &&
               memcmp(&SSR,  &rhs.SSR,    sizeof(SSR))   == 0 &&
               memcmp(&SSAO, &rhs.SSAO,   sizeof(SSAO))  == 0 &&
               memcmp(&TAA,  &rhs.TAA,    sizeof(TAA))   == 0 &&
               memcmp(&DOF,  &rhs.DOF,    sizeof(DOF))   == 0 &&
               memcmp(&Bloom, &rhs.Bloom, sizeof(Bloom)) == 0 &&
               memcmp(&Grid,  &rhs.Grid,  sizeof(Grid))  == 0;
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

    /// Suspends temporal super-sampling for the number of frames defined in the task parameters.
    void SuspendSuperSampling();

    void ResetTAA() { m_ResetTAA = true; }

private:
    void CreateVectorFieldRenderer(TEXTURE_FORMAT RTVFormat);

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    HnPostProcessTaskParams m_Params;

    RefCntAutoPtr<IBuffer> m_PostProcessAttribsCB;

    std::unique_ptr<PostFXContext>               m_PostFXContext;
    std::unique_ptr<VectorFieldRenderer>         m_VectorFieldRenderer;
    std::unique_ptr<ScreenSpaceReflection>       m_SSR;
    std::unique_ptr<ScreenSpaceAmbientOcclusion> m_SSAO;
    std::unique_ptr<TemporalAntiAliasing>        m_TAA;
    std::unique_ptr<DepthOfField>                m_DOF;
    std::unique_ptr<Bloom>                       m_Bloom;

    ITextureView*               m_FinalColorRTV   = nullptr; // Set in Prepare()
    const HnFrameRenderTargets* m_FrameTargets    = nullptr; // Set in Prepare()
    float                       m_BackgroundDepth = 1.f;     // Set in Prepare()
    float                       m_SSRScale        = 0;       // Set in Prepare()
    float                       m_SSAOScale       = 0;       // Set in Prepare()
    bool                        m_UseTAA          = false;   // Set in Prepare()
    bool                        m_UseSSR          = false;   // Set in Prepare()
    bool                        m_UseSSAO         = false;   // Set in Prepare()
    bool                        m_UseDOF          = false;   // Set in Prepare()
    bool                        m_UseBloom        = false;   // Set in Prepare()

    bool m_ResetTAA       = true;
    bool m_AttribsCBDirty = true;

    struct SuperSamplingFactors
    {
        Uint32 Version    = ~0u;
        bool   UseSSR     = false;
        bool   UseSSAO    = false;
        bool   UseShadows = false;

        PBR_Renderer::DebugViewType DebugView  = PBR_Renderer::DebugViewType::NumDebugViews;
        HN_RENDER_MODE              RenderMode = HN_RENDER_MODE_COUNT;

        constexpr bool operator==(const SuperSamplingFactors& rhs) const
        {
            // clang-format off
            return (Version    == rhs.Version &&
                    UseSSR     == rhs.UseSSR &&
                    UseSSAO    == rhs.UseSSAO &&
                    UseShadows == rhs.UseShadows &&
                    DebugView  == rhs.DebugView &&
                    RenderMode == rhs.RenderMode);
            // clang-format on
        }
        constexpr bool operator!=(const SuperSamplingFactors& rhs) const
        {
            return !(*this == rhs);
        }
    };
    SuperSamplingFactors m_LastSuperSamplingFactors;
    Uint32               m_SuperSamplingSuspensionFrame = 0;

    struct PostProcessingTechnique
    {
        const HnPostProcessTask& PPTask;

        RefCntAutoPtr<IPipelineState>             PSO{};
        RefCntAutoPtr<IPipelineResourceSignature> PRS{};

        IShaderResourceBinding* CurrSRB = nullptr;

        struct ShaderResources
        {
            RefCntAutoPtr<IShaderResourceBinding> SRB;

            struct ShaderVariables
            {
                ShaderResourceVariableX Color;
                ShaderResourceVariableX Depth;
                ShaderResourceVariableX SelectionDepth;
                ShaderResourceVariableX ClosestSelectedLocation;
                ShaderResourceVariableX SSR;
                ShaderResourceVariableX SSAO;
                ShaderResourceVariableX SpecularIBL;
                ShaderResourceVariableX Normal;
                ShaderResourceVariableX BaseColor;
                ShaderResourceVariableX Material;
            };
            ShaderVariables Vars{};
        };

        // Two set of resources for each of the two depth buffers
        std::array<ShaderResources, 2> Resources{};

        PostProcessingTechnique(const HnPostProcessTask& _PPTask) :
            PPTask{_PPTask}
        {}
        void PreparePRS();
        void PreparePSO(TEXTURE_FORMAT RTVFormat);
        void PrepareSRB(ITextureView* pClosestSelectedLocationSRV, Uint32 FrameIdx);

    private:
        bool ConvertOutputToSRGB = false;
        int  ToneMappingMode     = 0;

        CoordinateGridRenderer::FEATURE_FLAGS GridFeatureFlags = CoordinateGridRenderer::FEATURE_FLAG_NONE;
    } m_PostProcessTech;

    struct CopyFrameTechnique
    {
        const HnPostProcessTask& PPTask;

        RefCntAutoPtr<IPipelineState>             PSO{};
        RefCntAutoPtr<IPipelineResourceSignature> PRS{};

        IShaderResourceBinding* CurrSRB = nullptr;

        struct ShaderResources
        {
            RefCntAutoPtr<IShaderResourceBinding> SRB{};

            struct ShaderVariables
            {
                ShaderResourceVariableX Color;
                ShaderResourceVariableX Depth;
            };
            ShaderVariables Vars{};
        };
        std::array<ShaderResources, 2> Resources{};

        CopyFrameTechnique(const HnPostProcessTask& _PPTask) :
            PPTask{_PPTask}
        {}
        void PreparePRS();
        void PreparePSO(TEXTURE_FORMAT RTVFormat);
        void PrepareSRB(Uint32 FrameIdx);

    private:
        bool ConvertOutputToSRGB = false;
        int  ToneMappingMode     = 0;

        CoordinateGridRenderer::FEATURE_FLAGS GridFeatureFlags = CoordinateGridRenderer::FEATURE_FLAG_NONE;
    } m_CopyFrameTech;
};

} // namespace USD

} // namespace Diligent
