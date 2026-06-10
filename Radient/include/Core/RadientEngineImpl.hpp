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

#include "RadientEngine.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "ThreadPool.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

namespace Diligent
{

class RadientEngineImpl final : public ObjectBase<IRadientEngine>
{
public:
    using TBase = ObjectBase<IRadientEngine>;

    RadientEngineImpl(IReferenceCounters* pRefCounters, const RadientEngineCreateInfo& CreateInfo);
    ~RadientEngineImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientEngine, TBase)

    static RefCntAutoPtr<IRadientEngine> Create(const RadientEngineCreateInfo& CreateInfo);

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetBackend(IRadientBackend** ppBackend) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetAssetManager(IRadientAssetManager** ppAssetManager) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateScene(const RadientSceneDesc& Desc,
                                                          IRadientScene**         ppScene) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateSceneWriter(IRadientScene*        pScene,
                                                                IRadientSceneWriter** ppWriter) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateSceneImporter(IRadientSceneWriter*    pWriter,
                                                                  IRadientSceneImporter** ppImporter) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateRenderer(const RadientRendererDesc& Desc,
                                                             IRadientRenderer**         ppRenderer) override final;

private:
    RefCntAutoPtr<IThreadPool>             m_pThreadPool;
    RefCntAutoPtr<IRadientBackend>         m_pBackend;
    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;
    bool                                   m_OwnsThreadPool = false;
};

} // namespace Diligent
