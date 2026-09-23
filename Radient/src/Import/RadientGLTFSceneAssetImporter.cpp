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

#include "Import/RadientGLTFSceneAssetImporter.hpp"

#include "Assets/RadientGLTFLoader.hpp"
#include "Errors.hpp"
#include "GLTFDocument.hpp"
#include "ObjectBase.hpp"

#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Diligent
{

namespace
{

bool EndsWithCaseInsensitive(const std::string& Text, const char* Suffix)
{
    const size_t SuffixLength = std::char_traits<char>::length(Suffix);
    if (Text.size() < SuffixLength)
        return false;

    const size_t Offset = Text.size() - SuffixLength;
    for (size_t Index = 0; Index < SuffixLength; ++Index)
    {
        const unsigned char Lhs = static_cast<unsigned char>(Text[Offset + Index]);
        const unsigned char Rhs = static_cast<unsigned char>(Suffix[Index]);
        if (std::tolower(Lhs) != std::tolower(Rhs))
            return false;
    }

    return true;
}

bool IsGLTFSource(const char* URI)
{
    if (URI == nullptr)
        return false;

    std::string  Path{URI};
    const size_t QueryPos = Path.find_first_of("?#");
    if (QueryPos != std::string::npos)
        Path.resize(QueryPos);

    if (EndsWithCaseInsensitive(Path, ".gltf") || EndsWithCaseInsensitive(Path, ".glb"))
        return true;

    return false;
}


class RadientGLTFSceneAssetImporter final : public ObjectBase<IRadientSceneAssetImporter>
{
public:
    using TBase = ObjectBase<IRadientSceneAssetImporter>;
    using TBase::TBase;

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSceneAssetImporter, TBase)

    const Char* DILIGENT_CALL_TYPE GetIdentifier() const override
    {
        return "gltf";
    }

    Bool DILIGENT_CALL_TYPE CanImport(const Char* URI) const override
    {
        return IsGLTFSource(URI) ? True : False;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE Import(const RadientSceneAssetImportContext& Context,
                                             RadientImport::ImportedDocument&      ImportedScene) override
    {
        IRadientAssetData* pSceneData        = Context.pSourceData;
        const char*        ResolvedSourceURI = pSceneData->GetResolvedURI();

        GLTF::DocumentLoadInfo DocLoadInfo;
        DocLoadInfo.FileName           = ResolvedSourceURI;
        DocLoadInfo.DecodeImages       = false;
        DocLoadInfo.FileExistsCallback = [pAssetResolver = RefCntAutoPtr<IRadientAssetResolver>{Context.pAssetResolver},
                                          pSceneData     = RefCntAutoPtr<IRadientAssetData>{pSceneData},
                                          ResolvedSourceURI](const char* FilePath) {
            if (FilePath != nullptr && std::strcmp(FilePath, pSceneData->GetResolvedURI()) == 0)
                return true;

            return CheckAsset(pAssetResolver, {FilePath, ResolvedSourceURI}) == RADIENT_STATUS_OK;
        };
        DocLoadInfo.ReadWholeFileCallback = [pAssetResolver = RefCntAutoPtr<IRadientAssetResolver>{Context.pAssetResolver},
                                             pSceneData     = RefCntAutoPtr<IRadientAssetData>{pSceneData},
                                             ResolvedSourceURI](const char* FilePath, std::vector<unsigned char>& Data, std::string& Error) {
            RefCntAutoPtr<IRadientAssetData> pData;
            if (FilePath != nullptr && std::strcmp(FilePath, pSceneData->GetResolvedURI()) == 0)
            {
                pData = pSceneData;
            }
            else
            {
                const RADIENT_STATUS Status =
                    OpenAsset(pAssetResolver,
                              {FilePath, ResolvedSourceURI},
                              pData.GetAddressOfEmpty());
                if (Status != RADIENT_STATUS_OK || pData == nullptr)
                {
                    Error += FormatString("Failed to open asset '", FilePath != nullptr ? FilePath : "", "'\n");
                    return false;
                }
            }

            const size_t Size = pData->GetSize();
            if (Size == 0)
            {
                Error += FormatString("Asset is empty: ", FilePath != nullptr ? FilePath : "", "\n");
                return false;
            }
            Data.resize(Size);
            std::memcpy(Data.data(), pData->GetData(), Size);
            return true;
        };

        std::shared_ptr<GLTF::Document> pDocument = std::make_shared<GLTF::Document>(DocLoadInfo);

        ImportedScene.Textures =
            RadientGLTFLoader::LoadTextures(*Context.pAssetManager,
                                            ResolvedSourceURI,
                                            pDocument);

        ImportedScene.Materials =
            RadientGLTFLoader::LoadMaterials(*Context.pAssetManager,
                                             pDocument,
                                             ImportedScene.Textures);

        return RadientGLTFLoader::LoadScene(*Context.pMeshImportServices,
                                            ResolvedSourceURI,
                                            pDocument,
                                            ImportedScene.Materials,
                                            Context.pDefaultMaterial,
                                            Context.pAssetManager,
                                            ImportedScene);
    }
};

} // namespace

RefCntAutoPtr<IRadientSceneAssetImporter> CreateRadientGLTFSceneAssetImporter()
{
    return RefCntAutoPtr<IRadientSceneAssetImporter>{MakeNewRCObj<RadientGLTFSceneAssetImporter>()()};
}

} // namespace Diligent
