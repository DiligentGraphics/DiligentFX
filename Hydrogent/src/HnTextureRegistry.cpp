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

#include "HnTextureRegistry.hpp"
#include "HnTextureUtils.hpp"

#include <mutex>

namespace Diligent
{

namespace USD
{

HnTextureRegistry::HnTextureRegistry(IRenderDevice* pDevice) :
    m_pDevice{pDevice}
{
}

HnTextureRegistry::~HnTextureRegistry()
{
}

static void InitializeHandle(IRenderDevice* pDevice, ITextureLoader* pLoader, HnTextureRegistry::TextureHandle& Handle)
{
    VERIFY_EXPR(!Handle.pTexture);
    pLoader->CreateTexture(pDevice, &Handle.pTexture);
    if (!Handle.pTexture)
        return;

    RefCntAutoPtr<ISampler> pSampler;

    SamplerDesc SamDesc;
    SamDesc.AddressU = TEXTURE_ADDRESS_WRAP;
    SamDesc.AddressV = TEXTURE_ADDRESS_WRAP;
    pDevice->CreateSampler(SamDesc, &pSampler);
    VERIFY_EXPR(pSampler);
    Handle.pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(pSampler);
}

void HnTextureRegistry::Commit(IDeviceContext* pContext)
{
    std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
    for (auto tex_it : m_PendingTextures)
    {
        InitializeHandle(m_pDevice, tex_it.second.pLoader, *tex_it.second.Handle);
    }
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const HnTextureIdentifier& TexId)
{
    auto TexHandle = std::make_shared<TextureHandle>();

    TextureLoadInfo LoadInfo;

    auto pLoader = CreateTextureLoaderFromSdfPath(TexId.FilePath.GetText(), LoadInfo);
    if (!pLoader)
    {
        LOG_ERROR_MESSAGE("Failed to create texture loader for texture ", TexId.FilePath);
        return {};
    }

    if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation)
    {
        InitializeHandle(m_pDevice, pLoader, *TexHandle);
    }
    else
    {
        std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
        m_PendingTextures.emplace(TexId.FilePath, PendingTextureInfo{std::move(pLoader), TexHandle});
    }

    return TexHandle;
}

} // namespace USD

} // namespace Diligent
