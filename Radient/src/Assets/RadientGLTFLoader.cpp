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

#include "Assets/RadientGLTFLoader.hpp"

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "Assets/RadientMeshViewSource.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "Errors.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFDocument.hpp"
#include "GLTFLoader.hpp"
#include "HashUtils.hpp"
#include "Import/RadientGLTFConverter.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

using MaterialAssetList = RadientImport::MaterialAssetList;
using MeshAssetList     = RadientImport::MeshAssetList;

std::string MakeEmbeddedGLTFTextureURI(const std::string& SourceURI, Uint32 TextureIndex)
{
    return SourceURI + "#texture:" + std::to_string(TextureIndex);
}

void ReleaseGLTFTextureSourceData(const void*, Uint64, void* pUserData)
{
    delete static_cast<std::shared_ptr<const GLTF::Document>*>(pUserData);
}

struct PrimitiveVertexKey
{
    std::vector<int> AttributeAccessors;

    explicit PrimitiveVertexKey(const GLTF::TinyGltfPrimitiveView& GltfPrimitive)
    {
        AttributeAccessors.reserve(GLTF::DefaultVertexAttributes.size());

        for (size_t AttribIndex = 0; AttribIndex < GLTF::DefaultVertexAttributes.size(); ++AttribIndex)
        {
            const GLTF::VertexAttributeDesc& DstAttrib = GLTF::DefaultVertexAttributes[AttribIndex];
            const int*                       pAccessor = GltfPrimitive.GetAttribute(DstAttrib.Name);
            AttributeAccessors.push_back(pAccessor != nullptr ? *pAccessor : -1);
        }
    }

    bool operator==(const PrimitiveVertexKey& Rhs) const noexcept
    {
        return AttributeAccessors == Rhs.AttributeAccessors;
    }

    struct Hasher
    {
        size_t operator()(const PrimitiveVertexKey& Key) const noexcept
        {
            size_t Hash = ComputeHash(Key.AttributeAccessors.size());
            for (int Accessor : Key.AttributeAccessors)
                HashCombine(Hash, Accessor);
            return Hash;
        }
    };
};

struct PrimitiveIndexKey
{
    int    IndexAccessor       = -1;
    Uint32 GeneratedIndexCount = 0;

    PrimitiveIndexKey(const GLTF::TinyGltfPrimitiveView& GltfPrimitive,
                      Uint32                             VertexCount) :
        IndexAccessor{GltfPrimitive.GetIndicesId()},
        GeneratedIndexCount{IndexAccessor < 0 ? VertexCount : 0u}
    {
    }

    bool operator==(const PrimitiveIndexKey& Rhs) const noexcept
    {
        return IndexAccessor == Rhs.IndexAccessor &&
            GeneratedIndexCount == Rhs.GeneratedIndexCount;
    }

    struct Hasher
    {
        size_t operator()(const PrimitiveIndexKey& Key) const noexcept
        {
            return ComputeHash(Key.IndexAccessor, Key.GeneratedIndexCount);
        }
    };
};

struct PlannedVertexData
{
    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;

    Uint32 VertexCount = 0;
    float3 BBMin;
    float3 BBMax;
};

struct PlannedIndexData
{
    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;

    Uint32 IndexCount = 0;
};

struct PlannedPrimitive
{
    Uint32 VertexDataIndex = ~0u;
    Uint32 IndexDataIndex  = ~0u;
    int    MaterialId      = -1;
};

struct PlannedMesh
{
    std::string                   Name;
    std::vector<PlannedPrimitive> Primitives;
};

class RadientGLTFGeometryPlan
{
public:
    RadientGLTFGeometryPlan(const GLTF::TinyGltfModelView&        GltfModel,
                            IThreadPool&                          ThreadPool,
                            RadientMeshAssetManager&              MeshManager,
                            std::shared_ptr<const GLTF::Document> pDocument) :
        m_GltfModel{GltfModel},
        m_ThreadPool{ThreadPool},
        m_MeshManager{MeshManager},
        m_pDocument{std::move(pDocument)}
    {
    }

