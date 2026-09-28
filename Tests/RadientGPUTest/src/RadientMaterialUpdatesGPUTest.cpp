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

#include "Radient.h"
#include "RadientTypesX.hpp"
#include "RadientStandardMaterialParameters.h"

#include "GPUTestingEnvironment.hpp"
#include "RadientMaterialTestHelpers.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstring>
#include <thread>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

constexpr Uint32        OutputWidth  = 256;
constexpr Uint32        OutputHeight = 64;
constexpr RadientFloat4 UpdatedColor{0.25f, 0.5f, 0.75f, 1.f};

using SampledColors = std::array<std::array<Uint8, 4>, 4>;

RefCntAutoPtr<IRadientDataBlob> MakeBlob(const void* pData, size_t Size)
{
    RadientDataBlobCreateInfo CI;
    CI.pData = pData;
    CI.Size  = Size;
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pBlob), RADIENT_STATUS_OK);
    return pBlob;
}

template <typename ValueType>
RADIENT_STATUS SetParameter(IRadientMaterialDefinitionAsset& Definition,
                            IRadientMaterialWriter&          Writer,
                            const char*                      Name,
                            const ValueType&                 Value)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           Status = Definition.FindParameter(Name, &Handle);
    return Status == RADIENT_STATUS_OK ? Writer.SetParameter(Handle, Value) : Status;
}

class MaterialUpdateScene
{
public:
    ~MaterialUpdateScene()
    {
        if (pAssets != nullptr)
            pAssets->Stop(pContext);
    }

    void Initialize()
    {
        GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
        pDevice                                   = pEnvironment->GetDevice();
        pContext                                  = pEnvironment->GetDeviceContext();
        ASSERT_NE(pDevice, nullptr);
        ASSERT_NE(pContext, nullptr);

        RadientEngineCreateInfo EngineCI;
        EngineCI.Backend.pDevice                     = pDevice;
        EngineCI.Backend.pImmediateContext           = pContext;
        EngineCI.WorkerThreadCount                   = 1;
        EngineCI.Resources.IndexBufferSize           = 4096;
        EngineCI.Resources.VertexPoolSize            = 64;
        EngineCI.Resources.TextureAtlasSize          = 64;
        EngineCI.Resources.TextureAtlasMipLevel0Size = 64 * 64 * 4;
        ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
        ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);

        RadientRendererDesc RendererDesc;
        RendererDesc.EnableAsyncPipelineCompilation = False;
        ASSERT_EQ(pEngine->CreateRenderer(RendererDesc, &pRenderer), RADIENT_STATUS_OK);
        ASSERT_EQ(pEngine->CreateScene({}, &pScene), RADIENT_STATUS_OK);

