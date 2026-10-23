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

#include "RadientQuadTestScene.hpp"

#include "GPUTestingEnvironment.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"

#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

constexpr RadientFloat4 UpdatedColor = DefaultQuadColor;

QuadSetups MakeDefaultQuadSetups()
{
    QuadSetups Setups;
    Setups[0].Color       = {1.f, 1.f, 1.f, 1.f};
    Setups[2].UVBias.x    = 0.75f;
    Setups[3].UVBias.x    = 0.75f;
    Setups[3].AlphaCutoff = 0.75f;
    return Setups;
}

TEST(RadientMaterialUpdatesGPUTest, UpdatesRenderedShaderPropertiesWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    RadientQuadTestScene               Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(MakeDefaultQuadSetups()));
    const RadientSceneRevisions           SceneRevisions = Scene.pScene->GetSceneRevisions();
    IRadientMaterialAsset&                Material       = *Scene.Materials[0];
    IRadientMaterialDefinitionAsset&      Definition     = *Material.GetDefinition();
    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(Material.CreateWriter(&pWriter), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{pWriter, IID_RadientSurfaceMaterialWriter};
    ASSERT_NE(pSurfaceWriter, nullptr);

    SampledColors Colors;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ASSERT_GT(Colors[0][0], Colors[1][0] + 20);
    ASSERT_GT(Colors[1][0], Colors[2][0] + 20);
    ASSERT_GT(Colors[2][1], 20);
    ASSERT_EQ(Colors[3][0], 0);
    ASSERT_EQ(Colors[3][1], 0);
    ASSERT_EQ(Colors[3][2], 0);

    // A value-only commit must reach the next frame with no scene edit, resource
    // replacement, or transition back to pending rendering.
    const Uint64 InitialVersion = Material.GetVersion();
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorFactorName, UpdatedColor), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(Material.GetVersion(), InitialVersion + 1);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[1]);

    // UV scale and bias are shader data too; move from the white band to the colored band
    // without replacing the texture or changing the pre-existing drawables.
    const RadientFloat2 UpdatedScale{0.5f, 0.5f};
    const RadientFloat2 UpdatedBias{0.5f, 0.25f};
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVScaleName, UpdatedScale), RADIENT_STATUS_OK);
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVBiasName, UpdatedBias), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[2]);
    EXPECT_GT(Colors[1][0], Colors[0][0] + 20);

    // Rotate the scaled UVs into the white band, then restore the previous
    // rotation. Both edits retain the same texture, scale, bias, and drawables.
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVRotationName, -PI_F / 2.f), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[1]);
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVRotationName, 0.f), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[2]);

    // Unsupported texture changes reject the complete commit. The valid color
    // and surface-mode assignments in the same commit must not leak into rendering.
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(Definition.FindParameter(RadientStandardMaterialBaseColorTextureName, &TextureHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(SetMaterialParameter(Definition, *pWriter, RadientStandardMaterialBaseColorFactorName, RadientFloat4{1.f, 0.f, 0.f, 1.f}), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetTexture(TextureHandle, 0, nullptr), RADIENT_STATUS_OK);
    ASSERT_EQ(pSurfaceWriter->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_OPAQUE), RADIENT_STATUS_OK);
    const Uint64 BeforeRejectedCommit = Material.GetVersion();
    EXPECT_EQ(pWriter->Commit(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Material.GetVersion(), BeforeRejectedCommit);
    EXPECT_EQ(GetMaterialParameter<RadientFloat4>(Material, RadientStandardMaterialBaseColorFactorName), UpdatedColor);
    RefCntAutoPtr<IRadientTextureAsset> pRetainedTexture;
    ASSERT_EQ(Material.GetTexture(TextureHandle, 0, &pRetainedTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(pRetainedTexture, Scene.pTexture);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{&Material, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);
    EXPECT_EQ(pSurface->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_MASKED);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[2]);

    // Discard the rejected writer's retained changes, then update only cutoff.
    // The masked pipeline stays the same, but the quad must disappear.
    pSurfaceWriter.Release();
    pWriter.Release();
    ASSERT_EQ(Material.CreateWriter(&pWriter), RADIENT_STATUS_OK);
    pSurfaceWriter = RefCntAutoPtr<IRadientSurfaceMaterialWriter>{pWriter, IID_RadientSurfaceMaterialWriter};
    ASSERT_EQ(pSurfaceWriter->SetAlphaCutoff(0.75f), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[3]);
    EXPECT_EQ(Scene.pScene->GetSceneRevisions(), SceneRevisions);
}


// Compare the edited quad against independently initialized opaque, transparent,
// and discarded masked quads. Only the first material changes between frames.
TEST(RadientMaterialUpdatesGPUTest, UpdatesRenderedSurfaceModeWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (QuadSetup& Setup : Setups)
    {
        Setup.DoubleSided = False;
    }
    Setups[0].SurfaceMode = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Setups[0].AlphaCutoff = 0.75f;
    Setups[1].SurfaceMode = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Setups[2].SurfaceMode = RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT;
    Setups[3].AlphaCutoff = 0.75f;
    RadientQuadTestScene Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups, True));

    const RadientSceneRevisions           SceneRevisions = Scene.pScene->GetSceneRevisions();
    IRadientMaterialAsset&                Material       = *Scene.Materials[0];
    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(Material.CreateWriter(&pWriter), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{pWriter, IID_RadientSurfaceMaterialWriter};
    RefCntAutoPtr<IRadientSurfaceMaterialAsset>  pSurface{&Material, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceWriter, nullptr);
    ASSERT_NE(pSurface, nullptr);

    SampledColors ReferenceColors;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(ReferenceColors));
    ExpectSameColor(ReferenceColors[0], ReferenceColors[1]);
    ASSERT_GT(ReferenceColors[1][0], ReferenceColors[2][0] + 20);
    ASSERT_GT(ReferenceColors[2][0], 20);
    ASSERT_EQ(ReferenceColors[3][0], 0);
    ASSERT_EQ(ReferenceColors[3][1], 0);
    ASSERT_EQ(ReferenceColors[3][2], 0);

    struct SurfaceUpdate
    {
        RADIENT_MATERIAL_SURFACE_MODE Mode;
        Float32                       Cutoff;
        Bool                          DoubleSided;
        Uint32                        ReferenceIndex;
    };
    // Double-sided variants were not requested by the initial single-sided
    // references, so these edits also exercise preparation of new pipelines.
    const SurfaceUpdate Updates[] = {
        {RADIENT_MATERIAL_SURFACE_MODE_MASKED, 0.75f, True, 3},
        {RADIENT_MATERIAL_SURFACE_MODE_MASKED, 0.25f, True, 1},
        {RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT, 0.75f, True, 2},
        {RADIENT_MATERIAL_SURFACE_MODE_OPAQUE, 0.75f, True, 1},
        {RADIENT_MATERIAL_SURFACE_MODE_MASKED, 0.75f, False, 3},
    };
    for (const SurfaceUpdate& Update : Updates)
    {
        SCOPED_TRACE(static_cast<int>(Update.Mode));
        SCOPED_TRACE(Update.Cutoff);
        const Uint64 PreviousVersion = Material.GetVersion();
        ASSERT_EQ(pSurfaceWriter->SetSurfaceMode(Update.Mode), RADIENT_STATUS_OK);
        ASSERT_EQ(pSurfaceWriter->SetAlphaCutoff(Update.Cutoff), RADIENT_STATUS_OK);
        ASSERT_EQ(pSurfaceWriter->SetDoubleSided(Update.DoubleSided), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        EXPECT_EQ(Material.GetVersion(), PreviousVersion + 1);
        EXPECT_EQ(pSurface->GetSurfaceMode(), Update.Mode);
        EXPECT_FLOAT_EQ(pSurface->GetAlphaCutoff(), Update.Cutoff);
        EXPECT_EQ(pSurface->IsDoubleSided(), Update.DoubleSided);
        ASSERT_NO_FATAL_FAILURE(Scene.RenderUntilReady(&Material));

        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
        ExpectSameColor(Colors[0], ReferenceColors[Update.ReferenceIndex]);
        for (Uint32 Index = 1; Index < Colors.size(); ++Index)
        {
            ExpectSameColor(Colors[Index], ReferenceColors[Index]);
        }
        EXPECT_EQ(Scene.pScene->GetSceneRevisions(), SceneRevisions);
    }
}

// Reverse the first three quads so the camera sees their back faces. Static
// culled/double-sided references distinguish culling changes from color updates.
TEST(RadientMaterialUpdatesGPUTest, UpdatesRenderedDoubleSidedWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    QuadSetups                         Setups;
    for (Uint32 Index = 0; Index < Setups.size(); ++Index)
    {
        Setups[Index].SurfaceMode = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
        Setups[Index].DoubleSided = Index == 1 ? True : False;
        Setups[Index].BackFacing  = Index < 3;
    }
    RadientQuadTestScene Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(Setups, True));

    const RadientSceneRevisions           SceneRevisions = Scene.pScene->GetSceneRevisions();
    IRadientMaterialAsset&                Material       = *Scene.Materials[0];
    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(Material.CreateWriter(&pWriter), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{pWriter, IID_RadientSurfaceMaterialWriter};
    RefCntAutoPtr<IRadientSurfaceMaterialAsset>  pSurface{&Material, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceWriter, nullptr);
    ASSERT_NE(pSurface, nullptr);

    SampledColors ReferenceColors;
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(ReferenceColors));
    ExpectSameColor(ReferenceColors[0], ReferenceColors[2]);
    ExpectSameColor(ReferenceColors[1], ReferenceColors[3]);
    ASSERT_GT(ReferenceColors[1][0], 20);
    ASSERT_EQ(ReferenceColors[2][0], 0);
    ASSERT_EQ(ReferenceColors[2][1], 0);
    ASSERT_EQ(ReferenceColors[2][2], 0);

    const Bool DoubleSidedStates[] = {True, False, True};
    for (Bool DoubleSided : DoubleSidedStates)
    {
        SCOPED_TRACE(DoubleSided);
        const Uint64 PreviousVersion = Material.GetVersion();
        ASSERT_EQ(pSurfaceWriter->SetDoubleSided(DoubleSided), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        EXPECT_EQ(Material.GetVersion(), PreviousVersion + 1);
        EXPECT_EQ(pSurface->IsDoubleSided(), DoubleSided);
        ASSERT_NO_FATAL_FAILURE(Scene.RenderUntilReady(&Material));

        SampledColors Colors;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
        ExpectSameColor(Colors[0], ReferenceColors[DoubleSided ? 1 : 2]);
        for (Uint32 Index = 1; Index < Colors.size(); ++Index)
        {
            ExpectSameColor(Colors[Index], ReferenceColors[Index]);
        }
        EXPECT_EQ(Scene.pScene->GetSceneRevisions(), SceneRevisions);
    }
}

// Evaluate a material clip on an already-renderable textured quad. Its color,
// texture offset, and cutoff must reach the next frame without any scene edits.
TEST(RadientMaterialUpdatesGPUTest, AnimatesRenderedMaterialPropertiesWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    RadientQuadTestScene               Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize(MakeDefaultQuadSetups()));
    const RadientSceneRevisions SceneRevisions = Scene.pScene->GetSceneRevisions();
    IRadientMaterialAsset&      Material       = *Scene.Materials[0];

    const Float32                    Times[]   = {0.f, 1.f, 2.f, 3.f};
    const RadientFloat4              Colors[]  = {{1.f, 1.f, 1.f, 1.f}, UpdatedColor, UpdatedColor, UpdatedColor};
    const RadientFloat2              Offsets[] = {{0.25f, 0.5f}, {0.25f, 0.5f}, {0.75f, 0.5f}, {0.75f, 0.5f}};
    const Float32                    Cutoffs[] = {0.25f, 0.25f, 0.25f, 0.75f};
    const RadientAnimationTargetDesc Targets[] = {
        {RadientMaterialAnimationSchemaID, 0, "Animated quad"},
        {RadientSurfaceMaterialAnimationSchemaID, 0, "Animated quad"},
    };
    const RadientAnimationSamplerDesc Samplers[] = {
        {{RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, 1}, RADIENT_ANIMATION_INTERPOLATION_LINEAR, Times, Colors, sizeof(Colors), 4},
        {{RADIENT_ANIMATION_VALUE_TYPE_FLOAT2, 1}, RADIENT_ANIMATION_INTERPOLATION_LINEAR, Times, Offsets, sizeof(Offsets), 4},
        {{RADIENT_ANIMATION_VALUE_TYPE_FLOAT, 1}, RADIENT_ANIMATION_INTERPOLATION_STEP, Times, Cutoffs, sizeof(Cutoffs), 4},
    };
    const RadientAnimationChannelDesc Channels[] = {
        {0, RadientStandardMaterialBaseColorFactorName, 0, 0},
        {0, RadientStandardMaterialBaseColorTextureUVBiasName, 0, 1},
        {1, RadientSurfaceMaterialAlphaCutoffPropertyName, 0, 2},
    };
    RadientAnimationClipDesc ClipDesc;
    ClipDesc.Duration     = 3.f;
    ClipDesc.pTargets     = Targets;
    ClipDesc.TargetCount  = 2;
    ClipDesc.pSamplers    = Samplers;
    ClipDesc.SamplerCount = 3;
    ClipDesc.pChannels    = Channels;
    ClipDesc.ChannelCount = 3;
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(Scene.pAssets->CreateAnimationClip(ClipDesc, &pClip), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAnimationDestination> pDestination{&Material, IID_RadientAnimationDestination};
    ASSERT_NE(pDestination, nullptr);
    const RadientAnimationDestinationMappingDesc Mappings[] = {{0, 0}, {1, 0}};
    const RadientAnimationDestinationDesc        Destination{pDestination, Mappings, 2};
    const RadientAnimationBindingDesc            BindingDesc{&Destination, 1};
    RefCntAutoPtr<IRadientAnimationBinding>      pBinding;
    ASSERT_EQ(pClip->CreateBinding(BindingDesc, &pBinding), RADIENT_STATUS_OK);

    for (Uint32 Frame = 1; Frame <= 3; ++Frame)
    {
        const Uint64                 PreviousVersion = Material.GetVersion();
        RadientAnimationEvaluateInfo EvaluateInfo;
        EvaluateInfo.Time = static_cast<Float32>(Frame);
        ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);
        EXPECT_EQ(Material.GetVersion(), PreviousVersion + 1);
        ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
        SampledColors Result;
        ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Result));
        ExpectSameColor(Result[0], Result[Frame]);
        EXPECT_EQ(Scene.pScene->GetSceneRevisions(), SceneRevisions);

        // Repeating the same animation sample produces no material revision.
        ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);
        EXPECT_EQ(Material.GetVersion(), PreviousVersion + 1);
    }
}

} // namespace
