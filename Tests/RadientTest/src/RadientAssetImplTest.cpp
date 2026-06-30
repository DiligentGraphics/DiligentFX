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

#include "Assets/RadientAssetImpl.hpp"

#include "gtest/gtest.h"

#include <atomic>
#include <memory>
#include <string>

using namespace Diligent;

namespace
{

static constexpr INTERFACE_ID IID_TestTextureAssetImpl =
    {0xfad047dd, 0xfaf4, 0x49ed, {0x9e, 0x15, 0x28, 0x49, 0x96, 0x77, 0x13, 0x90}};

struct TestTextureStorage
{
    TestTextureStorage() = default;

    TestTextureStorage(Uint32         Value,
                       RADIENT_STATUS LoadStatus) :
        Value{Value},
        LoadStatus{LoadStatus}
    {
    }

    TestTextureStorage(TestTextureStorage&& Rhs) noexcept :
        Value{Rhs.Value},
        LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)}
    {
    }

    // clang-format off
    TestTextureStorage& operator=(TestTextureStorage&&)      = delete;
    TestTextureStorage(const TestTextureStorage&)            = delete;
    TestTextureStorage& operator=(const TestTextureStorage&) = delete;
    // clang-format on

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        return LoadStatus.load(std::memory_order_acquire);
    }

    Uint32                      Value = 0;
    std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
};

class TestTexturePayloadImpl final : public RadientAssetPayloadImpl<TestTextureStorage, TestTexturePayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<TestTextureStorage, TestTexturePayloadImpl>;
    using TBase::TBase;
};

using TestTextureAssetImpl =
    RadientAssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TestTextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TestTexturePayloadImpl>;

RefCntAutoPtr<TestTexturePayloadImpl> CreatePayload(Uint32         Value,
                                                    RADIENT_STATUS LoadStatus = RADIENT_STATUS_OK)
{
    return TestTexturePayloadImpl::Create(TestTextureStorage{Value, LoadStatus});
}

class TestCacheRemovalHandler final : public IRadientAssetCacheRemovalHandler
{
public:
    virtual void RemoveAssetFromCache(const Char* CacheKey) noexcept override final
    {
        ++RemoveCount;
        LastKey = CacheKey != nullptr ? CacheKey : "";
    }

    Uint32      RemoveCount = 0;
    std::string LastKey;
};

} // namespace

TEST(RadientAssetImplTest, CreatesPendingAssetReference)
{
    RefCntAutoPtr<TestTextureAssetImpl> pAsset = TestTextureAssetImpl::Create("texture://pending");
    ASSERT_NE(pAsset, nullptr);

    const RadientAssetReference& Ref = pAsset->GetReference();
    ASSERT_NE(Ref.URI, nullptr);
    EXPECT_STREQ(Ref.URI, "texture://pending");
    EXPECT_EQ(Ref.Version, 1u);
    EXPECT_EQ(pAsset->GetType(), RADIENT_ASSET_TYPE_TEXTURE);

    EXPECT_EQ(pAsset->GetPayloadStatus(), RADIENT_STATUS_PENDING);
    EXPECT_EQ(pAsset->GetPayload(), nullptr);
    EXPECT_EQ(TestTextureAssetImpl::ResolveAsset(pAsset), nullptr);
    EXPECT_EQ(TestTextureAssetImpl::GetLoadStatus(pAsset), RADIENT_STATUS_PENDING);
}

TEST(RadientAssetImplTest, SetPayloadExposesPayloadStorageAndInterfaces)
{
    RefCntAutoPtr<TestTexturePayloadImpl> pPayload = CreatePayload(17, RADIENT_STATUS_PENDING);
    RefCntAutoPtr<TestTextureAssetImpl>   pAsset   = TestTextureAssetImpl::Create("texture://resolved", std::move(pPayload));
    ASSERT_NE(pAsset, nullptr);

    EXPECT_EQ(pAsset->GetPayloadStatus(), RADIENT_STATUS_OK);
    ASSERT_NE(pAsset->GetPayload(), nullptr);
    EXPECT_EQ(pAsset->GetStorage().Value, 17u);
    EXPECT_EQ(pAsset->GetLoadStatus(), RADIENT_STATUS_PENDING);

    pAsset->GetStorage().LoadStatus.store(RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);
    EXPECT_EQ(TestTextureAssetImpl::GetLoadStatus(pAsset), RADIENT_STATUS_INVALID_OPERATION);

    RefCntAutoPtr<IRadientTextureAsset> pTexture{pAsset, IID_RadientTextureAsset};
    EXPECT_EQ(pTexture.RawPtr(), static_cast<IRadientTextureAsset*>(pAsset.RawPtr()));

    RefCntAutoPtr<IRadientAsset> pBase{pAsset, IID_RadientAsset};
    EXPECT_EQ(pBase.RawPtr(), static_cast<IRadientAsset*>(pAsset.RawPtr()));

    RefCntAutoPtr<TestTextureAssetImpl> pImpl{pTexture, IID_TestTextureAssetImpl};
    EXPECT_EQ(pImpl.RawPtr(), pAsset.RawPtr());

    RefCntAutoPtr<IRadientSceneAsset> pWrongInterface{pAsset, IID_RadientSceneAsset};
    EXPECT_EQ(pWrongInterface, nullptr);
}

