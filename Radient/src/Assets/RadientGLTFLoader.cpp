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

#include "Core/RadientValidation.hpp"
#include "Errors.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFDocument.hpp"
#include "GLTFLoader.hpp"
#include "HashUtils.hpp"
#include "Import/RadientGLTFConverter.hpp"
#include "Math/RadientMath.hpp"
#include "RadientDataBlob.h"
#include "RadientMeshImportServices.h"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <algorithm>
#include <array>
#include <limits>
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

struct TextureColorSpaceUsage
{
    bool Linear = false;
    bool SRGB   = false;
};

bool IsGLTFMaterialTextureSRGB(const GLTF::Material& Material,
                               Uint32                TextureAttribId) noexcept
{
    const bool IsSRGB =
        TextureAttribId == GLTF::DefaultBaseColorTextureAttribId ||
        TextureAttribId == GLTF::DefaultEmissiveTextureAttribId ||
        TextureAttribId == GLTF::DefaultSheenColorTextureAttribId ||
        TextureAttribId == GLTF::DefaultSpecularColorTextureAttribId ||
        (Material.Attribs.Workflow == GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS &&
         TextureAttribId == GLTF::DefaultSpecularGlossinessTextureAttibId);

    return IsSRGB;
}

std::vector<TextureColorSpaceUsage> GetTextureColorSpaceUsages(const GLTF::Document& Document)
{
    std::vector<TextureColorSpaceUsage> TextureUsages(Document.GetTextureCount());

    for (Uint32 MaterialIndex = 0; MaterialIndex < Document.GetMaterialCount(); ++MaterialIndex)
    {
        const GLTF::Material Material = GLTF::LoadMaterial(Document, MaterialIndex);
        Material.ProcessActiveTextureAttibs(
            [&](Uint32 TextureAttribId, const GLTF::Material::TextureShaderAttribs&, int TextureIndex) {
                if (TextureIndex >= 0 && static_cast<size_t>(TextureIndex) < TextureUsages.size())
                {
                    TextureColorSpaceUsage& Usage = TextureUsages[TextureIndex];
                    if (IsGLTFMaterialTextureSRGB(Material, TextureAttribId))
                        Usage.SRGB = true;
                    else
                        Usage.Linear = true;
                }
                return true;
            });
    }

    return TextureUsages;
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
    Uint32 VertexDataIndex      = ~0u;
    Uint32 IndexDataIndex       = ~0u;
    Uint32 SourcePrimitiveIndex = ~0u;
    int    MaterialId           = -1;
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
                            IRadientMeshImportServices&           MeshImportServices,
                            std::shared_ptr<const GLTF::Document> pDocument) :
        m_GltfModel{GltfModel},
        m_MeshImportServices{MeshImportServices},
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
            return RADIENT_STATUS_INVALID_DATA;

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
            return RADIENT_STATUS_INVALID_DATA;

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
                return RADIENT_STATUS_FAILED;

            Uint32 IndexDataIndex = ~0u;

            Status = GetOrCreateIndexData(GltfPrimitive, pVertexData->VertexCount, IndexDataIndex);
            if (RADIENT_FAILED(Status))
                return Status;

            PlannedPrimitive& Primitive    = Mesh.Primitives.emplace_back();
            Primitive.VertexDataIndex      = VertexDataIndex;
            Primitive.IndexDataIndex       = IndexDataIndex;
            Primitive.SourcePrimitiveIndex = static_cast<Uint32>(PrimitiveIndex);
            Primitive.MaterialId           = GltfPrimitive.GetMaterialId();
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

        RadientGLTFConverter::MeshVertexDataResult Result =
            RadientGLTFConverter::CreateMeshVertexData(m_MeshImportServices, m_GltfModel, GltfPrimitive, m_pDocument);
        if (RADIENT_FAILED(Result.Status) || Result.pVertexData == nullptr)
            return RADIENT_FAILED(Result.Status) ? Result.Status : RADIENT_STATUS_FAILED;

        PlannedVertexData VertexData;
        VertexData.pVertexData = std::move(Result.pVertexData);
        VertexData.VertexCount = Result.VertexCount;
        VertexData.BBMin       = Result.BBMin;
        VertexData.BBMax       = Result.BBMax;

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

        RadientGLTFConverter::MeshIndexDataResult Result =
            RadientGLTFConverter::CreateMeshIndexData(m_MeshImportServices, m_GltfModel, GltfPrimitive, m_pDocument, VertexCount);
        if (RADIENT_FAILED(Result.Status) || Result.pIndexData == nullptr)
            return RADIENT_FAILED(Result.Status) ? Result.Status : RADIENT_STATUS_FAILED;

        PlannedIndexData IndexData;
        IndexData.pIndexData = std::move(Result.pIndexData);
        IndexData.IndexCount = Result.IndexCount;

        IndexDataIndex = static_cast<Uint32>(m_IndexData.size());
        m_IndexData.emplace_back(std::move(IndexData));
        m_IndexDataMap.emplace(Key, IndexDataIndex);
        return RADIENT_STATUS_OK;
    }

