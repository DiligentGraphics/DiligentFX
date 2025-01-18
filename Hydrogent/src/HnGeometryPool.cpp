/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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
    m_UseIndexPool{UseIndexPool},
    m_VertexCache{/*NumRequestsToPurge = */ 128},
    m_IndexCache{/*NumRequestsToPurge = */ 128}
{
}

HnGeometryPool::~HnGeometryPool()
{
    VERIFY((m_PendingVertexData.empty() && m_PendingVertexDataSize >= 0) || m_PendingVertexDataSize == 0, "Pending vertex data size must be 0 when there are no pending data");
    VERIFY((m_PendingIndexData.empty() && m_PendingIndexDataSize >= 0) || m_PendingIndexDataSize == 0, "Pending index data size must be 0 when there are no pending data");
    VERIFY(m_ReservedDataSize.load() == 0, "Reserved data size (", m_ReservedDataSize, ") must be 0");
}

class GeometryPoolData
{
public:
    ~GeometryPoolData()
    {
        VERIFY(m_UseCount.load() == 0, "Use count is not 0");
    }

    int AddUse()
    {
        VERIFY(IsInitialized(), "Uninitialized data should be initialized first. Before that only pending uses can be added.");
        return m_UseCount.fetch_add(1) + 1;
    }
    int RemoveUse()
    {
        VERIFY(IsInitialized(), "Any unitiailized data should be initialized before a use can be removed since the timeline is as follows:\n"
                                "Frame n:\n"
                                "- HdEngine::Execute() -> HdRenderIndex::SyncAll() -> HnMesh::Sync() -> create VertexData /* Add pending use */\n"
                                "- HdEngine::Execute() -> HnRenderDelegate::CommitResources() -> HnGeometryPool::Commit() -> VertexData::Initialize() /* Pending uses -> uses */\n"
                                "Frame n+m:\n"
                                "- HdEngine::Execute() -> HdRenderIndex::SyncAll() -> HnMesh::Sync() -> Release VertexHandle /* Remove use */\n");
        VERIFY(m_UseCount.load() > 0, "Use count is already 0");
        return m_UseCount.fetch_add(-1) - 1;
    }
    int GetUseCount() const
    {
        return m_UseCount.load();
    }
    int AddPendingUse()
    {
        VERIFY(!IsInitialized(), "Adding pending use for initialized data. This is a bug.");
        VERIFY(m_UseCount.load() == 0,
               "Adding pending use for data that is already in use. This should never happen as the data is only initialized once, which is when "
               "all pending uses are committed. After data is initialized, it can never be in pending state again.");
        return m_PendingUseCount.fetch_add(1) + 1;
    }
    int GetPendingUseCount() const
    {
        return m_PendingUseCount.load();
    }
    void CommitPendingUses()
    {
        m_UseCount.fetch_add(m_PendingUseCount.exchange(0));
    }

    virtual bool IsInitialized() const = 0;

private:
    std::atomic<int> m_UseCount{0};
    std::atomic<int> m_PendingUseCount{0};
};

// Vertex data.
//
// The vertex data is stored in a pool of buffers managed by the GLTF::ResourceManager,
// or in standalone buffers if the pool is not used.
// The data is immutable. Updating the contents requires creating a new VertexData object.
// The buffers from the existing data can be reused if the new data is compatible with the existing data
// and there are no other handles that reference the existing data.
class HnGeometryPool::VertexData final : public GeometryPoolData
{
public:
    // A mapping from the source name to the source data hash, e.g.:
    //  "points"  -> 7957704880698454784
    //  "normals" -> 7753567487763246288
    using VertexStreamHashesType = std::map<pxr::TfToken, size_t>;

