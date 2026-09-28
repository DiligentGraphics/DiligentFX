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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTypesX.hpp"
#include "GPUUploadManager.h"
#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"
#include "GLTFLoader.hpp"
#include "ObjectBase.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

// Keep the actual asset/storage/writer implementation, but stop initial worker
// publication after its shader data has been packed and allocated. Packing never
// reads IsDoubleSided(); ProcessMaterial reads it for PublishSuccess() arguments.
class GatedSurfaceMaterial final : public ObjectBase<IRadientSurfaceMaterialAsset>
{
public:
    GatedSurfaceMaterial(IReferenceCounters*    pRefCounters,
                         IRadientMaterialAsset* pMaterial,
                         Threading::Signal&     Allocated,
                         Threading::Signal&     Release) :
        ObjectBase<IRadientSurfaceMaterialAsset>{pRefCounters},
        m_pMaterial{pMaterial, IID_RadientSurfaceMaterialAsset},
        m_Allocated{Allocated},
        m_Release{Release}
    {}

    void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override
    {
        if (ppInterface == nullptr)
            return;
        if (IID == IID_Unknown || IID == IID_RadientAsset ||
            IID == IID_RadientMaterialAsset || IID == IID_RadientSurfaceMaterialAsset)
        {
            *ppInterface = this;
            AddRef();
        }
        else
        {
            // The material cache and packing code obtain the real storage.
            m_pMaterial->QueryInterface(IID, ppInterface);
        }
    }

    const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override
    {
        return m_pMaterial->GetReference();
    }
    RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override
    {
        return m_pMaterial->GetType();
    }
    IRadientMaterialDefinitionAsset* DILIGENT_CALL_TYPE GetDefinition() const override
    {
        return m_pMaterial->GetDefinition();
    }
    Uint64 DILIGENT_CALL_TYPE GetVersion() const override
    {
        return m_pMaterial->GetVersion();
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                   void*                          pData,
                                                   Uint32                         DataSize) const override
    {
        return m_pMaterial->GetParameter(Handle, pData, DataSize);
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle Handle,
                                                 Uint32                         ArrayIndex,
                                                 IRadientTextureAsset**         ppTexture) const override
    {
        return m_pMaterial->GetTexture(Handle, ArrayIndex, ppTexture);
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialWriter** ppWriter) override
    {
        return m_pMaterial->CreateWriter(ppWriter);
    }
    RADIENT_MATERIAL_SURFACE_MODE DILIGENT_CALL_TYPE GetSurfaceMode() const override
    {
        return m_pMaterial->GetSurfaceMode();
    }
    Float32 DILIGENT_CALL_TYPE GetAlphaCutoff() const override
    {
        return m_pMaterial->GetAlphaCutoff();
    }
    Bool DILIGENT_CALL_TYPE IsDoubleSided() const override
    {
        m_Allocated.Trigger();
        m_Release.Wait();
        return m_pMaterial->IsDoubleSided();
    }

private:
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> m_pMaterial;
    Threading::Signal&                          m_Allocated;
    Threading::Signal&                          m_Release;
};

struct MaterialWorkerReleaseGuard
{
    Threading::Signal& ReleaseSignal;
    IThreadPool&       ThreadPool;

    ~MaterialWorkerReleaseGuard()
    {
        ReleaseAndWait();
    }

    void ReleaseAndWait()
    {
        if (!ReleaseSignal.IsTriggered())
            ReleaseSignal.Trigger();
        ThreadPool.WaitForAllTasks();
    }
};

RadientMaterialParameterHandle FindMaterialParameter(const RadientMaterialAssetView& MaterialData,
                                                     const char*                     Name)
{
    RadientMaterialParameterHandle Handle;
    if (MaterialData.pMaterial == nullptr)
    {
        ADD_FAILURE() << "Material render data has no material";
        return Handle;
    }

    IRadientMaterialDefinitionAsset* const pDefinition = MaterialData.pMaterial->GetDefinition();
    if (pDefinition == nullptr)
    {
        ADD_FAILURE() << "Material has no definition";
        return Handle;
    }

    EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
    return Handle;
}

