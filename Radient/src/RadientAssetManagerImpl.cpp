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

#include "RadientAssetManagerImpl.hpp"

#include <utility>

namespace Diligent
{

namespace
{

template <typename ValueType>
std::vector<ValueType> CopyArray(const ValueType* pData, Uint32 Count)
{
    return pData != nullptr && Count != 0 ?
        std::vector<ValueType>{pData, pData + Count} :
        std::vector<ValueType>{};
}

} // namespace

RadientAssetManagerImpl::RadientAssetManagerImpl(IReferenceCounters* pRefCounters, const RadientAssetManagerCreateInfo& CreateInfo) :
    TBase{pRefCounters},
    m_Name{CreateInfo.Desc.Name != nullptr ? CreateInfo.Desc.Name : ""},
    m_Desc{CreateInfo.Desc}
{
    m_Desc.Name = m_Name.c_str();
}

RadientAssetManagerImpl::~RadientAssetManagerImpl()
{
}

RefCntAutoPtr<IRadientAssetManager> RadientAssetManagerImpl::Create(const RadientAssetManagerCreateInfo& CreateInfo)
{
    return RefCntAutoPtr<RadientAssetManagerImpl>{MakeNewRCObj<RadientAssetManagerImpl>()(CreateInfo)};
}

const RadientAssetManagerDesc& RadientAssetManagerImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                   RadientAssetReference&       Mesh)
{
    Mesh = {};

    if (!ValidateMesh(MeshCI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_MESH;
    Record.Name = MeshCI.Name != nullptr ? MeshCI.Name : "";
    Record.URI  = MakeURI("mesh");

    Record.MeshPrimitives.reserve(MeshCI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];

        MeshPrimitiveStorage Primitive;
        Primitive.Name            = PrimitiveCI.Name != nullptr ? PrimitiveCI.Name : "";
        Primitive.Positions       = CopyArray(PrimitiveCI.pPositions, PrimitiveCI.VertexCount);
        Primitive.Normals         = CopyArray(PrimitiveCI.pNormals, PrimitiveCI.VertexCount);
        Primitive.Tangents        = CopyArray(PrimitiveCI.pTangents, PrimitiveCI.VertexCount);
        Primitive.TexCoords0      = CopyArray(PrimitiveCI.pTexCoords0, PrimitiveCI.VertexCount);
        Primitive.Colors0         = CopyArray(PrimitiveCI.pColors0, PrimitiveCI.VertexCount);
        Primitive.IndexType       = PrimitiveCI.IndexType;
        Primitive.MaterialVersion = PrimitiveCI.Material.Version;
        Primitive.MaterialURI     = PrimitiveCI.Material.URI != nullptr ? PrimitiveCI.Material.URI : "";

        if (PrimitiveCI.IndexCount != 0)
        {
            const size_t IndexSize =
                PrimitiveCI.IndexType == RADIENT_INDEX_TYPE_UINT16 ? sizeof(Uint16) : sizeof(Uint32);
            const Uint8* pIndexData = static_cast<const Uint8*>(PrimitiveCI.pIndices);
            Primitive.Indices.assign(pIndexData, pIndexData + PrimitiveCI.IndexCount * IndexSize);
        }

        Record.MeshPrimitives.emplace_back(std::move(Primitive));
    }

    Mesh = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientAssetManagerImpl::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                       RadientAssetReference&           Material)
{
    Material = {};

    AssetRecord Record;
    Record.Type = RADIENT_ASSET_TYPE_MATERIAL;
    Record.Name = MaterialCI.Name != nullptr ? MaterialCI.Name : "";
    Record.URI  = MakeURI("material");

    Record.Material.BaseColorFactor          = MaterialCI.BaseColorFactor;
    Record.Material.MetallicFactor           = MaterialCI.MetallicFactor;
    Record.Material.RoughnessFactor          = MaterialCI.RoughnessFactor;
    Record.Material.EmissiveFactor           = MaterialCI.EmissiveFactor;
    Record.Material.AlphaCutoff              = MaterialCI.AlphaCutoff;
    Record.Material.DoubleSided              = MaterialCI.DoubleSided;
    Record.Material.BaseColorTexture         = CopyAssetReference(MaterialCI.BaseColorTexture, Record.Material.BaseColorTextureURI);
    Record.Material.MetallicRoughnessTexture = CopyAssetReference(MaterialCI.MetallicRoughnessTexture, Record.Material.MetallicRoughnessTextureURI);
    Record.Material.NormalTexture            = CopyAssetReference(MaterialCI.NormalTexture, Record.Material.NormalTextureURI);
    Record.Material.OcclusionTexture         = CopyAssetReference(MaterialCI.OcclusionTexture, Record.Material.OcclusionTextureURI);
    Record.Material.EmissiveTexture          = CopyAssetReference(MaterialCI.EmissiveTexture, Record.Material.EmissiveTextureURI);

    Material = StoreAsset(std::move(Record));
    return RADIENT_STATUS_OK;
}

bool RadientAssetManagerImpl::ValidateMesh(const RadientMeshCreateInfo& MeshCI) const
{
    if (MeshCI.PrimitiveCount == 0 || MeshCI.pPrimitives == nullptr)
        return false;

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.VertexCount == 0 || PrimitiveCI.pPositions == nullptr)
            return false;

        if (PrimitiveCI.IndexCount == 0)
        {
            if (PrimitiveCI.IndexType != RADIENT_INDEX_TYPE_NONE || PrimitiveCI.pIndices != nullptr)
                return false;
        }
        else
        {
            if (PrimitiveCI.pIndices == nullptr ||
                (PrimitiveCI.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
                 PrimitiveCI.IndexType != RADIENT_INDEX_TYPE_UINT32))
            {
                return false;
            }
        }
    }

    return true;
}

std::string RadientAssetManagerImpl::MakeURI(const char* Type)
{
    const RadientHandle AssetID = m_NextAssetID++;
    return std::string{"radient://session/"} + Type + "/" + std::to_string(AssetID);
}

RadientAssetReference RadientAssetManagerImpl::StoreAsset(AssetRecord&& Record)
{
    m_Assets.emplace_back(std::move(Record));
    FixupAssetRecord(m_Assets.back());

    RadientAssetReference Ref;
    Ref.URI     = m_Assets.back().URI.c_str();
    Ref.Version = 1;
    return Ref;
}

void RadientAssetManagerImpl::FixupAssetRecord(AssetRecord& Record)
{
    FixupAssetReference(Record.Material.BaseColorTexture, Record.Material.BaseColorTextureURI);
    FixupAssetReference(Record.Material.MetallicRoughnessTexture, Record.Material.MetallicRoughnessTextureURI);
    FixupAssetReference(Record.Material.NormalTexture, Record.Material.NormalTextureURI);
    FixupAssetReference(Record.Material.OcclusionTexture, Record.Material.OcclusionTextureURI);
    FixupAssetReference(Record.Material.EmissiveTexture, Record.Material.EmissiveTextureURI);
}

void RadientAssetManagerImpl::FixupAssetReference(RadientAssetReference& Ref, const std::string& URIStorage)
{
    Ref.URI = Ref.URI != nullptr ? URIStorage.c_str() : nullptr;
}

RadientAssetReference RadientAssetManagerImpl::CopyAssetReference(const RadientAssetReference& Ref, std::string& URIStorage)
{
    URIStorage = Ref.URI != nullptr ? Ref.URI : "";

    RadientAssetReference Result = Ref;
    Result.URI                   = Ref.URI != nullptr ? URIStorage.c_str() : nullptr;
    return Result;
}

} // namespace Diligent
