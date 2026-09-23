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

#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshTestHelpers.hpp"
#include "RadientTestAssetHelpers.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <memory>
#include <limits>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Verifies that storage metadata is queried inside the retained read scope.
class ScopeCheckedIndexBlob final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;
    ScopeCheckedIndexBlob(IReferenceCounters* pRefCounters, const void* pData, Uint64 Size) :
        TBase{pRefCounters}, m_pData{pData}, m_Size{Size} {}
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);

    Uint64 DILIGENT_CALL_TYPE GetSize() const override
    {
        EXPECT_NE(ReaderCount, 0u);
        return m_Size;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE BeginRead(const void** ppData) override
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        ++ReaderCount;
        *ppData = m_pData;
        return RADIENT_STATUS_OK;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE EndRead() override
    {
        if (ReaderCount == 0)
            return RADIENT_STATUS_INVALID_OPERATION;
        --ReaderCount;
        return RADIENT_STATUS_OK;
    }

    Uint32 ReaderCount = 0;

private:
    const void* const m_pData;
    const Uint64      m_Size;
};

template <typename IndexType>
RadientMeshIndexSource MakeIndexSource(const std::array<IndexType, 3>& Indices)
{
    const RefCntAutoPtr<IRadientDataBlob> pBlob = MakeTestDataBlob(Indices.data(), sizeof(Indices));

    RadientMeshCreateInfo CI;
    CI.pIndexBuffer = pBlob;
    CI.IndexCount   = static_cast<Uint32>(Indices.size());

    if constexpr (sizeof(IndexType) == sizeof(Uint8))
        CI.IndexType = RADIENT_INDEX_TYPE_UINT8;
    else if constexpr (sizeof(IndexType) == sizeof(Uint16))
        CI.IndexType = RADIENT_INDEX_TYPE_UINT16;
    else
        CI.IndexType = RADIENT_INDEX_TYPE_UINT32;

    return RadientMeshIndexSource{CI};
}

} // namespace

