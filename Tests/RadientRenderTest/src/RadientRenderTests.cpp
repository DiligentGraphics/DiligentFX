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

#include "RadientRenderTests.hpp"

#include "RadientRenderTestManifest.hpp"
#include "RadientRenderTestOptions.hpp"
#include "RadientRenderTestFixture.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "Math/RadientMath.hpp"
#include "FileSystem.hpp"
#include "GPUTestingEnvironment.hpp"
#include "GraphicsAccessories.hpp"
#include "Render/RadientPBRRenderer.hpp"
#include "RadientEngine.h"
#include "RadientSkinning.h"
#include "RefCntAutoPtr.hpp"
#include "TestingSwapChainBase.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <thread>
#include <unordered_set>

namespace Diligent
{
namespace Testing
{

namespace
{

static constexpr std::chrono::seconds RenderReadyTimeout{120};

bool IsPendingOrOK(RADIENT_STATUS Status)
{
    return Status == RADIENT_STATUS_PENDING || Status == RADIENT_STATUS_OK;
}

RadientTransform MakeCameraTransform(const RadientRenderTestCamera& Camera)
{
    const float3 Eye      = Camera.Eye;
    const float3 Backward = normalize(Eye - Camera.Target);
    const float3 Right    = normalize(cross(Camera.Up, Backward));
    const float3 Up       = cross(Backward, Right);

    // Radient cameras look along their local negative Z axis.
    const float4x4 CameraWorld{
        Right.x,
        Right.y,
        Right.z,
        0.f,
        Up.x,
        Up.y,
        Up.z,
        0.f,
        Backward.x,
        Backward.y,
        Backward.z,
        0.f,
        Eye.x,
        Eye.y,
        Eye.z,
        1.f,
    };
    return RadientMath::MatrixToTransform(RadientMath::ToRadientMatrix(CameraWorld));
}

class CurrentDirectoryScope
{
public:
    explicit CurrentDirectoryScope(const std::string& Directory)
    {
        std::error_code Error;
        m_OriginalDirectory = std::filesystem::current_path(Error);
        if (!Error)
        {
            std::filesystem::current_path(Directory, Error);
            m_Active = !Error;
        }
    }

    ~CurrentDirectoryScope()
    {
        if (m_Active)
        {
            std::error_code Error;
            std::filesystem::current_path(m_OriginalDirectory, Error);
        }
    }

