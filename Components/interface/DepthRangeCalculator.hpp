/*
 *  Copyright 2025 Diligent Graphics LLC
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

#include <memory>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/GPUCompletionAwaitQueue.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

namespace Diligent
{

/// Computes depth range from the scene depth buffer.
///
/// The class uses a compute shader to read the depth buffer and compute the near and far
/// depth values. It writes the results to a buffer with the following layout:
///
///     struct DepthRange
///     {
///         float SceneNearZ;
///         float SceneFarZ;
///         float SceneNearDepth;
///         float SceneFarDepth;
///     };
///
/// SceneNearZ is always less than SceneFarZ.
/// SceneNearDepth is the depth value corresponding to SceneNearZ.
/// SceneFarDepth is the depth value corresponding to SceneFarZ.
/// Note that if reverse depth is used, SceneNearDepth will be greater than SceneFarDepth.
///
/// \remarks SceneNearZ and SceneFarZ must be positive values.
class DepthRangeCalculator
{
public:
    /// Depth range calculator create info.
    struct CreateInfo
    {
        /// Render device.
        IRenderDevice* pDevice = nullptr;

        /// An optional render state cache.
        IRenderStateCache* pStateCache = nullptr;

        /// Whether shader matrices are laid out in row-major order in GPU memory.
        ///
        /// \remarks    By default, shader matrices are laid out in column-major order
        ///             in GPU memory. If this option is set to true, shaders will be compiled
        ///             with the SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR flag and
        ///             use the row-major layout.
        bool PackMatrixRowMajor = false;

        /// Whether to compile shaders asynchronously.
        bool AsyncShaders = false;

        /// Whether to read back the depth range data to the CPU.
        bool ReadBackData = false;
    };

    /// Constructs a depth range calculator object.
    ///
    /// \param [in] CI  Create info.
    ///
    /// In case of failure, an exception is thrown.
    DepthRangeCalculator(const CreateInfo& CI) noexcept(false);

    /// Returns true if the depth range calculator is ready to be used.
    bool IsReady() const
    {
        return m_ComputeDepthRangePSO && m_ComputeDepthRangePSO->GetStatus() == PIPELINE_STATE_STATUS_READY;
    }

    /// Creates a shader resource binding object to use in ComputeRange() method.
    RefCntAutoPtr<IShaderResourceBinding> CreateSRB(ITextureView* pDepthBufferSRV,
                                                    IBuffer*      pCameraAttribs);

    /// Attributes for ComputeRange() method.
    struct ComputeRangeAttribs
    {
        /// Device context to use for command recording.
        IDeviceContext* pContext = nullptr;

        /// Shader resource binding object.
        ///
        /// The SRB must be created using the CreateSRB() method.
        IShaderResourceBinding* pSRB = nullptr;

        /// Depth buffer width.
        Uint32 Width = 0;

        /// Depth buffer height.
        Uint32 Height = 0;
    };

    /// Computes the depth range.
    ///
    /// \param [in] Attribs  Method attributes.
    ///
    /// The near/far depth values are written to the depth range buffer.
    /// If the ReadBackData option was set to true in the CreateInfo structure,
    /// the depth range will also be read back to the CPU and can be accessed using
    /// the GetDepthRange() method.
    ///
    /// Note that the data read back to the CPU is typically a few frames behind the GPU.
    void ComputeRange(const ComputeRangeAttribs& Attribs);

    /// Returns the depth range buffer.
    IBuffer* GetDepthRangeBuffer() const
    {
        return m_DepthRangeBuff;
    }

    struct DepthRange
    {
        float fSceneNearZ     = 0;
        float fSceneFarZ      = 0;
        float fSceneNearDepth = 0;
        float fSceneFarDepth  = 0;
    };

    /// Returns the depth range read back to the CPU.
    ///
    /// \param [in] pCtx  If not null, the function will poll the read back queue for the latest data.
    /// \return           The depth range read back to the CPU.
    const DepthRange& GetDepthRange(IDeviceContext* pCtx = nullptr);

private:
    void PollReadBackQueue(IDeviceContext* pCtx);

private:
    RefCntAutoPtr<IRenderDevice>              m_pDevice;
    RefCntAutoPtr<IPipelineResourceSignature> m_Signature;
    RefCntAutoPtr<IPipelineState>             m_ClearDepthRangePSO;
    RefCntAutoPtr<IPipelineState>             m_ComputeDepthRangePSO;

    using DepthRangeReadBackQueueType = GPUCompletionAwaitQueue<RefCntAutoPtr<IBuffer>>;
    std::unique_ptr<DepthRangeReadBackQueueType> m_DepthRangeReadBackQueue;

    RefCntAutoPtr<IBuffer> m_DepthRangeBuff;

    DepthRange m_DepthRange;
};

} // namespace Diligent