TEST(RadientMeshIndexSourceTest, RejectsInvalidCreateInfo)
{
    std::array<Uint16, 3> Indices{0, 1, 2};

    auto pBlob = MakeTestDataBlob(Indices.data(), sizeof(Indices));

    RadientMeshIndexDataCreateInfo CI{};
    CI.pIndexBuffer = pBlob;
    CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount   = static_cast<Uint32>(Indices.size());

    auto ExpectInvalid = [](const RadientMeshIndexDataCreateInfo& InvalidCI) //
    {
        RadientMeshIndexSource Source{InvalidCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshIndexDataCreateInfo InvalidCI = CI;
    InvalidCI.pIndexBuffer                   = nullptr;
    ExpectInvalid(InvalidCI);

    InvalidCI           = CI;
    InvalidCI.IndexType = RADIENT_INDEX_TYPE_NONE;
    ExpectInvalid(InvalidCI);

    InvalidCI            = CI;
    InvalidCI.IndexCount = 0;
    ExpectInvalid(InvalidCI);

    InvalidCI.IndexCount = (std::numeric_limits<Uint32>::max)();
    ExpectInvalid(InvalidCI);
}

TEST(RadientMeshIndexSourceTest, RejectsInvalidRadientCreateInfo)
{
    std::array<Uint16, 3> Indices{0, 1, 2};

    auto pBlob = MakeTestDataBlob(Indices.data(), sizeof(Indices));

    RadientMeshCreateInfo MeshCI{};
    MeshCI.pIndexBuffer = pBlob;
    MeshCI.IndexCount   = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType    = RADIENT_INDEX_TYPE_UINT16;

    RadientMeshIndexSource ValidSource{MeshCI};
    EXPECT_EQ(ValidSource.GetStatus(), RADIENT_STATUS_OK);

    RadientMeshCreateInfo InvalidMeshCI = MeshCI;
    InvalidMeshCI.pIndexBuffer          = nullptr;
    RadientMeshIndexSource MissingIndices{InvalidMeshCI};
    EXPECT_EQ(MissingIndices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidMeshCI           = MeshCI;
    InvalidMeshCI.IndexType = RADIENT_INDEX_TYPE_NONE;
    RadientMeshIndexSource InvalidType{InvalidMeshCI};
    EXPECT_EQ(InvalidType.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshIndexSourceTest, PacksUint8Uint16AndUint32AsUint32)
{
    std::array<Uint8, 3>  Indices8{255, 1, 0};
    std::array<Uint16, 3> Indices16{3, 4, 5};
    std::array<Uint32, 3> Indices32{6, 7, 8};

    const RadientMeshIndexSource Source8  = MakeIndexSource(Indices8);
    const RadientMeshIndexSource Source16 = MakeIndexSource(Indices16);
    const RadientMeshIndexSource Source32 = MakeIndexSource(Indices32);

    ASSERT_EQ(Source8.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source16.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source32.GetStatus(), RADIENT_STATUS_OK);

    ExpectPackedIndices(Source8, {255, 1, 0});
    ExpectPackedIndices(Source16, {3, 4, 5});
    ExpectPackedIndices(Source32, {6, 7, 8});
}

TEST(RadientMeshIndexSourceTest, RetainsReferencedBlobAndKeepsOwnerAlive)
{
    struct SourceData
    {
        std::array<Uint16, 3> Indices;
    };

    std::weak_ptr<SourceData>               WeakOwner;
    std::unique_ptr<RadientMeshIndexSource> Source;

    {
        auto Data     = std::make_shared<SourceData>();
        Data->Indices = {0, 1, 2};
        WeakOwner     = Data;

        auto                      Owner = std::make_unique<std::shared_ptr<SourceData>>(Data);
        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size      = sizeof(Data->Indices);
        BlobCI.pData     = Data->Indices.data();
        BlobCI.pUserData = Owner.get();
        BlobCI.OnDestroy = [](void* pContext) {
            delete static_cast<std::shared_ptr<SourceData>*>(pContext);
        };
        RefCntAutoPtr<IRadientDataBlob> pBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
        Owner.release();

        RadientMeshIndexDataCreateInfo CI{};
        CI.pIndexBuffer = pBlob;
        CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
        CI.IndexCount   = static_cast<Uint32>(Data->Indices.size());
        Source.reset(new RadientMeshIndexSource{CI});
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
    }

    ASSERT_FALSE(WeakOwner.expired());
    ExpectPackedIndices(*Source, {0, 1, 2});

    Source.reset();
    EXPECT_TRUE(WeakOwner.expired());
}

TEST(RadientMeshIndexSourceTest, RetainsMutableBlobReadAccessUntilAllSourcesAreDestroyed)
{
    const std::array<Uint16, 3> Indices{0, 1, 2};
    Uint32                      ReadReleases = 0;
    auto                        pBlob        = MakeTestMutableDataBlob(Indices.data(), sizeof(Indices), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);

    RadientMeshIndexDataCreateInfo CI;
    CI.pIndexBuffer = pBlob;
    CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount   = static_cast<Uint32>(Indices.size());
    auto Source     = std::make_unique<RadientMeshIndexSource>(CI);
    ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);

    RadientMeshCreateInfo MeshCI;
    MeshCI.pIndexBuffer = pBlob;
    MeshCI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    MeshCI.IndexCount   = CI.IndexCount;
    auto OtherSource    = std::make_unique<RadientMeshIndexSource>(MeshCI);
    ASSERT_EQ(OtherSource->GetStatus(), RADIENT_STATUS_OK);
    ExpectPackedIndices(*OtherSource, {0, 1, 2});

    void* pWrite = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(ReadReleases, 0u);
    Source.reset();
    EXPECT_EQ(ReadReleases, 0u);
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    OtherSource.reset();
    EXPECT_EQ(ReadReleases, 1u);
    ASSERT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_OK);
}

TEST(RadientMeshIndexSourceTest, RejectsBusyBlobWithoutRetainingReadAccess)
{
    const std::array<Uint16, 3> Indices{0, 1, 2};
    auto                        pBlob = MakeTestMutableDataBlob(Indices.data(), sizeof(Indices));
    ASSERT_NE(pBlob, nullptr);
    void* pWrite = nullptr;
    ASSERT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_OK);

    RadientMeshCreateInfo MeshCI;
    MeshCI.pIndexBuffer = pBlob;
    MeshCI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    MeshCI.IndexCount   = static_cast<Uint32>(Indices.size());
    RadientMeshIndexSource Source{MeshCI};
    EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(Source.MakeCacheKey().empty());
    EXPECT_EQ(Source.PackIndexData({}), RADIENT_STATUS_INVALID_OPERATION);

    ASSERT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_OK);
}

