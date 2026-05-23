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

#include "RadientBackendImpl.hpp"

namespace Diligent
{

RadientBackendImpl::RadientBackendImpl(IReferenceCounters* pRefCounters, const RadientBackendCreateInfo& CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Desc.Name != nullptr ? CreateInfo.Desc.Name : ""},
    m_RemoteEndpoint{CreateInfo.Desc.RemoteEndpoint != nullptr ? CreateInfo.Desc.RemoteEndpoint : ""},
    m_Desc{CreateInfo.Desc},
    m_pDevice{CreateInfo.pDevice},
    m_pImmediateContext{CreateInfo.pImmediateContext},
    m_pSwapChain{CreateInfo.pSwapChain}
{
    m_Desc.Name           = m_Name.c_str();
    m_Desc.RemoteEndpoint = m_RemoteEndpoint.c_str();
}

RadientBackendImpl::~RadientBackendImpl()
{
}

RefCntAutoPtr<RadientBackendImpl> RadientBackendImpl::Create(const RadientBackendCreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientBackendImpl>{MakeNewRCObj<RadientBackendImpl>()(CreateInfo)};
}

const RadientBackendDesc& RadientBackendImpl::GetDesc() const
{
    return m_Desc;
}

IRenderDevice* RadientBackendImpl::GetNativeDevice()
{
    return m_pDevice;
}

IDeviceContext* RadientBackendImpl::GetNativeImmediateContext()
{
    return m_pImmediateContext;
}

ISwapChain* RadientBackendImpl::GetNativeSwapChain()
{
    return m_pSwapChain;
}

} // namespace Diligent