private:
    const GLTF::TinyGltfModelView&        m_GltfModel;
    IRadientMeshImportServices&           m_MeshImportServices;
    std::shared_ptr<const GLTF::Document> m_pDocument;

    std::vector<PlannedVertexData>                                             m_VertexData;
    std::unordered_map<PrimitiveVertexKey, Uint32, PrimitiveVertexKey::Hasher> m_VertexDataMap;
    std::vector<PlannedIndexData>                                              m_IndexData;
    std::unordered_map<PrimitiveIndexKey, Uint32, PrimitiveIndexKey::Hasher>   m_IndexDataMap;
    std::vector<PlannedMesh>                                                   m_Meshes;
    std::vector<bool>                                                          m_ScannedMeshes;
};

struct MorphTargetAttributeSchema
{
    std::string Semantic;
    Uint32      ComponentCount = 0;
};

struct MorphTargetSchema
{
    std::string                             Name;
    Float32                                 DefaultWeight = 0.f;
    std::vector<MorphTargetAttributeSchema> Attributes;
};

RADIENT_STATUS CreateMorphTargetDataAssets(const GLTF::Mesh&                                        Mesh,
                                           IRadientMeshImportServices&                              MeshImportServices,
                                           std::vector<RefCntAutoPtr<IRadientMeshMorphTargetData>>& MorphTargetDataAssets)
{
    if (Mesh.Primitives.empty())
        return RADIENT_STATUS_OK;

    const size_t TargetCount = Mesh.Primitives.front().MorphTargets.size();
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh.Primitives.size(); ++PrimitiveIndex)
    {
        const GLTF::Primitive& Primitive = Mesh.Primitives[PrimitiveIndex];
        if (Primitive.MorphTargets.size() != TargetCount)
        {
            LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                "' because primitive ", PrimitiveIndex, " has ", Primitive.MorphTargets.size(),
                                " targets, while primitive 0 has ", TargetCount);
            return RADIENT_STATUS_OK;
        }
    }
    if (TargetCount == 0)
        return RADIENT_STATUS_OK;
    if (TargetCount > (std::numeric_limits<Uint32>::max)())
    {
        LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                            "' because the target count exceeds the supported range");
        return RADIENT_STATUS_OK;
    }

    if (!Mesh.Weights.empty() && Mesh.Weights.size() != TargetCount)
    {
        LOG_WARNING_MESSAGE("GLTF mesh '", Mesh.Name, "' provides ", Mesh.Weights.size(),
                            " default morph weights for ", TargetCount,
                            " targets; missing defaults will be zero and extra defaults will be ignored");
    }

    try
    {
        std::vector<MorphTargetSchema> TargetSchemas(TargetCount);
        for (size_t TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
        {
            MorphTargetSchema& Target = TargetSchemas[TargetIndex];
            if (TargetIndex < Mesh.MorphTargetNames.size())
                Target.Name = Mesh.MorphTargetNames[TargetIndex];
            if (TargetIndex < Mesh.Weights.size())
            {
                if (!RadientMath::IsFinite(Mesh.Weights[TargetIndex]))
                {
                    LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                        "' because default weight ", TargetIndex, " is not finite");
                    return RADIENT_STATUS_OK;
                }
                Target.DefaultWeight = Mesh.Weights[TargetIndex];
            }

            for (const GLTF::Primitive& Primitive : Mesh.Primitives)
            {
                for (const GLTF::MorphTargetAttribute& SourceAttribute : Primitive.MorphTargets[TargetIndex].Attributes)
                {
                    const auto ExistingAttribute = std::find_if(
                        Target.Attributes.begin(), Target.Attributes.end(),
                        [&SourceAttribute](const MorphTargetAttributeSchema& Attribute) {
                            return Attribute.Semantic == SourceAttribute.Semantic;
                        });
                    if (ExistingAttribute != Target.Attributes.end())
                    {
                        if (ExistingAttribute->ComponentCount != SourceAttribute.NumComponents)
                        {
                            LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                                "' because target ", TargetIndex, " attribute '",
                                                SourceAttribute.Semantic,
                                                "' has inconsistent component counts between primitives");
                            return RADIENT_STATUS_OK;
                        }
                        continue;
                    }

                    const bool IsStandardSemantic =
                        SourceAttribute.Semantic == RadientMorphTargetPositionSemantic ||
                        SourceAttribute.Semantic == RadientMorphTargetNormalSemantic ||
                        SourceAttribute.Semantic == RadientMorphTargetTangentSemantic;
                    if (SourceAttribute.NumComponents == 0 || SourceAttribute.NumComponents > 4 ||
                        (IsStandardSemantic && SourceAttribute.NumComponents != 3))
                    {
                        LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                            "' because target ", TargetIndex, " attribute '",
                                            SourceAttribute.Semantic, "' has an unsupported size");
                        return RADIENT_STATUS_OK;
                    }

                    MorphTargetAttributeSchema& Attribute = Target.Attributes.emplace_back();
                    Attribute.Semantic                    = SourceAttribute.Semantic;
                    Attribute.ComponentCount              = SourceAttribute.NumComponents;
                }
            }
        }

        std::vector<RefCntAutoPtr<IRadientMeshMorphTargetData>> Assets;
        Assets.reserve(Mesh.Primitives.size());
        for (size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh.Primitives.size(); ++PrimitiveIndex)
        {
            const GLTF::Primitive& Primitive = Mesh.Primitives[PrimitiveIndex];
            if (Primitive.VertexCount == 0)
                return RADIENT_STATUS_OK;

            std::vector<std::vector<RadientMorphTargetAttributeDesc>>       AttributeDescs(TargetCount);
            std::vector<std::vector<RadientMorphTargetAttributeCreateInfo>> AttributeCreateInfos(TargetCount);
            std::vector<RadientMorphTargetCreateInfo>                       TargetCreateInfos(TargetCount);
            std::array<std::vector<Float32>, 5>                              ZeroDeltas;

            for (size_t TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
            {
                const MorphTargetSchema& Target       = TargetSchemas[TargetIndex];
                const GLTF::MorphTarget& SourceTarget = Primitive.MorphTargets[TargetIndex];
                AttributeDescs[TargetIndex].resize(Target.Attributes.size());
                AttributeCreateInfos[TargetIndex].resize(Target.Attributes.size());

                for (size_t AttributeIndex = 0; AttributeIndex < Target.Attributes.size(); ++AttributeIndex)
                {
                    const MorphTargetAttributeSchema& Attribute = Target.Attributes[AttributeIndex];
                    AttributeDescs[TargetIndex][AttributeIndex].Semantic       = Attribute.Semantic.c_str();
                    AttributeDescs[TargetIndex][AttributeIndex].ComponentCount = Attribute.ComponentCount;

                    if (!RadientValidation::IsAddressableArray(Primitive.VertexCount, Attribute.ComponentCount))
                    {
                        LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                            "' because primitive ", PrimitiveIndex, " target ", TargetIndex,
                                            " attribute '", Attribute.Semantic, "' exceeds the supported size");
                        return RADIENT_STATUS_OK;
                    }
                    const size_t ValueCount = size_t{Primitive.VertexCount} * Attribute.ComponentCount;

                    const GLTF::MorphTargetAttribute* const pSourceAttribute =
                        SourceTarget.FindAttribute(Attribute.Semantic.c_str());
                    if (pSourceAttribute != nullptr)
                    {
                        if (!RadientValidation::IsValidSubrange(pSourceAttribute->FirstValue,
                                                                ValueCount,
                                                                SourceTarget.Values.size()))
                        {
                            LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                                "' because primitive ", PrimitiveIndex, " target ", TargetIndex,
                                                " attribute '", Attribute.Semantic, "' has an invalid value range");
                            return RADIENT_STATUS_OK;
                        }
                        AttributeCreateInfos[TargetIndex][AttributeIndex].pDeltas =
                            SourceTarget.GetAttributeData(*pSourceAttribute);
                    }
                    else
                    {
                        std::vector<Float32>& Deltas = ZeroDeltas[Attribute.ComponentCount];
                        Deltas.resize(ValueCount, 0.f);
                        AttributeCreateInfos[TargetIndex][AttributeIndex].pDeltas = Deltas.data();
                    }
                }

                RadientMorphTargetCreateInfo& TargetCI = TargetCreateInfos[TargetIndex];
                TargetCI.Desc.Name                     = Target.Name.c_str();
                TargetCI.Desc.pAttributes              = AttributeDescs[TargetIndex].empty() ? nullptr : AttributeDescs[TargetIndex].data();
                TargetCI.Desc.AttributeCount           = static_cast<Uint32>(AttributeDescs[TargetIndex].size());
                TargetCI.Desc.DefaultWeight            = Target.DefaultWeight;
                TargetCI.pAttributeData                = AttributeCreateInfos[TargetIndex].empty() ? nullptr : AttributeCreateInfos[TargetIndex].data();
            }

            const RadientMeshMorphTargetData MorphTargetData{
                TargetCreateInfos.data(), static_cast<Uint32>(TargetCreateInfos.size())};
            RefCntAutoPtr<IRadientMeshMorphTargetData> pMorphTargetData;
            const RADIENT_STATUS                       Status = MeshImportServices.CreateMeshMorphTargetData(
                MorphTargetData, Primitive.VertexCount, &pMorphTargetData);
            if (Status == RADIENT_STATUS_INVALID_ARGUMENT && pMorphTargetData == nullptr)
            {
                LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                                    "' because primitive ", PrimitiveIndex, " could not be packed");
                return RADIENT_STATUS_OK;
            }
            if (RADIENT_FAILED(Status) || pMorphTargetData == nullptr)
                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

            Assets.emplace_back(std::move(pMorphTargetData));
        }

        MorphTargetDataAssets = std::move(Assets);
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_WARNING_MESSAGE("Ignoring morph targets for GLTF mesh '", Mesh.Name,
                            "' because their data could not be packed: ", Error.what());
        return RADIENT_STATUS_OK;
    }
}

