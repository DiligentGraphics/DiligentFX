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

#include "RadientRenderTestFixture.hpp"
#include "RadientRenderTestOptions.hpp"

#include "Math/RadientMath.hpp"
#include "FileSystem.hpp"
#include "GPUTestingEnvironment.hpp"
#include "GraphicsAccessories.hpp"
#include "Radient.h"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RefCntAutoPtr.hpp"
#include "TestingSwapChainBase.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
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

RADIENT_STATUS RenderFrameAndWait(IRadientRenderer* pRenderer,
                                  IRadientView*     pView,
                                  IRenderDevice*    pDevice,
                                  IDeviceContext*   pContext,
                                  double&           Time)
{
    static constexpr double FrameDuration = 1.0 / 60.0;

    RadientFrameAttribs FrameAttribs{};
    FrameAttribs.Time      = Time;
    FrameAttribs.DeltaTime = FrameDuration;

    RADIENT_STATUS Status = pRenderer->BeginFrame(FrameAttribs);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RadientRenderAttribs RenderAttribs{};
    RenderAttribs.pView          = pView;
    RenderAttribs.pDeviceContext = pContext;

    const RADIENT_STATUS RenderStatus   = pRenderer->Render(RenderAttribs);
    const RADIENT_STATUS EndFrameStatus = pRenderer->EndFrame();
    Status                              = RenderStatus != RADIENT_STATUS_OK ? RenderStatus : EndFrameStatus;

    pContext->Flush();
    pContext->FinishFrame();
    pContext->WaitForIdle();
    pDevice->ReleaseStaleResources();

    Time += FrameDuration;
    return Status;
}

struct StandardMaterialValues
{
    RadientFloat4         BaseColor{1.f, 1.f, 1.f, 1.f};
    Float32               Metallic  = 1.f;
    Float32               Roughness = 1.f;
    RadientFloat3         Emissive{};
    IRadientTextureAsset* pBaseColorTexture = nullptr;
};

template <typename ValueType>
RADIENT_STATUS SetMaterialParameter(IRadientMaterialDefinitionAsset& Definition,
                                    IRadientMaterialWriter&          Writer,
                                    const char*                      Name,
                                    const ValueType&                 Value)
{
    RadientMaterialParameterHandle Handle;
    RADIENT_STATUS                 Status = Definition.FindParameter(Name, &Handle);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return Writer.SetParameter(Handle, Value);
}

RADIENT_STATUS CreateStandardMaterialAsset(IRadientAssetManager&         AssetManager,
                                           const StandardMaterialValues& Values,
                                           IRadientMaterialAsset**       ppMaterial)
{
    return Diligent::Testing::CreateStandardMaterialAsset(
        AssetManager,
        {},
        [&Values](IRadientMaterialDefinitionAsset& Definition,
                  IRadientMaterialWriter&          Writer) {
            const auto SetParameter = [&](const char* Name, const auto& Value) {
                return SetMaterialParameter(Definition, Writer, Name, Value);
            };

            RADIENT_STATUS Status = SetParameter(RadientStandardMaterialBaseColorFactorName, Values.BaseColor);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetParameter(RadientStandardMaterialMetallicFactorName, Values.Metallic);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetParameter(RadientStandardMaterialRoughnessFactorName, Values.Roughness);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetParameter(RadientStandardMaterialEmissiveFactorName, Values.Emissive);
            if (RADIENT_FAILED(Status))
                return Status;

            if (Values.pBaseColorTexture != nullptr)
            {
                Status = SetStandardMaterialTextureParameters(
                    Definition,
                    Writer,
                    RadientStandardMaterialBaseColorTextureParameterNames,
                    RadientStandardMaterialTextureParameters{Values.pBaseColorTexture});
            }

            return Status;
        },
        ppMaterial);
}

