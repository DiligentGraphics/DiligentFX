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
#include "HnRenderDelegate.hpp"
#include "HnShadowMapManager.hpp"
#include "HnTokens.hpp"

#include <array>

#include "pxr/imaging/hd/sceneDelegate.h"

#include "GfTypeConversions.hpp"
#include "BasicMath.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

namespace USD
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnLightPrivateTokens,
    ((shadowResolution, "inputs:shadow:resolution"))
);
// clang-format on

HnLight* HnLight::Create(const pxr::SdfPath& Id, const pxr::TfToken& TypeId)
{
    return new HnLight{Id, TypeId};
}

HnLight::HnLight(const pxr::SdfPath& Id, const pxr::TfToken& TypeId) :
    pxr::HdLight{Id},
    m_TypeId{TypeId},
    m_SceneBounds{BoundBox::Invalid()}
{
}

HnLight::~HnLight()
{
}

pxr::HdDirtyBits HnLight::GetInitialDirtyBitsMask() const
{
    return pxr::HdLight::AllDirty;
}

//  Lookup table from:
//  Colour Rendering of Spectra
//  by John Walker
//  https://www.fourmilab.ch/documents/specrend/specrend.c
//
//  Covers range from 1000k to 10000k in 500k steps
//  assuming Rec709 / sRGB colorspace chromaticity.
//
// NOTE: 6500K doesn't give a pure white because the D65
//       illuminant used by Rec. 709 doesn't lie on the
//       Planckian Locus. We would need to compute the
//       Correlated Colour Temperature (CCT) using Ohno's
//       method to get pure white. Maybe one day.
//
// Note that the beginning and ending knots are repeated to simplify
// boundary behavior.  The last 4 knots represent the segment starting
// at 1.0.
//
static constexpr float kMinBlackbodyTemp = 1000.0f;
static constexpr float kMaxBlackbodyTemp = 10000.0f;

static constexpr std::array<float3, 22> kBblackbodyRGB = {
    float3{1.000000f, 0.027490f, 0.000000f}, //  1000 K (Approximation)
    float3{1.000000f, 0.027490f, 0.000000f}, //  1000 K (Approximation)
    float3{1.000000f, 0.149664f, 0.000000f}, //  1500 K (Approximation)
    float3{1.000000f, 0.256644f, 0.008095f}, //  2000 K
    float3{1.000000f, 0.372033f, 0.067450f}, //  2500 K
    float3{1.000000f, 0.476725f, 0.153601f}, //  3000 K
    float3{1.000000f, 0.570376f, 0.259196f}, //  3500 K
    float3{1.000000f, 0.653480f, 0.377155f}, //  4000 K
    float3{1.000000f, 0.726878f, 0.501606f}, //  4500 K
    float3{1.000000f, 0.791543f, 0.628050f}, //  5000 K
    float3{1.000000f, 0.848462f, 0.753228f}, //  5500 K
    float3{1.000000f, 0.898581f, 0.874905f}, //  6000 K
    float3{1.000000f, 0.942771f, 0.991642f}, //  6500 K
    float3{0.906947f, 0.890456f, 1.000000f}, //  7000 K
    float3{0.828247f, 0.841838f, 1.000000f}, //  7500 K
    float3{0.765791f, 0.801896f, 1.000000f}, //  8000 K
    float3{0.715255f, 0.768579f, 1.000000f}, //  8500 K
    float3{0.673683f, 0.740423f, 1.000000f}, //  9000 K
    float3{0.638992f, 0.716359f, 1.000000f}, //  9500 K
    float3{0.609681f, 0.695588f, 1.000000f}, // 10000 K
    float3{0.609681f, 0.695588f, 1.000000f}, // 10000 K
    float3{0.609681f, 0.695588f, 1.000000f}, // 10000 K
};