class RadientMeshLoader
{
public:
    RadientMeshLoader(IRadientMeshImportServices&    MeshImportServices,
                      GLTF::Model&                   Model,
                      const RadientGLTFGeometryPlan& GeometryPlan,
                      const MaterialAssetList&       Materials,
                      IRadientMaterialAsset*         pDefaultMaterial,
                      MeshAssetList&                 Meshes) :
        m_MeshImportServices{MeshImportServices},
        m_Model{Model},
        m_GeometryPlan{GeometryPlan},
        m_Materials{Materials},
        m_pDefaultMaterial{pDefaultMaterial},
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
            m_Status = RADIENT_STATUS_FAILED;
            return nullptr;
        }

        const auto GltfMesh = GltfModel.GetMesh(GltfMeshIndex);
        pNewMesh->Name      = GltfMesh.GetName();

        const PlannedMesh* pPlannedMesh = m_GeometryPlan.GetMesh(GltfMeshIndex);
        if (pPlannedMesh == nullptr ||
            pPlannedMesh->Primitives.empty())
        {
            m_Status = RADIENT_STATUS_INVALID_DATA;
            return pNewMesh;
        }

        std::vector<RadientMeshPrimitiveCreateInfo> Primitives;
        std::vector<Uint32>                         GeometryIndices;
        std::vector<RadientMeshGeometryData>        MeshGeometryData;

        Primitives.reserve(pPlannedMesh->Primitives.size());
        GeometryIndices.reserve(pPlannedMesh->Primitives.size());
        pNewMesh->Primitives.reserve(pPlannedMesh->Primitives.size());
        bool HasMorphTargets    = false;
        bool MorphTargetsLoaded = true;

        for (const PlannedPrimitive& PlannedPrimitive : pPlannedMesh->Primitives)
        {
            if (GltfMesh.GetPrimitive(PlannedPrimitive.SourcePrimitiveIndex).GetMorphTargetCount() != 0)
            {
                HasMorphTargets = true;
                break;
            }
        }

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
                m_Status = RADIENT_STATUS_FAILED;
                return pNewMesh;
            }

            Uint32 LocalGeometryIndex = ~0u;
            if (!HasMorphTargets && !MeshGeometryData.empty())
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

            GLTF::Primitive& NewPrimitive = pNewMesh->Primitives.emplace_back(0u,
                                                                              pIndexData->IndexCount,
                                                                              0u,
                                                                              pVertexData->VertexCount,
                                                                              MaterialSlot,
                                                                              pVertexData->BBMin,
                                                                              pVertexData->BBMax);

            const auto GltfPrimitive = GltfMesh.GetPrimitive(PlannedPrimitive.SourcePrimitiveIndex);
            if (!GLTF::LoadMorphTargets(GltfModel, GltfPrimitive, pVertexData->VertexCount, NewPrimitive))
            {
                LOG_WARNING_MESSAGE("Failed to load morph targets for GLTF mesh '", pNewMesh->Name,
                                    "' primitive ", PlannedPrimitive.SourcePrimitiveIndex,
                                    "; loading the mesh without morph targets");
                MorphTargetsLoaded = false;
            }

            RadientMeshPrimitiveCreateInfo& Primitive = Primitives.emplace_back();
            Primitive.FirstIndex                      = 0;
            Primitive.IndexCount                      = pIndexData->IndexCount;
            if (PlannedPrimitive.MaterialId >= 0 && static_cast<size_t>(PlannedPrimitive.MaterialId) < m_Materials.size())
                Primitive.pMaterial = m_Materials[static_cast<size_t>(PlannedPrimitive.MaterialId)];
            else if (PlannedPrimitive.MaterialId < 0)
                Primitive.pMaterial = m_pDefaultMaterial;

            GeometryIndices.push_back(LocalGeometryIndex);
        }

        pNewMesh->UpdateBoundingBox();

        // MeshGeometryData stores borrowed pointers. Retain the assets until
        // CreateMeshView has captured its own strong references. Attach morph
        // data only when every primitive has the complete common target schema.
        std::vector<RefCntAutoPtr<IRadientMeshMorphTargetData>> MorphTargetDataAssets;
        if (MorphTargetsLoaded && HasMorphTargets)
        {
            const RADIENT_STATUS Status = CreateMorphTargetDataAssets(*pNewMesh, m_MeshImportServices, MorphTargetDataAssets);
            if (RADIENT_FAILED(Status))
            {
                m_Status = Status;
                return pNewMesh;
            }
            MorphTargetsLoaded = MorphTargetDataAssets.size() == pNewMesh->Primitives.size();
        }

        if (MorphTargetsLoaded && HasMorphTargets)
        {
            VERIFY_EXPR(MorphTargetDataAssets.size() == MeshGeometryData.size());
            for (size_t GeometryIndex = 0; GeometryIndex < MorphTargetDataAssets.size(); ++GeometryIndex)
                MeshGeometryData[GeometryIndex].pMorphTargetData = MorphTargetDataAssets[GeometryIndex];
        }
        else if (!MorphTargetsLoaded)
        {
            for (GLTF::Primitive& Primitive : pNewMesh->Primitives)
                Primitive.MorphTargets.clear();
        }

        RadientMeshViewCreateInfo ViewCI;
        ViewCI.pPrimitives      = Primitives.data();
        ViewCI.PrimitiveCount   = static_cast<Uint32>(Primitives.size());
        ViewCI.pGeometryIndices = GeometryIndices.data();
        ViewCI.pGeometryData    = MeshGeometryData.data();
        ViewCI.GeometryCount    = static_cast<Uint32>(MeshGeometryData.size());

        RefCntAutoPtr<IRadientMeshAsset> pMeshAsset;

        const RADIENT_STATUS Status = m_MeshImportServices.CreateMeshView(ViewCI, pMeshAsset.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || pMeshAsset == nullptr)
        {
            m_Status = RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
            return pNewMesh;
        }

        if (m_Meshes.size() <= static_cast<size_t>(LoadedMeshId))
            m_Meshes.resize(static_cast<size_t>(LoadedMeshId) + 1);
        m_Meshes[static_cast<size_t>(LoadedMeshId)] = pMeshAsset;

        pNewMesh->pUserData = RefCntAutoPtr<IObject>{pMeshAsset.RawPtr(), IID_Unknown};
        return pNewMesh;
    }