IRadientTextureAsset* GetMaterialTexture(const RadientMaterialAssetView& MaterialData,
                                         const char*                     ParameterName)
{
    const RadientMaterialParameterHandle Handle = FindMaterialParameter(MaterialData, ParameterName);
    return Handle ? MaterialData.GetTextureAsset(Handle.Index) : nullptr;
}

template <typename ValueType>
ValueType GetMaterialParameter(const RadientMaterialAssetView& MaterialData,
                               const char*                     ParameterName)
{
    ValueType                            Value{};
    const RadientMaterialParameterHandle Handle = FindMaterialParameter(MaterialData, ParameterName);
    if (Handle)
    {
        EXPECT_EQ(MaterialData.pMaterial->GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))),
                  RADIENT_STATUS_OK)
            << ParameterName;
    }
    return Value;
}

struct MaterialWithTextureManagers
{
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     pUploadManager;
    RadientTextureAssetManagerSharedPtr  pTextureManager;
    RadientMaterialAssetManagerSharedPtr pMaterialManager;
};

struct StandardMaterialTextureBinding
{
    StandardMaterialTextureBinding(const RadientStandardMaterialTextureParameterNames& Names_,
                                   IRadientTextureAsset*                               pTexture,
                                   Int32                                               UVSelector) :
        Names{Names_},
        Parameters{pTexture}
    {
        Parameters.UVSelector = UVSelector;
    }

    RadientStandardMaterialTextureParameterNames Names;
    RadientStandardMaterialTextureParameters     Parameters;
};

RADIENT_STATUS CreateStandardMaterial(
    RadientMaterialAssetManager&                          MaterialManager,
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS                Features,
    std::initializer_list<StandardMaterialTextureBinding> TextureBindings,
    IRadientMaterialAsset**                               ppMaterial)
{
    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features = Features;

    return CreateStandardMaterialAsset(
        MaterialManager,
        DefinitionCI,
        [TextureBindings](IRadientMaterialDefinitionAsset& Definition,
                          IRadientMaterialWriter&          Writer) {
            for (const StandardMaterialTextureBinding& Binding : TextureBindings)
            {
                const RADIENT_STATUS Status = SetStandardMaterialTextureParameters(
                    Definition,
                    Writer,
                    Binding.Names,
                    Binding.Parameters);
                if (RADIENT_FAILED(Status))
                    return Status;
            }

            return RADIENT_STATUS_OK;
        },
        ppMaterial);
}

bool CreateMaterialWithBaseColorTexture(IRenderDevice*                        pDevice,
                                        IDeviceContext*                       pContext,
                                        IThreadPool&                          ThreadPool,
                                        const RadientTextureData&             TextureData,
                                        MaterialWithTextureManagers&          Managers,
                                        RefCntAutoPtr<IRadientTextureAsset>&  pTexture,
                                        RefCntAutoPtr<IRadientMaterialAsset>& pMaterial)
{
    Managers.pResourceManager = CreateTestResourceManager(pDevice);
    if (Managers.pResourceManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create resource manager";
        return false;
    }

    Managers.pUploadManager = CreateTestUploadManager(pDevice, pContext);
    if (Managers.pUploadManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create upload manager";
        return false;
    }

    Managers.pTextureManager = CreateTextureManager(pDevice, Managers.pResourceManager, Managers.pUploadManager);
    if (Managers.pTextureManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create texture manager";
        return false;
    }

    Managers.pMaterialManager = RadientMaterialAssetManager::Create();
    if (Managers.pMaterialManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create material manager";
        return false;
    }

    const RADIENT_STATUS TextureStatus =
        Managers.pTextureManager->LoadTexture(ThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture);
    if (!IsPendingOrOK(TextureStatus) || pTexture == nullptr)
    {
        ADD_FAILURE() << "Failed to load texture: " << TextureStatus;
        return false;
    }

    const RADIENT_STATUS MaterialStatus = CreateStandardMaterial(
        *Managers.pMaterialManager,
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE,
        {{RadientStandardMaterialBaseColorTextureParameterNames,
          pTexture,
          0}},
        &pMaterial);
    if (MaterialStatus != RADIENT_STATUS_OK || pMaterial == nullptr)
    {
        ADD_FAILURE() << "Failed to create material: " << MaterialStatus;
        return false;
    }

    return true;
}