// Catmull-Rom basis
static constexpr float kCRBasis[4][4] = {
    {-0.5f, 1.5f, -1.5f, 0.5f},
    {1.0f, -2.5f, 2.0f, -0.5f},
    {-0.5f, 0.0f, 0.5f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
};

static inline float Rec709RgbToLuma(const float3& rgb)
{
    return dot(rgb, float3{0.2126f, 0.7152f, 0.0722f});
}


static float3 BlackbodyTemperatureAsRgb(float temp)
{
    // Catmull-Rom interpolation of kBblackbodyRGB
    constexpr int numKnots = kBblackbodyRGB.size();

    // Parametric distance along spline
    const float u_spline = clamp((temp - kMinBlackbodyTemp) / (kMaxBlackbodyTemp - kMinBlackbodyTemp), 0.0f, 1.0f);

    // Last 4 knots represent a trailing segment starting at u_spline==1.0,
    // to simplify boundary behavior
    constexpr int numSegs = (numKnots - 4);
    const float   x       = u_spline * numSegs;
    const int     seg     = static_cast<int>(std::floor(x));
    const float   u_seg   = x - seg; // Parameter within segment

    // Knot values for this segment
    float3 k0 = kBblackbodyRGB[seg + 0];
    float3 k1 = kBblackbodyRGB[seg + 1];
    float3 k2 = kBblackbodyRGB[seg + 2];
    float3 k3 = kBblackbodyRGB[seg + 3];

    // Compute cubic coefficients.
    float3 a = kCRBasis[0][0] * k0 + kCRBasis[0][1] * k1 + kCRBasis[0][2] * k2 + kCRBasis[0][3] * k3;
    float3 b = kCRBasis[1][0] * k0 + kCRBasis[1][1] * k1 + kCRBasis[1][2] * k2 + kCRBasis[1][3] * k3;
    float3 c = kCRBasis[2][0] * k0 + kCRBasis[2][1] * k1 + kCRBasis[2][2] * k2 + kCRBasis[2][3] * k3;
    float3 d = kCRBasis[3][0] * k0 + kCRBasis[3][1] * k1 + kCRBasis[3][2] * k2 + kCRBasis[3][3] * k3;

    // Eval cubic polynomial.
    float3 rgb = ((a * u_seg + b) * u_seg + c) * u_seg + d;

    // Clamp at zero, since the spline can produce small negative values,
    // e.g. in the blue component at 1300k.
    rgb = std::max(rgb, float3{0, 0, 0});

    // Normalize to the same luminance as (1, 1, 1)
    rgb /= Rec709RgbToLuma(rgb);

    return rgb;
}

static float3 GetLightColor(pxr::HdSceneDelegate& SceneDelegate, const pxr::SdfPath& Id)
{
    float3 Color{1, 1, 1};

    {
        const pxr::VtValue ColorVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->color);
        if (ColorVal.IsHolding<pxr::GfVec3f>())
        {
            Color = ToFloat3(ColorVal.Get<pxr::GfVec3f>());
        }
    }

    {
        const pxr::VtValue EnableColorTempVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->enableColorTemperature);
        if (EnableColorTempVal.GetWithDefault<bool>(false))
        {
            pxr::VtValue ColorTempVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->colorTemperature);
            if (ColorTempVal.IsHolding<float>())
            {
                float  ColorTemp      = ColorTempVal.Get<float>();
                float3 BlackbodyColor = BlackbodyTemperatureAsRgb(ColorTemp);
                Color *= BlackbodyColor;
            }
        }
    }

    return Color;
}

static float GetLightIntensity(pxr::HdSceneDelegate& SceneDelegate, const pxr::SdfPath& Id)
{
    float Intensity = 1;

    const pxr::VtValue IntensityVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->intensity);
    if (IntensityVal.IsHolding<float>())
    {
        Intensity = IntensityVal.Get<float>();
    }

    return Intensity;
}

static float GetLightExposedPower(pxr::HdSceneDelegate& SceneDelegate, const pxr::SdfPath& Id)
{
    float ExposedPower = 1;

    const pxr::VtValue ExposureVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->exposure);
    if (ExposureVal.IsHolding<float>())
    {
        float Exposure = ExposureVal.Get<float>();
        ExposedPower   = std::pow(2.0f, clamp(Exposure, -50.0f, 50.0f));
    }

    return ExposedPower;
}

