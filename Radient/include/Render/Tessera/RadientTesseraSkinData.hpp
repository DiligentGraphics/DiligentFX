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

#include "Render/Tessera/RadientTesseraBufferSuballocator.hpp"
#include "RadientSkinning.h"

#include "RefCntAutoPtr.hpp"

namespace Diligent
{

/// Returns the configuration used by Tessera's shared structured joint buffer.
RadientTesseraBufferSuballocator::CreateInfo GetTesseraJointBufferCreateInfo();

/// Tessera-side joint palettes for one skin and pose.
///
/// Each palette matrix transforms a mesh-space vertex through the inverse bind
/// transform and the corresponding skeleton-space joint transform. The
/// attachment-specific skeleton-to-mesh transform and the primitive transform
/// to world space are applied separately by the renderer. The object retains
/// its skin and pose and is not thread-safe.
class RadientTesseraSkinData final
{
public:
    RadientTesseraSkinData(IRadientSkinAsset*                pSkin,
                           IRadientSkeletonPose*             pPose,
                           RadientTesseraBufferSuballocator& JointBuffer);

    RadientTesseraSkinData(const RadientTesseraSkinData&)            = delete;
    RadientTesseraSkinData& operator=(const RadientTesseraSkinData&) = delete;

    /// Updates dirty pose globals and writes a new palette when their version
    /// changes. Repeated calls for the same frame are idempotent; if the pose
    /// changes again, the current-frame palette is replaced while the previous
    /// frame remains intact. On the first unchanged subsequent frame, both
    /// roles are pointed at the current half so motion vectors become zero. On
    /// the first successful preparation, both roles reference the populated
    /// half. Failures leave the last valid roles unchanged.
    RADIENT_STATUS Prepare(Uint32 FrameIndex, bool PackMatrixRowMajor = true);

    bool Matches(IRadientSkinAsset*    pSkin,
                 IRadientSkeletonPose* pPose) const noexcept
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
        return m_JointCount;
    }

    /// Returns the structured-buffer element at which the current joint
    /// palette begins.
    Uint32 GetFirstJoint() const noexcept
    {
        return m_FirstJoint;
    }

    /// Returns the structured-buffer element at which the previous joint
    /// palette begins. This equals GetFirstJoint() after initial preparation.
    Uint32 GetPreviousFirstJoint() const noexcept
    {
        return m_PreviousFirstJoint;
    }

    const RadientTesseraBufferAllocation& GetJointBufferAllocation() const noexcept
    {
        return m_JointBufferAllocation;
    }

private:
    RefCntAutoPtr<IRadientSkinAsset>    m_pSkin;
    RefCntAutoPtr<IRadientSkeletonPose> m_pPose;

    RadientTesseraBufferSuballocator& m_JointBuffer;
    RadientTesseraBufferAllocation    m_JointBufferAllocation;

    Uint32 m_JointCount         = 0;
    Uint32 m_PaletteByteSize    = 0;
    Uint32 m_CurrentHalf        = 0;
    Uint32 m_HalfFirstJoints[2] = {~Uint32{0}, ~Uint32{0}};
    Uint32 m_FirstJoint         = ~Uint32{0};
    Uint32 m_PreviousFirstJoint = ~Uint32{0};

    Uint64 m_PreparedPoseVersion = 0;
    Uint32 m_PreparedFrameIndex  = ~Uint32{0};
    bool   m_IsPrepared          = false;
};

} // namespace Diligent