    explicit operator bool() const
    {
        return m_Active;
    }

private:
    std::filesystem::path m_OriginalDirectory;
    bool                  m_Active = false;
};

const char* GetDebugVisualizationName(RADIENT_DEBUG_VISUALIZATION Visualization)
{
    return PBR_Renderer::GetDebugViewTypeString(
        RadientPBRRenderer::GetDebugViewType(Visualization));
}

std::string GetImageBaseName(const RadientRenderTestCase& TestCase,
                             RADIENT_DEBUG_VISUALIZATION  Visualization,
                             const char*                  BackendSuffix)
{
    std::string Name = TestCase.Name;
    if (Visualization != RADIENT_DEBUG_VISUALIZATION_NONE)
    {
        Name += '_';
        Name += GetDebugVisualizationName(Visualization);
    }
    Name += '_';
    Name += BackendSuffix;
    return Name;
}

void FlipImageVertically(std::vector<Uint8>& Pixels, Uint32 Width, Uint32 Height)
{
    const size_t       RowSize = static_cast<size_t>(Width) * 4;
    std::vector<Uint8> TempRow(RowSize);
    for (Uint32 TopRow = 0; TopRow < Height / 2; ++TopRow)
    {
        Uint8* const pTopRow    = Pixels.data() + static_cast<size_t>(TopRow) * RowSize;
        Uint8* const pBottomRow = Pixels.data() + static_cast<size_t>(Height - 1 - TopRow) * RowSize;
        std::memcpy(TempRow.data(), pTopRow, RowSize);
        std::memcpy(pTopRow, pBottomRow, RowSize);
        std::memcpy(pBottomRow, TempRow.data(), RowSize);
    }
}

class RadientRenderScene
{
public:
    RadientRenderScene(IRadientEngine*                pEngine,
                       IRadientRenderer*              pRenderer,
                       IRadientView*                  pView,
                       IRadientTextureAsset*          pEnvironmentMap,
                       ISwapChain*                    pSwapChain,
                       const RadientRenderTestCamera& Camera,
                       bool                           DirectionalLight,
                       bool                           EnableAnimationRegistry) :
        m_pRenderer{pRenderer},
        m_pView{pView},
        m_pEnvironmentMap{pEnvironmentMap}
    {
        RadientSceneCreateInfo SceneCI{};
        SceneCI.Desc.Name = "Radient render test scene";
        m_Status          = pEngine->CreateScene(SceneCI, &m_pScene);
        if (RADIENT_FAILED(m_Status))
            return;

        if (EnableAnimationRegistry)
        {
            m_Status = pEngine->CreateAnimationRegistry(m_pScene, &m_pAnimationRegistry);
            if (RADIENT_FAILED(m_Status))
                return;
        }

        m_Status = pEngine->CreateSceneWriter(m_pScene, &m_pWriter);
        if (RADIENT_FAILED(m_Status))
            return;

        m_Status = pEngine->CreateSceneImporter(m_pWriter, &m_pImporter);
        if (RADIENT_FAILED(m_Status))
            return;

        RadientRenderTargetDesc TargetDesc{};
        TargetDesc.Name       = "Radient render test target";
        TargetDesc.Size       = {pSwapChain->GetDesc().Width, pSwapChain->GetDesc().Height};
        TargetDesc.pSwapChain = pSwapChain;
        TargetDesc.pColorRTV  = pSwapChain->GetCurrentBackBufferRTV();
        TargetDesc.pDepthDSV  = pSwapChain->GetDepthBufferDSV();
        m_Status              = m_pRenderer->CreateRenderTarget(TargetDesc, &m_pRenderTarget);
        if (RADIENT_FAILED(m_Status))
            return;

        RadientEntityDesc CameraDesc{};
        CameraDesc.Name      = "Render test camera";
        CameraDesc.Transform = MakeCameraTransform(Camera);
        m_Status             = m_pWriter->CreateEntity(CameraDesc, m_CameraEntity);
        if (RADIENT_FAILED(m_Status))
            return;

        RadientCameraComponent CameraComponent{};
        CameraComponent.FocalLength = CameraComponent.VerticalAperture /
            (2.f * std::tan(Camera.Fov * PI_F / 360.f));
        CameraComponent.ClippingRange = {Camera.ClippingRange.x, Camera.ClippingRange.y};
        m_Status                      = m_pWriter->SetCamera(m_CameraEntity, CameraComponent);
        if (RADIENT_FAILED(m_Status))
            return;

        if (DirectionalLight)
        {
            RadientEntityDesc LightDesc{};
            LightDesc.Name              = "Render test directional light";
            RadientEntityID LightEntity = InvalidRadientEntityID;
            m_Status                    = m_pWriter->CreateEntity(LightDesc, LightEntity);
            if (RADIENT_FAILED(m_Status))
                return;

            RadientLightComponent Light{};
            Light.Type      = RADIENT_LIGHT_TYPE_DIRECTIONAL;
            Light.Intensity = 2.f;
            m_Status        = m_pWriter->SetLight(LightEntity, Light);
            if (RADIENT_FAILED(m_Status))
                return;
        }

        m_Status = m_pWriter->CommitChanges();
        if (RADIENT_FAILED(m_Status))
            return;

        m_Status = m_pView->SetScene(m_pScene);
        if (RADIENT_FAILED(m_Status))
            return;
        m_ViewAttached = true;

        m_Status = m_pView->SetCamera(m_CameraEntity);
        if (RADIENT_FAILED(m_Status))
            return;

        m_Status = m_pView->SetRenderTarget(m_pRenderTarget);
    }