        // Constant UVs sample the middle of either wide color band. The alpha
        // lets the same scene verify cutoff changes without changing pipelines.
        std::array<Uint8, 8 * 8 * 4> Pixels{};
        for (Uint32 Y = 0; Y < 8; ++Y)
        {
            for (Uint32 X = 0; X < 8; ++X)
            {
                const Uint32 Offset = (Y * 8 + X) * 4;
                Pixels[Offset + 0]  = X < 4 ? 255 : 32;
                Pixels[Offset + 1]  = X < 4 ? 255 : 200;
                Pixels[Offset + 2]  = X < 4 ? 255 : 80;
                Pixels[Offset + 3]  = 128;
            }
        }
        const RefCntAutoPtr<IRadientDataBlob> pPixels = MakeBlob(Pixels.data(), Pixels.size());
        ASSERT_NE(pPixels, nullptr);
        RadientTextureDataX TextureData{8, 8, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
        TextureData.AddMip(pPixels);
        RadientTextureLoadInfoX LoadInfo;
        LoadInfo.SetTextureData(TextureData);
        const RADIENT_STATUS TextureStatus = pAssets->LoadTexture(LoadInfo, &pTexture);
        ASSERT_TRUE(TextureStatus == RADIENT_STATUS_OK || TextureStatus == RADIENT_STATUS_PENDING);
        ASSERT_NE(pTexture, nullptr);

        // The first quad changes at runtime. The other three are independent
        // references initialized to the states expected after each update.
        for (Uint32 Index = 0; Index < Materials.size(); ++Index)
        {
            const RadientFloat4                         Color  = Index == 0 ? RadientFloat4{1.f, 1.f, 1.f, 1.f} : UpdatedColor;
            const RadientFloat2                         Bias   = {Index < 2 ? 0.25f : 0.75f, 0.5f};
            const Float32                               Cutoff = Index < 3 ? 0.25f : 0.75f;
            RadientStandardMaterialDefinitionCreateInfo DefinitionCI;
            DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
            ASSERT_EQ(CreateStandardMaterialAsset(
                          *pAssets, DefinitionCI,
                          [&](IRadientMaterialDefinitionAsset& Definition, IRadientMaterialWriter& Writer) {
                              RADIENT_STATUS                           Status = SetParameter(Definition, Writer, RadientStandardMaterialBaseColorFactorName, Color);
                              RadientStandardMaterialTextureParameters TextureParameters{pTexture};
                              TextureParameters.UVScaleAndRotation = {0.f, 0.f, 0.f, 0.f};
                              TextureParameters.UVBias             = Bias;
                              if (RADIENT_SUCCEEDED(Status))
                                  Status = SetStandardMaterialTextureParameters(Definition, Writer, RadientStandardMaterialBaseColorTextureParameterNames, TextureParameters);
                              RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurface{&Writer, IID_RadientSurfaceMaterialWriter};
                              if (pSurface == nullptr)
                                  return RADIENT_STATUS_INVALID_OPERATION;
                              if (RADIENT_SUCCEEDED(Status))
                                  Status = pSurface->SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE_MASKED);
                              if (RADIENT_SUCCEEDED(Status))
                                  Status = pSurface->SetDoubleSided(True);
                              if (RADIENT_SUCCEEDED(Status))
                                  Status = pSurface->SetAlphaCutoff(Cutoff);
                              return Status;
                          },
                          &Materials[Index]),
                      RADIENT_STATUS_OK);
        }

