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

#include "HnGeometryPool.hpp"
#include "HnTokens.hpp"

#include "GraphicsTypesX.hpp"
#include "ObjectBase.hpp"
#include "GLTFResourceManager.hpp"

#include "pxr/base/tf/token.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/vt/array.h"
#include "pxr/imaging/hd/tokens.h"

namespace Diligent
{

namespace USD
{

HnGeometryPool::HnGeometryPool(IRenderDevice*         pDevice,
                               GLTF::ResourceManager& ResMgr,
                               bool                   UseVertexPool,
                               bool                   UseIndexPool) :
    m_pDevice{pDevice},
    m_ResMgr{ResMgr},
    m_UseVertexPool{UseVertexPool},
    m_UseIndexPool{UseIndexPool}
{
}

HnGeometryPool::~HnGeometryPool()
{
}

class HnGeometryPool::VertexHandleImpl final : public ObjectBase<VertexHandle>
{
public:
    static RefCntAutoPtr<VertexHandleImpl> Create(std::string                 Name,
                                                  const BufferSourcesMapType& Sources,
                                                  GLTF::ResourceManager*      pResMgr)
    {
        return RefCntAutoPtr<VertexHandleImpl>{
            MakeNewRCObj<VertexHandleImpl>()(std::move(Name), Sources, pResMgr),
        };
    }

    IBuffer* GetBuffer(const pxr::TfToken& Name) override final
    {
        auto it = m_Buffers.find(Name);
        return it != m_Buffers.end() ? it->second.RawPtr() : nullptr;
    }

    Uint32 GetNumVertices() const override final
    {
        return m_NumVertices;
    }

    virtual Uint32 GetStartVertex() const override final
    {
        return m_PoolAllocation ? m_PoolAllocation->GetStartVertex() : 0;
    }

