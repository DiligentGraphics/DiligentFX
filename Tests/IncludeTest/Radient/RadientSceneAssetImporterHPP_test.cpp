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

#include "Radient/interface/RadientSceneAssetImporter.hpp"

#include <type_traits>

using namespace Diligent;

static_assert(std::is_base_of<IObject, IRadientSceneAssetImporter>::value, "Scene asset importers must implement IObject");
static_assert(std::is_standard_layout<RadientSceneAssetImportContext>::value, "The import context must be a standard-layout type");

class SceneAssetImporterHeaderCheck : public IRadientSceneAssetImporter
{
public:
    const Char* DILIGENT_CALL_TYPE    GetIdentifier() const override;
    Bool DILIGENT_CALL_TYPE           CanImport(const Char* URI) const override;
    RADIENT_STATUS DILIGENT_CALL_TYPE Import(const RadientSceneAssetImportContext& Context,
                                             RadientImport::ImportedDocument&      Document) override;
};

void RadientSceneAssetImporter_CPP_UseInterfaces(IRadientAssetManager*       pManager,
                                                 IRadientSceneAssetImporter* pImporter,
                                                 IRadientAssetData*          pSourceData,
                                                 IRadientAssetResolver*      pResolver,
                                                 IRadientMeshImportServices* pMeshImportServices,
                                                 IRadientMaterialAsset*      pDefaultMaterial)
{
    (void)pManager->RegisterSceneAssetImporter(pImporter);

    const Char* Identifier = pImporter->GetIdentifier();
    const Bool  CanImport  = pImporter->CanImport(pSourceData->GetResolvedURI());
    (void)CanImport;

    RadientSceneAssetImportContext Context;
    Context.pSourceData         = pSourceData;
    Context.pAssetResolver      = pResolver;
    Context.pAssetManager       = pManager;
    Context.pMeshImportServices = pMeshImportServices;
    Context.pDefaultMaterial    = pDefaultMaterial;

    RadientImport::ImportedDocument Document;
    (void)pImporter->Import(Context, Document);

    RadientSceneLoadInfo LoadInfo;
    LoadInfo.URI        = pSourceData->GetResolvedURI();
    LoadInfo.ImporterId = Identifier;
    RefCntAutoPtr<IRadientSceneAsset> pScene;
    (void)pManager->LoadScene(LoadInfo, &pScene);
}