private:
    IRadientMeshImportServices&    m_MeshImportServices;
    GLTF::Model&                   m_Model;
    const RadientGLTFGeometryPlan& m_GeometryPlan;
    const MaterialAssetList&       m_Materials;
    IRadientMaterialAsset*         m_pDefaultMaterial = nullptr;
    MeshAssetList&                 m_Meshes;
    RADIENT_STATUS                 m_Status = RADIENT_STATUS_OK;
};

} // namespace

namespace RadientGLTFLoader
{

RadientImport::TextureAssetList LoadTextures(IRadientAssetManager&                  AssetManager,
                                             const std::string&                     SourceURI,
                                             const std::shared_ptr<GLTF::Document>& pDocument)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return {};

    const Uint32                              TextureCount  = pDocument->GetTextureCount();
    const std::vector<TextureColorSpaceUsage> TextureUsages = GetTextureColorSpaceUsages(*pDocument);
    RadientImport::TextureAssetList           Textures(TextureCount);

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
        LoadInfo.URI = TextureURI.c_str();
        // External image URIs returned by GetTextureSourceInfo() are already
        // resolved relative to the GLTF document, so no base URI is needed.
        LoadInfo.BaseURI = nullptr;

        const TextureColorSpaceUsage& Usage = TextureUsages[TextureIndex];
        if (Usage.SRGB && Usage.Linear)
        {
            LOG_WARNING_MESSAGE("GLTF texture ", TextureIndex, " in '", SourceURI,
                                "' is used by both sRGB and linear material attributes; loading it as linear");
        }
        LoadInfo.IsSRGB = Usage.SRGB && !Usage.Linear;

