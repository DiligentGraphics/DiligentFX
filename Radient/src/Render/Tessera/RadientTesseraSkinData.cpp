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

#include "Render/Tessera/RadientTesseraSkinData.hpp"

#include "DebugUtilities.hpp"

#include <limits>

namespace Diligent
{

namespace
{

constexpr Uint32 JointMatrixSize = static_cast<Uint32>(sizeof(RadientMatrix4x4));

struct WriteSkinningMatricesAttribs
{
    IRadientSkeletonPose& Pose;
    IRadientSkinAsset&    Skin;
    Uint32                JointCount;
    bool                  PackMatrixRowMajor;
};

RADIENT_STATUS WriteSkinningMatrices(void* pData, Uint32 Size, void* pUserData)
{
    VERIFY_EXPR(pData != nullptr);
    VERIFY_EXPR(pUserData != nullptr);

    WriteSkinningMatricesAttribs& Attribs = *static_cast<WriteSkinningMatricesAttribs*>(pUserData);
    VERIFY_EXPR(Size == JointMatrixSize * Attribs.JointCount);

    return Attribs.Pose.ComputeSkinningMatrices(
        &Attribs.Skin,
        static_cast<RadientMatrix4x4*>(pData),
        !Attribs.PackMatrixRowMajor,
        False);
}

} // namespace

RadientTesseraBufferSuballocator::CreateInfo GetTesseraJointBufferCreateInfo()
{
    RadientTesseraBufferSuballocator::CreateInfo CI;
    CI.Desc.Name              = "Radient Tessera joint matrices";
    CI.Desc.Usage             = USAGE_DEFAULT;
    CI.Desc.BindFlags         = BIND_SHADER_RESOURCE;
    CI.Desc.Mode              = BUFFER_MODE_STRUCTURED;
    CI.Desc.ElementByteStride = JointMatrixSize;
    CI.Desc.Size              = CI.InitialSize;
    CI.AllocationAlignment    = JointMatrixSize;
    return CI;
}

RadientTesseraSkinData::RadientTesseraSkinData(IRadientSkinAsset*                pSkin,
                                               IRadientSkeletonPose*             pPose,
                                               RadientTesseraBufferSuballocator& JointBuffer) :
    m_pSkin{pSkin},
    m_pPose{pPose},
    m_JointBuffer{JointBuffer}
{
    VERIFY_EXPR(m_pSkin != nullptr);
    VERIFY_EXPR(m_pPose != nullptr);

    const RadientSkinDesc& SkinDesc = m_pSkin->GetDesc();
    VERIFY_EXPR(SkinDesc.pSkeleton != nullptr);
    VERIFY_EXPR(SkinDesc.pSkeleton == m_pPose->GetSkeleton());

    if (SkinDesc.JointCount == 0 ||
        SkinDesc.JointCount > (std::numeric_limits<Uint32>::max)() / (2u * JointMatrixSize))
    {
        UNEXPECTED("Invalid Tessera skin joint count");
        return;
    }

    m_JointCount            = SkinDesc.JointCount;
    m_PaletteByteSize       = m_JointCount * JointMatrixSize;
    m_JointBufferAllocation = m_JointBuffer.Allocate(2u * m_PaletteByteSize);
    if (!m_JointBufferAllocation)
    {
        LOG_ERROR_MESSAGE("Failed to allocate Tessera joint palettes");
        return;
    }

    const Uint32 AllocationOffset = m_JointBufferAllocation.GetOffset();
    VERIFY_EXPR(AllocationOffset % JointMatrixSize == 0);
    m_HalfFirstJoints[0] = AllocationOffset / JointMatrixSize;
    m_HalfFirstJoints[1] = m_HalfFirstJoints[0] + m_JointCount;
}

RADIENT_STATUS RadientTesseraSkinData::Prepare(bool PackMatrixRowMajor)
{
    if (!m_JointBufferAllocation)
        return RADIENT_STATUS_FAILED;

    const RADIENT_STATUS UpdateStatus = m_pPose->UpdateGlobalTransforms();
    if (RADIENT_FAILED(UpdateStatus))
        return UpdateStatus;

    const Uint64 PoseVersion = m_pPose->GetVersion();
    if (m_IsPrepared && m_PreparedPoseVersion == PoseVersion)
    {
        if (m_PreviousFirstJoint == m_FirstJoint)
            return RADIENT_STATUS_NO_CHANGE;

        // The pose stopped changing. Reference the current palette as both
        // current and previous so subsequent frames produce zero motion.
        m_PreviousFirstJoint = m_FirstJoint;
        return RADIENT_STATUS_OK;
    }

    const Uint32 DestinationHalf = m_IsPrepared ? 1u - m_CurrentHalf : 0u;

    WriteSkinningMatricesAttribs WriteAttribs{
        *m_pPose,
        *m_pSkin,
        m_JointCount,
        PackMatrixRowMajor,
    };
    const RADIENT_STATUS PoseStatus = m_JointBuffer.Update(
        m_JointBufferAllocation,
        DestinationHalf * m_PaletteByteSize,
        m_PaletteByteSize,
        WriteSkinningMatrices,
        &WriteAttribs);
    if (PoseStatus != RADIENT_STATUS_OK)
        return PoseStatus;

    m_PreviousFirstJoint = m_IsPrepared ? m_HalfFirstJoints[m_CurrentHalf] : m_HalfFirstJoints[DestinationHalf];
    m_FirstJoint         = m_HalfFirstJoints[DestinationHalf];

    m_CurrentHalf         = DestinationHalf;
    m_PreparedPoseVersion = PoseVersion;
    m_IsPrepared          = true;
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
