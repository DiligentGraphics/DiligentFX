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

#include "Import/RadientOBJSceneAssetImporter.hpp"

#include "Errors.hpp"
#include "ObjectBase.hpp"
#include "OBJLoader.hpp"
#include "RadientDataBlob.h"
#include "RadientStandardMaterialParameters.h"
#include "RadientTypesX.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

bool IsOBJSource(const char* URI)
{
    if (URI == nullptr)
        return false;

    const std::string Path{URI};
    const size_t      End    = Path.find_first_of("?#");
    const size_t      Length = End != std::string::npos ? End : Path.size();
    if (Length < 4)
        return false;

    const char Suffix[] = ".obj";
    for (size_t Index = 0; Index < 4; ++Index)
    {
        if (std::tolower(static_cast<unsigned char>(Path[Length - 4 + Index])) != Suffix[Index])
            return false;
    }
    return true;
}

template <typename ValueType>
RADIENT_STATUS CreateDocumentBlob(const std::shared_ptr<const OBJ::Document>& pDocument,
                                  const std::vector<ValueType>&               Values,
                                  IRadientDataBlob**                          ppBlob)
{
    // Each reference blob independently retains the immutable parser output.
    // No source bytes or numeric buffers are copied by the importer.
    std::unique_ptr<std::shared_ptr<const OBJ::Document>> pOwner =
        std::make_unique<std::shared_ptr<const OBJ::Document>>(pDocument);
    RadientDataBlobCreateInfo BlobCI;
    BlobCI.pData     = Values.data();
    BlobCI.Size      = static_cast<Uint64>(Values.size()) * sizeof(ValueType);
    BlobCI.pUserData = pOwner.get();
    BlobCI.OnDestroy = [](void* pUserData) {
        delete static_cast<std::shared_ptr<const OBJ::Document>*>(pUserData);
    };
    const RADIENT_STATUS Status =
        CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, ppBlob);
    if (Status == RADIENT_STATUS_OK)
        pOwner.release();
    return Status;
}

