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

#include "RadientViewImpl.hpp"

namespace Diligent
{

RadientViewImpl::RadientViewImpl(IReferenceCounters* pRefCounters, const RadientViewDesc& Desc) :
    TBase{pRefCounters},
    m_Name{Desc.Name != nullptr ? Desc.Name : ""},
    m_Desc{Desc},
    m_pScene{Desc.pScene},
    m_pRenderTarget{Desc.pRenderTarget}
{
    m_Desc.Name = m_Name.c_str();
}

RadientViewImpl::~RadientViewImpl()
{
}

RefCntAutoPtr<IRadientView> RadientViewImpl::Create(const RadientViewDesc& Desc)
{
    return RefCntAutoPtr<RadientViewImpl>{MakeNewRCObj<RadientViewImpl>()(Desc)};
}

const RadientViewDesc& RadientViewImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientViewImpl::SetScene(IRadientScene* pScene)
{
    if (m_pScene == pScene)
        return RADIENT_STATUS_NO_CHANGE;

    m_pScene      = pScene;
    m_Desc.pScene = m_pScene;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetCamera(RadientEntityID Camera)
{
    if (m_Desc.Camera == Camera)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.Camera = Camera;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientViewImpl::SetRenderTarget(IRadientRenderTarget* pRenderTarget)
{
    if (m_pRenderTarget == pRenderTarget)
        return RADIENT_STATUS_NO_CHANGE;

    m_pRenderTarget      = pRenderTarget;
    m_Desc.pRenderTarget = m_pRenderTarget;
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