    // Constructs the vertex data from the provided sources.
    //
    // The vertex data will contain data from the provided sources and the existing data.
    // The existing data is used to copy the sources that are not present in the new sources,
    // e.g. the normals from the existing data will be copied to the new data if the new data
    // does not provide normals. This happens when only some of the primvars are updated.
    // Buffers from the existing data may be reused if the existing data is not used by other handles.
    VertexData(std::string                   Name,
               const BufferSourcesMapType&   Sources,
               const VertexStreamHashesType& Hashes,
               GLTF::ResourceManager*        pResMgr,
               bool                          DisallowPoolAllocationReuse,
               std::shared_ptr<VertexData>   ExistingData) :
        m_Name{std::move(Name)},
        m_NumVertices{!Sources.empty() ? static_cast<Uint32>(Sources.begin()->second->GetNumElements()) : 0},
        m_StagingData{std::make_unique<StagingData>(pResMgr, ExistingData)}
    {
        if (Sources.empty() || m_NumVertices == 0)
        {
            UNEXPECTED("No vertex data sources provided");
            return;
        }

        VERIFY(!ExistingData || ExistingData->IsInitialized(), "Existing data must be initialized.");

        auto AddStream = [this, &Hashes](const pxr::TfToken&                  Name,
                                         size_t                               ElementSize,
                                         std::shared_ptr<pxr::HdBufferSource> Source,
                                         IBuffer*                             SrcBuffer) {
            size_t Hash = 0;
            {
                auto hash_it = Hashes.find(Name);
                if (hash_it != Hashes.end())
                    Hash = hash_it->second;
                else
                    UNEXPECTED("Failed to find hash for vertex stream '", Name, "'. This is a bug.");
            }

            {
                auto it_inserted = m_Streams.emplace(Name, VertexStream{ElementSize, Hash});
                VERIFY(it_inserted.second, "Vertex stream '", Name, "' already exists.");
            }

            {
                auto it_inserted = m_StagingData->Sources.emplace(Name, StagingData::SourceData{std::move(Source), SrcBuffer});
                VERIFY(it_inserted.second, "Vertex stream '", Name, "' already exists.");
            }
        };

        for (const auto& source_it : Sources)
        {
            const pxr::TfToken&                         SourceName = source_it.first;
            const std::shared_ptr<pxr::HdBufferSource>& Source     = source_it.second;

            VERIFY(m_NumVertices == Source->GetNumElements(), "The number of elements (", Source->GetNumElements(),
                   ") in buffer source '", SourceName, "' does not match the number of vertices (", m_NumVertices, ")");

            const pxr::HdTupleType ElementType = Source->GetTupleType();
            const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
            if (SourceName == pxr::HdTokens->points)
            {
                VERIFY((ElementType.type == pxr::HdTypeFloatVec3 || ElementType.type == pxr::HdTypeInt32Vec2) && ElementType.count == 1,
                       "Unexpected vertex element type");
            }
            else if (SourceName == pxr::HdTokens->normals)
            {
                VERIFY((ElementType.type == pxr::HdTypeFloatVec3 || ElementType.type == pxr::HdTypeInt32) && ElementType.count == 1,
                       "Unexpected normal element type");
            }
            else if (SourceName == pxr::HdTokens->displayColor)
            {
                VERIFY((ElementType.type == pxr::HdTypeFloatVec3 || ElementType.type == pxr::HdTypeInt32) && ElementType.count == 1,
                       "Unexpected vertex color element type");
            }
            else if (SourceName == HnTokens->joints)
            {
                VERIFY(ElementType.type == pxr::HdTypeFloatVec4 && ElementType.count == 2, "Unexpected joints element type");
            }

            AddStream(SourceName, ElementSize, Source, nullptr);
        }

        // Add sources from the existing data that are not present in the new sources
        if (ExistingData)
        {
            for (const auto& stream_it : ExistingData->m_Streams)
            {
                const pxr::TfToken& SourceName     = stream_it.first;
                const VertexStream& ExistingStream = stream_it.second;
                if (m_Streams.find(SourceName) == m_Streams.end())
                {
                    IBuffer* pSrcBuffer = ExistingData->GetBuffer(SourceName);
                    VERIFY(pSrcBuffer, "pSrcBuffer must not be null for existing data as it must be initialized.");
                    AddStream(SourceName, ExistingStream.ElementSize, nullptr, pSrcBuffer);
                }
            }
        }

        // If there is existing data, do not initialize the pool allocation yet (unless it is disallowed).
        // In the Initialize() method, we will check if the existing data is not used by other
        // handles and reuse its buffers and pool allocation if possible.
        if (!ExistingData || DisallowPoolAllocationReuse)
        {
            InitPoolAllocation();
        }
    }

    IBuffer* GetBuffer(const pxr::TfToken& Name) const
    {
        auto it = m_Streams.find(Name);
        if (it == m_Streams.end())
            return nullptr;

        VERIFY_EXPR(!m_PoolAllocation || !it->second.Buffer);
        return m_PoolAllocation ? m_PoolAllocation->GetBuffer(it->second.PoolIndex) : it->second.Buffer;
    }

