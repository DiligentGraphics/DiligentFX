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

#include "HnExtComputation.hpp"
#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

HnExtComputation* HnExtComputation::Create(const pxr::SdfPath& Id)
{
    return new HnExtComputation{Id};
}

HnExtComputation::HnExtComputation(const pxr::SdfPath& Id) :
    pxr::HdExtComputation{Id}
{
}

HnExtComputation::~HnExtComputation()
{
}

void HnExtComputation::Sync(pxr::HdSceneDelegate* SceneDelegate,
                            pxr::HdRenderParam*   RenderParam,
                            pxr::HdDirtyBits*     DirtyBits)
{
    pxr::HdExtComputation::_Sync(SceneDelegate, RenderParam, DirtyBits);

    if (*DirtyBits & pxr::HdExtComputation::DirtySceneInput)
    {
        m_SceneInputsVersion.fetch_add(1);
    }

    HnExtComputationImpl::ImplType Type = HnExtComputationImpl::GetType(*this);
    if (m_Impl && m_Impl->GetType() != Type)
    {
        VERIFY(m_Impl->GetType() != HnExtComputationImpl::ImplType::Skinning,
               "Deleting skinning computation may result in a crash since render passes may still keep references to the previous-frame Xforms owned by it.");
        m_Impl.reset();
    }

    if (!m_Impl)
    {
        m_Impl = HnExtComputationImpl::Create(*this);
    }

    if (m_Impl)
    {
        m_Impl->Sync(SceneDelegate, RenderParam, DirtyBits);
    }
    else
    {
        *DirtyBits = pxr::HdExtComputation::Clean;
    }
}

} // namespace USD

} // namespace Diligent