// Returns the area of the maximum possible facing profile.
static float GetLightArea(pxr::HdSceneDelegate& SceneDelegate, const pxr::SdfPath& Id, const pxr::TfToken LightType, float MetersPerUnit)
{
    if (LightType == pxr::HdPrimTypeTokens->diskLight ||
        LightType == pxr::HdPrimTypeTokens->sphereLight)
    {
        pxr::VtValue RadiusVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->radius);
        if (RadiusVal.IsHolding<float>())
        {
            float radius = RadiusVal.Get<float>() * MetersPerUnit;
            // Return area of the facing disk, not the sphere surface area
            return radius * radius * PI_F;
        }
    }
    else if (LightType == pxr::HdPrimTypeTokens->rectLight)
    {
        pxr::VtValue WidthVal  = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->width);
        pxr::VtValue HeightVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->height);
        if (WidthVal.IsHolding<float>() && HeightVal.IsHolding<float>())
        {
            float Width  = WidthVal.Get<float>() * MetersPerUnit;
            float Height = HeightVal.Get<float>() * MetersPerUnit;
            return Width * Height;
        }
    }
    else if (LightType == pxr::HdPrimTypeTokens->cylinderLight)
    {
        pxr::VtValue LengthVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->length);
        pxr::VtValue RadiusVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->radius);
        if (LengthVal.IsHolding<float>() && RadiusVal.IsHolding<float>())
        {
            float length = LengthVal.Get<float>() * MetersPerUnit;
            float radius = RadiusVal.Get<float>() * MetersPerUnit;
            // Return area of the facing rectangle, not the cylinder surface area
            return length * radius;
        }
    }
    else if (LightType == pxr::HdPrimTypeTokens->distantLight)
    {
        pxr::VtValue AngleDegVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->angle);
        if (AngleDegVal.IsHolding<float>())
        {
            // Convert from cone apex angle to solid angle
            float AngleGrad            = AngleDegVal.Get<float>();
            float AngleRadians         = DegToRad(AngleGrad);
            float SolidAngleSteradians = 2.f * PI_F * (1.f - std::cos(AngleRadians / 2.0));
            return SolidAngleSteradians;
        }
    }

    return 1.0;
}

static float GetShapingConeAngle(pxr::HdSceneDelegate& SceneDelegate, const pxr::SdfPath& Id)
{
    float ShapingConeAngle = 0;

    pxr::VtValue ShapingConeVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->shapingConeAngle);
    if (ShapingConeVal.IsHolding<float>())
    {
        ShapingConeAngle = DegToRad(ShapingConeVal.Get<float>());
    }

    return ShapingConeAngle;
}


bool HnLight::ApproximateAreaLight(pxr::HdSceneDelegate& SceneDelegate, float MetersPerUnit)
{
    bool ParamsDirty = false;

    const pxr::SdfPath& Id = GetId();

    // Light color
    {
        const float3 Color = GetLightColor(SceneDelegate, Id);
        if (Color != m_Params.Color)
        {
            m_Params.Color = Color;
            ParamsDirty    = true;
        }
    }

    // Light intensity
    {
        float Intensity    = GetLightIntensity(SceneDelegate, Id);
        float ExposedPower = GetLightExposedPower(SceneDelegate, Id);
        Intensity *= ExposedPower;

        pxr::VtValue NormalizeVal = SceneDelegate.GetLightParamValue(Id, pxr::HdLightTokens->normalize);
        if (!NormalizeVal.GetWithDefault<bool>(false))
        {
            float LightArea = GetLightArea(SceneDelegate, Id, m_TypeId, MetersPerUnit);
            Intensity *= LightArea;
        }

        if (Intensity != m_Params.Intensity)
        {
            m_Params.Intensity = Intensity;
            ParamsDirty        = true;
        }
    }

    if (m_TypeId == pxr::HdPrimTypeTokens->rectLight ||
        m_TypeId == pxr::HdPrimTypeTokens->diskLight)
    {
        float ShapingConeAngle = GetShapingConeAngle(SceneDelegate, Id);
        if (ShapingConeAngle != m_Params.OuterConeAngle)
        {
            m_Params.InnerConeAngle = 0;
            m_Params.OuterConeAngle = ShapingConeAngle;
            ParamsDirty             = true;
        }
    }

    return ParamsDirty;
}

