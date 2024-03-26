/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include <vector>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

namespace Diligent
{

/// G-buffer manages a set of render targets
class GBuffer
{
public:
    /// G-buffer element description
    struct ElementDesc
    {
        /// Texture format. Must not be TEX_FORMAT_UNKNOWN.
        TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;

        /// Texture bind flags. If BIND_NONE is specified, the following rules are used:
        /// - If the format is a depth-stencil format, BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE is used.
        /// - Otherwise, BIND_RENDER_TARGET | BIND_SHADER_RESOURCE is used.
        BIND_FLAGS BindFlags = BIND_NONE;

        /// Clear value.
        /// This value is used to clear the textures by the GBuffer::Clear() method.
        OptimizedClearValue ClearValue = {};
    };

    /// Initializes the G-buffer object.
    /// The textures will be created when the GBuffer::Resize() method is called.
    GBuffer(const ElementDesc* Elements,
            size_t             NumElements);

    /// Initializes the G-buffer object and creates the textures.
    GBuffer(const ElementDesc* Elements,
            size_t             NumElements,
            IRenderDevice*     pDevice,
            Uint32             Width,
            Uint32             Height);

    const ElementDesc& GetElementDesc(Uint32 Index) const
    {
        return m_ElemDesc[Index];
    }
    ITexture* GetBuffer(Uint32 Index) const
    {
        return m_Buffers[Index];
    }
    size_t GetBufferCount() const
    {
        return m_Buffers.size();
    }

    /// Resizes the G-buffer textures.
    void Resize(IRenderDevice* pDevice, Uint32 Width, Uint32 Height);

    /// Binds the G-buffer textures to the device context.
    ///
    /// \param [in] pContext    - Device context to bind the textures to.
    /// \param [in] BuffersMask - Bitmask indicating which buffers to bind.
    /// \param [in] pDSV        - Depth-stencil view to set.
    /// \param [in] ClearMask   - Bitmask indicating which buffers to clear.
    /// \param [in] RTIndices   - Optional array of render target indices to use for each buffer.
    ///                           If null, the buffer index is used.
    ///                           The array must contain one index for each buffer specified by the BuffersMask.
    ///
    /// \remarks    The method binds all textures in the order they were specified in the constructor.
    ///             If the corresponding bit in the BuffersMask is not set, null view is bound.
    ///             If there are depth-stencil textures in the G-buffer and the corresponding bit in the BuffersMask is set,
    ///             the texture is bound as depth-stencil view. Otherwise, the user-provided depth-stencil view is bound.
    ///             If the corresponding bit in the ClearMask is set, the texture is cleared with the clear value
    ///             specified in the corresponding element description.
    void Bind(IDeviceContext* pContext,
              Uint32          BuffersMask,
              ITextureView*   pDSV,
              Uint32          ClearMask = 0,
              const Uint32*   RTIndices = nullptr);

private:
    const std::vector<ElementDesc> m_ElemDesc;

    std::vector<RefCntAutoPtr<ITexture>> m_Buffers;

    Uint32 m_Width  = 0;
    Uint32 m_Height = 0;
};

} // namespace Diligent