template <typename ValueType>
RADIENT_STATUS AddVertexBuffer(const std::shared_ptr<const OBJ::Document>&   pDocument,
                               const std::vector<ValueType>&                 Values,
                               const char*                                   Semantic,
                               Uint32                                        ComponentCount,
                               RadientVertexLayoutDescX&                     Layout,
                               std::vector<RefCntAutoPtr<IRadientDataBlob>>& Blobs)
{
    if (Values.empty())
        return RADIENT_STATUS_OK;

    RefCntAutoPtr<IRadientDataBlob> pBlob;
    const RADIENT_STATUS            Status = CreateDocumentBlob(pDocument, Values, pBlob.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status))
        return Status;

    const Uint32 BufferIndex = Layout.GetBufferCount();
    Layout.AddBuffer(static_cast<Uint32>(sizeof(ValueType)));
    Layout.AddAttribute(Semantic, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, ComponentCount, BufferIndex, 0);
    Blobs.push_back(std::move(pBlob));
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS CreateGeometry(IRadientMeshImportServices&                 Services,
                              const std::shared_ptr<const OBJ::Document>& pDocument,
                              RefCntAutoPtr<IRadientMeshVertexData>&      pVertexData,
                              RefCntAutoPtr<IRadientMeshIndexData>&       pIndexData)
{
    RadientVertexLayoutDescX                     Layout;
    std::vector<RefCntAutoPtr<IRadientDataBlob>> Blobs;

    RADIENT_STATUS Status = AddVertexBuffer(pDocument, pDocument->Positions, "POSITION", 3, Layout, Blobs);
    if (RADIENT_SUCCEEDED(Status))
        Status = AddVertexBuffer(pDocument, pDocument->Normals, "NORMAL", 3, Layout, Blobs);
    if (RADIENT_SUCCEEDED(Status))
        Status = AddVertexBuffer(pDocument, pDocument->TexCoords, "TEXCOORD_0", 2, Layout, Blobs);
    if (RADIENT_SUCCEEDED(Status))
        Status = AddVertexBuffer(pDocument, pDocument->Colors, "COLOR_0", 4, Layout, Blobs);
    if (RADIENT_FAILED(Status))
        return Status;

    std::vector<IRadientDataBlob*> RawBlobs;
    RawBlobs.reserve(Blobs.size());
    for (const RefCntAutoPtr<IRadientDataBlob>& pBlob : Blobs)
        RawBlobs.push_back(pBlob);

    RadientMeshVertexData VertexData;
    VertexData.VertexLayout    = Layout;
    VertexData.ppVertexBuffers = RawBlobs.data();
    VertexData.VertexCount     = static_cast<Uint32>(pDocument->Positions.size());

    Status = Services.CreateMeshVertexData(VertexData, pVertexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pVertexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
    Status = CreateDocumentBlob(pDocument, pDocument->Indices, pIndexBlob.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status))
        return Status;

    RadientMeshIndexData IndexData;
    IndexData.pIndexBuffer = pIndexBlob;
    IndexData.IndexCount   = static_cast<Uint32>(pDocument->Indices.size());
    IndexData.IndexType    = RADIENT_INDEX_TYPE_UINT32;

    Status = Services.CreateMeshIndexData(IndexData, pIndexData.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pIndexData == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    return RADIENT_STATUS_OK;
}

struct MaterialSource
{
    OBJ::Material Material;
    std::string   URI;
};

RADIENT_STATUS ReadMaterialLibraries(const RadientSceneAssetImportContext& Context,
                                     const OBJ::Document&                  Document,
                                     std::vector<MaterialSource>&          Materials)
{
    const char* SourceURI = Context.pSourceData->GetResolvedURI();

    std::unordered_map<std::string, size_t> MaterialIndices;
    for (const std::string& Library : Document.MaterialLibraries)
    {
        RefCntAutoPtr<IRadientAssetData> pData;

        const RADIENT_STATUS Status = OpenAsset(Context.pAssetResolver, {Library.c_str(), SourceURI}, pData.GetAddressOfEmpty());
        if (Status == RADIENT_STATUS_CANCELLED || Status == RADIENT_STATUS_INVALID_OPERATION)
            return Status;
        if (Status != RADIENT_STATUS_OK || pData == nullptr)
        {
            LOG_WARNING_MESSAGE("Failed to open OBJ material library '", Library, "' referenced by '",
                                SourceURI, "' (status ", Status, "); unresolved materials use the default material");
            continue;
        }

        std::vector<OBJ::Material> ParsedMaterials;
        std::string                Error;
        std::string                Warnings;
        const bool                 Parsed = OBJ::ParseMaterials(static_cast<const char*>(pData->GetData()), pData->GetSize(),
                                                                ParsedMaterials, Error, Warnings);
        if (!Warnings.empty())
            LOG_WARNING_MESSAGE("OBJ material library '", pData->GetResolvedURI(), "': ", Warnings);
        if (!Parsed)
        {
            LOG_WARNING_MESSAGE("Failed to parse OBJ material library '", pData->GetResolvedURI(),
                                "' referenced by '", SourceURI, "': ", Error,
                                "; unresolved materials use the default material");
            continue;
        }

        for (OBJ::Material& Material : ParsedMaterials)
        {
            const std::unordered_map<std::string, size_t>::const_iterator Existing = MaterialIndices.find(Material.Name);
            if (Existing == MaterialIndices.end())
            {
                MaterialIndices.emplace(Material.Name, Materials.size());
                Materials.push_back(MaterialSource{std::move(Material), pData->GetResolvedURI()});
            }
            else
            {
                LOG_WARNING_MESSAGE("Duplicate OBJ material '", Material.Name, "' in '",
                                    pData->GetResolvedURI(), "'; using the last definition");
                Materials[Existing->second] = MaterialSource{std::move(Material), pData->GetResolvedURI()};
            }
        }
    }
    return RADIENT_STATUS_OK;
}

template <typename ValueType>
RADIENT_STATUS SetMaterialParameter(const IRadientMaterialDefinitionAsset& Definition,
                                    IRadientMaterialWriter&                Writer,
                                    const char*                            Name,
                                    const ValueType&                       Value)
{
    RadientMaterialParameterHandle Handle;
    const RADIENT_STATUS           Status = Definition.FindParameter(Name, &Handle);
    return Status == RADIENT_STATUS_OK ? Writer.SetParameter(Handle, Value) : Status;
}

class MaterialLoader
{
public:
    MaterialLoader(IRadientAssetManager& AssetManager, RadientImport::ImportedDocument& Document) :
        m_AssetManager{AssetManager},
        m_Document{Document}
    {}

    RADIENT_STATUS CreateMaterial(const MaterialSource& Source, RefCntAutoPtr<IRadientMaterialAsset>& pMaterial)
    {
        const OBJ::Material& Material = Source.Material;
        const bool           Unlit    = Material.IlluminationModel == 0;
        if (Material.IlluminationModel > 2)
        {
            LOG_WARNING_MESSAGE("OBJ material '", Material.Name, "' in '", Source.URI,
                                "' uses illum ", Material.IlluminationModel,
                                "; reflection/refraction effects are approximated with specular-glossiness shading");
        }

        RadientStandardMaterialDefinitionCreateInfo DefinitionCI;
        DefinitionCI.ShadingModel = Unlit ?
            RADIENT_SURFACE_SHADING_MODEL_UNLIT :
            RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS;
        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;

        RADIENT_STATUS Status = m_AssetManager.CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || pDefinition == nullptr)
            return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
        Status = m_AssetManager.CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || pMaterial == nullptr)
            return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

        RefCntAutoPtr<IRadientMaterialWriter> pWriter;
        Status = pMaterial->CreateWriter(pWriter.GetAddressOfEmpty());
        if (RADIENT_FAILED(Status) || pWriter == nullptr)
            return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
        RefCntAutoPtr<IRadientSurfaceMaterialWriter> pSurfaceWriter{pWriter.RawPtr(), IID_RadientSurfaceMaterialWriter};
        if (pSurfaceWriter == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const float Opacity = std::clamp(Material.Opacity, 0.f, 1.f);
        Status              = pSurfaceWriter->SetSurfaceMode(Opacity < 1.f ? RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT :
                                                                             RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
        const RadientFloat4 Diffuse{std::clamp(Material.Diffuse.x, 0.f, 1.f),
                                    std::clamp(Material.Diffuse.y, 0.f, 1.f),
                                    std::clamp(Material.Diffuse.z, 0.f, 1.f), Opacity};
        if (RADIENT_SUCCEEDED(Status))
            Status = SetMaterialParameter(*pDefinition, *pWriter,
                                          Unlit ? RadientStandardMaterialBaseColorFactorName : RadientStandardMaterialDiffuseFactorName,
                                          Diffuse);
        if (RADIENT_SUCCEEDED(Status))
            Status = SetTexture(Source, Material.DiffuseMap, True, *pDefinition, *pWriter,
                                Unlit ? RadientStandardMaterialBaseColorTextureParameterNames : RadientStandardMaterialDiffuseTextureParameterNames);

        if (!Unlit)
        {
            const RadientFloat3 Specular{std::clamp(Material.Specular.x, 0.f, 1.f),
                                         std::clamp(Material.Specular.y, 0.f, 1.f),
                                         std::clamp(Material.Specular.z, 0.f, 1.f)};
            const RadientFloat3 Emissive{std::max(Material.Emissive.x, 0.f),
                                         std::max(Material.Emissive.y, 0.f),
                                         std::max(Material.Emissive.z, 0.f)};
            const float         Glossiness = 1.f - std::sqrt(2.f / (std::max(Material.Shininess, 0.f) + 2.f));
            if (RADIENT_SUCCEEDED(Status))
                Status = SetMaterialParameter(*pDefinition, *pWriter, RadientStandardMaterialSpecularFactorName, Specular);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetMaterialParameter(*pDefinition, *pWriter, RadientStandardMaterialGlossinessFactorName, Glossiness);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetMaterialParameter(*pDefinition, *pWriter, RadientStandardMaterialEmissiveFactorName, Emissive);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetMaterialParameter(*pDefinition, *pWriter, RadientStandardMaterialNormalScaleName, Material.NormalMap.BumpMultiplier);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetTexture(Source, Material.SpecularMap, True, *pDefinition, *pWriter, RadientStandardMaterialSpecularGlossinessTextureParameterNames);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetTexture(Source, Material.EmissiveMap, True, *pDefinition, *pWriter, RadientStandardMaterialEmissiveTextureParameterNames);
            if (RADIENT_SUCCEEDED(Status))
                Status = SetTexture(Source, Material.NormalMap, False, *pDefinition, *pWriter, RadientStandardMaterialNormalTextureParameterNames);
        }
        if (RADIENT_FAILED(Status))
            return Status;
        Status = pWriter->Commit();
        return RADIENT_SUCCEEDED(Status) ? RADIENT_STATUS_OK : Status;
    }

private:
    RADIENT_STATUS SetTexture(const MaterialSource&                               Source,
                              const OBJ::TextureMap&                              Map,
                              Bool                                                IsSRGB,
                              IRadientMaterialDefinitionAsset&                    Definition,
                              IRadientMaterialWriter&                             Writer,
                              const RadientStandardMaterialTextureParameterNames& Names)
    {
        if (Map.Name.empty())
            return RADIENT_STATUS_OK;

        const TextureKey     Key{Source.URI, Map.Name, IsSRGB};
        TextureMap::iterator Texture = m_Textures.find(Key);
        if (Texture == m_Textures.end())
        {
            RadientTextureLoadInfoX LoadInfo;
            LoadInfo.SetURI(Map.Name).SetBaseURI(Source.URI).SetSRGB(IsSRGB);
            RefCntAutoPtr<IRadientTextureAsset> pTexture;

            const RADIENT_STATUS Status = m_AssetManager.LoadTexture(LoadInfo, pTexture.GetAddressOfEmpty());
            if (pTexture == nullptr)
            {
                LOG_WARNING_MESSAGE("Failed to load OBJ texture '", Map.Name, "' referenced by '", Source.URI,
                                    "' (status ", Status, "); using the standard material texture fallback");
            }
            // A worker may fail the texture before LoadTexture returns. Retain
            // the handle regardless of its current status so material dependencies
            // select the fallback consistently for early and late failures.
            m_Document.Textures.push_back(pTexture);
            Texture = m_Textures.emplace(Key, std::move(pTexture)).first;
        }

        RadientStandardMaterialTextureParameters Parameters{Texture->second};
        Parameters.UVScaleAndRotation = {{Map.Scale.x, 0.f, 0.f, Map.Scale.y}};
        Parameters.UVBias             = {Map.Offset.x, Map.Offset.y};
        Parameters.WrapU              = Map.Clamp ? RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP : RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP;
        Parameters.WrapV              = Parameters.WrapU;
        return SetStandardMaterialTextureParameters(Definition, Writer, Names, Parameters);
    }

    using TextureKey = std::tuple<std::string, std::string, Bool>;
    using TextureMap = std::map<TextureKey, RefCntAutoPtr<IRadientTextureAsset>>;

    IRadientAssetManager&            m_AssetManager;
    RadientImport::ImportedDocument& m_Document;
    TextureMap                       m_Textures;
};

class RadientOBJSceneAssetImporter final : public ObjectBase<IRadientSceneAssetImporter>
{
public:
    using TBase = ObjectBase<IRadientSceneAssetImporter>;
    using TBase::TBase;

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSceneAssetImporter, TBase)

    const Char* DILIGENT_CALL_TYPE GetIdentifier() const override
    {
        return "obj";
    }

    Bool DILIGENT_CALL_TYPE CanImport(const Char* URI) const override
    {
        return IsOBJSource(URI) ? True : False;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE Import(const RadientSceneAssetImportContext& Context,
                                             RadientImport::ImportedDocument&      ImportedScene) override
    {
        const char*                    SourceURI = Context.pSourceData->GetResolvedURI();
        std::shared_ptr<OBJ::Document> pDocument = std::make_shared<OBJ::Document>();
        std::string                    Error;
        std::string                    Warnings;
        const bool                     Parsed = OBJ::Parse(static_cast<const char*>(Context.pSourceData->GetData()),
                                                           Context.pSourceData->GetSize(), *pDocument, Error, Warnings);
        if (!Warnings.empty())
            LOG_WARNING_MESSAGE("OBJ source '", SourceURI, "': ", Warnings);
        if (!Parsed)
        {
            LOG_ERROR_MESSAGE("Failed to parse OBJ source '", SourceURI, "': ", Error);
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (pDocument->Positions.empty() || pDocument->Indices.empty() || pDocument->Meshes.empty())
        {
            LOG_ERROR_MESSAGE("OBJ source '", SourceURI, "' contains no triangle geometry");
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (pDocument->Positions.size() > std::numeric_limits<Uint32>::max() ||
            pDocument->Indices.size() > std::numeric_limits<Uint32>::max() ||
            pDocument->Meshes.size() > std::numeric_limits<Uint32>::max())
        {
            LOG_ERROR_MESSAGE("OBJ source '", SourceURI, "' exceeds Radient geometry limits");
            return RADIENT_STATUS_INVALID_DATA;
        }

        std::vector<MaterialSource> MaterialSources;

        RADIENT_STATUS Status = ReadMaterialLibraries(Context, *pDocument, MaterialSources);
        if (RADIENT_FAILED(Status))
            return Status;

        MaterialLoader Loader{*Context.pAssetManager, ImportedScene};

        std::unordered_map<std::string, RefCntAutoPtr<IRadientMaterialAsset>> Materials;
        for (const MaterialSource& Source : MaterialSources)
        {
            RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
            Status = Loader.CreateMaterial(Source, pMaterial);
            if (RADIENT_FAILED(Status))
            {
                LOG_ERROR_MESSAGE("Failed to create OBJ material '", Source.Material.Name, "' in '",
                                  Source.URI, "' (status ", Status, ")");
                return Status;
            }
            ImportedScene.Materials.push_back(pMaterial);
            Materials.emplace(Source.Material.Name, std::move(pMaterial));
        }

        RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
        RefCntAutoPtr<IRadientMeshIndexData>  pIndexData;
        Status = CreateGeometry(*Context.pMeshImportServices, pDocument, pVertexData, pIndexData);
        if (RADIENT_FAILED(Status))
            return Status;

        const RadientMeshGeometryData Geometry{pVertexData, pIndexData};
        RadientImport::ImportedScene  Scene;
        Scene.Name = SourceURI;
        std::unordered_set<std::string> MissingMaterials;
        bool                            DefaultMaterialRecorded = false;
        for (const OBJ::Mesh& Mesh : pDocument->Meshes)
        {
            std::vector<RadientMeshPrimitiveCreateInfo> Primitives;
            Primitives.reserve(Mesh.Primitives.size());
            for (const OBJ::Primitive& SourcePrimitive : Mesh.Primitives)
            {
                RadientMeshPrimitiveCreateInfo Primitive;
                Primitive.Name       = Mesh.Name.c_str();
                Primitive.FirstIndex = SourcePrimitive.FirstIndex;
                Primitive.IndexCount = SourcePrimitive.IndexCount;
                const std::unordered_map<std::string, RefCntAutoPtr<IRadientMaterialAsset>>::const_iterator Material =
                    Materials.find(SourcePrimitive.MaterialName);
                if (Material != Materials.end())
                {
                    Primitive.pMaterial = Material->second;
                }
                else
                {
                    Primitive.pMaterial = Context.pDefaultMaterial;
                    if (!DefaultMaterialRecorded)
                    {
                        ImportedScene.Materials.emplace_back(Context.pDefaultMaterial);
                        DefaultMaterialRecorded = true;
                    }
                    if (!SourcePrimitive.MaterialName.empty() && MissingMaterials.emplace(SourcePrimitive.MaterialName).second)
                    {
                        LOG_WARNING_MESSAGE("OBJ source '", SourceURI, "' references unknown material '",
                                            SourcePrimitive.MaterialName, "'; using the default material");
                    }
                }
                Primitives.push_back(Primitive);
            }

            RadientMeshViewCreateInfo ViewCI;
            ViewCI.pPrimitives    = Primitives.data();
            ViewCI.PrimitiveCount = static_cast<Uint32>(Primitives.size());
            ViewCI.pGeometryData  = &Geometry;
            ViewCI.GeometryCount  = 1;
            RefCntAutoPtr<IRadientMeshAsset> pMesh;
            Status = Context.pMeshImportServices->CreateMeshView(ViewCI, pMesh.GetAddressOfEmpty());
            if (RADIENT_FAILED(Status) || pMesh == nullptr)
                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

            Scene.RootNodes.push_back(static_cast<Uint32>(ImportedScene.Nodes.size()));
            RadientImport::ImportedNode Node;
            Node.Name  = Mesh.Name;
            Node.pMesh = pMesh;
            ImportedScene.Nodes.push_back(std::move(Node));
            ImportedScene.Meshes.push_back(std::move(pMesh));
        }
        ImportedScene.Scenes.push_back(std::move(Scene));
        ImportedScene.DefaultSceneId = 0;
        return RADIENT_STATUS_OK;
    }
};

} // namespace

RefCntAutoPtr<IRadientSceneAssetImporter> CreateRadientOBJSceneAssetImporter()
{
    return RefCntAutoPtr<IRadientSceneAssetImporter>{MakeNewRCObj<RadientOBJSceneAssetImporter>()()};
}

} // namespace Diligent