TEST(RadientMaterialAssetManagerGPUTest, WaitsForTextureStorage)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pTextureManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterial(
                  *pMaterialManager,
                  RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE,
                  {{RadientStandardMaterialBaseColorTextureParameterNames,
                    pTexture,
                    0}},
                  &pMaterial),
              RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    // The texture worker is blocked, so the material must not expose texture
    // attributes that depend on texture storage placement.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_PENDING);
    EXPECT_FALSE(RadientMaterialAssetManager::GetMaterialView(pMaterial));

    ReleaseWorker.Trigger();

    ASSERT_TRUE(WaitForTextureManagerIdle(pTextureManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialData);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName), pTexture);
    EXPECT_EQ(GetMaterialParameter<Int32>(MaterialData, RadientStandardMaterialBaseColorTextureUVSelectorName), 0);

    RadientTextureSamplingInfo SamplingInfo;
    EXPECT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));

    pThreadPool->StopThreads();
}

TEST(RadientMaterialAssetManagerGPUTest, StandardMaterialWithSharedTextureWaitsForTextureStorage)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pTextureManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterial(
                  *pMaterialManager,
                  RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT,
                  {
                      {RadientStandardMaterialBaseColorTextureParameterNames,
                       pTexture,
                       0},
                      {RadientStandardMaterialNormalTextureParameterNames,
                       pTexture,
                       1},
                      {RadientStandardMaterialClearCoatTextureParameterNames,
                       pTexture,
                       2},
                  },
                  &pMaterial),
              RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    // The texture worker is blocked, so the material must wait for referenced
    // texture assets before exposing atlas-dependent texture attributes.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_PENDING);
    EXPECT_FALSE(RadientMaterialAssetManager::GetMaterialView(pMaterial));

    ReleaseWorker.Trigger();

    ASSERT_TRUE(WaitForTextureManagerIdle(pTextureManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialData);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName), pTexture);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialNormalTextureName), pTexture);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialClearCoatTextureName), pTexture);
    EXPECT_EQ(GetMaterialParameter<Int32>(MaterialData, RadientStandardMaterialBaseColorTextureUVSelectorName), 0);
    EXPECT_EQ(GetMaterialParameter<Int32>(MaterialData, RadientStandardMaterialNormalTextureUVSelectorName), 1);
    EXPECT_EQ(GetMaterialParameter<Int32>(MaterialData, RadientStandardMaterialClearCoatTextureUVSelectorName), 2);

    RadientTextureSamplingInfo SamplingInfo;
    EXPECT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo));

    pThreadPool->StopThreads();
}

TEST(RadientMaterialAssetManagerGPUTest, MaterialHandleMayOutliveManagersAfterTextureUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RefCntAutoPtr<IRadientTextureAsset>  pTexture;
    {
        MaterialWithTextureManagers Managers;
        ASSERT_TRUE(CreateMaterialWithBaseColorTexture(pDevice, pContext, *pThreadPool, TextureData, Managers, pTexture, pMaterial));

        ASSERT_TRUE(WaitForTextureManagerIdle(Managers.pTextureManager, *Managers.pUploadManager, *pContext));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

        ProcessUploads(*Managers.pUploadManager, *pContext, *pTexture);

        // Drop the explicit texture handle. The material payload keeps the
        // texture dependency alive after all managers leave this scope.
        pTexture.Release();
    }

    pThreadPool->StopThreads();

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialData);
    IRadientTextureAsset* const pRetainedTexture =
        GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName);
    ASSERT_NE(pRetainedTexture, nullptr);
    EXPECT_EQ(GetMaterialParameter<Int32>(MaterialData, RadientStandardMaterialBaseColorTextureUVSelectorName), 0);

    RadientTextureSamplingInfo SamplingInfo;
    EXPECT_TRUE(RadientTextureAssetManager::GetTextureSamplingInfo(pRetainedTexture, SamplingInfo));
}

