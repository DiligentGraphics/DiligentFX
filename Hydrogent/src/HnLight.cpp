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

#include "HnLight.hpp"
#include "HnRenderParam.hpp"

#include "pxr/imaging/hd/sceneDelegate.h"

#include "GfTypeConversions.hpp"

namespace Diligent
{

namespace USD
{

HnLight* HnLight::Create(const pxr::SdfPath& Id)
{
    return new HnLight{Id};
}

HnLight::HnLight(const pxr::SdfPath& Id) :
    pxr::HdLight{Id}
{
}

HnLight::~HnLight()
{
}

pxr::HdDirtyBits HnLight::GetInitialDirtyBitsMask() const
{
    return pxr::HdLight::AllDirty;
}

void HnLight::Sync(pxr::HdSceneDelegate* SceneDelegate,
                   pxr::HdRenderParam*   RenderParam,
                   pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits == pxr::HdLight::Clean)
        return;

    const pxr::SdfPath& Id = GetId();

    bool LightDirty = false;

    {
        bool IsVisible = SceneDelegate->GetVisible(Id);
        if (IsVisible != m_IsVisible)
        {
            m_IsVisible = IsVisible;
            LightDirty  = true;
        }
    }

    if (*DirtyBits & DirtyTransform)
    {
        const pxr::GfMatrix4d Transform = SceneDelegate->GetTransform(Id);

        const float3 Position = ToFloat3(Transform.ExtractTranslation());
        if (Position != m_Position)
        {
            m_Position = Position;
            LightDirty = true;
        }

        // Convention is to emit light along -Z
        const pxr::GfVec4d zDir      = Transform.GetRow(2);
        const float3       Direction = -ToFloat3(pxr::GfVec3d{zDir[0], zDir[1], zDir[2]});
        if (Direction != m_Direction)
        {
            m_Direction = Direction;
            LightDirty  = true;
        }

        *DirtyBits &= ~DirtyTransform;
    }

    if (*DirtyBits & DirtyParams)
    {
        const float Intensity = SceneDelegate->GetLightParamValue(Id, pxr::HdLightTokens->intensity).Get<float>();
        if (Intensity != m_Params.Intensity)
        {
            m_Params.Intensity = Intensity;
            LightDirty         = true;
        }

        const float3 Color = ToFloat3(SceneDelegate->GetLightParamValue(Id, pxr::HdLightTokens->color).Get<pxr::GfVec3f>());
        if (Color != m_Params.Color)
        {
            m_Params.Color = Color;
            LightDirty     = true;
        }

        auto LightType = GLTF::Light::TYPE::DIRECTIONAL;
        if (!SceneDelegate->GetLightParamValue(Id, pxr::HdLightTokens->radius).IsEmpty())
        {
            LightType = GLTF::Light::TYPE::POINT;
        }
        else
        {
            const pxr::VtValue ShapingConeVal = SceneDelegate->GetLightParamValue(Id, pxr::HdLightTokens->shapingConeAngle);
            if (!ShapingConeVal.IsEmpty())
            {
                LightType = GLTF::Light::TYPE::SPOT;

                const float ShapingConeAngle = DegToRad(ShapingConeVal.Get<float>());
                if (ShapingConeAngle != m_Params.OuterConeAngle)
                {
                    m_Params.InnerConeAngle = 0;
                    m_Params.OuterConeAngle = ShapingConeAngle;
                    LightDirty              = true;
                }
            }
        }

        //m_Params.Range;

        if (LightType != m_Params.Type)
        {
            m_Params.Type = LightType;
            LightDirty    = true;
        }

        *DirtyBits &= ~DirtyParams;
    }

    if (LightDirty && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::Light);
    }

    *DirtyBits = HdLight::Clean;
}

} // namespace USD

} // namespace Diligent
