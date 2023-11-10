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

#include "HnCamera.hpp"

#include "pxr/imaging/hd/changeTracker.h"

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

    if (OrigDirtyBits & pxr::HdChangeTracker::DirtyTransform)
    {
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                m_WorldMatrix[r][c] = static_cast<float>(_transform[r][c]);
            }
        }
        m_ViewMatrix = m_WorldMatrix.Inverse();
    }
}

void HnCamera::SetViewMatrix(const float4x4& ViewMatrix)
{
    m_ViewMatrix  = ViewMatrix;
    m_WorldMatrix = m_ViewMatrix.Inverse();
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            _transform[r][c] = m_WorldMatrix[r][c];
        }
    }
}

void HnCamera::SetProjectionMatrix(const float4x4& ProjectionMatrix)
{
    m_ProjectionMatrix = ProjectionMatrix;
}

} // namespace USD

} // namespace Diligent
