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

#include "Core/RadientViewImpl.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Math/RadientMath.hpp"
#include "Render/RadientPBRRenderer.hpp"

#include <utility>

namespace Diligent
{

namespace
{

bool IsValidEnvironment(const RadientEnvironmentDesc& Environment)
{
    return (Environment.pEnvironmentMap == nullptr ||
            Environment.pEnvironmentMap->GetType() == RADIENT_ASSET_TYPE_TEXTURE) &&
        RadientMath::IsFiniteNonNegative(Environment.Color) &&
        RadientMath::IsFiniteNonNegative(Environment.Intensity) &&
        RadientMath::IsFinite(Environment.Exposure);
}

} // namespace

RadientViewImpl::RadientViewImpl(IReferenceCounters* pRefCounters, const RadientViewDesc& Desc) :
    TBase{pRefCounters},
    m_Name{Desc.Name != nullptr ? Desc.Name : ""},
    m_Desc{Desc},
    m_pScene{Desc.pScene},
    m_pRenderTarget{Desc.pRenderTarget}
{
    m_Desc.Name = m_Name.c_str();
    CopyEnvironment(Desc.Environment);
    CopySkybox(Desc.Skybox);
}

RadientViewImpl::~RadientViewImpl()
{
}

RefCntAutoPtr<IRadientView> RadientViewImpl::Create(const RadientViewDesc& Desc)
{
    if (!IsValidEnvironment(Desc.Environment))
        return {};

    return RefCntAutoPtr<RadientViewImpl>{MakeNewRCObj<RadientViewImpl>()(Desc)};
}

const RadientViewDesc& RadientViewImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientViewImpl::SetScene(IRadientScene* pScene)
{
    if (m_pScene == pScene)
        return RADIENT_STATUS_NO_CHANGE;

    m_pScene      = pScene;
    m_Desc.pScene = m_pScene;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetCamera(RadientEntityID Camera)
{
    if (m_Desc.Camera == Camera)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.Camera = Camera;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetRenderTarget(IRadientRenderTarget* pRenderTarget)
{
    if (m_pRenderTarget == pRenderTarget)
        return RADIENT_STATUS_NO_CHANGE;

    m_pRenderTarget      = pRenderTarget;
    m_Desc.pRenderTarget = m_pRenderTarget;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetEnvironment(const RadientEnvironmentDesc& Environment)
{
    if (!IsValidEnvironment(Environment))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_Desc.Environment == Environment)
        return RADIENT_STATUS_NO_CHANGE;

    CopyEnvironment(Environment);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetSkybox(const RadientSkyboxDesc& Skybox)
{
    RadientSkyboxDesc NewSkybox = Skybox;
    NewSkybox.pTexture          = Skybox.pTexture;

    if (m_Desc.Skybox == NewSkybox)
        return RADIENT_STATUS_NO_CHANGE;

    m_pSkyboxTexture   = Skybox.pTexture;
    NewSkybox.pTexture = m_pSkyboxTexture;
    m_Desc.Skybox      = NewSkybox;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::Prepare(PBR_Renderer&   Renderer,
                                        IDeviceContext* pContext,
                                        IBuffer*        pFrameAttribsCB)
{
    if (pContext == nullptr || pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    bool ResourcesCreated = false;
    if (m_pIrradianceCubeSRV == nullptr ||
        m_pPrefilteredEnvMapSRV == nullptr ||
        m_pFrameSRB == nullptr)
    {
        const RADIENT_STATUS Status = CreateIBLResources(Renderer, pContext, pFrameAttribsCB);
        if (RADIENT_FAILED(Status))
            return Status;

        ResourcesCreated = true;
    }

    IRadientTextureAsset*               pEnvironmentMap         = m_Desc.Environment.pEnvironmentMap;
    ITextureView*                       pDirtyEnvironmentSRV    = nullptr;
    RefCntAutoPtr<IRadientTextureAsset> pPreparedEnvironmentMap = m_WeakPreparedEnvironmentMap.Lock();
    if (pEnvironmentMap != nullptr &&
        (ResourcesCreated || pPreparedEnvironmentMap != pEnvironmentMap))
    {
        pDirtyEnvironmentSRV = RadientAssetManagerImpl::GetTextureSRV(pEnvironmentMap);
    }

    // Keep the current IBL unchanged while a replacement environment is unavailable.
    if (pDirtyEnvironmentSRV != nullptr)
    {
        PrecomputeIBLCubemaps(Renderer, pContext, pDirtyEnvironmentSRV);
        m_WeakPreparedEnvironmentMap = pEnvironmentMap;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::CreateIBLResources(PBR_Renderer&   Renderer,
                                                   IDeviceContext* pContext,
                                                   IBuffer*        pFrameAttribsCB)
{
    RefCntAutoPtr<ITexture> pIrradianceCube = Renderer.CreateIrradianceCube(pContext, "Radient irradiance cube map");
    if (pIrradianceCube != nullptr)
        m_pIrradianceCubeSRV = pIrradianceCube->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<ITexture> pPrefilteredEnvMap = Renderer.CreatePrefilteredEnvMap(pContext, "Radient prefiltered environment map");
    if (pPrefilteredEnvMap != nullptr)
        m_pPrefilteredEnvMapSRV = pPrefilteredEnvMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    if (m_pIrradianceCubeSRV != nullptr && m_pPrefilteredEnvMapSRV != nullptr)
    {
        Renderer.CreateResourceBinding(m_pFrameSRB.GetAddressOfEmpty(), 0);
        if (m_pFrameSRB != nullptr)
        {
            constexpr bool BindPrimitiveAttribsBuffer = false;
            constexpr bool BindMaterialAttribsBuffer  = false;
            Renderer.InitCommonSRBVars(m_pFrameSRB,
                                       pFrameAttribsCB,
                                       BindPrimitiveAttribsBuffer,
                                       BindMaterialAttribsBuffer);
            Renderer.SetIBLResourceViews(m_pFrameSRB,
                                         m_pIrradianceCubeSRV,
                                         m_pPrefilteredEnvMapSRV);
        }
    }

    if (m_pIrradianceCubeSRV == nullptr ||
        m_pPrefilteredEnvMapSRV == nullptr ||
        m_pFrameSRB == nullptr)
    {
        m_pIrradianceCubeSRV.Release();
        m_pPrefilteredEnvMapSRV.Release();
        m_pFrameSRB.Release();
        UNEXPECTED("Failed to create Radient view IBL resources");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    return RADIENT_STATUS_OK;
}

void RadientViewImpl::PrecomputeIBLCubemaps(PBR_Renderer&   Renderer,
                                            IDeviceContext* pContext,
                                            ITextureView*   pEnvironmentMapSRV)
{
    PBR_Renderer::PrecomputeCubemapsAttribs Attribs;
    Attribs.pEnvironmentMapSRV = pEnvironmentMapSRV;
    Attribs.pIrradianceCube    = m_pIrradianceCubeSRV != nullptr ? m_pIrradianceCubeSRV->GetTexture() : nullptr;
    Attribs.pPrefilteredEnvMap = m_pPrefilteredEnvMapSRV != nullptr ? m_pPrefilteredEnvMapSRV->GetTexture() : nullptr;
    Renderer.PrecomputeCubemaps(pContext, Attribs);
}

void RadientViewImpl::CopyEnvironment(const RadientEnvironmentDesc& Environment)
{
    m_pEnvironmentMap                  = Environment.pEnvironmentMap;
    m_Desc.Environment                 = Environment;
    m_Desc.Environment.pEnvironmentMap = m_pEnvironmentMap;
}

void RadientViewImpl::CopySkybox(const RadientSkyboxDesc& Skybox)
{
    m_pSkyboxTexture       = Skybox.pTexture;
    m_Desc.Skybox          = Skybox;
    m_Desc.Skybox.pTexture = m_pSkyboxTexture;
}

} // namespace Diligent
