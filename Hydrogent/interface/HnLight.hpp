/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "pxr/imaging/hd/light.h"

#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/AdvancedMath.hpp"
#include "../../../DiligentTools/AssetLoader/interface/GLTFLoader.hpp"

namespace Diligent
{

struct ITextureAtlasSuballocation;

namespace HLSL
{
struct PBRShadowMapInfo;
}

namespace USD
{

/// Light implementation in Hydrogent.
class HnLight final : public pxr::HdLight
{
public:
    static HnLight* Create(const pxr::SdfPath& Id, const pxr::TfToken& TypeId);

    ~HnLight();

    // Synchronizes state from the delegate to this object.
    virtual void Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    // Returns the minimal set of dirty bits to place in the
    // change tracker for use in the first sync of this prim.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    const float3&      GetPosition() const { return m_Position; }
    const float3&      GetDirection() const { return m_Direction; }
    const GLTF::Light& GetParams() const { return m_Params; }
    bool               IsVisible() const { return m_IsVisible; }
    const float4x4&    GetViewMatrix() const { return m_ViewMatrix; }
    const float4x4&    GetProjMatrix() const { return m_ProjMatrix; }
    const float4x4&    GetViewProjMatrix() const { return m_ViewProjMatrix; }
    bool               ShadowsEnabled() const { return m_ShadowMapSuballocation != nullptr && m_SceneBounds.IsValid(); }

    /// Sets the index of the light's frame attributes data in the frame attribs buffer.
    /// This index is passed to the HnRenderDelegate::GetShadowPassFrameAttribsSRB
    /// method to set the offset in the frame attribs buffer.
    void SetFrameAttribsIndex(Int32 Index) { m_FrameAttribsIndex = Index; }

    /// Returns the index of the light's frame attributes data in the frame attribs buffer.
    Int32 GetFrameAttribsIndex() const { return m_FrameAttribsIndex; }

    ITextureAtlasSuballocation*   GetShadowMapSuballocation() const { return m_ShadowMapSuballocation; }
    const HLSL::PBRShadowMapInfo* GetShadowMapShaderInfo() const { return m_ShadowMapShaderInfo.get(); }

    bool IsShadowMapDirty() const { return m_IsShadowMapDirty; }
    void SetShadowMapDirty(bool IsDirty) { m_IsShadowMapDirty = IsDirty; }

private:
    HnLight(const pxr::SdfPath& Id, const pxr::TfToken& TypeId);

    bool ApproximateAreaLight(pxr::HdSceneDelegate& SceneDelegate, float MetersPerUnit);
    void ComputeDirectLightProjMatrix(pxr::HdSceneDelegate& SceneDelegate);

private:
    const pxr::TfToken m_TypeId;

    float3      m_Position;
    float3      m_Direction;
    GLTF::Light m_Params;
    bool        m_IsVisible        = true;
    bool        m_IsShadowMapDirty = true;

    float4x4 m_ViewMatrix;
    float4x4 m_ProjMatrix;
    float4x4 m_ViewProjMatrix;
    BoundBox m_SceneBounds;

    Int32                                     m_FrameAttribsIndex   = 0;
    Uint32                                    m_ShadowMapResolution = 1024;
    RefCntAutoPtr<ITextureAtlasSuballocation> m_ShadowMapSuballocation;
    std::unique_ptr<HLSL::PBRShadowMapInfo>   m_ShadowMapShaderInfo;
};

} // namespace USD

} // namespace Diligent
