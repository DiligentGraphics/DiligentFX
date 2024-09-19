/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "Computations/HnSkinningComputation.hpp"
#include "HnExtComputation.hpp"
#include "HnTokens.hpp"
#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnSkinningComputationPrivateTokens,
    (skinnedPoints)
    (skinningXforms)
);
// clang-format on

std::unique_ptr<HnSkinningComputation> HnSkinningComputation::Create(HnExtComputation& Owner)
{
    return std::make_unique<HnSkinningComputation>(Owner);
}

HnSkinningComputation::HnSkinningComputation(HnExtComputation& Owner) :
    HnExtComputationImpl{Owner, Type}
{
    VERIFY_EXPR(IsCompatible(m_Owner));
}

HnSkinningComputation::~HnSkinningComputation()
{
}

void HnSkinningComputation::Sync(pxr::HdSceneDelegate* SceneDelegate,
                                 pxr::HdRenderParam*   RenderParam,
                                 pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdExtComputation::DirtySceneInput)
    {
        const pxr::SdfPath& Id = m_Owner.GetId();

        pxr::VtValue SkinningXformsVal = SceneDelegate->GetExtComputationInput(Id, HnSkinningComputationPrivateTokens->skinningXforms);
        if (SkinningXformsVal.IsHolding<pxr::VtMatrix4fArray>())
        {
            m_Xforms = SkinningXformsVal.Get<pxr::VtMatrix4fArray>();
        }
        else
        {
            LOG_ERROR_MESSAGE("Skinning transforms of computation ", Id, " are of type ", SkinningXformsVal.GetTypeName(), ", but VtMatrix4fArray is expected");
        }
    }

    *DirtyBits &= pxr::HdExtComputation::Clean;
}

bool HnSkinningComputation::IsCompatible(const HnExtComputation& Owner)
{
    const pxr::HdExtComputationOutputDescriptorVector& Outputs = Owner.GetComputationOutputs();
    return Outputs.size() == 1 && Outputs[0].name == HnSkinningComputationPrivateTokens->skinnedPoints;
}

} // namespace USD

} // namespace Diligent
