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
#include "Render/RadientFrameSRBCache.hpp"

#include "PBR_Renderer.hpp"

#include <utility>

namespace Diligent
{

namespace
{

constexpr float IBLClearColor[] = {0.5f, 0.5f, 0.5f, 0.5f};

bool IsValidEnvironment(const RadientEnvironmentDesc& Environment)
{
    return (Environment.pEnvironmentMap == nullptr ||
            Environment.pEnvironmentMap->GetType() == RADIENT_ASSET_TYPE_TEXTURE) &&
        RadientMath::IsFiniteNonNegative(Environment.Color) &&
        RadientMath::IsFiniteNonNegative(Environment.Intensity) &&
        RadientMath::IsFinite(Environment.Exposure);
}

bool IsValidToneMapping(const RadientToneMappingDesc& ToneMapping)
{
    return ToneMapping.Mode < RADIENT_TONE_MAPPING_MODE_COUNT &&
        RadientMath::IsFinitePositive(ToneMapping.MiddleGray) &&
        RadientMath::IsFinitePositive(ToneMapping.WhitePoint) &&
        RadientMath::IsFiniteNonNegative(ToneMapping.LuminanceSaturation) &&
        RadientMath::IsFiniteNonNegative(ToneMapping.AgX.Saturation) &&
        RadientMath::IsFiniteNonNegative(ToneMapping.AgX.Slope) &&
        RadientMath::IsFiniteNonNegative(ToneMapping.AgX.Power) &&
        RadientMath::IsFinite(ToneMapping.AgX.Offset);
}

bool IsValidBloom(const RadientBloomDesc& Bloom)
{
    return RadientMath::IsFiniteNonNegative(Bloom.Intensity) &&
        RadientMath::IsFiniteNonNegative(Bloom.Threshold) &&
        RadientMath::IsFiniteNonNegative(Bloom.SoftThreshold) &&
        Bloom.SoftThreshold <= 1.f &&
        RadientMath::IsFiniteNonNegative(Bloom.Radius) &&
        Bloom.Radius <= 1.f;
}

bool IsValidTemporalAntiAliasing(const RadientTemporalAntiAliasingDesc& TemporalAntiAliasing)
{
    return RadientMath::IsFiniteNonNegative(TemporalAntiAliasing.TemporalStabilityFactor) &&
        TemporalAntiAliasing.TemporalStabilityFactor <= 1.f;
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
    if (!IsValidEnvironment(Desc.Environment) ||
        !IsValidToneMapping(Desc.ToneMapping) ||
        !IsValidBloom(Desc.Bloom) ||
        !IsValidTemporalAntiAliasing(Desc.TemporalAntiAliasing))
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

RADIENT_STATUS RadientViewImpl::SetToneMapping(const RadientToneMappingDesc& ToneMapping)
{
    if (!IsValidToneMapping(ToneMapping))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_Desc.ToneMapping == ToneMapping)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.ToneMapping = ToneMapping;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetBloom(const RadientBloomDesc& Bloom)
{
    if (!IsValidBloom(Bloom))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_Desc.Bloom == Bloom)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.Bloom = Bloom;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetTemporalAntiAliasing(const RadientTemporalAntiAliasingDesc& TemporalAntiAliasing)
{
    if (!IsValidTemporalAntiAliasing(TemporalAntiAliasing))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_Desc.TemporalAntiAliasing == TemporalAntiAliasing)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.TemporalAntiAliasing = TemporalAntiAliasing;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::Prepare(PBR_Renderer&   Renderer,
                                        IDeviceContext* pContext)
{
    if (pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    bool ResourcesCreated = false;
    if (m_pIBLResources == nullptr)
    {
        const RADIENT_STATUS Status = CreateIBLResources(Renderer, pContext);
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
                                                   IDeviceContext* pContext)
{
    RefCntAutoPtr<ITexture> pIrradianceCube =
        Renderer.CreateIrradianceCube(pContext,
                                      "Radient irradiance cube map",
                                      PBR_Renderer::IrradianceCubeFmt,
                                      PBR_Renderer::IrradianceCubeDim,
                                      IBLClearColor);
    RefCntAutoPtr<ITextureView> pIrradianceCubeSRV;
    if (pIrradianceCube != nullptr)
        pIrradianceCubeSRV = pIrradianceCube->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<ITexture> pPrefilteredEnvMap =
        Renderer.CreatePrefilteredEnvMap(pContext,
                                         "Radient prefiltered environment map",
                                         PBR_Renderer::PrefilteredEnvMapFmt,
                                         PBR_Renderer::PrefilteredEnvMapDim,
                                         IBLClearColor);
    RefCntAutoPtr<ITextureView> pPrefilteredEnvMapSRV;
    if (pPrefilteredEnvMap != nullptr)
        pPrefilteredEnvMapSRV = pPrefilteredEnvMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    if (pIrradianceCubeSRV == nullptr || pPrefilteredEnvMapSRV == nullptr)
    {
        UNEXPECTED("Failed to create Radient view IBL resources");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    m_pIBLResources = std::make_unique<RadientIBLResources>(pIrradianceCubeSRV, pPrefilteredEnvMapSRV);
    return RADIENT_STATUS_OK;
}

void RadientViewImpl::PrecomputeIBLCubemaps(PBR_Renderer&   Renderer,
                                            IDeviceContext* pContext,
                                            ITextureView*   pEnvironmentMapSRV)
{
    PBR_Renderer::PrecomputeCubemapsAttribs Attribs;
    Attribs.pEnvironmentMapSRV = pEnvironmentMapSRV;
    Attribs.pIrradianceCube    = GetIrradianceCubeSRV() != nullptr ? GetIrradianceCubeSRV()->GetTexture() : nullptr;
    Attribs.pPrefilteredEnvMap = GetPrefilteredEnvMapSRV() != nullptr ? GetPrefilteredEnvMapSRV()->GetTexture() : nullptr;
    Renderer.PrecomputeCubemaps(pContext, Attribs);
}

ITextureView* RadientViewImpl::GetIrradianceCubeSRV() const noexcept
{
    return m_pIBLResources != nullptr ? m_pIBLResources->GetIrradianceCubeSRV() : nullptr;
}

ITextureView* RadientViewImpl::GetPrefilteredEnvMapSRV() const noexcept
{
    return m_pIBLResources != nullptr ? m_pIBLResources->GetPrefilteredEnvMapSRV() : nullptr;
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