    ~RadientRenderScene()
    {
        if (m_ViewAttached)
        {
            m_pView->SetRenderTarget(nullptr);
            m_pView->SetCamera(InvalidRadientEntityID);
            m_pView->SetScene(nullptr);
        }
    }

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    RADIENT_STATUS Import(const char* ModelPath)
    {
        RadientSceneLoadInfo LoadInfo{};
        LoadInfo.URI = ModelPath;

        RadientSceneInstantiateInfo InstantiateInfo{};
        InstantiateInfo.Name               = "Render test model";
        InstantiateInfo.pAnimationRegistry = m_pAnimationRegistry;

        RadientEntityID      ModelRoot = InvalidRadientEntityID;
        const RADIENT_STATUS Status    = m_pImporter->ImportScene(
            LoadInfo, InstantiateInfo, &m_pSceneAsset, ModelRoot);
        if (RADIENT_FAILED(Status))
            return Status;

        const RADIENT_STATUS CommitStatus = m_pWriter->CommitChanges();
        return RADIENT_FAILED(CommitStatus) ? CommitStatus : Status;
    }

    RADIENT_STATUS PrepareFrame()
    {
        const RADIENT_STATUS ImportStatus = m_pImporter->ProcessPendingImports();
        if (RADIENT_FAILED(ImportStatus))
            return ImportStatus;

        const RADIENT_STATUS CommitStatus = m_pWriter->CommitChanges();
        if (RADIENT_FAILED(CommitStatus))
            return CommitStatus;

        const RADIENT_STATUS SceneStatus =
            RadientAssetManagerImpl::GetSceneGPUResourceStatus(m_pSceneAsset);
        if (RADIENT_FAILED(SceneStatus))
            return SceneStatus;

        const RADIENT_STATUS EnvironmentStatus =
            RadientTextureAssetManager::GetGPUResourceStatus(m_pEnvironmentMap);
        if (RADIENT_FAILED(EnvironmentStatus))
            return EnvironmentStatus;

        return (ImportStatus == RADIENT_STATUS_OK &&
                SceneStatus == RADIENT_STATUS_OK &&
                EnvironmentStatus == RADIENT_STATUS_OK) ?
            RADIENT_STATUS_OK :
            RADIENT_STATUS_PENDING;
    }