    void Update(IRenderDevice*              pDevice,
                IDeviceContext*             pContext,
                const BufferSourcesMapType& Sources)
    {
        const RenderDeviceX_N& Device{pDevice};

        for (auto source_it : Sources)
        {
            const pxr::HdBufferSource* pSource = source_it.second.get();
            if (pSource == nullptr)
                continue;
            const pxr::TfToken& PrimName = source_it.first;

            const size_t NumElements = pSource->GetNumElements();
            VERIFY(NumElements == m_NumVertices, "Unexpected number of elements in vertex data source ", PrimName);

            const pxr::HdTupleType ElementType = pSource->GetTupleType();
            const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
            if (PrimName == pxr::HdTokens->points)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex element type");
            else if (PrimName == pxr::HdTokens->normals)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected normal element type");
            else if (PrimName == pxr::HdTokens->displayColor)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex color element type");
            else if (PrimName == HnTokens->joints)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec4 && ElementType.count == 2, "Unexpected joints element type");

            RefCntAutoPtr<IBuffer> pBuffer;
            if (!m_PoolAllocation)
            {
                const auto BufferName = m_Name + " - " + PrimName.GetString();
                BufferDesc Desc{
                    BufferName.c_str(),
                    NumElements * ElementSize,
                    BIND_VERTEX_BUFFER,
                    USAGE_DEFAULT,
                };

                BufferData InitData{pSource->GetData(), Desc.Size};
                if (pBuffer && pBuffer->GetDesc().Size == Desc.Size)
                {
                    pContext->UpdateBuffer(pBuffer, 0, InitData.DataSize, InitData.pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                }
                else
                {
                    pBuffer = Device.CreateBuffer(Desc, &InitData);
                }

                StateTransitionDesc Barrier{pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
                pContext->TransitionResourceStates(1, &Barrier);
            }
            else
            {
                auto idx_it = m_NameToPoolIndex.find(PrimName);
                if (idx_it != m_NameToPoolIndex.end())
                {
                    pBuffer = m_PoolAllocation->GetBuffer(idx_it->second);

                    VERIFY(m_PoolAllocation->GetVertexCount() == NumElements, "The number of vertices has changed. This is unexpected as in this case the topology is expected to change and new vertex handle should be created.");
                    pContext->UpdateBuffer(pBuffer, m_PoolAllocation->GetStartVertex() * ElementSize, NumElements * ElementSize, pSource->GetData(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                }
                else
                {
                    UNEXPECTED("Failed to find vertex buffer index for ", PrimName, ". This is unexpected as when a new buffer is added, a new vertex handle should be created.");
                }
            }

            m_Buffers[source_it.first] = pBuffer;
        }
    }

private:
    template <typename AllocatorType, typename ObjectType>
    friend class MakeNewRCObj;

    VertexHandleImpl(IReferenceCounters*         pRefCounters,
                     std::string                 Name,
                     const BufferSourcesMapType& Sources,
                     GLTF::ResourceManager*      pResMgr) :
        ObjectBase<VertexHandle>{pRefCounters},
        m_Name{std::move(Name)}
    {
        if (Sources.empty())
        {
            UNEXPECTED("No vertex data sources provided");
            return;
        }

        m_NumVertices = static_cast<Uint32>(Sources.begin()->second->GetNumElements());
#ifdef DILIGENT_DEBUG
        for (const auto& source_it : Sources)
        {
            VERIFY(m_NumVertices == source_it.second->GetNumElements(), "Inconsistent number of elements in vertex data sources");
        }
#endif

        if (pResMgr != nullptr)
        {
            GLTF::ResourceManager::VertexLayoutKey VtxKey;
            VtxKey.Elements.reserve(Sources.size());
            for (const auto& source_it : Sources)
            {
                const pxr::TfToken&                         SourceName = source_it.first;
                const std::shared_ptr<pxr::HdBufferSource>& Source     = source_it.second;

                const pxr::HdTupleType ElementType = Source->GetTupleType();
                const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;

                m_NameToPoolIndex[SourceName] = static_cast<Uint32>(VtxKey.Elements.size());
                VtxKey.Elements.emplace_back(static_cast<Uint32>(ElementSize), BIND_VERTEX_BUFFER);
            }

            m_PoolAllocation = pResMgr->AllocateVertices(VtxKey, m_NumVertices);
            VERIFY_EXPR(m_PoolAllocation);
        }
    }

private:
    const std::string m_Name;

    Uint32 m_NumVertices = 0;

    RefCntAutoPtr<IVertexPoolAllocation> m_PoolAllocation;

    // Buffer name to vertex pool element index (e.g. "normals" -> 0, "points" -> 1, etc.)
    std::unordered_map<pxr::TfToken, Uint32, pxr::TfToken::HashFunctor> m_NameToPoolIndex;

    // Buffer name to buffer
    std::unordered_map<pxr::TfToken, RefCntAutoPtr<IBuffer>, pxr::TfToken::HashFunctor> m_Buffers;
};

class HnGeometryPool::IndexHandleImpl final : public ObjectBase<IndexHandle>
{
public:
    static RefCntAutoPtr<IndexHandleImpl> Create(std::string Name, const pxr::VtValue& Indices, GLTF::ResourceManager* pResMgr)
    {
        return RefCntAutoPtr<IndexHandleImpl>{
            MakeNewRCObj<IndexHandleImpl>()(std::move(Name), Indices, pResMgr),
        };
    }

    IBuffer* GetBuffer() override final
    {
        return m_Buffer;
    }

    Uint32 GetStartIndex() const override final
    {
        return m_Suballocation ? m_Suballocation->GetOffset() / sizeof(Uint32) : 0;
    }

    Uint32 GetNumIndices() const override final
    {
        return m_NumIndices;
    }

    void Update(IRenderDevice*      pDevice,
                IDeviceContext*     pContext,
                const pxr::VtValue& Indices)
    {
        std::pair<size_t, const void*> IndexCountAndPtr = GetIndexCountAndPtr(Indices);
        if (IndexCountAndPtr.second == nullptr)
        {
            return;
        }

        if (IndexCountAndPtr.first != m_NumIndices)
        {
            DEV_ERROR("The number of indices has changed. This is unexpected as in this case the topology is expected to change and new index handle should be created.");
            return;
        }

        BufferData IBData{IndexCountAndPtr.second, IndexCountAndPtr.first * sizeof(Uint32)};
        if (m_Suballocation == nullptr)
        {
            if (m_Buffer)
            {
                pContext->UpdateBuffer(m_Buffer, 0, IBData.DataSize, IBData.pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                BufferDesc Desc{
                    m_Name.c_str(),
                    IBData.DataSize,
                    BIND_INDEX_BUFFER,
                    USAGE_DEFAULT,
                };
                pDevice->CreateBuffer(Desc, &IBData, &m_Buffer);
                VERIFY_EXPR(m_Buffer != nullptr);
            }

            StateTransitionDesc Barrier{m_Buffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }
        else
        {
            m_Buffer = m_Suballocation->GetBuffer();
            VERIFY_EXPR(m_Suballocation->GetSize() == IBData.DataSize);
            pContext->UpdateBuffer(m_Buffer, m_Suballocation->GetOffset(), IBData.DataSize, IBData.pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    static std::pair<size_t, const void*> GetIndexCountAndPtr(const pxr::VtValue& Indices)
    {
        if (Indices.IsHolding<pxr::VtVec3iArray>())
        {
            const pxr::VtVec3iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec3iArray>();
            return {IndicesArray.size() * 3, IndicesArray.data()};
        }
        else if (Indices.IsHolding<pxr::VtVec2iArray>())
        {
            const pxr::VtVec2iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec2iArray>();
            return {IndicesArray.size() * 2, IndicesArray.data()};
        }
        else if (Indices.IsHolding<pxr::VtIntArray>())
        {
            const pxr::VtIntArray& IndicesArray = Indices.UncheckedGet<pxr::VtIntArray>();
            return {IndicesArray.size(), IndicesArray.data()};
        }
        else
        {
            UNEXPECTED("Unexpected index data type");
            return {};
        }
    }

private:
    template <typename AllocatorType, typename ObjectType>
    friend class MakeNewRCObj;

    IndexHandleImpl(IReferenceCounters*    pRefCounters,
                    std::string            Name,
                    const pxr::VtValue&    Indices,
                    GLTF::ResourceManager* pResMgr) :
        ObjectBase<IndexHandle>{pRefCounters},
        m_Name{std::move(Name)},
        m_NumIndices{static_cast<Uint32>(GetIndexCountAndPtr(Indices).first)}
    {
        if (pResMgr != nullptr && m_NumIndices > 0)
        {
            m_Suballocation = pResMgr->AllocateIndices(m_NumIndices * sizeof(Uint32));
        }
    }

private:
    const std::string m_Name;
    const Uint32      m_NumIndices;

    RefCntAutoPtr<IBuffer>              m_Buffer;
    RefCntAutoPtr<IBufferSuballocation> m_Suballocation;
};

struct HnGeometryPool::StagingVertexData
{
    BufferSourcesMapType            Sources;
    RefCntAutoPtr<VertexHandleImpl> Handle;

    StagingVertexData(const BufferSourcesMapType& Sources, VertexHandle* _Handle) :
        Sources{Sources},
        Handle{static_cast<VertexHandleImpl*>(_Handle)}
    {}
};

struct HnGeometryPool::StagingIndexData
{
    pxr::VtValue                   Indices;
    RefCntAutoPtr<IndexHandleImpl> Handle;

    StagingIndexData(pxr::VtValue&& Indices, IndexHandle* _Handle) :
        Indices{std::move(Indices)},
        Handle{static_cast<IndexHandleImpl*>(_Handle)}
    {}
};

void HnGeometryPool::AllocateVertices(const std::string&           Name,
                                      const BufferSourcesMapType&  Sources,
                                      RefCntAutoPtr<VertexHandle>& Handle)
{
    if (Sources.empty())
    {
        UNEXPECTED("No vertex data sources provided");
        return;
    }

    if (!Handle)
    {
        Handle = VertexHandleImpl::Create(Name, Sources, m_UseVertexPool ? &m_ResMgr : nullptr);
    }

    std::lock_guard<std::mutex> Guard{m_StagingVertexDataMtx};
    m_StagingVertexData.emplace_back(Sources, Handle);
}

void HnGeometryPool::AllocateIndices(const std::string& Name, pxr::VtValue Indices, Uint32 StartVertex, RefCntAutoPtr<IndexHandle>& Handle)
{
    if (Indices.IsEmpty())
    {
        UNEXPECTED("No index data provided");
        return;
    }

    if (!Handle)
    {
        Handle = IndexHandleImpl::Create(Name, Indices, m_UseIndexPool ? &m_ResMgr : nullptr);
    }
    else
    {
        DEV_CHECK_ERR(Handle->GetNumIndices() == IndexHandleImpl::GetIndexCountAndPtr(Indices).first,
                      "The number of indices has changed. This is unexpected as in this case the topology is expected to change and new index handle should be created.");
    }

    if (StartVertex != 0)
    {
        if (Indices.IsHolding<pxr::VtVec3iArray>())
        {
            pxr::VtVec3iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec3iArray>();
            for (pxr::GfVec3i& idx : IndicesArray)
            {
                idx[0] += StartVertex;
                idx[1] += StartVertex;
                idx[2] += StartVertex;
            }
            Indices = IndicesArray;
        }
        else if (Indices.IsHolding<pxr::VtVec2iArray>())
        {
            pxr::VtVec2iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec2iArray>();
            for (pxr::GfVec2i& idx : IndicesArray)
            {
                idx[0] += StartVertex;
                idx[1] += StartVertex;
            }
            Indices = IndicesArray;
        }
        else if (Indices.IsHolding<pxr::VtIntArray>())
        {
            pxr::VtIntArray IndicesArray = Indices.UncheckedRemove<pxr::VtIntArray>();
            for (int& idx : IndicesArray)
                idx += StartVertex;
            Indices = IndicesArray;
        }
        else
        {
            UNEXPECTED("Unexpected index data type");
        }
    }

    std::lock_guard<std::mutex> Guard{m_StagingIndexDataMtx};
    m_StagingIndexData.emplace_back(std::move(Indices), std::move(Handle));
}


void HnGeometryPool::Commit(IDeviceContext* pContext)
{
    for (StagingVertexData& VertData : m_StagingVertexData)
    {
        VertData.Handle->Update(m_pDevice, pContext, VertData.Sources);
    }
    m_StagingVertexData.clear();

    for (StagingIndexData& IdxData : m_StagingIndexData)
    {
        IdxData.Handle->Update(m_pDevice, pContext, IdxData.Indices);
    }
    m_StagingIndexData.clear();
}

} // namespace USD

} // namespace Diligent
