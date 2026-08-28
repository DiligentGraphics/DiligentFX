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

#include "RadientSkinning.h"

#include "RefCntAutoPtr.hpp"

#include <vector>

namespace Diligent
{

/// Tessera-side joint palettes for one skin and pose.
///
/// Each palette matrix transforms a mesh-space vertex through the inverse bind
/// transform and the corresponding skeleton-space joint transform. The
/// primitive transform to world space is applied separately by the renderer.
/// The object retains its skin and pose and is not thread-safe.
class RadientTesseraSkinData final
{
public:
    RadientTesseraSkinData(IRadientSkinAsset*    pSkin,
                           IRadientSkeletonPose* pPose);

    RadientTesseraSkinData(const RadientTesseraSkinData&)            = delete;
    RadientTesseraSkinData& operator=(const RadientTesseraSkinData&) = delete;

    /// Updates dirty pose globals and rebuilds the current palette when their
    /// version changes. The old current palette becomes the previous palette.
    /// On the first successful preparation, both palettes contain the same
    /// matrices. Failures leave the last valid palettes unchanged.
    RADIENT_STATUS Prepare();

    bool Matches(IRadientSkinAsset* pSkin, IRadientSkeletonPose* pPose) const noexcept
    {
        return m_pSkin == pSkin && m_pPose == pPose;
    }

    IRadientSkinAsset* GetSkin() const noexcept
    {
        return m_pSkin;
    }

    IRadientSkeletonPose* GetPose() const noexcept
    {
        return m_pPose;
    }

    bool IsPrepared() const noexcept
    {
        return m_IsPrepared;
    }

    Uint64 GetPreparedPoseVersion() const noexcept
    {
        return m_PreparedPoseVersion;
    }

    Uint32 GetJointCount() const noexcept
    {
        return static_cast<Uint32>(m_CurrentJointMatrices.size());
    }

    const std::vector<RadientMatrix4x4>& GetCurrentJointMatrices() const noexcept
    {
        return m_CurrentJointMatrices;
    }

    const std::vector<RadientMatrix4x4>& GetPreviousJointMatrices() const noexcept
    {
        return m_PreviousJointMatrices;
    }

private:
    RefCntAutoPtr<IRadientSkinAsset>    m_pSkin;
    RefCntAutoPtr<IRadientSkeletonPose> m_pPose;

    std::vector<RadientMatrix4x4> m_CurrentJointMatrices;
    std::vector<RadientMatrix4x4> m_PreviousJointMatrices;

    Uint64 m_PreparedPoseVersion = 0;
    bool   m_IsPrepared          = false;
};

} // namespace Diligent