    Uint32 GetNumVertices() const
    {
        return m_NumVertices;
    }

    Uint32 GetStartVertex() const
    {
        return m_PoolAllocation ? m_PoolAllocation->GetStartVertex() : 0;
    }

    virtual bool IsInitialized() const override final
    {
        return !m_StagingData;
    }

    void Initialize(IRenderDevice*  pDevice,
                    IDeviceContext* pContext)
    {
        if (!m_StagingData)
        {
            UNEXPECTED("No staging data. This may indicate the Initialize() method is called more than once, which is a bug.");
            return;
        }

        std::shared_ptr<VertexData>& ExistingData = m_StagingData->ExistingData;
        if (ExistingData && !m_PoolAllocation)
        {
            if (ExistingData->GetUseCount() == 0)
            {
                // If existing data has no uses, take its buffers and pool allocation
                VERIFY(ExistingData->IsInitialized(), "Existing data is not initialized. This is a bug.");
                VERIFY(ExistingData->GetPendingUseCount() == 0, "Existing data has pending uses. This is a bug as the data should be initialized before it can be referenced as existing.");
                m_PoolAllocation = std::move(ExistingData->m_PoolAllocation);
                for (auto& stream_it : m_Streams)
                {
                    const pxr::TfToken& StreamName = stream_it.first;
                    VertexStream&       Stream     = stream_it.second;

                    auto src_stream_it = ExistingData->m_Streams.find(StreamName);
                    if (src_stream_it != ExistingData->m_Streams.end())
                    {
                        VERIFY_EXPR(Stream.ElementSize == src_stream_it->second.ElementSize);
                        VERIFY_EXPR(m_PoolAllocation || src_stream_it->second.Buffer);
                        Stream.Buffer = std::move(src_stream_it->second.Buffer);
                    }
                    else
                    {
                        UNEXPECTED("Failed to find existing data for vertex stream '", StreamName, "'. This is a bug.");
                    }
                }

                // Reset existing data since we don't need to copy anything from it
                ExistingData.reset();
            }
            else
            {
                // If existing data has uses, we need to create new buffers and pool allocation
                InitPoolAllocation();
            }
        }

        for (auto& stream_it : m_Streams)
        {
            const pxr::TfToken& StreamName = stream_it.first;
            VertexStream&       Stream     = stream_it.second;

            auto source_it = m_StagingData->Sources.find(StreamName);
            if (source_it == m_StagingData->Sources.end())
            {
                UNEXPECTED("Failed to find staging data for vertex stream '", StreamName, "'. This is a bug.");
                continue;
            }

            IBuffer*     pBuffer  = nullptr;
            const Uint32 DataSize = static_cast<Uint32>(m_NumVertices * Stream.ElementSize);
            if (m_PoolAllocation)
            {
                VERIFY(m_PoolAllocation->GetVertexCount() == m_NumVertices, "Unexpected number of vertices in the pool allocation.");
                pBuffer = m_PoolAllocation->GetBuffer(Stream.PoolIndex);
            }
            else
            {
                if (!Stream.Buffer)
                {
                    const auto BufferName = m_Name + " - " + StreamName.GetString();
                    BufferDesc Desc{
                        BufferName.c_str(),
                        DataSize,
                        BIND_VERTEX_BUFFER,
                        USAGE_DEFAULT,
                    };
                    pDevice->CreateBuffer(Desc, nullptr, &Stream.Buffer);
                }
                pBuffer = Stream.Buffer;
            }
            VERIFY_EXPR(pBuffer != nullptr);

            if (const pxr::HdBufferSource* pSource = source_it->second.Source.get())
            {
                VERIFY(pSource->GetNumElements() == m_NumVertices, "Unexpected number of elements in vertex data source ", StreamName);
                pContext->UpdateBuffer(pBuffer, GetStartVertex() * Stream.ElementSize, DataSize, pSource->GetData(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else if (IBuffer* pSrcBuffer = source_it->second.Buffer)
            {
                if (ExistingData)
                {
                    pContext->CopyBuffer(
                        pSrcBuffer,
                        ExistingData->GetStartVertex() * Stream.ElementSize,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                        pBuffer,
                        GetStartVertex() * Stream.ElementSize,
                        DataSize,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                }
                else
                {
                    // We have taken the buffer from the existing data - no need to copy
                    VERIFY(pBuffer == pSrcBuffer, "Unexpected buffer for vertex stream '", StreamName, "'");
                }
            }
            else
            {
                UNEXPECTED("No source data provided for vertex stream '", StreamName, "'");
            }

            if (!m_PoolAllocation)
            {
                StateTransitionDesc Barrier{Stream.Buffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
                pContext->TransitionResourceStates(1, &Barrier);
            }
        }

        m_StagingData.reset();
        CommitPendingUses();
    }

    bool IsCompatible(const BufferSourcesMapType& Sources) const
    {
        if (Sources.empty())
        {
            UNEXPECTED("No vertex data sources provided");
            return false;
        }

        for (const auto& source_it : Sources)
        {
            const pxr::TfToken&                         SourceName = source_it.first;
            const std::shared_ptr<pxr::HdBufferSource>& Source     = source_it.second;

            if (Source->GetNumElements() != m_NumVertices)
                return false;

            const auto stream_it = m_Streams.find(SourceName);
            if (stream_it == m_Streams.end())
                return false;

            const pxr::HdTupleType ElementType = Source->GetTupleType();
            const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
            if (ElementSize != stream_it->second.ElementSize)
                return false;
        }

        return true;
    }

    // Computes vertex stream hashes for the data sources and the existing data.
    static VertexStreamHashesType ComputeVertexStreamHashes(const BufferSourcesMapType& Sources, const VertexData* pData)
    {
        VertexStreamHashesType Hashes;
        // If the existing data is provided, copy the hashes from it
        if (pData != nullptr)
        {
            VERIFY_EXPR(pData->IsCompatible(Sources));
            for (const auto& it : pData->m_Streams)
            {
                const pxr::TfToken& StreamName = it.first;
                const VertexStream& Stream     = it.second;
                Hashes[StreamName]             = Stream.Hash;
            }
        }

        // Override the hashes with the new data
        for (const auto& source_it : Sources)
        {
            Hashes[source_it.first] = source_it.second->ComputeHash();
        }

        return Hashes;
    }

    size_t GetTotalSize() const
    {
        size_t TotalSize = 0;
        for (const auto& stream_it : m_Streams)
        {
            const VertexStream& Stream = stream_it.second;
            TotalSize += m_NumVertices * Stream.ElementSize;
        }
        return TotalSize;
    }

private:
    void InitPoolAllocation()
    {
        if (!m_StagingData)
        {
            UNEXPECTED("Staging data is null. This is a bug.");
            return;
        }

        if (m_StagingData->pResMgr == nullptr)
        {
            return;
        }

        VERIFY(!m_PoolAllocation, "Pool allocation is already initialized");

        GLTF::ResourceManager::VertexLayoutKey VtxKey;
        VtxKey.Elements.reserve(m_Streams.size());
        for (auto& stream_it : m_Streams)
        {
            VertexStream& Stream = stream_it.second;
            Stream.PoolIndex     = static_cast<Uint32>(VtxKey.Elements.size());
            VtxKey.Elements.emplace_back(static_cast<Uint32>(Stream.ElementSize), BIND_VERTEX_BUFFER);
        }

        m_PoolAllocation = m_StagingData->pResMgr->AllocateVertices(VtxKey, m_NumVertices);
        VERIFY_EXPR(m_PoolAllocation);
    }

private:
    const std::string m_Name;

    const Uint32 m_NumVertices;

    RefCntAutoPtr<IVertexPoolAllocation> m_PoolAllocation;

    struct VertexStream
    {
        const size_t ElementSize;
        const size_t Hash;

        // pool element index (e.g. "normals" -> 0, "points" -> 1, etc.)
        Uint32 PoolIndex = 0;

        RefCntAutoPtr<IBuffer> Buffer;

        VertexStream(size_t _ElementSize, size_t _Hash) :
            ElementSize{_ElementSize},
            Hash{_Hash}
        {}
    };
    // Keep streams sorted by name to ensure deterministic order
    std::map<pxr::TfToken, VertexStream> m_Streams;

    struct StagingData
    {
        GLTF::ResourceManager* const pResMgr;
        std::shared_ptr<VertexData>  ExistingData;

        struct SourceData
        {
            std::shared_ptr<pxr::HdBufferSource> Source;
            RefCntAutoPtr<IBuffer>               Buffer;

            SourceData(std::shared_ptr<pxr::HdBufferSource> _Source, IBuffer* _Buffer) :
                Source{std::move(_Source)},
                Buffer{_Buffer}
            {}
        };

        std::unordered_map<pxr::TfToken, SourceData, pxr::TfToken::HashFunctor> Sources;

        StagingData(GLTF::ResourceManager* _pResMgr, std::shared_ptr<VertexData> _ExistingData) :
            pResMgr{_pResMgr},
            ExistingData{std::move(_ExistingData)}
        {}
    };
    std::unique_ptr<StagingData> m_StagingData;
};


template <typename T, typename Base>
class PoolDataHandle : public Base
{
public:
    PoolDataHandle(std::shared_ptr<T> Data)
    {
        SetData(std::move(Data));
    }

    ~PoolDataHandle()
    {
        if (m_Data)
        {
            m_Data->RemoveUse();
        }
    }

    std::shared_ptr<T> GetData() const
    {
        return m_Data;
    }

    void SetData(std::shared_ptr<T> Data)
    {
        if (m_Data == Data)
            return;

        if (m_Data)
        {
            m_Data->RemoveUse();
        }

        if (Data)
        {
            if (!Data->IsInitialized())
                Data->AddPendingUse();
            else
                Data->AddUse();
        }

        m_Data = std::move(Data);
    }

protected:
    std::shared_ptr<T> m_Data;
};

// Vertex handle keeps a reference to the vertex data.
// Multiple handles can reference the same data.
class HnGeometryPool::VertexHandleImpl final : public PoolDataHandle<VertexData, HnGeometryPool::VertexHandle>
{
public:
    VertexHandleImpl(std::shared_ptr<VertexData> Data) :
        PoolDataHandle<VertexData, HnGeometryPool::VertexHandle>{std::move(Data)}
    {
    }

    virtual IBuffer* GetBuffer(const pxr::TfToken& Name) const override final
    {
        return m_Data ? m_Data->GetBuffer(Name) : nullptr;
    }

    virtual Uint32 GetNumVertices() const override final
    {
        return m_Data ? m_Data->GetNumVertices() : 0;
    }

    virtual Uint32 GetStartVertex() const override final
    {
        return m_Data ? m_Data->GetStartVertex() : 0;
    }
};


// Index data.
//
// The index data is stored in the index pool managed by the GLTF::ResourceManager,
// or in a standalone buffer if the pool is not used.
// The data is immutable. Updating the contents requires creating a new IndexData object.
// The buffer from the existing data can be reused if the new data is compatible with the existing data
// and there are no other handles that reference the existing data.
class HnGeometryPool::IndexData final : public GeometryPoolData
{
public:
    IndexData(std::string                Name,
              pxr::VtValue               Indices,
              Uint32                     StartVertex,
              GLTF::ResourceManager*     pResMgr,
              std::shared_ptr<IndexData> ExistingData) :
        m_Name{std::move(Name)},
        m_StagingData{std::make_unique<StagingData>(pResMgr, ExistingData, std::move(Indices), StartVertex)}
    {
        if (m_StagingData->Size == 0)
        {
            UNEXPECTED("No index data provided");
            return;
        }

        VERIFY(!ExistingData || ExistingData->IsInitialized(), "Existing data must be initialized.");

        m_NumIndices = static_cast<Uint32>(m_StagingData->Size / sizeof(Uint32));

        // If there is existing data, do not initialize the pool allocation yet.
        // In the Initialize() method, we will check if the existing data is not used by other
        // handles and reuse its buffer and pool allocation if possible.
        if (!ExistingData)
        {
            InitPoolAllocation();
        }
    }

    IBuffer* GetBuffer() const
    {
        VERIFY_EXPR(!m_Suballocation || !m_Buffer);
        return m_Suballocation ? m_Suballocation->GetBuffer() : m_Buffer;
    }

    Uint32 GetStartIndex() const
    {
        return m_Suballocation ? m_Suballocation->GetOffset() / sizeof(Uint32) : 0;
    }

    Uint32 GetNumIndices() const
    {
        return m_NumIndices;
    }

    virtual bool IsInitialized() const override final
    {
        return !m_StagingData;
    }

    void Initialize(IRenderDevice*  pDevice,
                    IDeviceContext* pContext)
    {
        if (!m_StagingData)
        {
            UNEXPECTED("No staging data. This may indicate the Initialize() method is called more than once, which is a bug.");
            return;
        }

        if (m_StagingData->Size == 0)
        {
            UNEXPECTED("No index data provided");
            return;
        }

        VERIFY(m_StagingData->Size / sizeof(Uint32) == m_NumIndices, "Unexpected number of indices in the staging data");

        std::shared_ptr<IndexData>& ExistingData = m_StagingData->ExistingData;
        if (ExistingData && ExistingData->GetNumIndices() != m_NumIndices)
        {
            UNEXPECTED("Existing data has different number of indices");
            ExistingData.reset();
        }
        if (ExistingData)
        {
            if (ExistingData->GetUseCount() == 0)
            {
                // If existing data has no uses, take its buffer
                VERIFY(ExistingData->IsInitialized(), "Existing data is not initialized.");
                VERIFY(ExistingData->GetPendingUseCount() == 0, "Existing data has pending uses. This is a bug.");
                m_Suballocation = std::move(ExistingData->m_Suballocation);
                m_Buffer        = std::move(ExistingData->m_Buffer);
                ExistingData.reset();
            }
            else
            {
                // If existing data has uses, we need to create a new buffer
                InitPoolAllocation();
            }
        }

        IBuffer* pBuffer = nullptr;
        if (m_Suballocation)
        {
            pBuffer = m_Suballocation->GetBuffer();
            VERIFY_EXPR(m_Suballocation->GetSize() == m_StagingData->Size);
        }
        else
        {
            if (!m_Buffer)
            {
                BufferDesc Desc{
                    m_Name.c_str(),
                    m_StagingData->Size,
                    BIND_INDEX_BUFFER,
                    USAGE_DEFAULT,
                };
                pDevice->CreateBuffer(Desc, nullptr, &m_Buffer);
            }
            pBuffer = m_Buffer;
        }
        VERIFY_EXPR(pBuffer != nullptr);

        pContext->UpdateBuffer(pBuffer, GetStartIndex() * sizeof(Uint32), m_StagingData->Size, m_StagingData->Ptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (!m_Suballocation)
        {
            StateTransitionDesc Barrier{pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }

        m_StagingData.reset();
        CommitPendingUses();
    }

    size_t GetSize() const
    {
        return m_NumIndices * sizeof(Uint32);
    }

private:
    void InitPoolAllocation()
    {
        if (!m_StagingData)
        {
            UNEXPECTED("Staging data is null. This is a bug.");
            return;
        }

        if (m_StagingData->pResMgr == nullptr)
        {
            return;
        }

        VERIFY(!m_Suballocation, "Pool allocation is already initialized");

        m_Suballocation = m_StagingData->pResMgr->AllocateIndices(m_StagingData->Size);
        VERIFY_EXPR(m_Suballocation);
    }

private:
    const std::string m_Name;
    Uint32            m_NumIndices = 0;

    RefCntAutoPtr<IBuffer>              m_Buffer;
    RefCntAutoPtr<IBufferSuballocation> m_Suballocation;

    struct StagingData
    {
        GLTF::ResourceManager* const pResMgr;
        std::shared_ptr<IndexData>   ExistingData;

        pxr::VtValue Indices;

        const void* Ptr  = nullptr;
        size_t      Size = 0;

        StagingData(GLTF::ResourceManager*     _pResMgr,
                    std::shared_ptr<IndexData> _ExistingData,
                    pxr::VtValue               _Indices,
                    Uint32                     StartVertex) :
            pResMgr{_pResMgr},
            ExistingData{std::move(_ExistingData)},
            Indices{std::move(_Indices)}
        {
            auto ShiftIndex = [StartVertex](int& Idx) {
                if (Idx >= 0)
                {
                    Idx += static_cast<int>(StartVertex);
                }
            };

            if (Indices.IsHolding<pxr::VtVec3iArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtVec3iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec3iArray>();
                    for (pxr::GfVec3i& idx : IndicesArray)
                    {
                        ShiftIndex(idx[0]);
                        ShiftIndex(idx[1]);
                        ShiftIndex(idx[2]);
                    }
                    Indices = pxr::VtValue::Take(IndicesArray);
                }
                const pxr::VtVec3iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec3iArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(pxr::GfVec3i);
            }
            else if (Indices.IsHolding<pxr::VtVec2iArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtVec2iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec2iArray>();
                    for (pxr::GfVec2i& idx : IndicesArray)
                    {
                        ShiftIndex(idx[0]);
                        ShiftIndex(idx[1]);
                    }
                    Indices = pxr::VtValue::Take(IndicesArray);
                }

                const pxr::VtVec2iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec2iArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(pxr::GfVec2i);
            }
            else if (Indices.IsHolding<pxr::VtIntArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtIntArray IndicesArray = Indices.UncheckedRemove<pxr::VtIntArray>();
                    for (int& idx : IndicesArray)
                        ShiftIndex(idx);
                    Indices = pxr::VtValue::Take(IndicesArray);
                }

                const pxr::VtIntArray& IndicesArray = Indices.UncheckedGet<pxr::VtIntArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(int);
            }
            else
            {
                UNEXPECTED("Unexpected index data type: ", Indices.GetTypeName());
            }
        }
    };
    std::unique_ptr<StagingData> m_StagingData;
};


// Index handle keeps a reference to the index data.
// Multiple handles can reference the same data.
class HnGeometryPool::IndexHandleImpl final : public PoolDataHandle<IndexData, HnGeometryPool::IndexHandle>
{
public:
    IndexHandleImpl(std::shared_ptr<IndexData> Data) :
        PoolDataHandle<IndexData, HnGeometryPool::IndexHandle>{std::move(Data)}
    {
    }

    virtual IBuffer* GetBuffer() const override final
    {
        return m_Data ? m_Data->GetBuffer() : nullptr;
    }

    virtual Uint32 GetNumIndices() const override final
    {
        return m_Data ? m_Data->GetNumIndices() : 0;
    }

    virtual Uint32 GetStartIndex() const override final
    {
        return m_Data ? m_Data->GetStartIndex() : 0;
    }

    std::shared_ptr<IndexData> GetData() const
    {
        return m_Data;
    }
};


void HnGeometryPool::AllocateVertices(const std::string&             Name,
                                      const BufferSourcesMapType&    Sources,
                                      std::shared_ptr<VertexHandle>& Handle,
                                      bool                           DisallowPoolAllocationReuse)
{
    if (Sources.empty())
    {
        UNEXPECTED("No vertex data sources provided");
        return;
    }

    std::shared_ptr<VertexData> ExistingData = Handle ?
        static_cast<VertexHandleImpl*>(Handle.get())->GetData() :
        nullptr;
    VERIFY(!ExistingData || ExistingData->IsInitialized(), "Existing data must be initialized.");
    if (ExistingData && !ExistingData->IsCompatible(Sources))
    {
        UNEXPECTED("Existing vertex data is not compatible with the provided sources. "
                   "This indicates that the number of vertices, the element size or the vertex data source names have changed. "
                   "In any of this cases the mesh should request a new vertex handle.");
        ExistingData.reset();
    }

    VertexData::VertexStreamHashesType Hashes = VertexData::ComputeVertexStreamHashes(Sources, ExistingData.get());

    size_t Hash = 0;
    for (const auto& hash_it : Hashes)
    {
        Hash = pxr::TfHash::Combine(Hash, hash_it.second);
    }

    std::shared_ptr<VertexData> Data = m_VertexCache.Get(
        Hash,
        [&]() {
            // This code is executed only if the data is not found in the cache, and the data is created only once.

            std::shared_ptr<VertexData> Data = std::make_shared<VertexData>(Name, Sources, Hashes,
                                                                            m_UseVertexPool ? &m_ResMgr : nullptr,
                                                                            DisallowPoolAllocationReuse, ExistingData);

            size_t TotalSize = Data->GetTotalSize();
            m_PendingVertexDataSize.fetch_add(static_cast<Int64>(TotalSize));

            {
                std::lock_guard<std::mutex> Guard{m_PendingVertexDataMtx};
                m_PendingVertexData.emplace_back(Data);
            }

            return Data;
        });


    if (Handle)
    {
        static_cast<VertexHandleImpl*>(Handle.get())->SetData(std::move(Data));
    }
    else
    {
        Handle = std::make_shared<VertexHandleImpl>(std::move(Data));
    }
}

void HnGeometryPool::AllocateIndices(const std::string&                            Name,
                                     pxr::VtValue                                  Indices,
                                     Uint32                                        StartVertex,
                                     std::shared_ptr<HnGeometryPool::IndexHandle>& Handle)
{
    if (Indices.IsEmpty())
    {
        UNEXPECTED("No index data provided");
        return;
    }

    size_t Hash = Indices.GetHash();
    Hash        = pxr::TfHash::Combine(Hash, StartVertex);

    std::shared_ptr<IndexData> ExistingData = Handle ?
        static_cast<IndexHandleImpl*>(Handle.get())->GetData() :
        nullptr;
    VERIFY(!ExistingData || ExistingData->IsInitialized(), "Existing data must be initialized.");

    std::shared_ptr<IndexData> Data = m_IndexCache.Get(
        Hash,
        [&]() {
            // This code is executed only if the data is not found in the cache, and the data is created only once.

            std::shared_ptr<IndexData> Data = std::make_shared<IndexData>(Name, std::move(Indices), StartVertex, m_UseIndexPool ? &m_ResMgr : nullptr, ExistingData);

            m_PendingIndexDataSize.fetch_add(static_cast<Int64>(Data->GetSize()));

            {
                std::lock_guard<std::mutex> Guard{m_PendingIndexDataMtx};
                m_PendingIndexData.emplace_back(Data);
            }

            return Data;
        });

    if (Handle)
    {
        static_cast<IndexHandleImpl*>(Handle.get())->SetData(std::move(Data));
    }
    else
    {
        Handle = std::make_shared<IndexHandleImpl>(std::move(Data));
    }
}


void HnGeometryPool::Commit(IDeviceContext* pContext)
{
    {
        std::lock_guard<std::mutex> Guard{m_PendingVertexDataMtx};
        for (auto& Data : m_PendingVertexData)
        {
            Data->Initialize(m_pDevice, pContext);
            m_PendingVertexDataSize.fetch_add(-static_cast<Int64>(Data->GetTotalSize()));
            VERIFY_EXPR(m_PendingVertexDataSize >= 0);
        }
        m_PendingVertexData.clear();
    }

    {
        std::lock_guard<std::mutex> Guard{m_PendingIndexDataMtx};
        for (auto& Data : m_PendingIndexData)
        {
            Data->Initialize(m_pDevice, pContext);
            m_PendingIndexDataSize.fetch_add(-static_cast<Int64>(Data->GetSize()));
            VERIFY_EXPR(m_PendingIndexDataSize >= 0);
        }
        m_PendingIndexData.clear();
    }
}

HnGeometryPool::ReservedSpace::ReservedSpace(HnGeometryPool& Pool,
                                             Uint64          Size,
                                             Uint64          TotalPendingSize) noexcept :
    m_Pool{Pool},
    m_Size{Size},
    m_TotalPendingSize{TotalPendingSize}
{
}

HnGeometryPool::ReservedSpace::ReservedSpace(ReservedSpace&& Other) noexcept :
    m_Pool{Other.m_Pool},
    m_Size{Other.m_Size},
    m_TotalPendingSize{Other.m_TotalPendingSize}
{
    Other.m_Size             = 0;
    Other.m_TotalPendingSize = 0;
}

void HnGeometryPool::ReservedSpace::Release(Uint64 Size)
{
    if (Size == ~Uint64{0})
    {
        Size = m_Size;
    }
    else
    {
        VERIFY_EXPR(Size <= m_Size);
        Size = std::min(Size, m_Size);
    }

    if (Size != 0)
    {
        m_Pool.ReleaseReservedSpace(Size);
        m_Size -= Size;
    }
}

HnGeometryPool::ReservedSpace::~ReservedSpace()
{
    Release();
    VERIFY_EXPR(m_Size == 0);
}

HnGeometryPool::ReservedSpace HnGeometryPool::ReserveSpace(Uint64 Size)
{
    const Int64 TotalPendingSize =
        GetPendingVertexDataSize() +
        GetPendingIndexDataSize() +
        m_ReservedDataSize.fetch_add(static_cast<Int64>(Size)) +
        static_cast<Int64>(Size);
    return {*this, Size, static_cast<Uint64>(TotalPendingSize)};
}

void HnGeometryPool::ReleaseReservedSpace(Uint64 Size)
{
    VERIFY_EXPR(m_ReservedDataSize.load() >= static_cast<Int64>(Size));
    m_ReservedDataSize.fetch_add(-static_cast<Int64>(Size));
}

} // namespace USD

} // namespace Diligent