RadientTransform MakeCameraTransform(const float3& Eye,
                                     const float3& Target,
                                     const float3& UpDirection)
{
    const float3 Backward = normalize(Eye - Target);
    const float3 Right    = normalize(cross(UpDirection, Backward));
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

class PostFXCapture
{
public:
    bool Prepare(IRenderDevice* pDevice, ISwapChain* pSwapChain)
    {
        const char* const BackendSuffix = GetRenderDeviceTypeShortString(pDevice->GetDeviceInfo().Type);

        const ::testing::TestInfo* const pTestInfo =
            ::testing::UnitTest::GetInstance()->current_test_info();
        if (pTestInfo == nullptr)
        {
            ADD_FAILURE() << "Current render test information is unavailable";
            return false;
        }

        const RadientRenderTestOptions& Options = GetRadientRenderTestOptions();
        m_ImageBaseName                         = std::string{pTestInfo->name()} + '_' + BackendSuffix;
        m_ReferenceDirectory                    = FileSystem::JoinPath(
            FileSystem::JoinPath(Options.GoldenImagesDirectory, "PostEffects"),
            pTestInfo->name());
        m_ReferenceBasePath = FileSystem::JoinPath(m_ReferenceDirectory, m_ImageBaseName);
        m_ReferencePath     = m_ReferenceBasePath + ".png";
        m_ReferenceExists   = FileSystem::FileExists(m_ReferencePath.c_str());

        if (Options.UpdateGoldenImages &&
            !FileSystem::IsDirectory(m_ReferenceDirectory.c_str()) &&
            !FileSystem::CreateDirectory(m_ReferenceDirectory.c_str()))
        {
            ADD_FAILURE() << "Failed to create golden image directory: " << m_ReferenceDirectory;
            return false;
        }

        RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
        if (pTestingSwapChain == nullptr)
        {
            ADD_FAILURE() << "Testing swap chain is unavailable";
            return false;
        }

        if (m_ReferenceExists)
        {
            m_HasReference = pTestingSwapChain->LoadReferenceImage(m_ReferencePath.c_str());
            if (!m_HasReference)
                ADD_FAILURE() << "Failed to load reference image: " << m_ReferencePath;
        }

        if (!m_HasReference)
            pSwapChain->Resize(Options.Width, Options.Height, pSwapChain->GetDesc().PreTransform);

        pTestingSwapChain->SetImageComparisonAttribs(Options.Comparison);
        return true;
    }

    void Capture(ITestingSwapChain* pTestingSwapChain) const
    {
        const RadientRenderTestOptions& Options = GetRadientRenderTestOptions();

        if (m_HasReference)
        {
            CurrentDirectoryScope DifferencesDirectory{Options.GoldenImageDifferencesDirectory};
            ASSERT_TRUE(DifferencesDirectory)
                << "Failed to use golden image differences directory: "
                << Options.GoldenImageDifferencesDirectory;
            pTestingSwapChain->CompareWithSnapshot(nullptr);
        }

        if (Options.UpdateGoldenImages)
        {
            pTestingSwapChain->DumpBackBuffer(m_ReferenceBasePath.c_str());
            if (!m_ReferenceExists)
                ADD_FAILURE() << "Reference image did not exist and was created: " << m_ReferencePath;
        }
        else if (!m_HasReference)
        {
            const std::string CandidateBasePath =
                FileSystem::JoinPath(Options.GoldenImageDifferencesDirectory, m_ImageBaseName);
            const std::string CandidatePath = CandidateBasePath + ".png";
            pTestingSwapChain->DumpBackBuffer(CandidateBasePath.c_str());
            if (!m_ReferenceExists)
            {
                ADD_FAILURE() << "Reference image does not exist: " << m_ReferencePath
                              << ". Rendered candidate: " << CandidatePath;
            }
        }
    }

private:
    std::string m_ImageBaseName;
    std::string m_ReferenceDirectory;
    std::string m_ReferenceBasePath;
    std::string m_ReferencePath;
    bool        m_ReferenceExists = false;
    bool        m_HasReference    = false;
};

RADIENT_STATUS AddDrawable(IRadientSceneWriter*    pWriter,
                           const char*             Name,
                           IRadientMeshAsset*      pMesh,
                           const RadientTransform& Transform)
{
    RadientEntityDesc Desc{};
    Desc.Name      = Name;
    Desc.Transform = Transform;

    RadientEntityID Entity = InvalidRadientEntityID;
    RADIENT_STATUS  Status = pWriter->CreateEntity(Desc, Entity);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientMeshComponent Mesh{};
    Mesh.pMesh = pMesh;
    Status     = pWriter->SetMesh(Entity, Mesh);
    if (RADIENT_FAILED(Status))
        return Status;

    return pWriter->SetMeshRenderer(Entity, {});
}

} // namespace