    RADIENT_STATUS Build(int SceneIndex)
    {
        m_VertexData.clear();
        m_VertexDataMap.clear();
        m_IndexData.clear();
        m_IndexDataMap.clear();
        m_Meshes.clear();
        m_ScannedMeshes.clear();

        m_Meshes.resize(m_GltfModel.GetMeshCount());
        m_ScannedMeshes.resize(m_GltfModel.GetMeshCount(), false);

        std::vector<int> NodesToScan;

        if (m_GltfModel.GetSceneCount() == 0 || SceneIndex < 0)
        {
            NodesToScan.reserve(m_GltfModel.GetNodeCount());
            for (size_t NodeIndex = 0; NodeIndex < m_GltfModel.GetNodeCount(); ++NodeIndex)
                NodesToScan.push_back(static_cast<int>(NodeIndex));
        }
        else if (SceneIndex >= static_cast<int>(m_GltfModel.GetSceneCount()))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        else
        {
            const auto GltfScene = m_GltfModel.GetScene(SceneIndex);
            NodesToScan.reserve(GltfScene.GetNodeCount());
            for (size_t NodeIndex = 0; NodeIndex < GltfScene.GetNodeCount(); ++NodeIndex)
                NodesToScan.push_back(GltfScene.GetNodeId(NodeIndex));
        }

        std::unordered_set<int> VisitedNodes;
        for (int NodeIndex : NodesToScan)
        {
            const RADIENT_STATUS Status = ScanNode(NodeIndex, VisitedNodes);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        return RADIENT_STATUS_OK;
    }

    const PlannedMesh* GetMesh(int GltfMeshIndex) const
    {
        return GltfMeshIndex >= 0 && static_cast<size_t>(GltfMeshIndex) < m_Meshes.size() ?
            &m_Meshes[static_cast<size_t>(GltfMeshIndex)] :
            nullptr;
    }

    const PlannedVertexData* GetVertexData(Uint32 VertexDataIndex) const
    {
        return VertexDataIndex < m_VertexData.size() ? &m_VertexData[VertexDataIndex] : nullptr;
    }

    const PlannedIndexData* GetIndexData(Uint32 IndexDataIndex) const
    {
        return IndexDataIndex < m_IndexData.size() ? &m_IndexData[IndexDataIndex] : nullptr;
    }

private:
    RADIENT_STATUS ScanNode(int                      NodeIndex,
                            std::unordered_set<int>& VisitedNodes)
    {
        if (NodeIndex < 0 || static_cast<size_t>(NodeIndex) >= m_GltfModel.GetNodeCount())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (!VisitedNodes.emplace(NodeIndex).second)
            return RADIENT_STATUS_OK;

        const auto GltfNode  = m_GltfModel.GetNode(NodeIndex);
        const int  MeshIndex = GltfNode.GetMeshId();
        if (MeshIndex >= 0)
        {
            const RADIENT_STATUS Status = ScanMesh(MeshIndex);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        for (int ChildNodeIndex : GltfNode.GetChildrenIds())
        {
            const RADIENT_STATUS Status = ScanNode(ChildNodeIndex, VisitedNodes);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS ScanMesh(int MeshIndex)
    {
        if (MeshIndex < 0 || static_cast<size_t>(MeshIndex) >= m_Meshes.size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (m_ScannedMeshes[static_cast<size_t>(MeshIndex)])
            return RADIENT_STATUS_OK;

        m_ScannedMeshes[static_cast<size_t>(MeshIndex)] = true;

        const auto   GltfMesh = m_GltfModel.GetMesh(MeshIndex);
        PlannedMesh& Mesh     = m_Meshes[static_cast<size_t>(MeshIndex)];
        Mesh.Name             = GltfMesh.GetName();
        Mesh.Primitives.reserve(GltfMesh.GetPrimitiveCount());

        for (size_t PrimitiveIndex = 0; PrimitiveIndex < GltfMesh.GetPrimitiveCount(); ++PrimitiveIndex)
        {
            const auto GltfPrimitive = GltfMesh.GetPrimitive(PrimitiveIndex);

            Uint32 VertexDataIndex = ~0u;

            RADIENT_STATUS Status = GetOrCreateVertexData(GltfPrimitive, VertexDataIndex);
            if (RADIENT_FAILED(Status))
                return Status;

            const PlannedVertexData* pVertexData = GetVertexData(VertexDataIndex);
            if (pVertexData == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            Uint32 IndexDataIndex = ~0u;

            Status = GetOrCreateIndexData(GltfPrimitive, pVertexData->VertexCount, IndexDataIndex);
            if (RADIENT_FAILED(Status))
                return Status;

            PlannedPrimitive& Primitive = Mesh.Primitives.emplace_back();
            Primitive.VertexDataIndex   = VertexDataIndex;
            Primitive.IndexDataIndex    = IndexDataIndex;
            Primitive.MaterialId        = GltfPrimitive.GetMaterialId();
        }

        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS GetOrCreateVertexData(const GLTF::TinyGltfPrimitiveView& GltfPrimitive,
                                         Uint32&                            VertexDataIndex)
    {
        const PrimitiveVertexKey Key{GltfPrimitive};

        auto VertexDataIt = m_VertexDataMap.find(Key);
        if (VertexDataIt != m_VertexDataMap.end())
        {
            VertexDataIndex = VertexDataIt->second;
            return RADIENT_STATUS_OK;
        }

        RadientGLTFConverter::MeshVertexSourceResult VertexSource =
            RadientGLTFConverter::CreateMeshVertexSource(m_GltfModel, GltfPrimitive, m_pDocument);
        if (RADIENT_FAILED(VertexSource.Status) || VertexSource.pSource == nullptr)
            return RADIENT_FAILED(VertexSource.Status) ? VertexSource.Status : RADIENT_STATUS_INVALID_OPERATION;

        PlannedVertexData VertexData;
        VertexData.VertexCount = VertexSource.pSource->GetVertexCount();
        VertexData.BBMin       = VertexSource.BBMin;
        VertexData.BBMax       = VertexSource.BBMax;

        RADIENT_STATUS Status = m_MeshManager.CreateMeshVertexData(m_ThreadPool,
                                                                   std::move(VertexSource.pSource),
                                                                   VertexData.pVertexData.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || VertexData.pVertexData == nullptr)
            return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

        VertexDataIndex = static_cast<Uint32>(m_VertexData.size());
        m_VertexData.emplace_back(std::move(VertexData));
        m_VertexDataMap.emplace(Key, VertexDataIndex);
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS GetOrCreateIndexData(const GLTF::TinyGltfPrimitiveView& GltfPrimitive,
                                        Uint32                             VertexCount,
                                        Uint32&                            IndexDataIndex)
    {
        const PrimitiveIndexKey Key{GltfPrimitive, VertexCount};

        auto IndexDataIt = m_IndexDataMap.find(Key);
        if (IndexDataIt != m_IndexDataMap.end())
        {
            IndexDataIndex = IndexDataIt->second;
            return RADIENT_STATUS_OK;
        }

        RadientGLTFConverter::MeshIndexSourceResult IndexSource =
            RadientGLTFConverter::CreateMeshIndexSource(m_GltfModel, GltfPrimitive, m_pDocument, VertexCount);
        if (RADIENT_FAILED(IndexSource.Status) || IndexSource.pSource == nullptr)
            return RADIENT_FAILED(IndexSource.Status) ? IndexSource.Status : RADIENT_STATUS_INVALID_OPERATION;

        PlannedIndexData IndexData;
        IndexData.IndexCount = IndexSource.pSource->GetIndexCount();

        RADIENT_STATUS Status = m_MeshManager.CreateMeshIndexData(m_ThreadPool,
                                                                  std::move(IndexSource.pSource),
                                                                  IndexData.pIndexData.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || IndexData.pIndexData == nullptr)
            return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;

        IndexDataIndex = static_cast<Uint32>(m_IndexData.size());
        m_IndexData.emplace_back(std::move(IndexData));
        m_IndexDataMap.emplace(Key, IndexDataIndex);
        return RADIENT_STATUS_OK;
    }

private:
    const GLTF::TinyGltfModelView&        m_GltfModel;
    IThreadPool&                          m_ThreadPool;
    RadientMeshAssetManager&              m_MeshManager;
    std::shared_ptr<const GLTF::Document> m_pDocument;

    std::vector<PlannedVertexData>                                             m_VertexData;
    std::unordered_map<PrimitiveVertexKey, Uint32, PrimitiveVertexKey::Hasher> m_VertexDataMap;
    std::vector<PlannedIndexData>                                              m_IndexData;
    std::unordered_map<PrimitiveIndexKey, Uint32, PrimitiveIndexKey::Hasher>   m_IndexDataMap;
    std::vector<PlannedMesh>                                                   m_Meshes;
    std::vector<bool>                                                          m_ScannedMeshes;
};

class RadientMeshLoader
{
public:
    RadientMeshLoader(IThreadPool&                   ThreadPool,
                      RadientMeshAssetManager&       MeshManager,
                      GLTF::Model&                   Model,
                      const RadientGLTFGeometryPlan& GeometryPlan,
                      const MaterialAssetList&       Materials,
                      MeshAssetList&                 Meshes) :
        m_ThreadPool{ThreadPool},
        m_MeshManager{MeshManager},
        m_Model{Model},
        m_GeometryPlan{GeometryPlan},
        m_Materials{Materials},
        m_Meshes{Meshes}
    {
    }

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    GLTF::Mesh* GetLoadedMesh(int LoadedMeshId)
    {
        return LoadedMeshId >= 0 && static_cast<size_t>(LoadedMeshId) < m_Model.Meshes.size() ?
            &m_Model.Meshes[static_cast<size_t>(LoadedMeshId)] :
            nullptr;
    }

    GLTF::Mesh* LoadMesh(const GLTF::TinyGltfModelView& GltfModel,
                         int                            GltfMeshIndex,
                         int                            LoadedMeshId)
    {
        GLTF::Mesh* pNewMesh = GetLoadedMesh(LoadedMeshId);
        if (pNewMesh == nullptr)
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return nullptr;
        }

        const auto GltfMesh = GltfModel.GetMesh(GltfMeshIndex);
        pNewMesh->Name      = GltfMesh.GetName();

        const PlannedMesh* pPlannedMesh = m_GeometryPlan.GetMesh(GltfMeshIndex);
        if (pPlannedMesh == nullptr ||
            pPlannedMesh->Primitives.empty())
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return pNewMesh;
        }

        std::vector<RadientMeshPrimitiveCreateInfo> Primitives;
        std::vector<Uint32>                         GeometryIndices;
        std::vector<RadientMeshGeometryData>        MeshGeometryData;

        Primitives.reserve(pPlannedMesh->Primitives.size());
        GeometryIndices.reserve(pPlannedMesh->Primitives.size());
        pNewMesh->Primitives.reserve(pPlannedMesh->Primitives.size());

        std::vector<const PlannedPrimitive*> SortedPrimitives;
        SortedPrimitives.reserve(pPlannedMesh->Primitives.size());
        for (const PlannedPrimitive& PlannedPrimitive : pPlannedMesh->Primitives)
            SortedPrimitives.push_back(&PlannedPrimitive);

        // Group primitives that use the same vertex/index data so the mesh
        // view references each unique geometry only once.
        std::stable_sort(SortedPrimitives.begin(),
                         SortedPrimitives.end(),
                         [](const PlannedPrimitive* pLhs, const PlannedPrimitive* pRhs) {
                             VERIFY_EXPR(pLhs != nullptr && pRhs != nullptr);
                             if (pLhs->VertexDataIndex != pRhs->VertexDataIndex)
                                 return pLhs->VertexDataIndex < pRhs->VertexDataIndex;
                             return pLhs->IndexDataIndex < pRhs->IndexDataIndex;
                         });

        for (const PlannedPrimitive* pSortedPrimitive : SortedPrimitives)
        {
            VERIFY_EXPR(pSortedPrimitive != nullptr);
            const PlannedPrimitive& PlannedPrimitive = *pSortedPrimitive;

            const PlannedVertexData* pVertexData = m_GeometryPlan.GetVertexData(PlannedPrimitive.VertexDataIndex);
            const PlannedIndexData*  pIndexData  = m_GeometryPlan.GetIndexData(PlannedPrimitive.IndexDataIndex);
            if (pVertexData == nullptr ||
                pIndexData == nullptr ||
                pVertexData->pVertexData == nullptr ||
                pIndexData->pIndexData == nullptr)
            {
                m_Status = RADIENT_STATUS_INVALID_OPERATION;
                return pNewMesh;
            }

            Uint32 LocalGeometryIndex = ~0u;
            if (!MeshGeometryData.empty())
            {
                const RadientMeshGeometryData& LastGeometry = MeshGeometryData.back();
                if (LastGeometry.pVertexData == pVertexData->pVertexData &&
                    LastGeometry.pIndexData == pIndexData->pIndexData)
                {
                    LocalGeometryIndex = static_cast<Uint32>(MeshGeometryData.size() - 1);
                }
            }

            if (LocalGeometryIndex == ~0u)
            {
                LocalGeometryIndex = static_cast<Uint32>(MeshGeometryData.size());
                MeshGeometryData.push_back(RadientMeshGeometryData{pVertexData->pVertexData, pIndexData->pIndexData});
            }

            const Uint32 MaterialSlot = PlannedPrimitive.MaterialId >= 0 ?
                static_cast<Uint32>(PlannedPrimitive.MaterialId) :
                0u;

            pNewMesh->Primitives.emplace_back(0u,
                                              pIndexData->IndexCount,
                                              0u,
                                              pVertexData->VertexCount,
                                              MaterialSlot,
                                              pVertexData->BBMin,
                                              pVertexData->BBMax);

            RadientMeshPrimitiveCreateInfo& Primitive = Primitives.emplace_back();
            Primitive.FirstIndex                      = 0;
            Primitive.IndexCount                      = pIndexData->IndexCount;
            if (PlannedPrimitive.MaterialId >= 0 && static_cast<size_t>(PlannedPrimitive.MaterialId) < m_Materials.size())
                Primitive.pMaterial = m_Materials[static_cast<size_t>(PlannedPrimitive.MaterialId)];

            GeometryIndices.push_back(LocalGeometryIndex);
        }

        pNewMesh->UpdateBoundingBox();

        RadientMeshViewCreateInfo ViewCI;
        ViewCI.pPrimitives      = Primitives.data();
        ViewCI.PrimitiveCount   = static_cast<Uint32>(Primitives.size());
        ViewCI.pGeometryIndices = GeometryIndices.data();

        RefCntAutoPtr<IRadientMeshAsset> pMeshAsset;

        RADIENT_STATUS Status = m_MeshManager.CreateMeshView(m_ThreadPool,
                                                             MeshGeometryData.data(),
                                                             static_cast<Uint32>(MeshGeometryData.size()),
                                                             ViewCI,
                                                             pMeshAsset.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || pMeshAsset == nullptr)
        {
            m_Status = RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_INVALID_OPERATION;
            return pNewMesh;
        }

        if (m_Meshes.size() <= static_cast<size_t>(LoadedMeshId))
            m_Meshes.resize(static_cast<size_t>(LoadedMeshId) + 1);
        m_Meshes[static_cast<size_t>(LoadedMeshId)] = pMeshAsset;

        pNewMesh->pUserData = RefCntAutoPtr<IObject>{pMeshAsset.RawPtr(), IID_Unknown};
        return pNewMesh;
    }

private:
    IThreadPool&                   m_ThreadPool;
    RadientMeshAssetManager&       m_MeshManager;
    GLTF::Model&                   m_Model;
    const RadientGLTFGeometryPlan& m_GeometryPlan;
    const MaterialAssetList&       m_Materials;
    MeshAssetList&                 m_Meshes;
    RADIENT_STATUS                 m_Status = RADIENT_STATUS_OK;
};

} // namespace

namespace RadientGLTFLoader
{

RadientImport::TextureAssetList LoadTextures(IThreadPool&                           ThreadPool,
                                             RadientTextureAssetManager&            TextureManager,
                                             const std::string&                     SourceURI,
                                             const std::shared_ptr<GLTF::Document>& pDocument)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return {};

    const Uint32                    TextureCount = pDocument->GetTextureCount();
    RadientImport::TextureAssetList Textures(TextureCount);

    for (Uint32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
    {
        GLTF::TextureSourceInfo Source;
        if (!pDocument->GetTextureSourceInfo(TextureIndex, Source))
        {
            LOG_ERROR_MESSAGE("Failed to resolve GLTF texture source ", TextureIndex, " in '", SourceURI, "'");
            continue;
        }

        const std::string TextureURI =
            !Source.URI.empty() ?
            Source.URI :
            MakeEmbeddedGLTFTextureURI(SourceURI, TextureIndex);

        RadientTextureLoadInfo LoadInfo;
        LoadInfo.URI      = TextureURI.c_str();
        LoadInfo.BaseURI  = !Source.URI.empty() ? SourceURI.c_str() : nullptr;
        LoadInfo.pData    = Source.pData;
        LoadInfo.DataSize = Source.DataSize;
        LoadInfo.IsSRGB   = False;

        std::unique_ptr<std::shared_ptr<const GLTF::Document>> pDocumentOwner;
        if (Source.pData != nullptr)
        {
            // Embedded texture bytes are owned by the temporary GLTF document.
            // The release callback keeps that document alive until the texture
            // worker has created its loader/cache key from the borrowed bytes.
            pDocumentOwner                = std::make_unique<std::shared_ptr<const GLTF::Document>>(pDocument);
            LoadInfo.ReleaseData          = ReleaseGLTFTextureSourceData;
            LoadInfo.pReleaseDataUserData = pDocumentOwner.get();
        }

        TextureManager.LoadTexture(ThreadPool, LoadInfo, Textures[TextureIndex].GetAddressOfEmpty());
        if (Textures[TextureIndex] == nullptr)
        {
            LOG_ERROR_MESSAGE("Failed to create Radient texture asset for GLTF texture ", TextureIndex, " in '", SourceURI, "'");
            continue;
        }

        pDocumentOwner.release();
    }

    return Textures;
}

RadientImport::MaterialAssetList LoadMaterials(RadientMaterialAssetManager&           MaterialManager,
                                               const std::shared_ptr<GLTF::Document>& pDocument,
                                               const RadientImport::TextureAssetList& Textures)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return {};

    std::vector<IRadientTextureAsset*> RawTextures(Textures.size());
    for (size_t TextureIndex = 0; TextureIndex < Textures.size(); ++TextureIndex)
        RawTextures[TextureIndex] = Textures[TextureIndex];

    const Uint32                     MaterialCount = pDocument->GetMaterialCount();
    RadientImport::MaterialAssetList Materials(MaterialCount);

    for (Uint32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        GLTF::Material Material = GLTF::LoadMaterial(*pDocument, MaterialIndex);

        const RADIENT_STATUS Status =
            MaterialManager.CreateGLTFMaterial(std::move(Material),
                                               RawTextures.empty() ? nullptr : RawTextures.data(),
                                               static_cast<Uint32>(RawTextures.size()),
                                               Materials[MaterialIndex].GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || Materials[MaterialIndex] == nullptr)
        {
            LOG_ERROR_MESSAGE("Failed to create Radient material asset for GLTF material ", MaterialIndex);
            Materials[MaterialIndex].Release();
        }
    }

    return Materials;
}

RADIENT_STATUS LoadScene(IThreadPool&                            ThreadPool,
                         RadientMeshAssetManager&                MeshManager,
                         const std::string&                      SourceURI,
                         const std::shared_ptr<GLTF::Document>&  pDocument,
                         const RadientImport::MaterialAssetList& Materials,
                         RadientImport::ImportedDocument&        Scene)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    GLTF::ModelCreateInfo   ModelCI{SourceURI.c_str()};
    GLTF::Model             MetadataModel{ModelCI};
    GLTF::TinyGltfModelView GltfModel{pDocument->GetModel()};

    RadientGLTFGeometryPlan GeometryPlan{GltfModel, ThreadPool, MeshManager, pDocument};
    RADIENT_STATUS          Status = GeometryPlan.Build(-1);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientMeshLoader MeshLoader{ThreadPool, MeshManager, MetadataModel, GeometryPlan, Materials, Scene.Meshes};

    GLTF::ModelBuilder Builder{ModelCI, MetadataModel};
    Builder.BuildModel(GltfModel, -1, MeshLoader);

    const RADIENT_STATUS MeshStatus = MeshLoader.GetStatus();
    if (RADIENT_FAILED(MeshStatus))
        return MeshStatus;

    return RadientGLTFConverter::ExtractSceneGraph(MetadataModel, Scene);
}

} // namespace RadientGLTFLoader

} // namespace Diligent