        RefCntAutoPtr<IRadientDataBlob> pDataBlob;
        if (Source.pData != nullptr)
        {
            // Retain the document while queued loads or decoders reference its image bytes.
            auto pDocumentOwner = std::make_unique<std::shared_ptr<const GLTF::Document>>(pDocument);
            RadientDataBlobCreateInfo BlobCI;
            BlobCI.pData     = Source.pData;
            BlobCI.Size      = Source.DataSize;
            BlobCI.pUserData = pDocumentOwner.get();
            BlobCI.OnDestroy = [](void* pUserData) {
                delete static_cast<std::shared_ptr<const GLTF::Document>*>(pUserData);
            };
            if (CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pDataBlob) != RADIENT_STATUS_OK)
            {
                LOG_ERROR_MESSAGE("Failed to create data blob for GLTF texture ", TextureIndex, " in '", SourceURI, "'");
                continue;
            }
            pDocumentOwner.release();
            LoadInfo.pDataBlob = pDataBlob;
        }

        AssetManager.LoadTexture(LoadInfo, Textures[TextureIndex].GetAddressOfEmpty());
        if (Textures[TextureIndex] == nullptr)
        {
            LOG_ERROR_MESSAGE("Failed to create Radient texture asset for GLTF texture ", TextureIndex, " in '", SourceURI, "'");
            continue;
        }
    }

    return Textures;
}

