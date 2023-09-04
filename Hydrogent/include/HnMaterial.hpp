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

#pragma once

#include <memory>
#include <unordered_map>

#include "HnMaterialNetwork.hpp"
#include "HnTextureRegistry.hpp"

#include "pxr/imaging/hd/material.h"

#include "RenderDevice.h"
#include "ShaderResourceBinding.h"
#include "RefCntAutoPtr.hpp"
#include "BasicMath.hpp"

namespace Diligent
{

class PBR_Renderer;

namespace USD
{

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

}

/// Hydra material implementation in Hydrogent.
class HnMaterial final : public pxr::HdMaterial
{
public:
    static std::shared_ptr<HnMaterial> Create(const pxr::SdfPath& id);

    ~HnMaterial();

    // Synchronizes state from the delegate to this object.
    virtual void Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    // Returns the minimal set of dirty bits to place in the
    // change tracker for use in the first sync of this prim.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    const HnTextureRegistry::TextureHandle* GetTexture(const pxr::TfToken& Name) const;

    void UpdateSRB(IRenderDevice* pDevice,
                   PBR_Renderer&  PbrRenderer,
                   IBuffer*       pCameraAttribs,
                   IBuffer*       pLightAttribs);

    IShaderResourceBinding* GetSRB() const { return m_SRB; }

    const HLSL::PBRMaterialShaderInfo& GetShaderAttribs() const { return m_ShaderAttribs; }

private:
    HnMaterial(pxr::SdfPath const& id);

    void AllocateTextures(HnTextureRegistry& TexRegistry);

private:
    HnMaterialNetwork m_Network;

    std::unordered_map<pxr::TfToken, HnTextureRegistry::TextureHandleSharedPtr, pxr::TfToken::HashFunctor> m_Textures;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    HLSL::PBRMaterialShaderInfo m_ShaderAttribs{};
};

} // namespace USD

} // namespace Diligent