TEST(RadientMaterialAssetManagerGPUTest, MaterialHandleMayOutliveManagersBeforeTextureUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    const RefCntAutoPtr<IRadientDataBlob> pTextureDataBlob = MakeTextureDataBlob();
    ASSERT_NE(pTextureDataBlob, nullptr);
    const RadientTextureDataX TextureData = MakeTextureData(pTextureDataBlob);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RefCntAutoPtr<IRadientTextureAsset>  pTexture;
    {
        MaterialWithTextureManagers Managers;
        ASSERT_TRUE(CreateMaterialWithBaseColorTexture(pDevice, pContext, *pThreadPool, TextureData, Managers, pTexture, pMaterial));

        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_PENDING);
        EXPECT_FALSE(RadientMaterialAssetManager::GetMaterialView(pMaterial));

        // Drop the explicit texture handle before managers are destroyed. The
        // material payload must keep the pending texture dependency alive.
        pTexture.Release();
    }

    // The queued texture load now runs without the resource/upload managers.
    // The material asset must remain valid; source load succeeds, while the
    // dependent GPU resource status reports the failure.
    ReleaseWorker.Trigger();
    pThreadPool->StopThreads();

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_CANCELLED);
    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialData);
    EXPECT_NE(GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName), nullptr);
}