TEST(RadientMeshIndexSourceTest, QueriesBlobSizeUnderRetainedReadAccess)
{
    const std::array<Uint16, 3>          Indices{0, 1, 2};
    RefCntAutoPtr<ScopeCheckedIndexBlob> pBlob{MakeNewRCObj<ScopeCheckedIndexBlob>()(Indices.data(), sizeof(Indices))};
    RadientMeshIndexDataCreateInfo       CI;
    CI.pIndexBuffer = pBlob;
    CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount   = static_cast<Uint32>(Indices.size());
    {
        RadientMeshIndexSource Source{CI};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->ReaderCount, 1u);
        ExpectPackedIndices(Source, {0, 1, 2});
        EXPECT_FALSE(Source.MakeCacheKey().empty());
        EXPECT_EQ(pBlob->ReaderCount, 1u);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
}

TEST(RadientMeshIndexSourceTest, RejectsInvalidBlobStorageAndImmediatelyReleasesReadAccess)
{
    const std::array<Uint16, 3> Indices{0, 1, 2};

    auto Check = [&Indices](const void* pData, Uint64 Size, RADIENT_STATUS ExpectedStatus) {
        RefCntAutoPtr<ScopeCheckedIndexBlob> pBlob{MakeNewRCObj<ScopeCheckedIndexBlob>()(pData, Size)};
        RadientMeshIndexDataCreateInfo       CI;
        CI.pIndexBuffer = pBlob;
        CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
        CI.IndexCount   = static_cast<Uint32>(Indices.size());
        RadientMeshIndexSource Source{CI};
        EXPECT_EQ(Source.GetStatus(), ExpectedStatus);
        EXPECT_EQ(pBlob->ReaderCount, 0u);
    };
    Check(nullptr, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(nullptr, sizeof(Indices), RADIENT_STATUS_INVALID_DATA);
    Check(Indices.data(), sizeof(Indices) - 1, RADIENT_STATUS_INVALID_ARGUMENT);
    if (sizeof(size_t) < sizeof(Uint64))
        Check(Indices.data(), (std::numeric_limits<Uint64>::max)(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshIndexSourceTest, PacksUnalignedIndicesAndIgnoresTrailingBlobBytes)
{
    const std::array<Uint16, 3>                            Indices{0, 1, 2};
    alignas(Uint32) std::array<Uint8, sizeof(Indices) + 2> Bytes;
    Bytes.fill(0xCD);
    std::memcpy(Bytes.data() + 1, Indices.data(), sizeof(Indices));

    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size  = Bytes.size() - 1;
    BlobCI.pData = Bytes.data() + 1;
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
    RadientMeshIndexDataCreateInfo CI;
    CI.pIndexBuffer = pBlob;
    CI.IndexType    = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount   = static_cast<Uint32>(Indices.size());
    RadientMeshIndexSource Source{CI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ExpectPackedIndices(Source, {0, 1, 2});
    EXPECT_EQ(Source.MakeCacheKey(), MakeIndexSource(Indices).MakeCacheKey());
}

TEST(RadientMeshIndexSourceTest, RejectsInvalidPackDestination)
{
    std::array<Uint16, 3>  Indices{0, 1, 2};
    RadientMeshIndexSource Source = MakeIndexSource(Indices);
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    std::array<Uint32, 2> SmallBuffer{};
    EXPECT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{nullptr, Source.GetIndexDataSize()}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{
                  SmallBuffer.data(),
                  static_cast<Uint32>(SmallBuffer.size() * sizeof(SmallBuffer[0]))}),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshIndexSourceTest, CacheKeyDependsOnIndexData)
{
    std::array<Uint16, 3> IndicesA{0, 1, 2};
    std::array<Uint16, 3> IndicesB{0, 1, 2};
    std::array<Uint16, 3> IndicesC{2, 1, 0};

    const RadientMeshIndexSource SourceA = MakeIndexSource(IndicesA);
    const RadientMeshIndexSource SourceB = MakeIndexSource(IndicesB);
    const RadientMeshIndexSource SourceC = MakeIndexSource(IndicesC);

    ASSERT_FALSE(SourceA.MakeCacheKey().empty());
    EXPECT_EQ(SourceA.MakeCacheKey(), SourceB.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceC.MakeCacheKey());
}