RADIENT_STATUS CreateImportedMaterial(IRadientAssetManager&        AssetManager,
                                      const GLTF::Material&        Material,
                                      IRadientTextureAsset* const* ppTextures,
                                      Uint32                       TextureCount,
                                      IRadientMaterialAsset**      ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppMaterial = nullptr;

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RADIENT_STATUS                              Status =
        RadientGLTFConverter::ConvertMaterialDefinition(Material, DefinitionCI);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    Status = AssetManager.CreateStandardMaterialDefinition(
        DefinitionCI, pDefinition.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    Status = AssetManager.CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    Status = pMaterial->CreateWriter(pWriter.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    Status = RadientGLTFConverter::PopulateMaterial(
        Material, ppTextures, TextureCount, *pDefinition, *pWriter);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    Status = pWriter->Commit();
    if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
        return Status;

    *ppMaterial = pMaterial.Detach();
    return RADIENT_STATUS_OK;
}

RadientImport::MaterialAssetList LoadMaterials(IRadientAssetManager&                  AssetManager,
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
        const GLTF::Material Material = GLTF::LoadMaterial(*pDocument, MaterialIndex);

        const RADIENT_STATUS Status =
            CreateImportedMaterial(AssetManager,
                                   Material,
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

RADIENT_STATUS LoadScene(IRadientMeshImportServices&             MeshImportServices,
                         const std::string&                      SourceURI,
                         const std::shared_ptr<GLTF::Document>&  pDocument,
                         const RadientImport::MaterialAssetList& Materials,
                         IRadientMaterialAsset*                  pDefaultMaterial,
                         IRadientAssetManager*                   pAssetManager,
                         RadientImport::ImportedDocument&        Scene)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    GLTF::ModelCreateInfo   ModelCI{SourceURI.c_str()};
    GLTF::Model             MetadataModel{ModelCI};
    GLTF::TinyGltfModelView GltfModel{pDocument->GetModel()};

    RadientGLTFGeometryPlan GeometryPlan{GltfModel, MeshImportServices, pDocument};
    RADIENT_STATUS          Status = GeometryPlan.Build(-1);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientMeshLoader MeshLoader{MeshImportServices,
                                 MetadataModel,
                                 GeometryPlan,
                                 Materials,
                                 pDefaultMaterial,
                                 Scene.Meshes};

    GLTF::ModelBuilder Builder{ModelCI, MetadataModel};
    Builder.BuildModel(GltfModel, -1, MeshLoader);

    const RADIENT_STATUS MeshStatus = MeshLoader.GetStatus();
    if (RADIENT_FAILED(MeshStatus))
        return MeshStatus;

    return RadientGLTFConverter::ExtractSceneGraph(MetadataModel, Scene, pAssetManager);
}

} // namespace RadientGLTFLoader

} // namespace Diligent