TEST_F(RadientRender, Bloom)
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
    ASSERT_NE(GetAssetManager(), nullptr);
    ASSERT_NE(GetRenderer(), nullptr);

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    PostFXCapture Capture;
    ASSERT_TRUE(Capture.Prepare(pDevice, pSwapChain));

    const auto CreateMaterial = [&](const char*   Name,
                                    RadientFloat4 BaseColor,
                                    Float32       Metallic,
                                    Float32       Roughness,
                                    RadientFloat3 Emissive) {
        (void)Name;
        StandardMaterialValues Values{};
        Values.BaseColor = BaseColor;
        Values.Metallic  = Metallic;
        Values.Roughness = Roughness;
        Values.Emissive  = Emissive;

        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        const RADIENT_STATUS                 Status = CreateStandardMaterialAsset(*GetAssetManager(), Values, &pMaterial);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pMaterial, nullptr);
        return pMaterial;
    };

    const auto CreateCube = [&](const char* Name, IRadientMaterialAsset* pMaterial) {
        RadientCubeMeshCreateInfo CubeCI{};
        CubeCI.Name      = Name;
        CubeCI.Size      = 1.f;
        CubeCI.pMaterial = pMaterial;

        RefCntAutoPtr<IRadientMeshAsset> pCube;
        const RADIENT_STATUS             Status = CreateRadientCubeMesh(GetAssetManager(), CubeCI, &pCube);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pCube, nullptr);
        return pCube;
    };

    const auto CreateSphere = [&](const char* Name, Float32 Radius, IRadientMaterialAsset* pMaterial) {
        RadientSphereMeshCreateInfo SphereCI{};
        SphereCI.Name         = Name;
        SphereCI.Radius       = Radius;
        SphereCI.Subdivisions = 16;
        SphereCI.pMaterial    = pMaterial;

        RefCntAutoPtr<IRadientMeshAsset> pSphere;
        const RADIENT_STATUS             Status = CreateRadientSphereMesh(GetAssetManager(), SphereCI, &pSphere);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pSphere, nullptr);
        return pSphere;
    };

    RefCntAutoPtr<IRadientMaterialAsset> pBackdropMaterial =
        CreateMaterial("Bloom backdrop material", {0.012f, 0.015f, 0.02f, 1.f}, 0.f, 1.f, {});
    RefCntAutoPtr<IRadientMaterialAsset> pBrightEmissiveMaterial =
        CreateMaterial("Bloom bright emissive material", {0.f, 0.f, 0.f, 1.f}, 0.f, 1.f, {10.f, 2.5f, 0.4f});
    RefCntAutoPtr<IRadientMaterialAsset> pDimEmissiveMaterial =
        CreateMaterial("Bloom dim emissive material", {0.f, 0.f, 0.f, 1.f}, 0.f, 1.f, {0.35f, 0.1f, 0.025f});
    RefCntAutoPtr<IRadientMaterialAsset> pGlossyMaterial =
        CreateMaterial("Bloom glossy material", {0.25f, 0.35f, 0.55f, 1.f}, 1.f, 0.08f, {});
    ASSERT_NE(pBackdropMaterial, nullptr);
    ASSERT_NE(pBrightEmissiveMaterial, nullptr);
    ASSERT_NE(pDimEmissiveMaterial, nullptr);
    ASSERT_NE(pGlossyMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pBackdrop = CreateCube("Bloom backdrop", pBackdropMaterial);
    RefCntAutoPtr<IRadientMeshAsset> pBrightEmissiveSphere =
        CreateSphere("Bloom bright emissive sphere", 0.55f, pBrightEmissiveMaterial);
    RefCntAutoPtr<IRadientMeshAsset> pDimEmissiveSphere =
        CreateSphere("Bloom dim emissive sphere", 0.4f, pDimEmissiveMaterial);
    RefCntAutoPtr<IRadientMeshAsset> pGlossySphere =
        CreateSphere("Bloom glossy sphere", 0.9f, pGlossyMaterial);
    ASSERT_NE(pBackdrop, nullptr);
    ASSERT_NE(pBrightEmissiveSphere, nullptr);
    ASSERT_NE(pDimEmissiveSphere, nullptr);
    ASSERT_NE(pGlossySphere, nullptr);

    RadientSceneCreateInfo SceneCI{};
    SceneCI.Desc.Name = "Bloom render test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(GetEngine()->CreateScene(SceneCI, &pScene), RADIENT_STATUS_OK);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(GetEngine()->CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    RadientTransform FloorTransform{};
    FloorTransform.Position = {0.f, -0.1f, 0.f};
    FloorTransform.Scale    = {7.f, 0.2f, 6.f};
    ASSERT_EQ(AddDrawable(pWriter, "Floor", pBackdrop, FloorTransform), RADIENT_STATUS_OK);

    RadientTransform BackWallTransform{};
    BackWallTransform.Position = {0.f, 2.5f, -2.8f};
    BackWallTransform.Scale    = {7.f, 5.f, 0.2f};
    ASSERT_EQ(AddDrawable(pWriter, "Back wall", pBackdrop, BackWallTransform), RADIENT_STATUS_OK);

    RadientTransform BrightEmissiveTransform{};
    BrightEmissiveTransform.Position = {-1.6f, 1.25f, -0.5f};
    ASSERT_EQ(AddDrawable(pWriter, "Bright emissive sphere", pBrightEmissiveSphere, BrightEmissiveTransform), RADIENT_STATUS_OK);

    RadientTransform DimEmissiveTransform{};
    DimEmissiveTransform.Position = {-1.6f, 0.45f, 0.65f};
    ASSERT_EQ(AddDrawable(pWriter, "Dim emissive sphere", pDimEmissiveSphere, DimEmissiveTransform), RADIENT_STATUS_OK);

    RadientTransform GlossyTransform{};
    GlossyTransform.Position = {0.8f, 0.9f, -0.25f};
    ASSERT_EQ(AddDrawable(pWriter, "Glossy sphere", pGlossySphere, GlossyTransform), RADIENT_STATUS_OK);

    RadientEntityDesc LightDesc{};
    LightDesc.Name      = "Bloom directional light";
    LightDesc.Transform = MakeCameraTransform(float3{},
                                              float3{-0.35f, -0.65f, -1.f},
                                              float3{0.f, 1.f, 0.f});

    RadientEntityID LightEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity(LightDesc, LightEntity), RADIENT_STATUS_OK);

    RadientLightComponent Light{};
    Light.Type      = RADIENT_LIGHT_TYPE_DIRECTIONAL;
    Light.Color     = {0.8f, 0.9f, 1.f};
    Light.Intensity = 8.f;
    ASSERT_EQ(pWriter->SetLight(LightEntity, Light), RADIENT_STATUS_OK);

    RadientEntityDesc CameraDesc{};
    CameraDesc.Name      = "Bloom test camera";
    CameraDesc.Transform = MakeCameraTransform(float3{4.5f, 3.f, 7.f},
                                               float3{0.f, 0.8f, -0.5f},
                                               float3{0.f, 1.f, 0.f});

    RadientEntityID CameraEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity(CameraDesc, CameraEntity), RADIENT_STATUS_OK);

    RadientCameraComponent Camera{};
    Camera.FocalLength   = Camera.VerticalAperture / (2.f * std::tan(42.f * PI_F / 360.f));
    Camera.ClippingRange = {0.1f, 100.f};
    ASSERT_EQ(pWriter->SetCamera(CameraEntity, Camera), RADIENT_STATUS_OK);

    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name       = "Bloom render test target";
    TargetDesc.Size       = {pSwapChain->GetDesc().Width, pSwapChain->GetDesc().Height};
    TargetDesc.pSwapChain = pSwapChain;
    TargetDesc.pColorRTV  = pSwapChain->GetCurrentBackBufferRTV();
    TargetDesc.pDepthDSV  = pSwapChain->GetDepthBufferDSV();

    RefCntAutoPtr<IRadientRenderTarget> pRenderTarget;
    ASSERT_EQ(GetRenderer()->CreateRenderTarget(TargetDesc, &pRenderTarget), RADIENT_STATUS_OK);
    ASSERT_NE(pRenderTarget, nullptr);

    RadientViewDesc ViewDesc{};
    ViewDesc.Name                         = "Bloom render test view";
    ViewDesc.pScene                       = pScene;
    ViewDesc.Camera                       = CameraEntity;
    ViewDesc.pRenderTarget                = pRenderTarget;
    ViewDesc.ClearColor                   = {0.005f, 0.005f, 0.008f, 1.f};
    ViewDesc.EnableIBL                    = False;
    ViewDesc.ToneMapping.Mode             = RADIENT_TONE_MAPPING_MODE_UNCHARTED2;
    ViewDesc.ToneMapping.AutoExposure     = False;
    ViewDesc.ToneMapping.AverageLogLum    = 0.3f;
    ViewDesc.ToneMapping.LightAdaptation  = False;
    ViewDesc.TemporalAntiAliasing.Enabled = False;
    ViewDesc.Bloom.Enabled                = True;
    ViewDesc.Bloom.Intensity              = 0.35f;
    ViewDesc.Bloom.Threshold              = 1.f;
    ViewDesc.Bloom.SoftThreshold          = 0.1f;
    ViewDesc.Bloom.Radius                 = 0.75f;

    double Time = 0.0;

    RefCntAutoPtr<IRadientView> pWarmupView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pWarmupView), RADIENT_STATUS_OK);
    ASSERT_NE(pWarmupView, nullptr);

    const auto     Deadline     = std::chrono::steady_clock::now() + RenderReadyTimeout;
    RADIENT_STATUS WarmupStatus = RADIENT_STATUS_PENDING;
    while (WarmupStatus == RADIENT_STATUS_PENDING && std::chrono::steady_clock::now() < Deadline)
    {
        WarmupStatus = RenderFrameAndWait(GetRenderer(), pWarmupView, pDevice, pContext, Time);
        ASSERT_FALSE(RADIENT_FAILED(WarmupStatus));
        if (WarmupStatus == RADIENT_STATUS_PENDING)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(WarmupStatus, RADIENT_STATUS_OK)
        << "Timed out waiting for the Bloom scene and renderer resources";

    pWarmupView.Release();
    Time = 0.0;

    RefCntAutoPtr<IRadientView> pView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    ASSERT_NE(pView, nullptr);

    static constexpr Uint32 StableFrameCount = 16;
    for (Uint32 FrameIndex = 0; FrameIndex < StableFrameCount; ++FrameIndex)
    {
        ASSERT_EQ(RenderFrameAndWait(GetRenderer(), pView, pDevice, pContext, Time), RADIENT_STATUS_OK)
            << "Capture view became pending after renderer warm-up at frame " << FrameIndex;
    }

    Capture.Capture(pTestingSwapChain);
}