    RADIENT_STATUS EvaluateAnimation(const RadientRenderTestAnimation& AnimationSettings)
    {
        if (m_pAnimationRegistry == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const RadientAnimationRegistryState& RegistryState  = m_pAnimationRegistry->GetState();
        bool                                 AnimationFound = false;
        for (Uint32 EntryIndex = 0; EntryIndex < RegistryState.EntryCount; ++EntryIndex)
        {
            const RadientAnimationRegistryEntry& Entry = RegistryState.pEntries[EntryIndex];
            if (Entry.pAnimation == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            const RadientSkeletonAnimationDesc& AnimationDesc = Entry.pAnimation->GetDesc();
            if (std::strcmp(AnimationDesc.Name, AnimationSettings.Name.c_str()) != 0)
                continue;

            AnimationFound = true;
            if (AnimationSettings.Time > AnimationDesc.Duration)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            if (Entry.TargetCount == 0)
                return RADIENT_STATUS_INVALID_OPERATION;

            // Multiple imported entities may attach the same pose in different
            // spaces; the animation only needs to update that pose once.
            std::unordered_set<IRadientSkeletonPose*> EvaluatedPoses;
            EvaluatedPoses.reserve(Entry.TargetCount);
            for (Uint32 TargetIndex = 0; TargetIndex < Entry.TargetCount; ++TargetIndex)
            {
                IRadientSkeletonPose* const pPose = Entry.pTargets[TargetIndex].pPose;
                if (pPose == nullptr)
                    return RADIENT_STATUS_INVALID_OPERATION;
                if (!EvaluatedPoses.insert(pPose).second)
                    continue;

                const RADIENT_STATUS Status = Entry.pAnimation->Evaluate(AnimationSettings.Time, pPose, True);
                if (RADIENT_FAILED(Status))
                    return Status;
            }
        }

        return AnimationFound ? RADIENT_STATUS_OK : RADIENT_STATUS_NOT_FOUND;
    }

    RADIENT_STATUS RenderFrame(IDeviceContext* pContext, double Time)
    {
        RadientRenderAttribs Attribs{};
        Attribs.pView          = m_pView;
        Attribs.pDeviceContext = pContext;
        Attribs.Time           = Time;
        Attribs.DeltaTime      = 1.0 / 60.0;
        return m_pRenderer->Render(Attribs);
    }

private:
    RefCntAutoPtr<IRadientScene>             m_pScene;
    RefCntAutoPtr<IRadientSceneWriter>       m_pWriter;
    RefCntAutoPtr<IRadientSceneImporter>     m_pImporter;
    RefCntAutoPtr<IRadientAnimationRegistry> m_pAnimationRegistry;
    RefCntAutoPtr<IRadientRenderer>          m_pRenderer;
    RefCntAutoPtr<IRadientRenderTarget>      m_pRenderTarget;
    RefCntAutoPtr<IRadientView>              m_pView;
    RefCntAutoPtr<IRadientTextureAsset>      m_pEnvironmentMap;
    RefCntAutoPtr<IRadientSceneAsset>        m_pSceneAsset;

    RadientEntityID m_CameraEntity = InvalidRadientEntityID;
    RADIENT_STATUS  m_Status       = RADIENT_STATUS_INVALID_OPERATION;
    bool            m_ViewAttached = false;
};

class RadientManifestRenderTest final : public RadientRender
{
public:
    explicit RadientManifestRenderTest(const RadientRenderTestCase& TestCase) :
        m_TestCase{TestCase}
    {}

private:
    struct CaptureEntry
    {
        RADIENT_DEBUG_VISUALIZATION Visualization = RADIENT_DEBUG_VISUALIZATION_NONE;
        std::string                 ImageBaseName;
        std::string                 ReferenceDirectory;
        std::string                 ReferenceBasePath;
        std::string                 ReferencePath;
        std::vector<Uint8>          ReferencePixels;
        Uint32                      ReferenceWidth  = 0;
        Uint32                      ReferenceHeight = 0;
        bool                        HasReference    = false;
        bool                        Captured        = false;
    };

    void TestBody() override
    {
        GPUTestingEnvironment::ScopedReset AutoReset;

        GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
        ASSERT_NE(pEnvironment, nullptr);

        IRenderDevice* const  pDevice    = pEnvironment->GetDevice();
        IDeviceContext* const pContext   = pEnvironment->GetDeviceContext();
        ISwapChain* const     pSwapChain = pEnvironment->GetSwapChain();
        ASSERT_NE(pDevice, nullptr);
        ASSERT_NE(pContext, nullptr);
        ASSERT_NE(pSwapChain, nullptr);
        ASSERT_NE(GetEngine(), nullptr);
        ASSERT_NE(GetRenderer(), nullptr);
        ASSERT_NE(GetView(), nullptr);

        RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
        ASSERT_NE(pTestingSwapChain, nullptr);

        const RadientRenderTestOptions& Options = GetRadientRenderTestOptions();

        const std::string ModelPath = FileSystem::JoinPath(Options.ModelsDirectory, m_TestCase.Model);
        ASSERT_TRUE(FileSystem::FileExists(ModelPath.c_str()))
            << "Model does not exist: " << ModelPath;

        std::vector<CaptureEntry> Captures = PrepareCaptures(pDevice, pSwapChain);
        if (Captures.empty())
            return;
        pTestingSwapChain->SetImageComparisonAttribs(m_TestCase.Comparison);

        const RADIENT_STATUS ToneMappingStatus = GetView()->SetToneMapping(m_TestCase.ToneMapping);
        ASSERT_TRUE(ToneMappingStatus == RADIENT_STATUS_OK ||
                    ToneMappingStatus == RADIENT_STATUS_NO_CHANGE);

        const RADIENT_STATUS IBLStatus = GetView()->SetIBLEnabled(m_TestCase.EnableIBL);
        ASSERT_TRUE(IBLStatus == RADIENT_STATUS_OK ||
                    IBLStatus == RADIENT_STATUS_NO_CHANGE);

        RadientRenderScene Scene{GetEngine(),
                                 GetRenderer(),
                                 GetView(),
                                 GetEnvironmentMap(),
                                 pSwapChain,
                                 m_TestCase.Camera,
                                 m_TestCase.DirectionalLight,
                                 m_TestCase.Animation.has_value()};
        ASSERT_EQ(Scene.GetStatus(), RADIENT_STATUS_OK);
        const RADIENT_STATUS ImportStatus = Scene.Import(ModelPath.c_str());
        ASSERT_TRUE(IsPendingOrOK(ImportStatus))
            << "Import failed with Radient status " << static_cast<Int32>(ImportStatus);

        const auto Deadline            = std::chrono::steady_clock::now() + RenderReadyTimeout;
        size_t     PendingCaptureCount = Captures.size();
        double     Time                = 0.0;

        // Render every output once after dependencies become ready to request
        // all asynchronous PSO permutations before capture readback begins.
        bool AllCapturesRequested = false;
        bool AnimationEvaluated   = !m_TestCase.Animation.has_value();

        while (PendingCaptureCount != 0 && std::chrono::steady_clock::now() < Deadline)
        {
            const RADIENT_STATUS PrepareStatus = Scene.PrepareFrame();
            ASSERT_FALSE(RADIENT_FAILED(PrepareStatus));
            const bool DependenciesReady = PrepareStatus == RADIENT_STATUS_OK;

            if (DependenciesReady && !AnimationEvaluated)
            {
                ASSERT_EQ(Scene.EvaluateAnimation(*m_TestCase.Animation), RADIENT_STATUS_OK)
                    << "Failed to evaluate animation '" << m_TestCase.Animation->Name
                    << "' at time " << m_TestCase.Animation->Time;
                AnimationEvaluated = true;
            }

            for (CaptureEntry& Capture : Captures)
            {
                if (Capture.Captured)
                    continue;

                const RADIENT_STATUS VisualizationStatus = GetView()->SetDebugVisualization(Capture.Visualization);
                ASSERT_TRUE(VisualizationStatus == RADIENT_STATUS_OK ||
                            VisualizationStatus == RADIENT_STATUS_NO_CHANGE);

                const DeviceContextCommandCounters Before       = pContext->GetStats().CommandCounters;
                const RADIENT_STATUS               RenderStatus = Scene.RenderFrame(pContext, Time);
                const DeviceContextCommandCounters After        = pContext->GetStats().CommandCounters;
                ASSERT_FALSE(RADIENT_FAILED(RenderStatus));

                if (AllCapturesRequested && DependenciesReady && RenderStatus == RADIENT_STATUS_OK)
                {
                    DeviceContextCommandCounters Counters{};
                    Counters.MultiDrawIndexed = After.MultiDrawIndexed - Before.MultiDrawIndexed;
                    Counters.MapBuffer        = After.MapBuffer - Before.MapBuffer;
                    Counters.UpdateBuffer     = After.UpdateBuffer - Before.UpdateBuffer;

                    if (Capture.Visualization == RADIENT_DEBUG_VISUALIZATION_NONE)
                        ValidateStatistics(pDevice->GetDeviceInfo().Type, Counters);

                    CaptureOutput(pTestingSwapChain, Capture);
                    Capture.Captured = true;
                    --PendingCaptureCount;
                }

                pContext->Flush();
                pContext->FinishFrame();
            }

            if (PendingCaptureCount == 0)
                break;

            AllCapturesRequested = DependenciesReady;
            Time += 1.0 / 60.0;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});

            pContext->WaitForIdle();
            pDevice->ReleaseStaleResources();
        }

        ASSERT_EQ(PendingCaptureCount, 0u)
            << "Timed out waiting for imported scene and renderer resources";
    }

