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

#pragma once

#include <map>
#include <vector>
#include <mutex>
#include <memory>

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/bufferSource.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/ObjectsRegistry.hpp"

namespace Diligent
{

namespace GLTF
{
class ResourceManager;
}

namespace USD
{

class HnGeometryPool final
{
public:
    HnGeometryPool(IRenderDevice*         pDevice,
                   GLTF::ResourceManager& ResMgr,
                   bool                   UseVertexPool,
                   bool                   UseIndexPool);

    ~HnGeometryPool();

    void Commit(IDeviceContext* pContext);

    class VertexHandle
    {
    public:
        virtual ~VertexHandle() {}
        virtual IBuffer* GetBuffer(const pxr::TfToken& Name) const = 0;
        virtual Uint32   GetNumVertices() const                    = 0;
        virtual Uint32   GetStartVertex() const                    = 0;
    };

    struct IndexHandle
    {
        virtual ~IndexHandle() {}
        virtual IBuffer* GetBuffer() const     = 0;
        virtual Uint32   GetNumIndices() const = 0;
        virtual Uint32   GetStartIndex() const = 0;
    };

    using BufferSourcesMapType = std::map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>>;

    void AllocateVertices(const std::string&             Name,
                          const BufferSourcesMapType&    Sources,
                          std::shared_ptr<VertexHandle>& Handle);

    void AllocateIndices(const std::string&                            Name,
                         pxr::VtValue                                  Indices,
                         Uint32                                        StartVertex,
                         std::shared_ptr<HnGeometryPool::IndexHandle>& Handle);

private:
    RefCntAutoPtr<IRenderDevice> m_pDevice;
    GLTF::ResourceManager&       m_ResMgr;

    const bool m_UseVertexPool;
    const bool m_UseIndexPool;

    class VertexData;
    class IndexData;
    class VertexHandleImpl;
    class IndexHandleImpl;

    std::mutex                               m_PendingVertexDataMtx;
    std::vector<std::shared_ptr<VertexData>> m_PendingVertexData;

    std::mutex                              m_PendingIndexDataMtx;
    std::vector<std::shared_ptr<IndexData>> m_PendingIndexData;

    ObjectsRegistry<size_t, std::shared_ptr<VertexData>> m_VertexCache;
    ObjectsRegistry<size_t, std::shared_ptr<IndexData>>  m_IndexCache;
};

} // namespace USD

} // namespace Diligent
