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
#include "HnRenderParam.hpp"
#include "DebugUtilities.hpp"
#include "GfTypeConversions.hpp"
#include "HnRenderDelegate.hpp"

namespace Diligent
{

namespace USD
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnSkinningComputationPrivateTokens,
    (skinnedPoints)
    (skinningXforms)
    (primWorldToLocal)
    (skelLocalToWorld)
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

        {
            pxr::VtValue SkinningXformsVal = SceneDelegate->GetExtComputationInput(Id, HnSkinningComputationPrivateTokens->skinningXforms);
            if (SkinningXformsVal.IsHolding<pxr::VtMatrix4fArray>())
            {
                VERIFY_EXPR(m_Xforms.size() == 2);
                m_CurrXformsIdx              = 1 - m_CurrXformsIdx;
                pxr::VtMatrix4fArray& Xforms = m_Xforms[m_CurrXformsIdx];

                Xforms       = SkinningXformsVal.UncheckedGet<pxr::VtMatrix4fArray>();
                m_XformsHash = pxr::TfHash{}(Xforms);

                m_LastXformSyncFrameNumber = static_cast<const HnRenderParam*>(RenderParam)->GetFrameNumber();
                static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::SkinningXForms);

                const HnRenderDelegate* RenderDelegate = static_cast<const HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate());
                const USD_Renderer&     USDRenderer    = *RenderDelegate->GetUSDRenderer();
                const Uint32            MaxJointCount  = USDRenderer.GetSettings().MaxJointCount;
                if (Xforms.size() > MaxJointCount)
                {
                    LOG_ERROR_MESSAGE("Skinning transforms of computation ", Id, " contain ", Xforms.size(), " elements, but the maximum number of joints supported by the renderer is ", MaxJointCount);
                }
            }
            else
            {
                LOG_ERROR_MESSAGE("Skinning transforms of computation ", Id, " are of type ", SkinningXformsVal.GetTypeName(), ", but VtMatrix4fArray is expected");
            }
        }

        {
            pxr::VtValue PrimWorldToLocalVal = SceneDelegate->GetExtComputationInput(Id, HnSkinningComputationPrivateTokens->primWorldToLocal);
            if (!PrimWorldToLocalVal.IsEmpty())
            {
                if (PrimWorldToLocalVal.IsHolding<pxr::GfMatrix4d>())
                {
                    m_PrimWorldToLocal = ToFloat4x4(PrimWorldToLocalVal.UncheckedGet<pxr::GfMatrix4d>());
                }
                else
                {
                    LOG_ERROR_MESSAGE("PrimWorldToLocal of computation ", Id, " is of type ", PrimWorldToLocalVal.GetTypeName(), ", but GfMatrix4d is expected");
                    m_PrimWorldToLocal = float4x4::Identity();
                }
            }
            else
            {
                m_PrimWorldToLocal = float4x4::Identity();
            }
        }

        {
            pxr::VtValue SkelLocalToWorldVal = SceneDelegate->GetExtComputationInput(Id, HnSkinningComputationPrivateTokens->skelLocalToWorld);
            if (!SkelLocalToWorldVal.IsEmpty())
            {
                if (SkelLocalToWorldVal.IsHolding<pxr::GfMatrix4d>())
                {
                    m_SkelLocalToWorld = ToFloat4x4(SkelLocalToWorldVal.UncheckedGet<pxr::GfMatrix4d>());
                }
                else
                {
                    LOG_ERROR_MESSAGE("SkelLocalToWorld of computation ", Id, " is of type ", SkelLocalToWorldVal.GetTypeName(), ", but GfMatrix4d is expected");
                    m_SkelLocalToWorld = float4x4::Identity();
                }
            }
            else
            {
                m_SkelLocalToWorld = float4x4::Identity();
            }
        }

        m_SkelLocalToPrimLocal = m_SkelLocalToWorld * m_PrimWorldToLocal;
    }

    *DirtyBits &= pxr::HdExtComputation::Clean;
}

bool HnSkinningComputation::IsCompatible(const HnExtComputation& Owner)
{
    const pxr::HdExtComputationOutputDescriptorVector& Outputs = Owner.GetComputationOutputs();
    return Outputs.size() == 1 && Outputs[0].name == HnSkinningComputationPrivateTokens->skinnedPoints;
}

const pxr::VtMatrix4fArray& HnSkinningComputation::GetPrevFrameXforms(Uint32 FrameNumber) const
{
    // Frame number is incremented by HnBeginFrameTask after all computations have been synced.
    if (FrameNumber == m_LastXformSyncFrameNumber + 1)
    {
        const pxr::VtMatrix4fArray& PrevXforms = m_Xforms[1 - m_CurrXformsIdx];
        return !PrevXforms.empty() ? PrevXforms : m_Xforms[m_CurrXformsIdx];
    }
    else
    {
        // Skinning xforms have not been updated for the current frame, so
        // they are the same as the previous frame.
        return m_Xforms[m_CurrXformsIdx];
    }
}

} // namespace USD

} // namespace Diligent