    void ValidateStatistics(RENDER_DEVICE_TYPE                  DeviceType,
                            const DeviceContextCommandCounters& Counters)
    {
        RecordProperty("multiDrawIndexed", Counters.MultiDrawIndexed);
        RecordProperty("mapBuffer", Counters.MapBuffer);
        RecordProperty("updateBuffer", Counters.UpdateBuffer);

        const auto& Statistics = m_TestCase.Statistics[static_cast<size_t>(DeviceType)];
        if (!Statistics)
            return;

        if (Statistics->MultiDrawIndexed)
            EXPECT_EQ(Counters.MultiDrawIndexed, *Statistics->MultiDrawIndexed);
        if (Statistics->MapBufferMax)
            EXPECT_LE(Counters.MapBuffer, *Statistics->MapBufferMax);
        if (Statistics->UpdateBufferMax)
            EXPECT_LE(Counters.UpdateBuffer, *Statistics->UpdateBufferMax);
    }

    std::vector<CaptureEntry> PrepareCaptures(IRenderDevice* pDevice,
                                              ISwapChain*    pSwapChain)
    {
        const RadientRenderTestOptions& Options       = GetRadientRenderTestOptions();
        const char* const               BackendSuffix = GetRenderDeviceTypeShortString(pDevice->GetDeviceInfo().Type);

        const std::string ReferenceDirectory = FileSystem::JoinPath(
            FileSystem::JoinPath(Options.GoldenImagesDirectory, "GLTF"),
            m_TestCase.Name);

        std::vector<CaptureEntry> Captures;
        Captures.reserve(1 + m_TestCase.DebugVisualizations.size());

        const auto AddCapture = [&](RADIENT_DEBUG_VISUALIZATION Visualization) {
            CaptureEntry Capture;
            Capture.Visualization      = Visualization;
            Capture.ImageBaseName      = GetImageBaseName(m_TestCase, Visualization, BackendSuffix);
            Capture.ReferenceDirectory = ReferenceDirectory;
            Capture.ReferenceBasePath  = FileSystem::JoinPath(ReferenceDirectory, Capture.ImageBaseName);
            Capture.ReferencePath      = Capture.ReferenceBasePath + ".png";
            Captures.push_back(std::move(Capture));
        };

        AddCapture(RADIENT_DEBUG_VISUALIZATION_NONE);
        for (RADIENT_DEBUG_VISUALIZATION Visualization : m_TestCase.DebugVisualizations)
            AddCapture(Visualization);

        if (Options.UpdateGoldenImages &&
            !FileSystem::IsDirectory(ReferenceDirectory.c_str()) &&
            !FileSystem::CreateDirectory(ReferenceDirectory.c_str()))
        {
            ADD_FAILURE() << "Failed to create golden image directory: " << ReferenceDirectory;
            return {};
        }

        Uint32 ReferenceWidth  = 0;
        Uint32 ReferenceHeight = 0;
        for (CaptureEntry& Capture : Captures)
        {
            if (!FileSystem::FileExists(Capture.ReferencePath.c_str()))
                continue;

            if (!LoadTestImage(Capture.ReferencePath.c_str(),
                               Capture.ReferencePixels,
                               Capture.ReferenceWidth,
                               Capture.ReferenceHeight))
            {
                ADD_FAILURE() << "Failed to load reference image: " << Capture.ReferencePath;
                continue;
            }

            if (ReferenceWidth != 0 &&
                (Capture.ReferenceWidth != ReferenceWidth || Capture.ReferenceHeight != ReferenceHeight))
            {
                ADD_FAILURE() << "Reference image " << Capture.ReferencePath << " has dimensions "
                              << Capture.ReferenceWidth << 'x' << Capture.ReferenceHeight
                              << ", but all capture references must use "
                              << ReferenceWidth << 'x' << ReferenceHeight;
                Capture.ReferencePixels.clear();
                Capture.ReferenceWidth  = 0;
                Capture.ReferenceHeight = 0;
                continue;
            }

            if (ReferenceWidth == 0)
            {
                ReferenceWidth  = Capture.ReferenceWidth;
                ReferenceHeight = Capture.ReferenceHeight;
            }

            if (pDevice->GetDeviceInfo().IsGLDevice())
                FlipImageVertically(Capture.ReferencePixels, Capture.ReferenceWidth, Capture.ReferenceHeight);

            Capture.HasReference = true;
        }

        if (ReferenceWidth == 0)
        {
            ReferenceWidth  = Options.Width;
            ReferenceHeight = Options.Height;
        }

        pSwapChain->Resize(ReferenceWidth, ReferenceHeight, pSwapChain->GetDesc().PreTransform);

        return Captures;
    }

