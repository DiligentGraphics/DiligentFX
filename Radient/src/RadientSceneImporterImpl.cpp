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

#include "RadientSceneImporterImpl.hpp"

#include "RadientAssetManagerImpl.hpp"
#include "RadientGLTFConverter.hpp"

#include "Cast.hpp"
#include "GLTFLoader.hpp"
#include "Errors.hpp"

#include <exception>
#include <memory>

namespace Diligent
{

namespace
{

std::unique_ptr<GLTF::Model> LoadGLTFModelMetadata(RadientAssetManagerImpl&     AssetManager,
                                                   const RadientAssetReference& Model)
{
    const Char* pSourceURI = nullptr;
    if (RADIENT_FAILED(AssetManager.GetGLTFSourceURI(Model, pSourceURI)) ||
        pSourceURI == nullptr || *pSourceURI == 0)
    {
        return {};
    }

    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName = pSourceURI;

    try
    {
        return std::make_unique<GLTF::Model>(nullptr, nullptr, ModelCI);
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF scene metadata '", pSourceURI, "': ", Error.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to load Radient GLTF scene metadata '", pSourceURI, "'");
    }

    return {};
}

} // namespace

RadientSceneImporterImpl::RadientSceneImporterImpl(IReferenceCounters* pRefCounters,
                                                   IRadientAssetManager* pAssetManager,
                                                   IRadientSceneWriter*  pWriter) :
    TBase{pRefCounters},
    m_pAssetManager{pAssetManager},
    m_pWriter{pWriter}
{
}

RadientSceneImporterImpl::~RadientSceneImporterImpl()
{
}

RefCntAutoPtr<IRadientSceneImporter> RadientSceneImporterImpl::Create(IRadientAssetManager* pAssetManager,
                                                                      IRadientSceneWriter*  pWriter)
{
    return RefCntAutoPtr<RadientSceneImporterImpl>{MakeNewRCObj<RadientSceneImporterImpl>()(pAssetManager, pWriter)};
}

RADIENT_STATUS RadientSceneImporterImpl::ImportGLTF(const RadientGLTFLoadInfo&        LoadInfo,
                                                    const RadientGLTFInstantiateInfo& InstantiateInfo,
                                                    RadientAssetReference&            Model,
                                                    RadientEntityID&                  RootEntity)
{
    Model      = {};
    RootEntity = InvalidRadientEntityID;

    if (m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const RADIENT_STATUS LoadStatus = m_pAssetManager->LoadGLTF(LoadInfo, Model);
    if (RADIENT_FAILED(LoadStatus))
        return LoadStatus;

    return InstantiateGLTF(Model, InstantiateInfo, RootEntity);
}

RADIENT_STATUS RadientSceneImporterImpl::InstantiateGLTF(const RadientAssetReference&     Model,
                                                         const RadientGLTFInstantiateInfo& InstantiateInfo,
                                                         RadientEntityID&                 RootEntity)
{
    RootEntity = InvalidRadientEntityID;

    if (m_pWriter == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (Model.URI == nullptr || *Model.URI == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientAssetManagerImpl* pAssetManagerImpl = ClassPtrCast<RadientAssetManagerImpl>(m_pAssetManager.RawPtr());
    if (pAssetManagerImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::unique_ptr<GLTF::Model> pModel = LoadGLTFModelMetadata(*pAssetManagerImpl, Model);
    if (pModel == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    return RadientGLTFConverter::InstantiateSceneGraph(*pModel, Model, InstantiateInfo, *m_pWriter, RootEntity);
}

} // namespace Diligent
