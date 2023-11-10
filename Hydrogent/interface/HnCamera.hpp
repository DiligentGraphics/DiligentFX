/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "pxr/imaging/hd/camera.h"

#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace USD
{

/// Camera implementation in Hydrogent.
class HnCamera final : public pxr::HdCamera
{
public:
    static HnCamera* Create(const pxr::SdfPath& Id);

    ~HnCamera();

    void Sync(pxr::HdSceneDelegate* SceneDelegate,
              pxr::HdRenderParam*   RenderParam,
              pxr::HdDirtyBits*     DirtyBits) override final;

    void SetViewMatrix(const float4x4& ViewMatrix);
    void SetProjectionMatrix(const float4x4& ProjectionMatrix);

    const float4x4& GetViewMatrix() const { return m_ViewMatrix; }
    const float4x4& GetWorldMatrix() const { return m_WorldMatrix; }
    const float4x4& GetProjectionMatrix() const { return m_ProjectionMatrix; }

private:
    HnCamera(const pxr::SdfPath& Id);

private:
    float4x4 m_ViewMatrix;
    float4x4 m_WorldMatrix;
    float4x4 m_ProjectionMatrix;
};

} // namespace USD

} // namespace Diligent
