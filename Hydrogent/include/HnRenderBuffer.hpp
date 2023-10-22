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

#include "pxr/imaging/hd/renderBuffer.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

namespace Diligent
{

namespace USD
{

class HnRenderDelegate;

/// Hydra render buffer implementation in Hydrogent.
class HnRenderBuffer final : public pxr::HdRenderBuffer
{
public:
    HnRenderBuffer(const pxr::SdfPath& Id);
    HnRenderBuffer(const pxr::SdfPath& Id, const HnRenderDelegate* RnederDelegate);
    HnRenderBuffer(const pxr::SdfPath& Id, ITextureView* pTarget);

    ~HnRenderBuffer() override;

    // Allocate a buffer.  Can be called from Sync(), or directly.
    // If the buffer has already been allocated, calling Allocate() again
    // will destroy the old buffer and allocate a new one.
    //
    // A negative dimension or invalid format will cause an allocation error.
    // If the requested buffer can't be allocated, the function will return
    // false.
    virtual bool Allocate(const pxr::GfVec3i& Dimensions,
                          pxr::HdFormat       Format,
                          bool                MultiSampled) override final;

    // Get the buffer's width.
    virtual unsigned int GetWidth() const override final;

    // Get the buffer's height.
    virtual unsigned int GetHeight() const override final;

    // Get the buffer's depth.
    virtual unsigned int GetDepth() const override final;

    // Get the buffer's per-pixel format.
    virtual pxr::HdFormat GetFormat() const override final;

    // Get whether the buffer is multisampled.
    virtual bool IsMultiSampled() const override final;

    // Map the buffer for reading.
    virtual void* Map() override final;

    // Unmap the buffer. It is no longer safe to read from the buffer.
    virtual void Unmap() override final;

    // Return whether the buffer is currently mapped by anybody.
    virtual bool IsMapped() const override final;

    // Resolve the buffer so that reads reflect the latest writes.
    //
    // Some buffer implementations may defer final processing of writes until
    // a buffer is read, for efficiency; examples include OpenGL MSAA or
    // multi-sampled raytraced buffers.
    virtual void Resolve() override final;

    // Return whether the buffer is converged (whether the renderer is
    // still adding samples or not).
    virtual bool IsConverged() const override final;

    // This optional API returns a (type-erased) resource that backs this
    // render buffer. For example, a render buffer implementation may allocate
    // a gpu texture that holds the data of the buffer. This function allows
    // other parts of Hydra, such as a HdTask to get access to this resource.
    virtual pxr::VtValue GetResource(bool MultiSampled) const override final;

    void SetTarget(ITextureView* pTarget);

    void ReleaseTarget();

    ITextureView* GetTarget() const { return m_pTarget; }

protected:
    // Deallocate the buffer, freeing any owned resources.
    virtual void _Deallocate() override final;

private:
    RefCntAutoPtr<ITextureView> m_pTarget;
    const HnRenderDelegate*     m_RenderDelegate = nullptr;
};

} // namespace USD

} // namespace Diligent