std::vector<Uint8> ReadMaterialShaderData(IRenderDevice&                    Device,
                                          IDeviceContext&                   Context,
                                          RadientTesseraMaterialCache&      Cache,
                                          const RadientTesseraMaterialData& Material)
{
    const RadientTesseraBufferAllocation& Allocation = Material.GetMaterialBufferAllocation();
    BufferDesc                            StagingDesc;
    StagingDesc.Name           = "Material shader data readback";
    StagingDesc.Size           = Allocation.GetSize();
    StagingDesc.Usage          = USAGE_STAGING;
    StagingDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pStagingBuffer;
    Device.CreateBuffer(StagingDesc, nullptr, &pStagingBuffer);
    EXPECT_NE(pStagingBuffer, nullptr);
    if (pStagingBuffer == nullptr)
        return {};

    Context.CopyBuffer(Cache.GetMaterialBuffer(), Allocation.GetOffset(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                       pStagingBuffer, 0, Allocation.GetSize(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Context.WaitForIdle();
    void* pMappedData = nullptr;
    Context.MapBuffer(pStagingBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT, pMappedData);
    EXPECT_NE(pMappedData, nullptr);
    if (pMappedData == nullptr)
        return {};

    std::vector<Uint8> Bytes(Allocation.GetSize());
    std::memcpy(Bytes.data(), pMappedData, Bytes.size());
    Context.UnmapBuffer(pStagingBuffer, MAP_READ);
    return Bytes;
}

TEST(RadientMaterialAssetManagerGPUTest, InitialShaderDataMustBeValidatedAfterWorkerPublication)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment* const       pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const               pDevice  = pEnv->GetDevice();
    IDeviceContext* const              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard                 StopThreads{pThreadPool};
    RadientAssetManagerImpl::CreateInfo AssetManagerCI;
    AssetManagerCI.pDevice                               = pDevice;
    AssetManagerCI.pThreadPool                           = pThreadPool;
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pMaterial), RADIENT_STATUS_OK);

    const std::chrono::steady_clock::time_point Deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial) == RADIENT_STATUS_PENDING &&
           std::chrono::steady_clock::now() < Deadline)
    {
        pAssetManager->UpdateGPUResources(pDevice, pContext);
        pContext->Flush();
        pContext->FinishFrame();
        std::this_thread::yield();
    }
    ASSERT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    RadientMaterialParameterHandle ColorHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter(RadientStandardMaterialBaseColorFactorName, &ColorHandle), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(pMaterial->CreateWriter(&pWriter), RADIENT_STATUS_OK);
    const RadientFloat4 OriginalColor = Testing::GetMaterialParameter<RadientFloat4>(*pMaterial, RadientStandardMaterialBaseColorFactorName);
    const RadientFloat4 UpdatedColor{0.25f, 0.5f, 0.75f, 1.f};
    const size_t        ColorOffset = offsetof(GLTF::Material::ShaderAttribs, BaseColorFactor);

    {
        RadientTesseraGeometryRenderer Renderer{8, pAssetManager->GetDefaultMaterialTextures()};
        ASSERT_EQ(Renderer.BeginFrame(pDevice, pContext), RADIENT_STATUS_OK);
        ASSERT_NE(Renderer.GetMaterialCache(), nullptr);
        RadientTesseraMaterialCache&        Cache = *Renderer.GetMaterialCache();
        Threading::Signal                   Allocated;
        Threading::Signal                   Release;
        RefCntAutoPtr<GatedSurfaceMaterial> pGatedMaterial{
            MakeNewRCObj<GatedSurfaceMaterial>()(pMaterial.RawPtr(), Allocated, Release)};
        // Release before destroying the renderer or stopping its pool if any
        // assertion returns while the worker is held at publication.
        MaterialWorkerReleaseGuard          WorkerGuard{Release, *pThreadPool};
        RadientTesseraMaterialResolveResult Result = Cache.Resolve(*pThreadPool, pGatedMaterial);
        ASSERT_TRUE(Result.Data);
        while (!Allocated.IsTriggered() && Result.Data->GetStatus() == RADIENT_STATUS_PENDING)
            std::this_thread::yield();
        ASSERT_TRUE(Allocated.IsTriggered());
        ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_PENDING);

        ASSERT_EQ(pWriter->SetParameter(ColorHandle, UpdatedColor), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        // Refresh skips the pending record, but the shared buffer already
        // contains its allocation and uploads the old values.
        ASSERT_EQ(Cache.PrepareMaterialBuffer(pDevice, pContext), RADIENT_STATUS_OK);
        WorkerGuard.ReleaseAndWait();
        ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

        // Prepare real texture bindings without refreshing shader data again.
        // This reproduces publication between the refresh and SRB preparation.
        RadientPBRRenderer& PBRRenderer = *Renderer.GetRenderer();
        ASSERT_EQ(Cache.Prepare(
                      pAssetManager->GetResourceManager()->GetTextureVersion(),
                      [](const RadientMaterialTextureSRBSlot& Binding) {
                          return RadientMaterialTextureSRVResolveResult{
                              RadientTextureAssetManager::GetGPUResourceStatus(Binding.pTexture),
                              RadientTextureAssetManager::GetTextureSRV(Binding.pTexture, Binding.ViewType)};
                      },
                      [&Cache, &PBRRenderer](ITextureView* const* ppTextures, Uint32 TextureCount) {
                          RefCntAutoPtr<IShaderResourceBinding> pSRB;
                          PBRRenderer.CreateResourceBinding(&pSRB, 1);
                          if (pSRB != nullptr)
                          {
                              PBRRenderer.InitMaterialSRBVars(pSRB, Cache.GetMaterialBuffer(), Cache.GetMaxMaterialAttribsSize());
                              if (!PBRRenderer.SetMaterialTextures(pSRB, ppTextures, 0, TextureCount))
                                  pSRB.Release();
                          }
                          return pSRB;
                      }),
                  RADIENT_STATUS_OK);
        ASSERT_NE(Result.Data->GetMaterialSRB().GetSRB(), nullptr);
        EXPECT_TRUE(Result.Data->GetMaterialBufferAllocation().IsUploadedThrough(
            Result.Data->GetMaterialSRB().GetMaterialBufferGeneration()));
        EXPECT_EQ(Result.Data->GetGPUResourceStatus(), RADIENT_STATUS_PENDING);
        const std::vector<Uint8> BeforeRefresh = ReadMaterialShaderData(*pDevice, *pContext, Cache, *Result.Data);
        ASSERT_GE(BeforeRefresh.size(), ColorOffset + sizeof(OriginalColor));
        EXPECT_EQ(std::memcmp(BeforeRefresh.data() + ColorOffset, &OriginalColor, sizeof(OriginalColor)), 0);

        ASSERT_EQ(Renderer.Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
        EXPECT_EQ(Result.Data->GetGPUResourceStatus(), RADIENT_STATUS_OK);
        const std::vector<Uint8> AfterRefresh = ReadMaterialShaderData(*pDevice, *pContext, Cache, *Result.Data);
        ASSERT_GE(AfterRefresh.size(), ColorOffset + sizeof(UpdatedColor));
        EXPECT_EQ(std::memcmp(AfterRefresh.data() + ColorOffset, &UpdatedColor, sizeof(UpdatedColor)), 0);
    }
    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
}