TEST(RadientAssetImplTest, SetPayloadAndFailHandleInvalidStatuses)
{
    RefCntAutoPtr<TestTextureAssetImpl> pNullResolvedAsset = TestTextureAssetImpl::Create("texture://null-payload");
    ASSERT_NE(pNullResolvedAsset, nullptr);

    RefCntAutoPtr<TestTexturePayloadImpl> pNullPayload;
    EXPECT_FALSE(pNullResolvedAsset->SetPayload(std::move(pNullPayload)));
    EXPECT_EQ(pNullResolvedAsset->GetPayloadStatus(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pNullResolvedAsset->GetPayload(), nullptr);
    EXPECT_EQ(TestTextureAssetImpl::ResolveAsset(pNullResolvedAsset), nullptr);
    EXPECT_EQ(TestTextureAssetImpl::GetLoadStatus(pNullResolvedAsset), RADIENT_STATUS_INVALID_OPERATION);

    RefCntAutoPtr<TestTextureAssetImpl> pOkFailureAsset = TestTextureAssetImpl::Create("texture://ok-failure");
    ASSERT_NE(pOkFailureAsset, nullptr);
    pOkFailureAsset->Fail(RADIENT_STATUS_OK);
    EXPECT_EQ(pOkFailureAsset->GetPayloadStatus(), RADIENT_STATUS_INVALID_OPERATION);

    RefCntAutoPtr<TestTextureAssetImpl> pPendingFailureAsset = TestTextureAssetImpl::Create("texture://pending-failure");
    ASSERT_NE(pPendingFailureAsset, nullptr);
    pPendingFailureAsset->Fail(RADIENT_STATUS_PENDING);
    EXPECT_EQ(pPendingFailureAsset->GetPayloadStatus(), RADIENT_STATUS_INVALID_OPERATION);

    RefCntAutoPtr<TestTextureAssetImpl> pInvalidArgAsset = TestTextureAssetImpl::Create("texture://invalid-arg");
    ASSERT_NE(pInvalidArgAsset, nullptr);
    pInvalidArgAsset->Fail(RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pInvalidArgAsset->GetPayloadStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(TestTextureAssetImpl::GetLoadStatus(pInvalidArgAsset), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientAssetPayloadImplTest, ReleaseInvokesCacheRemovalHandler)
{
    std::shared_ptr<TestCacheRemovalHandler> pHandler = std::make_shared<TestCacheRemovalHandler>();

    {
        RefCntAutoPtr<TestTexturePayloadImpl> pPayload = CreatePayload(1);
        pPayload->SetCacheRemovalHandler(pHandler, "texture-key");
        EXPECT_EQ(pHandler->RemoveCount, 0u);
    }

    EXPECT_EQ(pHandler->RemoveCount, 1u);
    EXPECT_EQ(pHandler->LastKey, "texture-key");
}

TEST(RadientAssetPayloadImplTest, ReleaseSkipsEmptyOrExpiredRemovalHandler)
{
    std::shared_ptr<TestCacheRemovalHandler> pHandler = std::make_shared<TestCacheRemovalHandler>();

    {
        RefCntAutoPtr<TestTexturePayloadImpl> pPayload = CreatePayload(1);
        pPayload->SetCacheRemovalHandler(pHandler, "");
    }
    EXPECT_EQ(pHandler->RemoveCount, 0u);

    {
        RefCntAutoPtr<TestTexturePayloadImpl> pPayload = CreatePayload(2);
        pPayload->SetCacheRemovalHandler(pHandler, "texture-key");
        pHandler.reset();
    }
}
