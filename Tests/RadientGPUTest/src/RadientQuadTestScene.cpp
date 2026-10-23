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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUTestingEnvironment.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTypesX.hpp"

#include "gtest/gtest.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace Diligent
{

namespace Testing
{

namespace RadientGPUTest
{

namespace
{

constexpr Uint32 OutputWidth  = 256;
constexpr Uint32 OutputHeight = 64;

RefCntAutoPtr<IRadientDataBlob> MakeBlob(const void* pData, size_t Size)
{
    RadientDataBlobCreateInfo CI;
    CI.pData = pData;
    CI.Size  = Size;
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pBlob), RADIENT_STATUS_OK);
    return pBlob;
}

} // namespace

RadientQuadTestScene::~RadientQuadTestScene()
{
    if (pAssets != nullptr)
        pAssets->Stop(pContext);
}

void RadientQuadTestScene::Initialize(const QuadSetups& Setups, Bool EnableAsyncPipelineCompilation)
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
    EngineCI.Resources.IndexBufferSize           = IndexBufferSize;
    EngineCI.Resources.MaxIndexBufferSize        = MaxIndexBufferSize;
    EngineCI.Resources.VertexPoolSize            = 64;
    EngineCI.Resources.TextureAtlasSize          = 64;
    EngineCI.Resources.TextureAtlasMipLevel0Size = 64 * 64 * 4;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);

    if (ReservedVertexCount != 0)
    {
        GLTF::ResourceManager::VertexLayoutKey LayoutKey;
        LayoutKey.Elements.emplace_back(static_cast<Uint32>(sizeof(RadientFloat3)), BIND_VERTEX_BUFFER);
        LayoutKey.Elements.emplace_back(static_cast<Uint32>(sizeof(RadientFloat2)), BIND_VERTEX_BUFFER);
        RadientAssetManagerImpl& AssetManager = *static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());
        pReservedVertices                     = AssetManager.GetResourceManager()->AllocateVertices(LayoutKey, ReservedVertexCount);
        ASSERT_NE(pReservedVertices, nullptr);
    }

    RadientRendererDesc RendererDesc;
    RendererDesc.EnableAsyncPipelineCompilation = EnableAsyncPipelineCompilation;
    RendererDesc.MultiDrawBatchSize             = MultiDrawBatchSize;
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

    // Quads use independently configured materials unless the test requests
    // one shared material to exercise mesh batching.
    for (Uint32 Index = 0; Index < Materials.size(); ++Index)
    {
        if (ShareMaterial && Index != 0)
        {
            Materials[Index] = Materials[0];
            continue;
        }
        const QuadSetup&                            Setup = Setups[Index];
        RadientStandardMaterialDefinitionCreateInfo DefinitionCI;
        DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;
        ASSERT_EQ(CreateStandardMaterialAsset(
                      *pAssets, DefinitionCI,
                      [&](IRadientMaterialDefinitionAsset& Definition, IRadientMaterialWriter& Writer) {
                          RADIENT_STATUS                           Status = SetMaterialParameter(Definition, Writer, RadientStandardMaterialBaseColorFactorName, Setup.Color);
                          RadientStandardMaterialTextureParameters TextureParameters{pTexture};
                          TextureParameters.UVScale    = Setup.UVScale;
                          TextureParameters.UVBias     = Setup.UVBias;
                          TextureParameters.UVSelector = static_cast<Int32>(Setup.TexCoordSet);
                          if (RADIENT_SUCCEEDED(Status))
                              Status = SetStandardMaterialTextureParameters(Definition, Writer, RadientStandardMaterialBaseColorTextureParameterNames, TextureParameters);
                          RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurface{&Writer, IID_RadientSurfaceMaterialWriter};
                          if (pSurface == nullptr)
                              return RADIENT_STATUS_INVALID_OPERATION;
                          if (RADIENT_SUCCEEDED(Status))
                              Status = pSurface->SetSurfaceMode(Setup.SurfaceMode);
                          if (RADIENT_SUCCEEDED(Status))
                              Status = pSurface->SetDoubleSided(Setup.DoubleSided);
                          if (RADIENT_SUCCEEDED(Status))
                              Status = pSurface->SetAlphaCutoff(Setup.AlphaCutoff);
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
    const Uint16             Indices[]     = {0, 1, 2, 0, 2, 3};
    const Uint16             BackIndices[] = {0, 2, 1, 0, 3, 2};
    RadientVertexLayoutDescX Layout;
    Layout.AddBuffer(sizeof(Vertex));
    Layout.AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);

    std::array<RadientEntityID, 4>     Entities{};
    RefCntAutoPtr<IRadientSceneWriter> pSceneWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, &pSceneWriter), RADIENT_STATUS_OK);
    for (Uint32 Index = 0; Index < Materials.size(); ++Index)
    {
        const QuadSetup&                Setup          = Setups[Index];
        const Uint16* const             pSourceIndices = Setup.BackFacing ? BackIndices : Indices;
        const bool                      IsIndexed      = Setup.IndexType != RADIENT_INDEX_TYPE_NONE;
        std::vector<Vertex>             MeshVertices;
        RefCntAutoPtr<IRadientDataBlob> pIndices;
        Uint32                          IndexCount = 0;
        if (IsIndexed)
        {
            // Prefixing indexed vertices makes otherwise identical quads
            // require different index data. Reading another mesh's indices
            // then selects degenerate vertices instead of the visible quad.
            MeshVertices.resize(Setup.VertexPrefixCount);
            MeshVertices.insert(MeshVertices.end(), Vertices, Vertices + 4);
            std::vector<Uint16> Indices16(Setup.FirstElement, Uint16{0});
            for (Uint32 VertexIndex = 0; VertexIndex < 6; ++VertexIndex)
            {
                Indices16.push_back(static_cast<Uint16>(pSourceIndices[VertexIndex] + Setup.VertexPrefixCount));
            }
            IndexCount = static_cast<Uint32>(Indices16.size());
            if (Setup.IndexType == RADIENT_INDEX_TYPE_UINT16)
            {
                pIndices = MakeBlob(Indices16.data(), Indices16.size() * sizeof(Uint16));
            }
            else
            {
                ASSERT_EQ(Setup.IndexType, RADIENT_INDEX_TYPE_UINT32);
                const std::vector<Uint32> Indices32(Indices16.begin(), Indices16.end());
                pIndices = MakeBlob(Indices32.data(), Indices32.size() * sizeof(Uint32));
            }
            ASSERT_NE(pIndices, nullptr);
        }
        else
        {
            // Prefix the nonindexed range with degenerate vertices, then
            // expand the same two triangles used by indexed references.
            MeshVertices.resize(Setup.FirstElement);
            for (Uint32 VertexIndex = 0; VertexIndex < 6; ++VertexIndex)
            {
                MeshVertices.push_back(Vertices[pSourceIndices[VertexIndex]]);
            }
        }

        RefCntAutoPtr<IRadientDataBlob>       pDeltas;
        RadientMorphTargetAttributeDesc       MorphAttribute{RadientMorphTargetPositionSemantic, 3};
        RadientMorphTargetAttributeCreateInfo MorphData;
        RadientMorphTargetCreateInfo          MorphTarget;
        if (Setup.UseMorphTarget)
        {
            // The undeformed quad is too small to cover off-center probes.
            // A weight of one must restore the reference quad's full size.
            std::vector<RadientFloat3> Deltas(MeshVertices.size());
            for (size_t VertexIndex = 0; VertexIndex < MeshVertices.size(); ++VertexIndex)
            {
                RadientFloat3& Position = MeshVertices[VertexIndex].Position;
                Deltas[VertexIndex]     = {Position.x * 0.75f, Position.y * 0.75f, 0.f};
                Position.x *= 0.25f;
                Position.y *= 0.25f;
            }
            pDeltas = MakeBlob(Deltas.data(), Deltas.size() * sizeof(RadientFloat3));
            ASSERT_NE(pDeltas, nullptr);
            MorphData.pDeltaBuffer          = pDeltas;
            MorphTarget.Desc.pAttributes    = &MorphAttribute;
            MorphTarget.Desc.AttributeCount = 1;
            MorphTarget.Desc.DefaultWeight  = 1.f;
            MorphTarget.pAttributeData      = &MorphData;
        }
        const RefCntAutoPtr<IRadientDataBlob> pVertices = MakeBlob(MeshVertices.data(), MeshVertices.size() * sizeof(Vertex));
        ASSERT_NE(pVertices, nullptr);
        RadientVertexLayoutDescX MeshLayout = Layout;
        MeshLayout.AddAttribute(Setup.TexCoordSet == 0 ? "TEXCOORD_0" : "TEXCOORD_1", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2);
        RefCntAutoPtr<IRadientDataBlob> pVertexColors;
        if (Setup.UseVertexColor)
        {
            const std::vector<RadientFloat4> VertexColors(MeshVertices.size(), Setup.VertexColor);
            pVertexColors = MakeBlob(VertexColors.data(), VertexColors.size() * sizeof(RadientFloat4));
            ASSERT_NE(pVertexColors, nullptr);
            MeshLayout.AddBuffer(sizeof(RadientFloat4));
            MeshLayout.AddAttribute("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, 1);
        }
        IRadientDataBlob* VertexBuffers[] = {pVertices, pVertexColors};

        RadientMeshPrimitiveCreateInfo Primitive;
        Primitive.FirstElement = Setup.FirstElement;
        Primitive.ElementCount = 6;
        Primitive.pMaterial    = Materials[Index];
        RadientMeshCreateInfo MeshCI;
        MeshCI.VertexData = {MeshLayout, VertexBuffers, static_cast<Uint32>(MeshVertices.size())};
        if (IsIndexed)
            MeshCI.IndexData = {pIndices, IndexCount, Setup.IndexType};
        if (Setup.UseMorphTarget)
            MeshCI.MorphTargetData = {&MorphTarget, 1};
        MeshCI.pPrimitives    = &Primitive;
        MeshCI.PrimitiveCount = 1;

        RefCntAutoPtr<IRadientMeshAsset>& pMesh      = Meshes[Index];
        const RADIENT_STATUS              MeshStatus = pAssets->CreateMesh(MeshCI, &pMesh);
        ASSERT_TRUE(MeshStatus == RADIENT_STATUS_OK || MeshStatus == RADIENT_STATUS_PENDING);
        ASSERT_NE(pMesh, nullptr);

        RadientEntityDesc EntityDesc;
        EntityDesc.Transform.Position.x = static_cast<float>(Index) - 1.5f;
        RadientEntityID Entity          = InvalidRadientEntityID;
        ASSERT_EQ(pSceneWriter->CreateEntity(EntityDesc, Entity), RADIENT_STATUS_OK);
        ASSERT_EQ(pSceneWriter->SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
        ASSERT_EQ(pSceneWriter->SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
        Entities[Index] = Entity;
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
    OutputDesc.Name      = "Quad test output";
    OutputDesc.Type      = RESOURCE_DIM_TEX_2D;
    OutputDesc.Width     = OutputWidth;
    OutputDesc.Height    = OutputHeight;
    OutputDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
    OutputDesc.BindFlags = BIND_RENDER_TARGET;
    pDevice->CreateTexture(OutputDesc, nullptr, &pOutput);
    ASSERT_NE(pOutput, nullptr);
    OutputDesc.Name           = "Quad test readback";
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

    ASSERT_NO_FATAL_FAILURE(RenderUntilReady());

    // Mesh payloads must be available before creating their weights. Pump
    // the renderer first: waiting here without rendering would prevent
    // queued uploads and their dependent mesh tasks from making progress.
    bool HasMorphComponents = false;
    for (Uint32 Index = 0; Index < Meshes.size(); ++Index)
    {
        if (!Setups[Index].UseMorphTarget)
            continue;
        RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
        ASSERT_EQ(Meshes[Index]->CreateMorphTargetWeights(&pWeights), RADIENT_STATUS_OK);
        ASSERT_EQ(pSceneWriter->SetMorph(Entities[Index], {pWeights}), RADIENT_STATUS_OK);
        HasMorphComponents = true;
    }
    if (HasMorphComponents)
    {
        ASSERT_EQ(pSceneWriter->CommitChanges(), RADIENT_STATUS_OK);
        ASSERT_NO_FATAL_FAILURE(RenderUntilReady());
    }
}

void RadientQuadTestScene::RenderUntilReady(IRadientMaterialAsset* pEditedMaterial)
{
    const std::chrono::steady_clock::time_point Deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    RADIENT_STATUS                              Status   = RADIENT_STATUS_PENDING;
    while (Status == RADIENT_STATUS_PENDING && std::chrono::steady_clock::now() < Deadline)
    {
        // Render-state edits can require a new pipeline, but the material
        // itself must remain loaded while the renderer prepares that pipeline.
        if (pEditedMaterial != nullptr)
        {
            ASSERT_EQ(RadientMaterialAssetManager::GetLoadStatus(pEditedMaterial), RADIENT_STATUS_OK);
        }
        Status = RenderFrame();
        ASSERT_FALSE(RADIENT_FAILED(Status));
        if (Status == RADIENT_STATUS_PENDING)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(Status, RADIENT_STATUS_OK);
    ASSERT_EQ(RenderFrame(), RADIENT_STATUS_OK);
    if (pEditedMaterial != nullptr)
    {
        ASSERT_EQ(RadientMaterialAssetManager::GetLoadStatus(pEditedMaterial), RADIENT_STATUS_OK);
    }
}

RADIENT_STATUS RadientQuadTestScene::RenderFrame()
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

void RadientQuadTestScene::ReadColors(SampledColors& Colors, Uint32 PixelOffsetX)
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
        const Uint32 X      = (2 * Index + 1) * OutputWidth / 8 + PixelOffsetX;
        const Uint8* pPixel = static_cast<const Uint8*>(Mapped.pData) + (OutputHeight / 2) * Mapped.Stride + X * 4;
        std::memcpy(Colors[Index].data(), pPixel, Colors[Index].size());
    }
    pContext->UnmapTextureSubresource(pReadback, 0, 0);
}

void ExpectSameColor(const std::array<Uint8, 4>& Actual, const std::array<Uint8, 4>& Reference)
{
    for (Uint32 Channel = 0; Channel < 3; ++Channel)
        EXPECT_NEAR(static_cast<int>(Actual[Channel]), static_cast<int>(Reference[Channel]), 2) << "Channel " << Channel;
}

} // namespace RadientGPUTest

} // namespace Testing

} // namespace Diligent
