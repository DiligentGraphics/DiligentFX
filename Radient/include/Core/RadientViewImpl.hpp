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

#pragma once

#include "RadientRenderer.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>
#include <string>

namespace Diligent
{

struct RadientTextureSamplingInfo;

class PBR_Renderer;
class RadientIBLResources;

class RadientViewImpl final : public ObjectBase<IRadientView>
{
public:
    using TBase = ObjectBase<IRadientView>;

    RadientViewImpl(IReferenceCounters* pRefCounters, const RadientViewDesc& Desc);
    ~RadientViewImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientView, TBase)

    static RefCntAutoPtr<IRadientView> Create(const RadientViewDesc& Desc);

    virtual const RadientViewDesc& DILIGENT_CALL_TYPE GetDesc() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetScene(IRadientScene* pScene) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetCamera(RadientEntityID Camera) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetRenderTarget(IRadientRenderTarget* pRenderTarget) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetClearColor(const RadientFloat4& ClearColor) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDebugVisualization(RADIENT_DEBUG_VISUALIZATION DebugVisualization) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetEnvironment(const RadientEnvironmentDesc& Environment) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSkybox(const RadientSkyboxDesc& Skybox) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetToneMapping(const RadientToneMappingDesc& ToneMapping) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetBloom(const RadientBloomDesc& Bloom) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTemporalAntiAliasing(const RadientTemporalAntiAliasingDesc& TemporalAntiAliasing) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSSAO(const RadientSSAODesc& SSAO) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSSR(const RadientSSRDesc& SSR) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDepthOfField(const RadientDepthOfFieldDesc& DepthOfField) override final;

    // Lazily creates the view-owned IBL cubemaps and refreshes their contents
    // when the environment changes.
    RADIENT_STATUS Prepare(PBR_Renderer&   Renderer,
                           IDeviceContext* pContext);

    RadientIBLResources* GetIBLResources() const noexcept { return m_pIBLResources.get(); }
    ITextureView*        GetIrradianceCubeSRV() const noexcept;
    ITextureView*        GetPrefilteredEnvMapSRV() const noexcept;

private:
    RADIENT_STATUS CreateIBLResources(PBR_Renderer&   Renderer,
                                      IDeviceContext* pContext);

    void PrecomputeIBLCubemaps(PBR_Renderer&                     Renderer,
                               IDeviceContext*                   pContext,
                               ITextureView*                     pEnvironmentMapSRV,
                               const RadientTextureSamplingInfo& SamplingInfo);

    void CopyEnvironment(const RadientEnvironmentDesc& Environment);
    void CopySkybox(const RadientSkyboxDesc& Skybox);

private:
    std::string m_Name;

    RadientViewDesc m_Desc;

    RefCntAutoPtr<IRadientScene>        m_pScene;
    RefCntAutoPtr<IRadientRenderTarget> m_pRenderTarget;
    RefCntAutoPtr<IRadientTextureAsset> m_pEnvironmentMap;
    RefCntAutoPtr<IRadientTextureAsset> m_pSkyboxTexture;

    std::unique_ptr<RadientIBLResources> m_pIBLResources;

    RefCntWeakPtr<IRadientTextureAsset> m_WeakPreparedEnvironmentMap;
};

} // namespace Diligent
