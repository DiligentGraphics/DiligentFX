/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include "HnCamera.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderParam.hpp"

#include "GfTypeConversions.hpp"
#include "BasicMath.hpp"

#include "pxr/imaging/hd/sceneDelegate.h"

namespace Diligent
{

namespace USD
{

HnCamera* HnCamera::Create(const pxr::SdfPath& Id)
{
    return new HnCamera{Id};
}

HnCamera::HnCamera(const pxr::SdfPath& Id) :
    pxr::HdCamera{Id}
{
}

HnCamera::~HnCamera()
{
}

void HnCamera::Sync(pxr::HdSceneDelegate* SceneDelegate,
                    pxr::HdRenderParam*   RenderParam,
                    pxr::HdDirtyBits*     DirtyBits)
{
    pxr::HdDirtyBits OrigDirtyBits = *DirtyBits;
    pxr::HdCamera::Sync(SceneDelegate, RenderParam, DirtyBits);

    const HnRenderParam::Configuration& RenderConfig  = static_cast<const HnRenderParam*>(RenderParam)->GetConfig();
    const float                         MetersPerUnit = RenderConfig.MetersPerUnit;
    const float                         UnitsPerMeter = 1.f / MetersPerUnit;
    if (OrigDirtyBits & pxr::HdCamera::DirtyTransform)
    {
        // USD camera transform is defined in scene units, with camera looking along -Z axis.
        // Diligent camera transform is defined in world units, with camera looking along +Z axis.
        m_WorldMatrix = float4x4::Scale(UnitsPerMeter, UnitsPerMeter, -UnitsPerMeter) * ToFloat4x4(_transform);
        m_ViewMatrix  = m_WorldMatrix.Inverse();
    }

    if (OrigDirtyBits & pxr::HdCamera::DirtyParams)
    {
        // By an odd convention, lens and filmback properties are measured in tenths of a scene unit rather than "raw" scene units.
        // https://openusd.org/dev/api/class_usd_geom_camera.html#UsdGeom_CameraUnits
        // So, for example after
        //      UsdCamera.GetFocalLengthAttr().Set(30.f)
        // Reading the attribute will return same value:
        //      float focalLength;
        //      UsdCamera.GetFocalLengthAttr().Get(&focalLength); // focalLength == 30
        //
        // Scene delegate, however, applies 0.1 scale when reading the property, so we don't need to
        //      focalLength = SceneDelegate->GetCameraParamValue(id, HdCameraTokens->focalLength).Get<float>(); //  focalLength == 3
        const float          HorzApertureUnits  = GetHorizontalAperture();
        const float          VertApertureUnits  = GetVerticalAperture();
        const float          FocalLengthUnits   = GetFocalLength();
        const pxr::GfRange1f ClippingRangeUnits = GetClippingRange();

        // Diligent expects camera attributes in world units
        const float          HorzApertureMeters  = HorzApertureUnits * MetersPerUnit;
        const float          VertApertureMeters  = VertApertureUnits * MetersPerUnit;
        const pxr::GfRange1f ClippingRangeMeters = ClippingRangeUnits * MetersPerUnit;

        const HnRenderDelegate* pRenderDelegate      = static_cast<const HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate());
        const IRenderDevice*    pDevice              = pRenderDelegate->GetDevice();
        const RenderDeviceInfo& DeviceInfo           = pDevice->GetDeviceInfo();
        const bool              NegativeOneToOneNDCZ = DeviceInfo.GetNDCAttribs().MinZ == -1;

        float NearPlane = ClippingRangeMeters.GetMin();
        float FarPlane  = ClippingRangeMeters.GetMax();
        if (RenderConfig.UseReverseDepth)
        {
            std::swap(NearPlane, FarPlane);
        }
        if (GetProjection() == pxr::HdCamera::Projection::Perspective)
        {
            m_ProjectionMatrix = {};

            m_ProjectionMatrix._11 = FocalLengthUnits / (0.5f * HorzApertureUnits);
            m_ProjectionMatrix._22 = FocalLengthUnits / (0.5f * VertApertureUnits);

            m_ProjectionMatrix.SetNearFarClipPlanes(NearPlane, FarPlane, NegativeOneToOneNDCZ);
        }
        else if (GetProjection() == pxr::HdCamera::Projection::Orthographic)
        {

            m_ProjectionMatrix = float4x4::Ortho(HorzApertureMeters, VertApertureMeters, NearPlane, FarPlane, NegativeOneToOneNDCZ);
        }
        else
        {
            LOG_ERROR_MESSAGE("Unknown camera projection");
            m_ProjectionMatrix = float4x4::Identity();
        }
    }
}

} // namespace USD

} // namespace Diligent
