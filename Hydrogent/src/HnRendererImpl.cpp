/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "HnRendererImpl.hpp"
#include "HnRenderDelegate.hpp"
#include "EngineMemory.h"

#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/unitTestNullRenderPass.h"

namespace Diligent
{

namespace USD
{

void CreateHnRenderer(IRenderDevice* pDevice, TEXTURE_FORMAT RTVFormat, TEXTURE_FORMAT DSVFormat, IHnRenderer** ppRenderer)
{
    auto* pRenderer = NEW_RC_OBJ(GetRawAllocator(), "HnRenderer instance", HnRendererImpl)(pDevice, RTVFormat, DSVFormat);
    pRenderer->QueryInterface(IID_HnRenderer, reinterpret_cast<IObject**>(ppRenderer));
}

HnRendererImpl::HnRendererImpl(IReferenceCounters* pRefCounters,
                               IRenderDevice*      pDevice,
                               TEXTURE_FORMAT      RTVFormat,
                               TEXTURE_FORMAT      DSVFormat) :
    TBase{pRefCounters},
    m_Device{pDevice},
    m_RenderDelegate{HnRenderDelegate::Create(pDevice)}
{
}

HnRendererImpl::~HnRendererImpl()
{
    delete m_ImagingDelegate;
    delete m_RenderIndex;
}

void HnRendererImpl::LoadUSDStage(const char* FileName)
{
    m_Stage = pxr::UsdStage::Open(FileName);
    if (!m_Stage)
    {
        LOG_ERROR_MESSAGE("Failed to open USD stage '", FileName, "'");
        return;
    }

    m_RenderIndex     = pxr::HdRenderIndex::New(m_RenderDelegate.get(), pxr::HdDriverVector{});
    m_ImagingDelegate = new pxr::UsdImagingDelegate(m_RenderIndex, pxr::SdfPath::AbsoluteRootPath());
    m_ImagingDelegate->Populate(m_Stage->GetPseudoRoot());

    m_RenderTags = {pxr::HdRenderTagTokens->geometry};

    auto Collection = pxr::HdRprimCollection{pxr::HdTokens->geometry, pxr::HdReprSelector(pxr::HdReprTokens->hull)};
    m_GeometryPass  = pxr::HdRenderPassSharedPtr{new pxr::Hd_UnitTestNullRenderPass{m_RenderIndex, Collection}};
}


namespace
{

class SyncTask final : public pxr::HdTask
{
public:
    SyncTask(pxr::HdRenderPassSharedPtr const& renderPass, pxr::TfTokenVector const& renderTags) :
        pxr::HdTask{pxr::SdfPath::EmptyPath()},
        m_RenderPass{renderPass},
        m_RenderTags{renderTags}
    {}

    void Sync(pxr::HdSceneDelegate* delegate, pxr::HdTaskContext* ctx, pxr::HdDirtyBits* dirtyBits) override final
    {
        m_RenderPass->Sync();

        *dirtyBits = pxr::HdChangeTracker::Clean;
    }

    void Prepare(pxr::HdTaskContext* ctx, pxr::HdRenderIndex* renderIndex) override final {}

    void Execute(pxr::HdTaskContext* ctx) override final {}

    const pxr::TfTokenVector& GetRenderTags() const override final
    {
        return m_RenderTags;
    }

private:
    pxr::HdRenderPassSharedPtr m_RenderPass;
    pxr::TfTokenVector         m_RenderTags;
};

} // namespace


void HnRendererImpl::Update()
{
    m_ImagingDelegate->ApplyPendingUpdates();
    pxr::HdTaskSharedPtrVector tasks = {
        std::make_shared<SyncTask>(m_GeometryPass, m_RenderTags)};
    m_Engine.Execute(&m_ImagingDelegate->GetRenderIndex(), &tasks);
}

void HnRendererImpl::Draw(IDeviceContext* pCtx, const float4x4& CameraViewProj)
{
}

} // namespace USD

} // namespace Diligent