TEST(RadientMaterialAssetManagerGPUTest, UpdatesShaderDataInEachRendererWithoutReplacingBindings)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    GPUTestingEnvironment* const       pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice* const               pDevice  = pEnv->GetDevice();
    IDeviceContext* const              pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreads{pThreadPool};

    RadientAssetManagerImpl::CreateInfo AssetManagerCI;
    AssetManagerCI.pDevice                               = pDevice;
    AssetManagerCI.pThreadPool                           = pThreadPool;
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pMaterial), RADIENT_STATUS_OK);

    // Complete the default texture dependencies before the material cache
    // starts its own packing task.
    const std::chrono::steady_clock::time_point Deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial) == RADIENT_STATUS_PENDING &&
           std::chrono::steady_clock::now() < Deadline)
    {
        pAssetManager->UpdateGPUResources(pDevice, pContext);
        pContext->Flush();
        pContext->FinishFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    RadientMaterialParameterHandle ColorHandle;
    ASSERT_EQ(pMaterial->GetDefinition()->FindParameter(RadientStandardMaterialBaseColorFactorName, &ColorHandle), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    ASSERT_EQ(pMaterial->CreateWriter(&pWriter), RADIENT_STATUS_OK);

    {
        RadientTesseraGeometryRenderer                       FirstRenderer{8, pAssetManager->GetDefaultMaterialTextures()};
        RadientTesseraGeometryRenderer                       SecondRenderer{8, pAssetManager->GetDefaultMaterialTextures()};
        const std::array<RadientTesseraGeometryRenderer*, 2> Renderers{&FirstRenderer, &SecondRenderer};
        std::array<RadientTesseraMaterialResolveResult, 2>   Materials;
        for (size_t Index = 0; Index < Renderers.size(); ++Index)
        {
            ASSERT_EQ(Renderers[Index]->BeginFrame(pDevice, pContext), RADIENT_STATUS_OK);
            ASSERT_NE(Renderers[Index]->GetMaterialCache(), nullptr);
            Materials[Index] = Renderers[Index]->GetMaterialCache()->Resolve(*pThreadPool, pMaterial);
            ASSERT_TRUE(Materials[Index].Data);
        }
        pThreadPool->WaitForAllTasks();

        // Both workers have already packed the original color. A commit before
        // the first upload must repair that stale packing in both caches.
        const RadientFloat4 FirstColor{0.25f, 0.5f, 0.75f, 1.f};
        ASSERT_EQ(pWriter->SetParameter(ColorHandle, FirstColor), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        const size_t                           ColorOffset = offsetof(GLTF::Material::ShaderAttribs, BaseColorFactor);
        std::array<IBuffer*, 2>                Buffers{};
        std::array<IShaderResourceBinding*, 2> SRBs{};
        std::array<Uint32, 2>                  Offsets{};
        std::array<Uint64, 2>                  Generations{};
        std::array<std::vector<Uint8>, 2>      InitialBytes;
        for (size_t Index = 0; Index < Renderers.size(); ++Index)
        {
            ASSERT_EQ(Renderers[Index]->Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
            ASSERT_EQ(Materials[Index].Data->GetGPUResourceStatus(), RADIENT_STATUS_OK);
            RadientTesseraMaterialCache& Cache = *Renderers[Index]->GetMaterialCache();
            InitialBytes[Index]                = ReadMaterialShaderData(*pDevice, *pContext, Cache, *Materials[Index].Data);
            ASSERT_GE(InitialBytes[Index].size(), ColorOffset + sizeof(FirstColor));
            EXPECT_EQ(std::memcmp(InitialBytes[Index].data() + ColorOffset, &FirstColor, sizeof(FirstColor)), 0);
            Buffers[Index]     = Cache.GetMaterialBuffer();
            SRBs[Index]        = Materials[Index].Data->GetMaterialSRB().GetSRB();
            Offsets[Index]     = Materials[Index].Data->GetMaterialBufferAllocation().GetOffset();
            Generations[Index] = Materials[Index].Data->GetMaterialSRB().GetMaterialBufferGeneration();
        }
        EXPECT_EQ(InitialBytes[0], InitialBytes[1]);

        // Repeated commits before preparation upload only the latest values.
        // Preparing one renderer must not consume the other cache's revision.
        const RadientFloat4 IntermediateColor{0.5f, 0.25f, 0.75f, 1.f};
        const RadientFloat4 FinalColor{0.75f, 0.5f, 0.25f, 1.f};
        ASSERT_EQ(pWriter->SetParameter(ColorHandle, IntermediateColor), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->SetParameter(ColorHandle, FinalColor), RADIENT_STATUS_OK);
        ASSERT_EQ(pWriter->Commit(), RADIENT_STATUS_OK);
        for (size_t Index = 0; Index < Renderers.size(); ++Index)
        {
            // An already rendered material stays available while its latest
            // shader-only edits wait for the next normal buffer upload.
            EXPECT_EQ(Materials[Index].Data->GetGPUResourceStatus(), RADIENT_STATUS_OK);
            ASSERT_EQ(Renderers[Index]->Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
            RadientTesseraMaterialCache&      Cache        = *Renderers[Index]->GetMaterialCache();
            const RadientTesseraMaterialData& MaterialData = *Materials[Index].Data;
            EXPECT_EQ(MaterialData.GetGPUResourceStatus(), RADIENT_STATUS_OK);
            EXPECT_EQ(Cache.GetMaterialBuffer(), Buffers[Index]);
            EXPECT_EQ(MaterialData.GetMaterialSRB().GetSRB(), SRBs[Index]);
            EXPECT_EQ(MaterialData.GetMaterialBufferAllocation().GetOffset(), Offsets[Index]);
            EXPECT_GT(MaterialData.GetMaterialSRB().GetMaterialBufferGeneration(), Generations[Index]);

            std::vector<Uint8> ExpectedBytes = InitialBytes[Index];
            std::memcpy(ExpectedBytes.data() + ColorOffset, &FinalColor, sizeof(FinalColor));
            EXPECT_EQ(ReadMaterialShaderData(*pDevice, *pContext, Cache, MaterialData), ExpectedBytes);

            // An unchanged frame must neither allocate nor upload another record.
            const Uint64 Generation = MaterialData.GetMaterialSRB().GetMaterialBufferGeneration();
            ASSERT_EQ(Renderers[Index]->Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
            EXPECT_EQ(MaterialData.GetMaterialSRB().GetMaterialBufferGeneration(), Generation);
        }
    }
    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
}

} // namespace