        struct Vertex
        {
            RadientFloat3 Position;
            RadientFloat2 UV;
        };
        const Vertex Vertices[] = {
            {{-0.4f, -0.4f, 0.f}, {0.f, 1.f}},
            {{0.4f, -0.4f, 0.f}, {1.f, 1.f}},
            {{0.4f, 0.4f, 0.f}, {1.f, 0.f}},
            {{-0.4f, 0.4f, 0.f}, {0.f, 0.f}},
        };
        const Uint16                          Indices[] = {0, 1, 2, 0, 2, 3};
        const RefCntAutoPtr<IRadientDataBlob> pVertices = MakeBlob(Vertices, sizeof(Vertices));
        const RefCntAutoPtr<IRadientDataBlob> pIndices  = MakeBlob(Indices, sizeof(Indices));
        ASSERT_NE(pVertices, nullptr);
        ASSERT_NE(pIndices, nullptr);
        IRadientDataBlob*        VertexBuffers[] = {pVertices};
        RadientVertexLayoutDescX Layout;
        Layout.AddBuffer(sizeof(Vertex));
        Layout.AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
        Layout.AddAttribute("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2);

        RefCntAutoPtr<IRadientSceneWriter> pSceneWriter;
        ASSERT_EQ(pEngine->CreateSceneWriter(pScene, &pSceneWriter), RADIENT_STATUS_OK);
        for (Uint32 Index = 0; Index < Materials.size(); ++Index)
        {
            RadientMeshPrimitiveCreateInfo Primitive;
            Primitive.IndexCount = 6;
            Primitive.pMaterial  = Materials[Index];
            RadientMeshCreateInfo MeshCI;
            MeshCI.VertexData     = {Layout, VertexBuffers, 4};
            MeshCI.IndexData      = {pIndices, 6, RADIENT_INDEX_TYPE_UINT16};
            MeshCI.pPrimitives    = &Primitive;
            MeshCI.PrimitiveCount = 1;
            RefCntAutoPtr<IRadientMeshAsset> pMesh;
            const RADIENT_STATUS             MeshStatus = pAssets->CreateMesh(MeshCI, &pMesh);
            ASSERT_TRUE(MeshStatus == RADIENT_STATUS_OK || MeshStatus == RADIENT_STATUS_PENDING);
            ASSERT_NE(pMesh, nullptr);

            RadientEntityDesc EntityDesc;
            EntityDesc.Transform.Position.x = static_cast<float>(Index) - 1.5f;
            RadientEntityID Entity          = InvalidRadientEntityID;
            ASSERT_EQ(pSceneWriter->CreateEntity(EntityDesc, Entity), RADIENT_STATUS_OK);
            ASSERT_EQ(pSceneWriter->SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
            ASSERT_EQ(pSceneWriter->SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
        }

        RadientEntityDesc CameraDesc;
        CameraDesc.Transform.Position.z = 2.f;
        RadientEntityID CameraEntity    = InvalidRadientEntityID;
        ASSERT_EQ(pSceneWriter->CreateEntity(CameraDesc, CameraEntity), RADIENT_STATUS_OK);
        RadientCameraComponent Camera;
        Camera.Projection         = RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
        Camera.HorizontalAperture = 4.f;
        Camera.VerticalAperture   = 1.f;
        ASSERT_EQ(pSceneWriter->SetCamera(CameraEntity, Camera), RADIENT_STATUS_OK);
        ASSERT_EQ(pSceneWriter->CommitChanges(), RADIENT_STATUS_OK);

        TextureDesc OutputDesc;
        OutputDesc.Name      = "Material update output";
        OutputDesc.Type      = RESOURCE_DIM_TEX_2D;
        OutputDesc.Width     = OutputWidth;
        OutputDesc.Height    = OutputHeight;
        OutputDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
        OutputDesc.BindFlags = BIND_RENDER_TARGET;
        pDevice->CreateTexture(OutputDesc, nullptr, &pOutput);
        ASSERT_NE(pOutput, nullptr);
        OutputDesc.Name           = "Material update readback";
        OutputDesc.BindFlags      = BIND_NONE;
        OutputDesc.Usage          = USAGE_STAGING;
        OutputDesc.CPUAccessFlags = CPU_ACCESS_READ;
        pDevice->CreateTexture(OutputDesc, nullptr, &pReadback);
        ASSERT_NE(pReadback, nullptr);

        RadientRenderTargetDesc TargetDesc;
        TargetDesc.Size      = {OutputWidth, OutputHeight};
        TargetDesc.pColorRTV = pOutput->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        ASSERT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_OK);
        RadientViewDesc ViewDesc;
        ViewDesc.pScene                       = pScene;
        ViewDesc.Camera                       = CameraEntity;
        ViewDesc.pRenderTarget                = pTarget;
        ViewDesc.ClearColor                   = {0.f, 0.f, 0.f, 1.f};
        ViewDesc.EnableIBL                    = False;
        ViewDesc.ToneMapping.Mode             = RADIENT_TONE_MAPPING_MODE_NONE;
        ViewDesc.TemporalAntiAliasing.Enabled = False;
        ASSERT_EQ(pRenderer->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);

        const std::chrono::steady_clock::time_point Deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        RADIENT_STATUS                              Status   = RADIENT_STATUS_PENDING;
        while (Status == RADIENT_STATUS_PENDING && std::chrono::steady_clock::now() < Deadline)
        {
            Status = RenderFrame();
            ASSERT_FALSE(RADIENT_FAILED(Status));
            if (Status == RADIENT_STATUS_PENDING)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        ASSERT_EQ(Status, RADIENT_STATUS_OK);
        ASSERT_EQ(RenderFrame(), RADIENT_STATUS_OK);
    }

    RADIENT_STATUS RenderFrame()
    {
        RadientFrameAttribs Frame;
        Frame.pDeviceContext  = pContext;
        Frame.Time            = Time;
        Frame.DeltaTime       = 1.0 / 60.0;
        RADIENT_STATUS Status = pRenderer->BeginFrame(Frame);
        if (Status != RADIENT_STATUS_OK)
            return Status;
        const RADIENT_STATUS RenderStatus = pRenderer->Render({pView});
        const RADIENT_STATUS EndStatus    = pRenderer->EndFrame();
        pContext->Flush();
        pContext->FinishFrame();
        pContext->WaitForIdle();
        pDevice->ReleaseStaleResources();
        Time += Frame.DeltaTime;
        return RenderStatus == RADIENT_STATUS_OK ? EndStatus : RenderStatus;
    }

    void ReadColors(SampledColors& Colors)
    {
        CopyTextureAttribs Copy;
        Copy.pSrcTexture              = pOutput;
        Copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Copy.pDstTexture              = pReadback;
        Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        pContext->CopyTexture(Copy);
        pContext->WaitForIdle();
        MappedTextureSubresource Mapped;
        pContext->MapTextureSubresource(pReadback, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, Mapped);
        ASSERT_NE(Mapped.pData, nullptr);
        for (Uint32 Index = 0; Index < Colors.size(); ++Index)
        {
            const Uint32 X      = (2 * Index + 1) * OutputWidth / 8;
            const Uint8* pPixel = static_cast<const Uint8*>(Mapped.pData) + (OutputHeight / 2) * Mapped.Stride + X * 4;
            std::memcpy(Colors[Index].data(), pPixel, Colors[Index].size());
        }
        pContext->UnmapTextureSubresource(pReadback, 0, 0);
    }

    IRenderDevice*                                      pDevice  = nullptr;
    IDeviceContext*                                     pContext = nullptr;
    RefCntAutoPtr<IRadientEngine>                       pEngine;
    RefCntAutoPtr<IRadientAssetManager>                 pAssets;
    RefCntAutoPtr<IRadientRenderer>                     pRenderer;
    RefCntAutoPtr<IRadientScene>                        pScene;
    RefCntAutoPtr<IRadientTextureAsset>                 pTexture;
    std::array<RefCntAutoPtr<IRadientMaterialAsset>, 4> Materials;
    RefCntAutoPtr<ITexture>                             pOutput;
    RefCntAutoPtr<ITexture>                             pReadback;
    RefCntAutoPtr<IRadientRenderTarget>                 pTarget;
    RefCntAutoPtr<IRadientView>                         pView;
    double                                              Time = 0.0;
};

void ExpectSameColor(const std::array<Uint8, 4>& Actual, const std::array<Uint8, 4>& Reference)
{
    for (Uint32 Channel = 0; Channel < 3; ++Channel)
        EXPECT_NEAR(static_cast<int>(Actual[Channel]), static_cast<int>(Reference[Channel]), 2) << "Channel " << Channel;
}

TEST(RadientMaterialUpdatesGPUTest, UpdatesRenderedShaderPropertiesWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    MaterialUpdateScene                Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize());
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
    ASSERT_EQ(SetParameter(Definition, *pWriter, RadientStandardMaterialBaseColorFactorName, UpdatedColor), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    EXPECT_EQ(Material.GetVersion(), InitialVersion + 1);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[1]);

    // UV scale and bias are shader data too; move from the white band to the colored band
    // without replacing the texture or changing the pre-existing drawables.
    const std::array<Float32, 4> UpdatedTransform{0.5f, 0.f, 0.f, 0.5f};
    const RadientFloat2          UpdatedBias{0.5f, 0.25f};
    ASSERT_EQ(SetParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVScaleAndRotationName, UpdatedTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(SetParameter(Definition, *pWriter, RadientStandardMaterialBaseColorTextureUVBiasName, UpdatedBias), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.RenderFrame(), RADIENT_STATUS_OK);
    ASSERT_NO_FATAL_FAILURE(Scene.ReadColors(Colors));
    ExpectSameColor(Colors[0], Colors[2]);
    EXPECT_GT(Colors[1][0], Colors[0][0] + 20);

    // Unsupported texture/render-state changes reject the complete commit. The
    // valid color assignment in the same commit must not leak into rendering.
    RadientMaterialParameterHandle TextureHandle;
    ASSERT_EQ(Definition.FindParameter(RadientStandardMaterialBaseColorTextureName, &TextureHandle), RADIENT_STATUS_OK);
    ASSERT_EQ(SetParameter(Definition, *pWriter, RadientStandardMaterialBaseColorFactorName, RadientFloat4{1.f, 0.f, 0.f, 1.f}), RADIENT_STATUS_OK);
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


// Evaluate a material clip on an already-renderable textured quad. Its color,
// texture offset, and cutoff must reach the next frame without any scene edits.
TEST(RadientMaterialUpdatesGPUTest, AnimatesRenderedMaterialPropertiesWithoutSceneChanges)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    MaterialUpdateScene                Scene;
    ASSERT_NO_FATAL_FAILURE(Scene.Initialize());
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
