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

#include "Render/RadientFrameSRBCache.hpp"

#include "PBR_Renderer.hpp"

namespace Diligent
{

/// PBR renderer specialization that separates frame/global resources from
/// draw/material resources into two pipeline resource signatures.
class RadientPBRRenderer final : public PBR_Renderer
{
public:
    RadientPBRRenderer(IRenderDevice*     pDevice,
                       IRenderStateCache* pStateCache,
                       IDeviceContext*    pContext,
                       const CreateInfo&  CI);

    /// Returns the cached immutable frame SRB for the IBL resources, creating it if necessary.
    RefCntAutoPtr<IShaderResourceBinding> GetOrCreateFrameSRB(RadientIBLResources* pResources);

    /// Initializes common material resources and binds the configured primitive
    /// attribute array so the pass can select batches through dynamic offsets.
    void InitMaterialSRBVars(IShaderResourceBinding* pSRB) const;

    IBuffer* GetFrameAttribsCB() const noexcept { return m_pFrameAttribsCB; }

protected:
    virtual void CreateCustomSignature(PipelineResourceSignatureDescX&& SignatureDesc) override final;

private:
    RefCntAutoPtr<IBuffer> m_pFrameAttribsCB;
    RadientFrameSRBCache   m_FrameSRBCache;
};

} // namespace Diligent
