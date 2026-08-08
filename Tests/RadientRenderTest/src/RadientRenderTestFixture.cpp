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

#include "FileSystem.hpp"
#include "GPUTestingEnvironment.hpp"
#include "RadientEngine.h"
#include "RefCntAutoPtr.hpp"

#include <memory>

namespace Diligent
{
namespace Testing
{

namespace
{

constexpr char EnvironmentMapFileName[] = "colosseum_1k.hdr";

class RadientRenderSuiteResources
{
public:
    RadientRenderSuiteResources(IRenderDevice*  pDevice,
                                IDeviceContext* pContext,
                                ISwapChain*     pSwapChain,
                                const char*     EnvironmentMapPath) :
        m_pContext{pContext}
    {
        RadientEngineCreateInfo EngineCI{};
        EngineCI.Backend.Desc.Name         = "Radient render test backend";
        EngineCI.Backend.Desc.Type         = RADIENT_BACKEND_TYPE_LOCAL;
        EngineCI.Backend.pDevice           = pDevice;
        EngineCI.Backend.pImmediateContext = pContext;
        EngineCI.Backend.pSwapChain        = pSwapChain;
        EngineCI.Assets.Desc.Name          = "Radient render test assets";
        m_Status                           = CreateRadientEngine(EngineCI, &m_pEngine);
        if (RADIENT_FAILED(m_Status))
            return;

        m_Status = m_pEngine->GetAssetManager(&m_pAssetManager);
        if (RADIENT_FAILED(m_Status))
            return;

        RadientTextureLoadInfo EnvironmentLoadInfo{};
        EnvironmentLoadInfo.URI = EnvironmentMapPath;
        m_Status                = m_pAssetManager->LoadTexture(EnvironmentLoadInfo, &m_pEnvironmentMap);
        if (m_Status != RADIENT_STATUS_OK && m_Status != RADIENT_STATUS_PENDING)
            return;
        if (m_pEnvironmentMap == nullptr)
        {
            m_Status = RADIENT_STATUS_INVALID_OPERATION;
            return;
        }

        RadientRendererDesc RendererDesc{};
        RendererDesc.Name                           = "Radient render test renderer";
        RendererDesc.EnableAsyncPipelineCompilation = False;
        m_Status                                    = m_pEngine->CreateRenderer(RendererDesc, &m_pRenderer);
        if (RADIENT_FAILED(m_Status))
            return;

        // The view is shared by every test so renderer state and IBL cubemaps
        // survive while per-test scenes and render targets are replaced.
        RadientViewDesc ViewDesc{};
        ViewDesc.Name                         = "Radient render test view";
        ViewDesc.ClearColor                   = {0.05f, 0.05f, 0.05f, 1.f};
        ViewDesc.ToneMapping.AutoExposure     = False;
        ViewDesc.ToneMapping.LightAdaptation  = False;
        ViewDesc.TemporalAntiAliasing.Enabled = False;
        ViewDesc.Environment.pEnvironmentMap  = m_pEnvironmentMap;
        ViewDesc.Skybox.Source                = RADIENT_SKYBOX_SOURCE_PREFILTERED_ENVIRONMENT;
        ViewDesc.Skybox.MipLevel              = 1;
        m_Status                              = m_pRenderer->CreateView(ViewDesc, &m_pView);
    }

    ~RadientRenderSuiteResources()
    {
        if (m_pView != nullptr)
        {
            m_pView->SetRenderTarget(nullptr);
            m_pView->SetCamera(InvalidRadientEntityID);
            m_pView->SetScene(nullptr);
        }

        if (m_pAssetManager != nullptr)
            m_pAssetManager->Stop(m_pContext);
    }

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    IRadientEngine* GetEngine() const
    {
        return m_pEngine;
    }

    IRadientAssetManager* GetAssetManager() const
    {
        return m_pAssetManager;
    }

    IRadientRenderer* GetRenderer() const
    {
        return m_pRenderer;
    }

    IRadientTextureAsset* GetEnvironmentMap() const
    {
        return m_pEnvironmentMap;
    }

    IRadientView* GetView() const
    {
        return m_pView;
    }

private:
    IDeviceContext* m_pContext = nullptr;

    RefCntAutoPtr<IRadientEngine>       m_pEngine;
    RefCntAutoPtr<IRadientAssetManager> m_pAssetManager;
    RefCntAutoPtr<IRadientTextureAsset> m_pEnvironmentMap;
    RefCntAutoPtr<IRadientRenderer>     m_pRenderer;
    RefCntAutoPtr<IRadientView>         m_pView;
    RADIENT_STATUS                      m_Status = RADIENT_STATUS_INVALID_OPERATION;
};

std::unique_ptr<RadientRenderSuiteResources> s_pSuiteResources;

} // namespace

void RadientRenderTestFixture::SetUpTestSuite()
{
    GPUTestingEnvironment* const pEnvironment = GPUTestingEnvironment::GetInstance();
    ASSERT_NE(pEnvironment, nullptr);

    IRenderDevice* const  pDevice    = pEnvironment->GetDevice();
    IDeviceContext* const pContext   = pEnvironment->GetDeviceContext();
    ISwapChain* const     pSwapChain = pEnvironment->GetSwapChain();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);
    ASSERT_NE(pSwapChain, nullptr);

    const RadientRenderTestOptions& Options = GetRadientRenderTestOptions();
    const std::string               EnvironmentMapPath =
        FileSystem::JoinPath(FileSystem::JoinPath(Options.AssetsDirectory, "Environments"),
                             EnvironmentMapFileName);
    ASSERT_TRUE(FileSystem::FileExists(EnvironmentMapPath.c_str()))
        << "Environment map does not exist: " << EnvironmentMapPath;

    s_pSuiteResources =
        std::make_unique<RadientRenderSuiteResources>(pDevice, pContext, pSwapChain, EnvironmentMapPath.c_str());
    ASSERT_EQ(s_pSuiteResources->GetStatus(), RADIENT_STATUS_OK);
}

void RadientRenderTestFixture::TearDownTestSuite()
{
    s_pSuiteResources.reset();
}

IRadientEngine* RadientRenderTestFixture::GetEngine()
{
    return s_pSuiteResources != nullptr ? s_pSuiteResources->GetEngine() : nullptr;
}

IRadientAssetManager* RadientRenderTestFixture::GetAssetManager()
{
    return s_pSuiteResources != nullptr ? s_pSuiteResources->GetAssetManager() : nullptr;
}

IRadientRenderer* RadientRenderTestFixture::GetRenderer()
{
    return s_pSuiteResources != nullptr ? s_pSuiteResources->GetRenderer() : nullptr;
}

IRadientTextureAsset* RadientRenderTestFixture::GetEnvironmentMap()
{
    return s_pSuiteResources != nullptr ? s_pSuiteResources->GetEnvironmentMap() : nullptr;
}

IRadientView* RadientRenderTestFixture::GetView()
{
    return s_pSuiteResources != nullptr ? s_pSuiteResources->GetView() : nullptr;
}

} // namespace Testing
} // namespace Diligent
