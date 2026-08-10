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
#include "RadientEngine.h"
#include "RefCntAutoPtr.hpp"
#include "TestingSwapChainBase.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <thread>

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

const char* GetBackendSuffix(RENDER_DEVICE_TYPE DeviceType)
{
    switch (DeviceType)
    {
        case RENDER_DEVICE_TYPE_D3D11:
            return "d3d11";
        case RENDER_DEVICE_TYPE_D3D12:
            return "d3d12";
        case RENDER_DEVICE_TYPE_GL:
            return "gl";
        case RENDER_DEVICE_TYPE_GLES:
            return "gles";
        case RENDER_DEVICE_TYPE_VULKAN:
            return "vk";
        case RENDER_DEVICE_TYPE_METAL:
            return "mtl";
        case RENDER_DEVICE_TYPE_WEBGPU:
            return "wgpu";
        default:
            return nullptr;
    }
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

class RadientRenderScene
{
public:
    RadientRenderScene(IRadientEngine*                pEngine,
                       IRadientRenderer*              pRenderer,
                       IRadientView*                  pView,
                       IRadientTextureAsset*          pEnvironmentMap,
                       IDeviceContext*                pContext,
                       ISwapChain*                    pSwapChain,
                       const RadientRenderTestCamera& Camera,
                       bool                           DirectionalLight) :
        m_pContext{pContext},
        m_pRenderer{pRenderer},
        m_pView{pView},
        m_pEnvironmentMap{pEnvironmentMap}
    {
        RadientSceneDesc SceneDesc{};
        SceneDesc.Name = "Radient render test scene";
        m_Status       = pEngine->CreateScene(SceneDesc, &m_pScene);
        if (RADIENT_FAILED(m_Status))
            return;

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
        InstantiateInfo.Name = "Render test model";

        const RADIENT_STATUS Status = m_pImporter->ImportScene(
            LoadInfo, InstantiateInfo, &m_pSceneAsset, m_ModelRoot);
        if (RADIENT_FAILED(Status))
            return Status;

        const RADIENT_STATUS CommitStatus = m_pWriter->CommitChanges();
        return RADIENT_FAILED(CommitStatus) ? CommitStatus : Status;
    }

    RADIENT_STATUS RenderUntilReady()
    {
        const auto Deadline = std::chrono::steady_clock::now() + RenderReadyTimeout;
        double     Time     = 0.0;

        while (std::chrono::steady_clock::now() < Deadline)
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

            const RADIENT_STATUS RenderStatus = RenderFrame(Time);
            if (RADIENT_FAILED(RenderStatus))
                return RenderStatus;

            m_pContext->Flush();
            m_pContext->FinishFrame();

            if (ImportStatus == RADIENT_STATUS_OK &&
                SceneStatus == RADIENT_STATUS_OK &&
                EnvironmentStatus == RADIENT_STATUS_OK &&
                RenderStatus == RADIENT_STATUS_OK)
            {
                return RADIENT_STATUS_OK;
            }

            Time += 1.0 / 60.0;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        return RADIENT_STATUS_PENDING;
    }

    RADIENT_STATUS RenderMeasuredFrame(DeviceContextCommandCounters& Counters)
    {
        const DeviceContextCommandCounters Before = m_pContext->GetStats().CommandCounters;
        const RADIENT_STATUS               Status = RenderFrame(0.0);
        const DeviceContextCommandCounters After  = m_pContext->GetStats().CommandCounters;

        Counters.MultiDrawIndexed = After.MultiDrawIndexed - Before.MultiDrawIndexed;
        Counters.MapBuffer        = After.MapBuffer - Before.MapBuffer;
        Counters.UpdateBuffer     = After.UpdateBuffer - Before.UpdateBuffer;
        return Status;
    }

private:
    RADIENT_STATUS RenderFrame(double Time)
    {
        RadientRenderAttribs Attribs{};
        Attribs.pView          = m_pView;
        Attribs.pDeviceContext = m_pContext;
        Attribs.Time           = Time;
        Attribs.DeltaTime      = 1.0 / 60.0;
        return m_pRenderer->Render(Attribs);
    }

private:
    IDeviceContext* m_pContext = nullptr;

    RefCntAutoPtr<IRadientScene>         m_pScene;
    RefCntAutoPtr<IRadientSceneWriter>   m_pWriter;
    RefCntAutoPtr<IRadientSceneImporter> m_pImporter;
    RefCntAutoPtr<IRadientRenderer>      m_pRenderer;
    RefCntAutoPtr<IRadientRenderTarget>  m_pRenderTarget;
    RefCntAutoPtr<IRadientView>          m_pView;
    RefCntAutoPtr<IRadientTextureAsset>  m_pEnvironmentMap;
    RefCntAutoPtr<IRadientSceneAsset>    m_pSceneAsset;

    RadientEntityID m_CameraEntity = InvalidRadientEntityID;
    RadientEntityID m_ModelRoot    = InvalidRadientEntityID;
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

        const RadientRenderTestOptions& Options       = GetRadientRenderTestOptions();
        const char* const               BackendSuffix = GetBackendSuffix(pDevice->GetDeviceInfo().Type);
        ASSERT_NE(BackendSuffix, nullptr);

        const std::string ImageBaseName      = m_TestCase.Name + '_' + BackendSuffix;
        const std::string ModelPath          = FileSystem::JoinPath(Options.ModelsDirectory, m_TestCase.Model);
        const std::string ReferenceDirectory = FileSystem::JoinPath(Options.GoldenImagesDirectory, m_TestCase.Name);
        const std::string ReferenceBasePath  = FileSystem::JoinPath(ReferenceDirectory, ImageBaseName);
        const std::string ReferencePath      = ReferenceBasePath + ".png";

        ASSERT_TRUE(FileSystem::FileExists(ModelPath.c_str()))
            << "Model does not exist: " << ModelPath;

        const bool HasReference = FileSystem::FileExists(ReferencePath.c_str());
        if (Options.UpdateGoldenImages)
        {
            ASSERT_TRUE(FileSystem::IsDirectory(ReferenceDirectory.c_str()) ||
                        FileSystem::CreateDirectory(ReferenceDirectory.c_str()))
                << "Failed to create golden image directory: " << ReferenceDirectory;
        }
        if (HasReference)
        {
            ASSERT_TRUE(pTestingSwapChain->LoadReferenceImage(ReferencePath.c_str()))
                << "Failed to load reference image: " << ReferencePath;
        }
        else
        {
            pSwapChain->Resize(Options.Width, Options.Height, pSwapChain->GetDesc().PreTransform);
        }
        pTestingSwapChain->SetImageComparisonAttribs(m_TestCase.Comparison);

        const RADIENT_STATUS ToneMappingStatus = GetView()->SetToneMapping(m_TestCase.ToneMapping);
        ASSERT_TRUE(ToneMappingStatus == RADIENT_STATUS_OK ||
                    ToneMappingStatus == RADIENT_STATUS_NO_CHANGE);

        RadientRenderScene Scene{GetEngine(),
                                 GetRenderer(),
                                 GetView(),
                                 GetEnvironmentMap(),
                                 pContext,
                                 pSwapChain,
                                 m_TestCase.Camera,
                                 m_TestCase.DirectionalLight};
        ASSERT_EQ(Scene.GetStatus(), RADIENT_STATUS_OK);
        const RADIENT_STATUS ImportStatus = Scene.Import(ModelPath.c_str());
        ASSERT_TRUE(IsPendingOrOK(ImportStatus))
            << "Import failed with Radient status " << static_cast<Int32>(ImportStatus);
        ASSERT_EQ(Scene.RenderUntilReady(), RADIENT_STATUS_OK)
            << "Timed out waiting for imported scene and renderer resources";

        DeviceContextCommandCounters Counters{};
        ASSERT_EQ(Scene.RenderMeasuredFrame(Counters), RADIENT_STATUS_OK);

        RecordProperty("multiDrawIndexed", Counters.MultiDrawIndexed);
        RecordProperty("mapBuffer", Counters.MapBuffer);
        RecordProperty("updateBuffer", Counters.UpdateBuffer);

        const auto& Statistics = m_TestCase.Statistics[static_cast<size_t>(pDevice->GetDeviceInfo().Type)];
        if (Statistics)
        {
            if (Statistics->MultiDrawIndexed)
                EXPECT_EQ(Counters.MultiDrawIndexed, *Statistics->MultiDrawIndexed);
            if (Statistics->MapBufferMax)
                EXPECT_LE(Counters.MapBuffer, *Statistics->MapBufferMax);
            if (Statistics->UpdateBufferMax)
                EXPECT_LE(Counters.UpdateBuffer, *Statistics->UpdateBufferMax);
        }

        if (!HasReference)
        {
            if (Options.UpdateGoldenImages)
            {
                pTestingSwapChain->DumpBackBuffer(ReferenceBasePath.c_str());
                ADD_FAILURE() << "Reference image did not exist and was created: " << ReferencePath;
            }
            else
            {
                const std::string CandidateBasePath = FileSystem::JoinPath(Options.GoldenImageDifferencesDirectory, ImageBaseName);
                const std::string CandidatePath     = CandidateBasePath + ".png";
                pTestingSwapChain->DumpBackBuffer(CandidateBasePath.c_str());
                ADD_FAILURE() << "Reference image does not exist: " << ReferencePath
                              << ". Rendered candidate: " << CandidatePath;
            }
            return;
        }

        {
            CurrentDirectoryScope DifferencesDirectory{Options.GoldenImageDifferencesDirectory};
            ASSERT_TRUE(DifferencesDirectory)
                << "Failed to use golden image differences directory: " << Options.GoldenImageDifferencesDirectory;
            pTestingSwapChain->CompareWithSnapshot(nullptr);
        }

        if (Options.UpdateGoldenImages)
            pTestingSwapChain->DumpBackBuffer(ReferenceBasePath.c_str());
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