    void CaptureOutput(ITestingSwapChain*  pTestingSwapChain,
                       const CaptureEntry& Capture)
    {
        const RadientRenderTestOptions& Options = GetRadientRenderTestOptions();

        if (!Capture.HasReference)
        {
            // The file may exist but have failed to load or have dimensions
            // inconsistent with the other references. PrepareCaptures() has
            // already reported that error and left the capture without usable
            // reference data so that a replacement can still be rendered.
            const bool ReferenceExists = FileSystem::FileExists(Capture.ReferencePath.c_str());
            if (Options.UpdateGoldenImages)
            {
                // Updating is enabled, so replace the missing or unusable
                // golden image directly.
                pTestingSwapChain->DumpBackBuffer(Capture.ReferenceBasePath.c_str());
                if (!ReferenceExists)
                    ADD_FAILURE() << "Reference image did not exist and was created: " << Capture.ReferencePath;
            }
            else
            {
                // Preserve the golden-image directory and write the rendered
                // candidate alongside the ordinary comparison differences.
                const std::string CandidateBasePath = FileSystem::JoinPath(Options.GoldenImageDifferencesDirectory, Capture.ImageBaseName);
                const std::string CandidatePath     = CandidateBasePath + ".png";
                pTestingSwapChain->DumpBackBuffer(CandidateBasePath.c_str());
                if (!ReferenceExists)
                {
                    ADD_FAILURE() << "Reference image does not exist: " << Capture.ReferencePath
                                  << ". Rendered candidate: " << CandidatePath;
                }
            }
            return;
        }

        pTestingSwapChain->SetReferenceData(Capture.ReferencePixels.data(),
                                            static_cast<size_t>(Capture.ReferenceWidth) * 4,
                                            false);

        {
            CurrentDirectoryScope DifferencesDirectory{Options.GoldenImageDifferencesDirectory};
            ASSERT_TRUE(DifferencesDirectory)
                << "Failed to use golden image differences directory: " << Options.GoldenImageDifferencesDirectory;
            pTestingSwapChain->CompareWithSnapshot(nullptr);
        }

        if (Options.UpdateGoldenImages)
            pTestingSwapChain->DumpBackBuffer(Capture.ReferenceBasePath.c_str());
    }

private:
    const RadientRenderTestCase& m_TestCase;
};

} // namespace

void RegisterRadientRenderTests()
{
    for (const RadientRenderTestCase& TestCase : GetRadientRenderTestManifest().Tests)
    {
        const RadientRenderTestCase* const pTestCase = &TestCase;
        ::testing::RegisterTest(
            "RadientRender",
            TestCase.Name.c_str(),
            nullptr,
            nullptr,
            __FILE__,
            __LINE__,
            [pTestCase]() -> RadientRender* {
                return new RadientManifestRenderTest{*pTestCase};
            });
    }
}

} // namespace Testing
} // namespace Diligent
