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

#pragma once

#include "RadientSceneImporter.h"
#include "RadientSceneWriter.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <string>
#include <vector>

namespace Diligent
{

class RadientSceneImporterImpl final : public ObjectBase<IRadientSceneImporter>
{
public:
    using TBase = ObjectBase<IRadientSceneImporter>;

    RadientSceneImporterImpl(IReferenceCounters*   pRefCounters,
                             IRadientAssetManager* pAssetManager,
                             IRadientSceneWriter*  pWriter);
    ~RadientSceneImporterImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSceneImporter, TBase)

    static RefCntAutoPtr<IRadientSceneImporter> Create(IRadientAssetManager* pAssetManager,
                                                       IRadientSceneWriter*  pWriter);

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ImportScene(const RadientSceneLoadInfo&        LoadInfo,
                                                          const RadientSceneInstantiateInfo& InstantiateInfo,
                                                          IRadientSceneAsset**               ppScene,
                                                          RadientEntityID&                   RootEntity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE InstantiateScene(IRadientSceneAsset*                pScene,
                                                               const RadientSceneInstantiateInfo& InstantiateInfo,
                                                               RadientEntityID&                   RootEntity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ProcessPendingImports() override final;

private:
    struct PendingSceneInstantiation
    {
        RefCntAutoPtr<IRadientSceneAsset> pModel;
        Uint32                            SceneIndex = InvalidRadientSceneIndex;
        RadientEntityID                   RootEntity = InvalidRadientEntityID;
    };

    RADIENT_STATUS CreateSceneRoot(IRadientSceneAsset*                pModel,
                                   const RadientSceneInstantiateInfo& InstantiateInfo,
                                   RadientEntityID&                   RootEntity);

    RADIENT_STATUS PopulateSceneRoot(IRadientSceneAsset* pModel,
                                     Uint32              SceneIndex,
                                     RadientEntityID     RootEntity);

    void AddPendingSceneInstantiation(IRadientSceneAsset*                pModel,
                                      const RadientSceneInstantiateInfo& InstantiateInfo,
                                      RadientEntityID                    RootEntity);

    RefCntAutoPtr<IRadientAssetManager> m_pAssetManager;
    RefCntAutoPtr<IRadientSceneWriter>  m_pWriter;

    std::vector<PendingSceneInstantiation> m_PendingSceneInstantiations;
};

} // namespace Diligent
