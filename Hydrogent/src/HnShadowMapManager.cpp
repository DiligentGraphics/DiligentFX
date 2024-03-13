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

#include "HnShadowMapManager.hpp"

#include "DynamicTextureAtlas.h"

namespace Diligent
{

namespace USD
{

HnShadowMapManager::HnShadowMapManager(const CreateInfo& CI)
{
    DynamicTextureAtlasCreateInfo AtlasCI;

    AtlasCI.Desc = {
        "Shadow map atlas",
        RESOURCE_DIM_TEX_2D_ARRAY,
        CI.AtlasDim,
        CI.AtlasDim,
        1, // Array size
        CI.Format,
        1, // Mip levels
    };
    AtlasCI.Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;

    AtlasCI.MinAlignment    = 0;
    AtlasCI.ExtraSliceCount = 1;

    CreateDynamicTextureAtlas(nullptr, AtlasCI, &m_ShadowMapAtlas);
    VERIFY_EXPR(m_ShadowMapAtlas);
}

RefCntAutoPtr<ITextureAtlasSuballocation> HnShadowMapManager::Allocate(Uint32 Width, Uint32 Height)
{
    RefCntAutoPtr<ITextureAtlasSuballocation> pSuballocation;
    m_ShadowMapAtlas->Allocate(Width, Height, &pSuballocation);
    return pSuballocation;
}

void HnShadowMapManager::Commit(IRenderDevice* pDevice, IDeviceContext* pCtx)
{
    m_ShadowMapAtlas->Update(pDevice, pCtx);

    const Uint32 AtlasVersion = m_ShadowMapAtlas->GetVersion();
    if (AtlasVersion == m_AtlasVersion)
        return;

    m_DSVs.clear();

    ITexture* pShadowTexture = m_ShadowMapAtlas->GetTexture();
    if (pShadowTexture == nullptr)
    {
        UNEXPECTED("Shadow map atlas texture is null");
        return;
    }

    const TextureDesc& SMDesc = pShadowTexture->GetDesc();
    VERIFY_EXPR(m_DSVs.empty() || m_DSVs.size() == SMDesc.ArraySize);

    if (m_DSVs.empty())
    {
        m_DSVs.resize(SMDesc.ArraySize);
        for (Uint32 Slice = 0; Slice < SMDesc.ArraySize; ++Slice)
        {
            std::string Name = std::string{"Shadow map DSV for slice "} + std::to_string(Slice);

            TextureViewDesc DSVDesc;
            DSVDesc.Name            = Name.c_str();
            DSVDesc.ViewType        = TEXTURE_VIEW_DEPTH_STENCIL;
            DSVDesc.Format          = SMDesc.Format;
            DSVDesc.TextureDim      = SMDesc.Type;
            DSVDesc.NumArraySlices  = 1;
            DSVDesc.FirstArraySlice = Slice;
            DSVDesc.MostDetailedMip = 0;
            DSVDesc.NumMipLevels    = 1;
            RefCntAutoPtr<ITextureView> pDSV;
            pShadowTexture->CreateView(DSVDesc, &pDSV);
            VERIFY_EXPR(pDSV);
            m_DSVs[Slice] = std::move(pDSV);
        }
    }

    m_AtlasVersion = AtlasVersion;
}

const TextureDesc& HnShadowMapManager::GetAtlasDesc() const
{
    return m_ShadowMapAtlas->GetAtlasDesc();
}

Uint32 HnShadowMapManager::GetAtlasVersion() const
{
    return m_ShadowMapAtlas->GetVersion();
}

ITexture* HnShadowMapManager::GetShadowTexture() const
{
    return m_ShadowMapAtlas->GetTexture();
}

ITextureView* HnShadowMapManager::GetShadowSRV() const
{
    return m_ShadowMapAtlas->GetTexture()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

} // namespace USD

} // namespace Diligent