TEST_F(RadientRender, SSAO)
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
    ASSERT_NE(GetAssetManager(), nullptr);
    ASSERT_NE(GetRenderer(), nullptr);

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    PostFXCapture Capture;
    ASSERT_TRUE(Capture.Prepare(pDevice, pSwapChain));

    // Uniform IBL keeps the unoccluded lighting spatially constant, making
    // SSAO the only source of lighting variation in the captured image.
    static constexpr Uint32                                     EnvironmentWidth  = 16;
    static constexpr Uint32                                     EnvironmentHeight = 8;
    std::array<Uint8, EnvironmentWidth * EnvironmentHeight * 4> WhiteEnvironmentPixels;
    WhiteEnvironmentPixels.fill(255);

    RadientTextureData EnvironmentData{};
    EnvironmentData.Width  = EnvironmentWidth;
    EnvironmentData.Height = EnvironmentHeight;
    EnvironmentData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    EnvironmentData.pData  = WhiteEnvironmentPixels.data();
    EnvironmentData.Stride = EnvironmentWidth * 4;

    RadientTextureLoadInfo EnvironmentLoadInfo{};
    EnvironmentLoadInfo.pTextureData = &EnvironmentData;

    RefCntAutoPtr<IRadientTextureAsset> pEnvironmentMap;
    ASSERT_TRUE(IsPendingOrOK(GetAssetManager()->LoadTexture(EnvironmentLoadInfo, &pEnvironmentMap)));
    ASSERT_NE(pEnvironmentMap, nullptr);

    StandardMaterialValues MaterialValues{};
    MaterialValues.BaseColor = {1.f, 1.f, 1.f, 1.f};
    MaterialValues.Metallic  = 0.f;
    MaterialValues.Roughness = 1.f;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_TRUE(IsPendingOrOK(CreateStandardMaterialAsset(*GetAssetManager(), MaterialValues, &pMaterial)));
    ASSERT_NE(pMaterial, nullptr);

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Name      = "SSAO test cube";
    CubeCI.Size      = 1.f;
    CubeCI.pMaterial = pMaterial;

    RefCntAutoPtr<IRadientMeshAsset> pCube;
    ASSERT_TRUE(IsPendingOrOK(CreateRadientCubeMesh(GetAssetManager(), CubeCI, &pCube)));
    ASSERT_NE(pCube, nullptr);

    RadientSphereMeshCreateInfo SphereCI{};
    SphereCI.Name         = "SSAO test sphere";
    SphereCI.Radius       = 0.75f;
    SphereCI.Subdivisions = 16;
    SphereCI.pMaterial    = pMaterial;

    RefCntAutoPtr<IRadientMeshAsset> pSphere;
    ASSERT_TRUE(IsPendingOrOK(CreateRadientSphereMesh(GetAssetManager(), SphereCI, &pSphere)));
    ASSERT_NE(pSphere, nullptr);

    RadientSceneCreateInfo SceneCI{};
    SceneCI.Desc.Name = "SSAO render test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(GetEngine()->CreateScene(SceneCI, &pScene), RADIENT_STATUS_OK);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(GetEngine()->CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    RadientTransform FloorTransform{};
    FloorTransform.Position = {0.f, -0.125f, 0.f};
    FloorTransform.Scale    = {8.f, 0.25f, 8.f};
    ASSERT_EQ(AddDrawable(pWriter, "Floor", pCube, FloorTransform), RADIENT_STATUS_OK);

    RadientTransform BackWallTransform{};
    BackWallTransform.Position = {0.f, 2.f, -2.5f};
    BackWallTransform.Scale    = {8.f, 4.f, 0.25f};
    ASSERT_EQ(AddDrawable(pWriter, "Back wall", pCube, BackWallTransform), RADIENT_STATUS_OK);

    RadientTransform CubeTransform{};
    CubeTransform.Position = {-1.25f, 0.5f, 0.f};
    ASSERT_EQ(AddDrawable(pWriter, "Cube", pCube, CubeTransform), RADIENT_STATUS_OK);

    RadientTransform StackedCubeTransform{};
    StackedCubeTransform.Position = {-1.25f, 1.35f, 0.f};
    StackedCubeTransform.Scale    = {0.7f, 0.7f, 0.7f};
    ASSERT_EQ(AddDrawable(pWriter, "Stacked cube", pCube, StackedCubeTransform), RADIENT_STATUS_OK);

    RadientTransform SmallCubeTransform{};
    SmallCubeTransform.Position = {1.5f, 0.35f, -2.f};
    SmallCubeTransform.Scale    = {0.7f, 0.7f, 0.7f};
    ASSERT_EQ(AddDrawable(pWriter, "Wall cube", pCube, SmallCubeTransform), RADIENT_STATUS_OK);

    RadientTransform SphereTransform{};
    SphereTransform.Position = {0.3f, 0.75f, 0.7f};
    ASSERT_EQ(AddDrawable(pWriter, "Sphere", pSphere, SphereTransform), RADIENT_STATUS_OK);

    RadientTransform SmallSphereTransform{};
    SmallSphereTransform.Position = {1.5f, 0.575f, 0.25f};
    SmallSphereTransform.Scale    = {0.5f, 0.5f, 0.5f};
    ASSERT_EQ(AddDrawable(pWriter, "Floating sphere", pSphere, SmallSphereTransform), RADIENT_STATUS_OK);

    RadientEntityDesc CameraDesc{};
    CameraDesc.Name      = "SSAO test camera";
    CameraDesc.Transform = MakeCameraTransform(float3{6.f, 4.5f, 7.f},
                                               float3{0.f, 0.6f, 0.f},
                                               float3{0.f, 1.f, 0.f});

    RadientEntityID CameraEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity(CameraDesc, CameraEntity), RADIENT_STATUS_OK);

    RadientCameraComponent Camera{};
    Camera.FocalLength   = Camera.VerticalAperture / (2.f * std::tan(45.f * PI_F / 360.f));
    Camera.ClippingRange = {0.1f, 100.f};
    ASSERT_EQ(pWriter->SetCamera(CameraEntity, Camera), RADIENT_STATUS_OK);

    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name       = "SSAO render test target";
    TargetDesc.Size       = {pSwapChain->GetDesc().Width, pSwapChain->GetDesc().Height};
    TargetDesc.pSwapChain = pSwapChain;
    TargetDesc.pColorRTV  = pSwapChain->GetCurrentBackBufferRTV();
    TargetDesc.pDepthDSV  = pSwapChain->GetDepthBufferDSV();

    RefCntAutoPtr<IRadientRenderTarget> pRenderTarget;
    ASSERT_EQ(GetRenderer()->CreateRenderTarget(TargetDesc, &pRenderTarget), RADIENT_STATUS_OK);
    ASSERT_NE(pRenderTarget, nullptr);

    RadientViewDesc ViewDesc{};
    ViewDesc.Name                         = "SSAO render test view";
    ViewDesc.pScene                       = pScene;
    ViewDesc.Camera                       = CameraEntity;
    ViewDesc.pRenderTarget                = pRenderTarget;
    ViewDesc.ClearColor                   = {0.05f, 0.05f, 0.05f, 1.f};
    ViewDesc.EnableIBL                    = True;
    ViewDesc.Environment.pEnvironmentMap  = pEnvironmentMap;
    ViewDesc.ToneMapping.Mode             = RADIENT_TONE_MAPPING_MODE_NONE;
    ViewDesc.TemporalAntiAliasing.Enabled = False;
    ViewDesc.SSAO.Enabled                 = True;
    ViewDesc.SSAO.EffectRadius            = 1.f;

    double Time = 0.0;

    RefCntAutoPtr<IRadientView> pWarmupView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pWarmupView), RADIENT_STATUS_OK);
    ASSERT_NE(pWarmupView, nullptr);

    const auto     Deadline     = std::chrono::steady_clock::now() + RenderReadyTimeout;
    RADIENT_STATUS WarmupStatus = RADIENT_STATUS_PENDING;
    while (WarmupStatus == RADIENT_STATUS_PENDING && std::chrono::steady_clock::now() < Deadline)
    {
        WarmupStatus = RenderFrameAndWait(GetRenderer(), pWarmupView, pDevice, pContext, Time);
        ASSERT_FALSE(RADIENT_FAILED(WarmupStatus));
        if (WarmupStatus == RADIENT_STATUS_PENDING)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(WarmupStatus, RADIENT_STATUS_OK)
        << "Timed out waiting for the SSAO scene and renderer resources";

    // The warm-up view absorbs the variable number of frames required by
    // asynchronous asset and PSO preparation. A new view starts post-FX history
    // and simulation time at zero, giving every run the same temporal sequence.
    pWarmupView.Release();
    Time = 0.0;

    RefCntAutoPtr<IRadientView> pView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    ASSERT_NE(pView, nullptr);

    static constexpr Uint32 StableFrameCount = 16;
    for (Uint32 FrameIndex = 0; FrameIndex < StableFrameCount; ++FrameIndex)
    {
        ASSERT_EQ(RenderFrameAndWait(GetRenderer(), pView, pDevice, pContext, Time), RADIENT_STATUS_OK)
            << "Capture view became pending after renderer warm-up at frame " << FrameIndex;
    }

    Capture.Capture(pTestingSwapChain);
}