void HnLight::ComputeDirectLightProjMatrix(pxr::HdSceneDelegate& SceneDelegate)
{
    pxr::HdRenderIndex& RenderIndex = SceneDelegate.GetRenderIndex();

    BoundBox LightSpaceBounds{BoundBox::Invalid()};
    if (!m_SceneBounds.IsValid())
    {
        // First time compute accurate scene bounds in light space by projecting
        // each primitive's bounding box into light space.
        // Also, compute the scnene bounds in world space.

        const pxr::SdfPathVector& RPrimIds = RenderIndex.GetRprimIds();
        for (const pxr::SdfPath& RPrimId : RPrimIds)
        {
            if (RPrimId.IsEmpty())
                continue;

            const pxr::GfRange3d PrimExtent = SceneDelegate.GetExtent(RPrimId);
            if (PrimExtent.IsEmpty())
                continue;

            const BoundBox PrimBB        = ToBoundBox(PrimExtent);
            const float4x4 PrimTransform = ToMatrix4x4<float>(SceneDelegate.GetTransform(RPrimId));
            for (Uint32 i = 0; i < 8; ++i)
            {
                float4 Corner = {PrimBB.GetCorner(i), 1.0};

                Corner        = Corner * PrimTransform;
                m_SceneBounds = m_SceneBounds.Enclose(Corner);

                Corner           = Corner * m_ViewMatrix;
                LightSpaceBounds = LightSpaceBounds.Enclose(Corner);
            }
        }
    }
    else
    {
        // Use precomputed scene bounds in world space. This is less accurate, but
        // much faster.
        for (Uint32 i = 0; i < 8; ++i)
        {
            float4 Corner    = {m_SceneBounds.GetCorner(i), 1.0};
            Corner           = Corner * m_ViewMatrix;
            LightSpaceBounds = LightSpaceBounds.Enclose(Corner);
        }
    }

    IRenderDevice*          pDevice    = static_cast<const HnRenderDelegate*>(RenderIndex.GetRenderDelegate())->GetDevice();
    const RenderDeviceInfo& DeviceInfo = pDevice->GetDeviceInfo();

    m_ProjMatrix = float4x4::OrthoOffCenter(LightSpaceBounds.Min.x, LightSpaceBounds.Max.x,
                                            LightSpaceBounds.Min.y, LightSpaceBounds.Max.y,
                                            LightSpaceBounds.Min.z, LightSpaceBounds.Max.z,
                                            DeviceInfo.NDC.MinZ == -1);
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
            m_IsVisible        = IsVisible;
            LightDirty         = true;
            m_IsShadowMapDirty = true;
        }
    }

    bool ShadowTransformDirty = false;
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

            float3 LightSpaceX, LightSpaceY, LightSpaceZ;
            BasisFromDirection(m_Direction, true, LightSpaceX, LightSpaceY, LightSpaceZ);
            m_ViewMatrix = float4x4::ViewFromBasis(LightSpaceX, LightSpaceY, LightSpaceZ);

            LightDirty = true;
        }

        ShadowTransformDirty = true;

        *DirtyBits &= ~DirtyTransform;
    }

    if (*DirtyBits & DirtyParams)
    {
        auto LightType = GLTF::Light::TYPE::UNKNOWN;
        if (m_TypeId == pxr::HdPrimTypeTokens->distantLight)
        {
            LightType = GLTF::Light::TYPE::DIRECTIONAL;
        }
        else if (m_TypeId == pxr::HdPrimTypeTokens->cylinderLight ||
                 m_TypeId == pxr::HdPrimTypeTokens->diskLight ||
                 m_TypeId == pxr::HdPrimTypeTokens->rectLight ||
                 m_TypeId == pxr::HdPrimTypeTokens->sphereLight)
        {
            LightType = m_Params.OuterConeAngle > 0 ?
                GLTF::Light::TYPE::SPOT :
                GLTF::Light::TYPE::POINT;
        }

        if (LightType != m_Params.Type)
        {
            m_Params.Type        = LightType;
            LightDirty           = true;
            ShadowTransformDirty = true;
        }

        if (m_TypeId == pxr::HdPrimTypeTokens->cylinderLight ||
            m_TypeId == pxr::HdPrimTypeTokens->diskLight ||
            m_TypeId == pxr::HdPrimTypeTokens->distantLight ||
            m_TypeId == pxr::HdPrimTypeTokens->rectLight ||
            m_TypeId == pxr::HdPrimTypeTokens->sphereLight)
        {
            float MetersPerUnit = 1;
            if (RenderParam != nullptr)
            {
                MetersPerUnit = static_cast<const HnRenderParam*>(RenderParam)->GetMetersPerUnit();
            }
            if (ApproximateAreaLight(*SceneDelegate, MetersPerUnit))
            {
                LightDirty = true;
            }
        }
        else
        {
            LOG_ERROR_MESSAGE("Unsupported light type: ", m_TypeId);
        }

        *DirtyBits &= ~DirtyParams;
    }

    pxr::HdRenderIndex& RenderIndex    = SceneDelegate->GetRenderIndex();
    HnRenderDelegate*   RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex.GetRenderDelegate());
    HnShadowMapManager* ShadowMapMgr   = RenderDelegate->GetShadowMapManager();
    if (*DirtyBits & DirtyShadowParams)
    {
        if (ShadowMapMgr != nullptr)
        {
            const TextureDesc& ShadowMapDesc = ShadowMapMgr->GetAtlasDesc();

            const pxr::VtValue ShadowResolutionVal = SceneDelegate->GetLightParamValue(Id, HnLightPrivateTokens->shadowResolution);
            if (ShadowResolutionVal.IsHolding<int>())
            {
                m_ShadowMapResolution = static_cast<Uint32>(ShadowResolutionVal.Get<int>());
            }

            if (m_ShadowMapResolution > ShadowMapDesc.Width ||
                m_ShadowMapResolution > ShadowMapDesc.Height)
            {
                LOG_WARNING_MESSAGE("Requested shadow map resolution ", m_ShadowMapResolution, "x", m_ShadowMapResolution,
                                    "  for light ", Id, " is too large for the shadow map atlas ", ShadowMapDesc.Width, "x", ShadowMapDesc.Height);
                m_ShadowMapResolution = std::min(ShadowMapDesc.Width, ShadowMapDesc.Height);
            }

            if (m_ShadowMapSuballocation &&
                m_ShadowMapSuballocation->GetSize() != uint2{m_ShadowMapResolution, m_ShadowMapResolution})
            {
                m_ShadowMapSuballocation.Release();
                m_ShadowMapShaderInfo.reset();
                LightDirty = true;
            }
        }

        *DirtyBits &= ~DirtyShadowParams;
    }

    if (ShadowMapMgr != nullptr)
    {
        const bool ShadowsEnabled =
            SceneDelegate->GetLightParamValue(Id, pxr::HdLightTokens->shadowEnable).GetWithDefault<bool>(false) &&
            m_Params.Type == GLTF::Light::TYPE::DIRECTIONAL;
        if (!ShadowsEnabled && m_ShadowMapSuballocation)
        {
            m_ShadowMapSuballocation.Release();
            m_ShadowMapShaderInfo.reset();
            LightDirty = true;
        }

        if (ShadowsEnabled && !m_ShadowMapSuballocation)
        {
            m_ShadowMapSuballocation = ShadowMapMgr->Allocate(m_ShadowMapResolution, m_ShadowMapResolution);
            if (!m_ShadowMapSuballocation)
            {
                LOG_ERROR_MESSAGE("Failed to allocate shadow map for light ", Id);
            }
            m_ShadowMapShaderInfo = std::make_unique<HLSL::PBRShadowMapInfo>();

            const float4& UVScaleBias      = m_ShadowMapSuballocation->GetUVScaleBias();
            m_ShadowMapShaderInfo->UVScale = {UVScaleBias.x, UVScaleBias.y};
            m_ShadowMapShaderInfo->UVBias  = {UVScaleBias.z, UVScaleBias.w};

            m_ShadowMapShaderInfo->ShadowMapSlice = static_cast<float>(m_ShadowMapSuballocation->GetSlice());

            ShadowTransformDirty = true;

            LightDirty = true;
        }

        if (m_ShadowMapShaderInfo && ShadowTransformDirty)
        {
            if (m_TypeId == pxr::HdPrimTypeTokens->distantLight)
            {
                ComputeDirectLightProjMatrix(*SceneDelegate);
            }
            else
            {
                UNEXPECTED("Only distant light is supported for shadow map");
            }
            m_ViewProjMatrix = m_ViewMatrix * m_ProjMatrix;

            m_ShadowMapShaderInfo->WorldToLightProjSpace = m_ViewProjMatrix.Transpose();

            LightDirty         = true;
            m_IsShadowMapDirty = true;
        }
    }

    if (LightDirty && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::Light);
    }

    *DirtyBits = HdLight::Clean;
}

} // namespace USD

} // namespace Diligent
