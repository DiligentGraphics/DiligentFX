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

#include <utility>

namespace Diligent
{

RadientSceneImporterImpl::RadientSceneImporterImpl(IReferenceCounters*   pRefCounters,
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

RADIENT_STATUS RadientSceneImporterImpl::InstantiateGLTF(const RadientAssetReference&      Model,
                                                         const RadientGLTFInstantiateInfo& InstantiateInfo,
                                                         RadientEntityID&                  RootEntity)
{
    RootEntity = InvalidRadientEntityID;

    if (m_pWriter == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (Model.URI == nullptr || *Model.URI == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientAssetManagerImpl* pAssetManagerImpl = ClassPtrCast<RadientAssetManagerImpl>(m_pAssetManager.RawPtr());
    if (pAssetManagerImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS LoadStatus = pAssetManagerImpl->GetGLTFLoadStatus(Model);
    if (RADIENT_FAILED(LoadStatus))
        return LoadStatus;

    RADIENT_STATUS Status = CreateGLTFRoot(Model, InstantiateInfo, RootEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (LoadStatus == RADIENT_STATUS_PENDING)
    {
        AddPendingGLTFInstantiation(Model, InstantiateInfo, RootEntity);
        return RADIENT_STATUS_PENDING;
    }

    Status = PopulateGLTFRoot(Model, InstantiateInfo.SceneIndex, RootEntity);
    if (RADIENT_FAILED(Status))
    {
        m_pWriter->DestroyEntity(RootEntity);
        RootEntity = InvalidRadientEntityID;
    }

    return Status;
}

RADIENT_STATUS RadientSceneImporterImpl::ProcessPendingImports()
{
    if (m_PendingGLTFInstantiations.empty())
        return RADIENT_STATUS_OK;

    if (m_pWriter == nullptr || m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientAssetManagerImpl* pAssetManagerImpl = m_pAssetManager.RawPtr<RadientAssetManagerImpl>();

    RADIENT_STATUS Result = RADIENT_STATUS_OK;

    for (size_t Index = 0; Index < m_PendingGLTFInstantiations.size();)
    {
        const PendingGLTFInstantiation& Pending = m_PendingGLTFInstantiations[Index];

        RadientAssetReference Model{};
        Model.URI     = Pending.ModelURI.c_str();
        Model.Version = Pending.ModelVersion;

        const RADIENT_STATUS LoadStatus = pAssetManagerImpl->GetGLTFLoadStatus(Model);
        if (LoadStatus == RADIENT_STATUS_PENDING)
        {
            Result = Result == RADIENT_STATUS_OK ? RADIENT_STATUS_PENDING : Result;
            ++Index;
            continue;
        }

        RADIENT_STATUS Status = LoadStatus;
        if (!RADIENT_FAILED(Status))
            Status = PopulateGLTFRoot(Model, Pending.SceneIndex, Pending.RootEntity);

        if (RADIENT_FAILED(Status))
            m_pWriter->DestroyEntity(Pending.RootEntity);

        if (RADIENT_FAILED(Status) && !RADIENT_FAILED(Result))
            Result = Status;

        if (Index + 1 < m_PendingGLTFInstantiations.size())
            m_PendingGLTFInstantiations[Index] = std::move(m_PendingGLTFInstantiations.back());
        m_PendingGLTFInstantiations.pop_back();
    }

    return Result;
}

RADIENT_STATUS RadientSceneImporterImpl::CreateGLTFRoot(const RadientAssetReference&      Model,
                                                        const RadientGLTFInstantiateInfo& InstantiateInfo,
                                                        RadientEntityID&                  RootEntity)
{
    RootEntity = InvalidRadientEntityID;

    if (m_pWriter == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientEntityDesc RootDesc{};
    RootDesc.Name      = InstantiateInfo.Name != nullptr ? InstantiateInfo.Name : Model.URI;
    RootDesc.Parent    = InstantiateInfo.Parent;
    RootDesc.Flags     = InstantiateInfo.RootFlags;
    RootDesc.Transform = InstantiateInfo.RootTransform;

    return m_pWriter->CreateEntity(RootDesc, RootEntity);
}

RADIENT_STATUS RadientSceneImporterImpl::PopulateGLTFRoot(const RadientAssetReference& Model,
                                                          Uint32                       SceneIndex,
                                                          RadientEntityID              RootEntity)
{
    if (m_pWriter == nullptr || m_pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientAssetManagerImpl* pAssetManagerImpl = m_pAssetManager.RawPtr<RadientAssetManagerImpl>();

    const GLTF::Model* pModel = pAssetManagerImpl->GetGLTFModel(Model);
    if (pModel == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    return RadientGLTFConverter::InstantiateSceneGraph(*pModel, Model, SceneIndex, *pAssetManagerImpl, *m_pWriter, RootEntity);
}

void RadientSceneImporterImpl::AddPendingGLTFInstantiation(const RadientAssetReference&      Model,
                                                           const RadientGLTFInstantiateInfo& InstantiateInfo,
                                                           RadientEntityID                   RootEntity)
{
    PendingGLTFInstantiation Pending;
    Pending.ModelURI     = Model.URI != nullptr ? Model.URI : "";
    Pending.ModelVersion = Model.Version;
    Pending.SceneIndex   = InstantiateInfo.SceneIndex;
    Pending.RootEntity   = RootEntity;

    m_PendingGLTFInstantiations.emplace_back(std::move(Pending));
}

} // namespace Diligent