TEST_F(RadientRender, SSR)
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
    ASSERT_NE(GetAssetManager(), nullptr);
    ASSERT_NE(GetRenderer(), nullptr);

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
    ASSERT_NE(pTestingSwapChain, nullptr);

    PostFXCapture Capture;
    ASSERT_TRUE(Capture.Prepare(pDevice, pSwapChain));

    // Uniform IBL provides stable baseline illumination while the colored
    // scene geometry supplies spatially varying radiance for SSR.
    static constexpr Uint32                                     EnvironmentWidth  = 16;
    static constexpr Uint32                                     EnvironmentHeight = 8;
    std::array<Uint8, EnvironmentWidth * EnvironmentHeight * 4> WhiteEnvironmentPixels;
    WhiteEnvironmentPixels.fill(255);

    RadientTextureData EnvironmentData{};
    EnvironmentData.Width  = EnvironmentWidth;
    EnvironmentData.Height = EnvironmentHeight;
    EnvironmentData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    EnvironmentData.pData  = WhiteEnvironmentPixels.data();
    EnvironmentData.Stride = EnvironmentWidth * 4;

    RadientTextureLoadInfo EnvironmentLoadInfo{};
    EnvironmentLoadInfo.pTextureData = &EnvironmentData;

    RefCntAutoPtr<IRadientTextureAsset> pEnvironmentMap;
    ASSERT_TRUE(IsPendingOrOK(GetAssetManager()->LoadTexture(EnvironmentLoadInfo, &pEnvironmentMap)));
    ASSERT_NE(pEnvironmentMap, nullptr);

    static constexpr Uint32                              CheckerWidth    = 112;
    static constexpr Uint32                              CheckerHeight   = 64;
    static constexpr Uint32                              CheckerCellSize = 16;
    static constexpr std::array<std::array<Uint8, 3>, 2> CheckerColors{{
        {220, 205, 175},
        {115, 155, 190},
    }};
    std::array<Uint8, CheckerWidth * CheckerHeight * 4>  CheckerPixels;
    for (Uint32 y = 0; y < CheckerHeight; ++y)
    {
        for (Uint32 x = 0; x < CheckerWidth; ++x)
        {
            const auto&  Color        = CheckerColors[(x / CheckerCellSize + y / CheckerCellSize) & 1u];
            const size_t Offset       = (static_cast<size_t>(y) * CheckerWidth + x) * 4;
            CheckerPixels[Offset + 0] = Color[0];
            CheckerPixels[Offset + 1] = Color[1];
            CheckerPixels[Offset + 2] = Color[2];
            CheckerPixels[Offset + 3] = 255;
        }
    }

    RadientTextureData CheckerData{};
    CheckerData.Width  = CheckerWidth;
    CheckerData.Height = CheckerHeight;
    CheckerData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    CheckerData.pData  = CheckerPixels.data();
    CheckerData.Stride = CheckerWidth * 4;

    RadientTextureLoadInfo CheckerLoadInfo{};
    CheckerLoadInfo.pTextureData = &CheckerData;
    CheckerLoadInfo.IsSRGB       = True;

    RefCntAutoPtr<IRadientTextureAsset> pCheckerTexture;
    ASSERT_TRUE(IsPendingOrOK(GetAssetManager()->LoadTexture(CheckerLoadInfo, &pCheckerTexture)));
    ASSERT_NE(pCheckerTexture, nullptr);

    const auto CreateMaterial = [&](const char*           Name,
                                    RadientFloat4         BaseColor,
                                    Float32               Metallic,
                                    Float32               Roughness,
                                    IRadientTextureAsset* pBaseColorTexture = nullptr) {
        (void)Name;
        StandardMaterialValues Values{};
        Values.BaseColor         = BaseColor;
        Values.Metallic          = Metallic;
        Values.Roughness         = Roughness;
        Values.pBaseColorTexture = pBaseColorTexture;

        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        const RADIENT_STATUS                 Status = CreateStandardMaterialAsset(*GetAssetManager(), Values, &pMaterial);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pMaterial, nullptr);
        return pMaterial;
    };

    const auto CreateCube = [&](const char* Name, IRadientMaterialAsset* pMaterial) {
        RadientCubeMeshCreateInfo CubeCI{};
        CubeCI.Name      = Name;
        CubeCI.Size      = 1.f;
        CubeCI.pMaterial = pMaterial;

        RefCntAutoPtr<IRadientMeshAsset> pCube;
        const RADIENT_STATUS             Status = CreateRadientCubeMesh(GetAssetManager(), CubeCI, &pCube);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pCube, nullptr);
        return pCube;
    };

    const auto CreateSphere = [&](const char* Name, IRadientMaterialAsset* pMaterial) {
        RadientSphereMeshCreateInfo SphereCI{};
        SphereCI.Name         = Name;
        SphereCI.Radius       = 0.65f;
        SphereCI.Subdivisions = 16;
        SphereCI.pMaterial    = pMaterial;

        RefCntAutoPtr<IRadientMeshAsset> pSphere;
        const RADIENT_STATUS             Status = CreateRadientSphereMesh(GetAssetManager(), SphereCI, &pSphere);
        EXPECT_TRUE(IsPendingOrOK(Status));
        EXPECT_NE(pSphere, nullptr);
        return pSphere;
    };

    RefCntAutoPtr<IRadientMaterialAsset> pWallMaterial =
        CreateMaterial("SSR wall material", {1.f, 1.f, 1.f, 1.f}, 0.f, 1.f, pCheckerTexture);
    ASSERT_NE(pWallMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pWallCube = CreateCube("SSR wall cube", pWallMaterial);
    ASSERT_NE(pWallCube, nullptr);

    static constexpr std::array<Float32, 5>                                 FloorRoughness{0.f, 0.15f, 0.3f, 0.45f, 0.6f};
    std::array<RefCntAutoPtr<IRadientMaterialAsset>, FloorRoughness.size()> FloorMaterials;
    std::array<RefCntAutoPtr<IRadientMeshAsset>, FloorRoughness.size()>     FloorCubes;
    for (size_t StripeIndex = 0; StripeIndex < FloorRoughness.size(); ++StripeIndex)
    {
        const std::string Suffix    = std::to_string(StripeIndex);
        FloorMaterials[StripeIndex] = CreateMaterial(("SSR floor material " + Suffix).c_str(),
                                                     {0.35f, 0.35f, 0.35f, 1.f},
                                                     1.f,
                                                     FloorRoughness[StripeIndex]);
        ASSERT_NE(FloorMaterials[StripeIndex], nullptr);
        FloorCubes[StripeIndex] = CreateCube(("SSR floor stripe " + Suffix).c_str(), FloorMaterials[StripeIndex]);
        ASSERT_NE(FloorCubes[StripeIndex], nullptr);
    }

    static constexpr std::array<RadientFloat4, FloorRoughness.size()>     SphereColors{{
        {0.8f, 0.05f, 0.05f, 1.f},
        {0.8f, 0.55f, 0.05f, 1.f},
        {0.05f, 0.8f, 0.05f, 1.f},
        {0.05f, 0.6f, 0.8f, 1.f},
        {0.05f, 0.05f, 0.8f, 1.f},
    }};
    std::array<RefCntAutoPtr<IRadientMaterialAsset>, SphereColors.size()> SphereMaterials;
    std::array<RefCntAutoPtr<IRadientMeshAsset>, SphereColors.size()>     Spheres;
    for (size_t SphereIndex = 0; SphereIndex < SphereColors.size(); ++SphereIndex)
    {
        const std::string Suffix     = std::to_string(SphereIndex);
        SphereMaterials[SphereIndex] = CreateMaterial(("SSR sphere material " + Suffix).c_str(),
                                                      SphereColors[SphereIndex],
                                                      1.f,
                                                      0.15f);
        ASSERT_NE(SphereMaterials[SphereIndex], nullptr);
        Spheres[SphereIndex] = CreateSphere(("SSR sphere " + Suffix).c_str(), SphereMaterials[SphereIndex]);
        ASSERT_NE(Spheres[SphereIndex], nullptr);
    }

    RadientSceneCreateInfo SceneCI{};
    SceneCI.Desc.Name = "SSR render test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(GetEngine()->CreateScene(SceneCI, &pScene), RADIENT_STATUS_OK);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(GetEngine()->CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    static constexpr Float32 FloorWidth  = 7.f;
    const Float32            StripeWidth = FloorWidth / static_cast<Float32>(FloorCubes.size());
    for (size_t StripeIndex = 0; StripeIndex < FloorCubes.size(); ++StripeIndex)
    {
        RadientTransform FloorTransform{};
        FloorTransform.Position = {
            -FloorWidth * 0.5f + StripeWidth * (static_cast<Float32>(StripeIndex) + 0.5f),
            -0.1f,
            0.f,
        };
        FloorTransform.Scale = {StripeWidth, 0.2f, 7.f};

        const std::string Name = "Floor roughness stripe " + std::to_string(StripeIndex);
        ASSERT_EQ(AddDrawable(pWriter, Name.c_str(), FloorCubes[StripeIndex], FloorTransform), RADIENT_STATUS_OK);
    }

    RadientTransform BackWallTransform{};
    BackWallTransform.Position = {0.f, 2.f, -2.5f};
    BackWallTransform.Scale    = {7.f, 4.f, 0.2f};
    ASSERT_EQ(AddDrawable(pWriter, "Back wall", pWallCube, BackWallTransform), RADIENT_STATUS_OK);

    RadientTransform LeftWallTransform{};
    LeftWallTransform.Position = {-3.5f, 2.f, 0.f};
    LeftWallTransform.Scale    = {0.2f, 4.f, 7.f};
    ASSERT_EQ(AddDrawable(pWriter, "Left wall", pWallCube, LeftWallTransform), RADIENT_STATUS_OK);

    for (size_t SphereIndex = 0; SphereIndex < Spheres.size(); ++SphereIndex)
    {
        RadientTransform SphereTransform{};
        SphereTransform.Position = {
            -FloorWidth * 0.5f + StripeWidth * (static_cast<Float32>(SphereIndex) + 0.5f),
            0.75f,
            -1.5f,
        };
        const std::string Name = "Sphere " + std::to_string(SphereIndex);
        ASSERT_EQ(AddDrawable(pWriter, Name.c_str(), Spheres[SphereIndex], SphereTransform), RADIENT_STATUS_OK);
    }

    RadientEntityDesc CameraDesc{};
    CameraDesc.Name      = "SSR test camera";
    CameraDesc.Transform = MakeCameraTransform(float3{6.f, 4.5f, 8.f},
                                               float3{0.f, 0.6f, 0.f},
                                               float3{0.f, 1.f, 0.f});

    RadientEntityID CameraEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity(CameraDesc, CameraEntity), RADIENT_STATUS_OK);

    RadientCameraComponent Camera{};
    Camera.FocalLength   = Camera.VerticalAperture / (2.f * std::tan(45.f * PI_F / 360.f));
    Camera.ClippingRange = {0.1f, 100.f};
    ASSERT_EQ(pWriter->SetCamera(CameraEntity, Camera), RADIENT_STATUS_OK);

    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name       = "SSR render test target";
    TargetDesc.Size       = {pSwapChain->GetDesc().Width, pSwapChain->GetDesc().Height};
    TargetDesc.pSwapChain = pSwapChain;
    TargetDesc.pColorRTV  = pSwapChain->GetCurrentBackBufferRTV();
    TargetDesc.pDepthDSV  = pSwapChain->GetDepthBufferDSV();

    RefCntAutoPtr<IRadientRenderTarget> pRenderTarget;
    ASSERT_EQ(GetRenderer()->CreateRenderTarget(TargetDesc, &pRenderTarget), RADIENT_STATUS_OK);
    ASSERT_NE(pRenderTarget, nullptr);

    RadientViewDesc ViewDesc{};
    ViewDesc.Name                         = "SSR render test view";
    ViewDesc.pScene                       = pScene;
    ViewDesc.Camera                       = CameraEntity;
    ViewDesc.pRenderTarget                = pRenderTarget;
    ViewDesc.ClearColor                   = {0.025f, 0.025f, 0.025f, 1.f};
    ViewDesc.EnableIBL                    = True;
    ViewDesc.Environment.pEnvironmentMap  = pEnvironmentMap;
    ViewDesc.ToneMapping.Mode             = RADIENT_TONE_MAPPING_MODE_NONE;
    ViewDesc.TemporalAntiAliasing.Enabled = False;
    ViewDesc.SSR.Enabled                  = True;
    ViewDesc.SSR.RoughnessThreshold       = 0.7f;

    double Time = 0.0;

    RefCntAutoPtr<IRadientView> pWarmupView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pWarmupView), RADIENT_STATUS_OK);
    ASSERT_NE(pWarmupView, nullptr);

    const auto     Deadline     = std::chrono::steady_clock::now() + RenderReadyTimeout;
    RADIENT_STATUS WarmupStatus = RADIENT_STATUS_PENDING;
    while (WarmupStatus == RADIENT_STATUS_PENDING && std::chrono::steady_clock::now() < Deadline)
    {
        WarmupStatus = RenderFrameAndWait(GetRenderer(), pWarmupView, pDevice, pContext, Time);
        ASSERT_FALSE(RADIENT_FAILED(WarmupStatus));
        if (WarmupStatus == RADIENT_STATUS_PENDING)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(WarmupStatus, RADIENT_STATUS_OK)
        << "Timed out waiting for the SSR scene and renderer resources";

    pWarmupView.Release();
    Time = 0.0;

    RefCntAutoPtr<IRadientView> pView;
    ASSERT_EQ(GetRenderer()->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    ASSERT_NE(pView, nullptr);

    static constexpr Uint32 StableFrameCount = 16;
    for (Uint32 FrameIndex = 0; FrameIndex < StableFrameCount; ++FrameIndex)
    {
        ASSERT_EQ(RenderFrameAndWait(GetRenderer(), pView, pDevice, pContext, Time), RADIENT_STATUS_OK)
            << "Capture view became pending after renderer warm-up at frame " << FrameIndex;
    }

    Capture.Capture(pTestingSwapChain);
}

} // namespace Testing
} // namespace Diligent
