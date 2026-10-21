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

#include "Assets/RadientMorphTargetSource.hpp"
#include "RadientTestAssetHelpers.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Verifies that storage metadata is queried inside the retained read scope.
class ScopeCheckedMorphBlob final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;
    ScopeCheckedMorphBlob(IReferenceCounters* pRefCounters, const void* pData, Uint64 Size) :
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

struct MorphTargetSourceData
{
    static constexpr Uint32 VertexCount = 3;

    std::array<Float32, 9> PositionDeltas{
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        -0.1f, -0.2f, -0.3f};
    std::array<Float32, 6> CustomDeltas{
        1.f, 2.f,
        3.f, 4.f,
        5.f, 6.f};
    RefCntAutoPtr<IRadientDataBlob>                      pPositionBlob;
    RefCntAutoPtr<IRadientDataBlob>                      pCustomBlob;
    std::array<RadientMorphTargetAttributeDesc, 2>       Attributes{};
    std::array<RadientMorphTargetAttributeCreateInfo, 2> AttributeData{};
    std::array<RadientMorphTargetCreateInfo, 2>          Targets{};

    MorphTargetSourceData()
    {
        Reset();
    }

    void Reset()
    {
        PositionDeltas = {
            0.f, 0.f, 0.f,
            0.1f, 0.2f, 0.3f,
            -0.1f, -0.2f, -0.3f};
        CustomDeltas = {
            1.f, 2.f,
            3.f, 4.f,
            5.f, 6.f};
        Attributes    = {};
        AttributeData = {};
        Targets       = {};

        Attributes[0].Semantic        = RadientMorphTargetPositionSemantic;
        Attributes[0].ComponentCount  = 3;
        Attributes[1].Semantic        = "CUSTOM";
        Attributes[1].ComponentCount  = 2;
        pPositionBlob                 = MakeTestDataBlob(PositionDeltas.data(), sizeof(PositionDeltas));
        pCustomBlob                   = MakeTestDataBlob(CustomDeltas.data(), sizeof(CustomDeltas));
        AttributeData[0].pDeltaBuffer = pPositionBlob;
        AttributeData[1].pDeltaBuffer = pCustomBlob;

        Targets[0].Desc.Name           = "Smile";
        Targets[0].Desc.pAttributes    = Attributes.data();
        Targets[0].Desc.AttributeCount = static_cast<Uint32>(Attributes.size());
        Targets[0].Desc.DefaultWeight  = 0.25f;
        Targets[0].pAttributeData      = AttributeData.data();
        Targets[1].Desc.DefaultWeight  = -0.5f;
    }
};

} // namespace

TEST(RadientMorphTargetSourceTest, RejectsInvalidDimensions)
{
    MorphTargetSourceData Data;

    RadientMorphTargetSource MissingTargets{nullptr, 1, MorphTargetSourceData::VertexCount};
    EXPECT_EQ(MissingTargets.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMorphTargetSource EmptyTargets{Data.Targets.data(), 0, MorphTargetSourceData::VertexCount};
    EXPECT_EQ(EmptyTargets.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMorphTargetSource EmptyVertices{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), 0};
    EXPECT_EQ(EmptyVertices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidTargetDescription)
{
    MorphTargetSourceData Data;

    Data.Targets[0].Desc.DefaultWeight = (std::numeric_limits<Float32>::infinity)();
    RadientMorphTargetSource NonFiniteWeight{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NonFiniteWeight.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Targets[0].Desc.pAttributes = nullptr;
    RadientMorphTargetSource NoAttributes{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoAttributes.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Targets[0].pAttributeData = nullptr;
    RadientMorphTargetSource NoAttributeData{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoAttributeData.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidAttributeDescription)
{
    MorphTargetSourceData Data;

    Data.Attributes[0].Semantic = nullptr;
    RadientMorphTargetSource MissingSemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(MissingSemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].Semantic = "";
    RadientMorphTargetSource EmptySemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(EmptySemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 0;
    RadientMorphTargetSource ZeroComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(ZeroComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 5;
    RadientMorphTargetSource TooManyComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(TooManyComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.AttributeData[0].pDeltaBuffer = nullptr;
    RadientMorphTargetSource NoDeltas{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(NoDeltas.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].ComponentCount = 2;
    RadientMorphTargetSource InvalidStandardComponents{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(InvalidStandardComponents.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    Data.Reset();
    Data.Attributes[0].Semantic = "CUSTOM"; // Both attributes are valid individually.
    RadientMorphTargetSource DuplicateSemantic{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(DuplicateSemantic.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, RejectsDataLargerThanUint32)
{
    const Float32                         Delta = 0.f;
    const RefCntAutoPtr<IRadientDataBlob> pBlob = MakeTestDataBlob(&Delta, sizeof(Delta));
    RadientMorphTargetAttributeDesc       Attribute{};
    RadientMorphTargetAttributeCreateInfo AttributeData{};
    RadientMorphTargetCreateInfo          Target{};
    Attribute.Semantic         = "CUSTOM";
    Attribute.ComponentCount   = 4;
    AttributeData.pDeltaBuffer = pBlob;
    Target.Desc.pAttributes    = &Attribute;
    Target.Desc.AttributeCount = 1;
    Target.pAttributeData      = &AttributeData;

    RadientMorphTargetSource Source{&Target, 1, (std::numeric_limits<Uint32>::max)()};
    EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, CopiesMetadataAndPacksUploadData)
{
    MorphTargetSourceData Data;
    std::string           TargetName = "Expression";
    std::string           Semantic   = "CUSTOM_DATA";
    Data.Targets[0].Desc.Name        = TargetName.c_str();
    Data.Attributes[1].Semantic      = Semantic.c_str();

    const std::array<Float32, 9> ExpectedPositionDeltas = Data.PositionDeltas;
    const std::array<Float32, 6> ExpectedCustomDeltas   = Data.CustomDeltas;
    RadientMorphTargetSource     Source{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};

    TargetName.assign("changed");
    Semantic.assign("changed");
    Data.Targets       = {};
    Data.Attributes    = {};
    Data.AttributeData = {};
    Data.pPositionBlob.Release();
    Data.pCustomBlob.Release();

    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Source.GetVertexCount(), Data.VertexCount);
    ASSERT_EQ(Source.GetTargetCount(), 2u);

    const RadientMorphTargetSource::Target& Target0 = Source.GetTarget(0);
    const RadientMorphTargetSource::Target& Target1 = Source.GetTarget(1);
    EXPECT_EQ(Target0.Name, "Expression");
    EXPECT_FLOAT_EQ(Target0.DefaultWeight, 0.25f);
    ASSERT_EQ(Target0.Attributes.size(), 2u);
    EXPECT_EQ(Target0.Attributes[0].Semantic, RadientMorphTargetPositionSemantic);
    EXPECT_EQ(Target0.Attributes[1].Semantic, "CUSTOM_DATA");
    EXPECT_EQ(Target0.Attributes[0].ComponentCount, 3u);
    EXPECT_EQ(Target0.Attributes[1].ComponentCount, 2u);
    EXPECT_EQ(Target0.Attributes[0].DataOffset, 0u);
    EXPECT_EQ(Target0.Attributes[1].DataOffset, sizeof(ExpectedPositionDeltas));
    EXPECT_EQ(Target1.Name, "");
    EXPECT_FLOAT_EQ(Target1.DefaultWeight, -0.5f);

    std::vector<Uint8> PackedData(Source.GetDataSize());
    ASSERT_EQ(Source.PackData({PackedData.data(), static_cast<Uint32>(PackedData.size())}), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(PackedData.data() + Target0.Attributes[0].DataOffset,
                          ExpectedPositionDeltas.data(), sizeof(ExpectedPositionDeltas)),
              0);
    EXPECT_EQ(std::memcmp(PackedData.data() + Target0.Attributes[1].DataOffset,
                          ExpectedCustomDeltas.data(), sizeof(ExpectedCustomDeltas)),
              0);
}

TEST(RadientMorphTargetSourceTest, SupportsTargetsWithoutAttributes)
{
    RadientMorphTargetCreateInfo Target{};
    Target.Desc.Name          = "Empty";
    Target.Desc.DefaultWeight = 0.5f;

    RadientMorphTargetSource Source{&Target, 1, 3};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Source.GetDataSize(), 0u);
    EXPECT_EQ(Source.PackData({}), RADIENT_STATUS_OK);
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidPackDestination)
{
    MorphTargetSourceData    Data;
    RadientMorphTargetSource Source{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    std::vector<Uint8> SmallBuffer(Source.GetDataSize() - 1);
    EXPECT_EQ(Source.PackData({nullptr, Source.GetDataSize()}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackData({SmallBuffer.data(), static_cast<Uint32>(SmallBuffer.size())}), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMorphTargetSourceTest, CacheKeyIncludesMetadataAndDeltas)
{
    MorphTargetSourceData DataA;
    MorphTargetSourceData DataB;
    MorphTargetSourceData DifferentDeltas;
    MorphTargetSourceData DifferentMetadata;
    DifferentDeltas.PositionDeltas[4] += 1.f;
    DifferentDeltas.pPositionBlob                 = MakeTestDataBlob(DifferentDeltas.PositionDeltas.data(), sizeof(DifferentDeltas.PositionDeltas));
    DifferentDeltas.AttributeData[0].pDeltaBuffer = DifferentDeltas.pPositionBlob;
    DifferentMetadata.Targets[0].Desc.Name        = "Frown";

    const RadientMorphTargetSource SourceA{DataA.Targets.data(), static_cast<Uint32>(DataA.Targets.size()), DataA.VertexCount};
    const RadientMorphTargetSource SourceB{DataB.Targets.data(), static_cast<Uint32>(DataB.Targets.size()), DataB.VertexCount};
    const RadientMorphTargetSource SourceWithDifferentDeltas{
        DifferentDeltas.Targets.data(), static_cast<Uint32>(DifferentDeltas.Targets.size()), DifferentDeltas.VertexCount};
    const RadientMorphTargetSource SourceWithDifferentMetadata{
        DifferentMetadata.Targets.data(), static_cast<Uint32>(DifferentMetadata.Targets.size()), DifferentMetadata.VertexCount};

    ASSERT_FALSE(SourceA.MakeCacheKey().empty());
    EXPECT_EQ(SourceA.MakeCacheKey(), SourceB.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceWithDifferentDeltas.MakeCacheKey());
    EXPECT_NE(SourceA.MakeCacheKey(), SourceWithDifferentMetadata.MakeCacheKey());
}

TEST(RadientMorphTargetSourceTest, RejectsInvalidBlobRangesAndReleasesReadAccess)
{
    MorphTargetSourceData Data;
    const Uint64          DataSize = sizeof(Data.PositionDeltas);
    const auto            Check    = [&Data](const void* pBytes, Uint64 BlobSize, Uint64 ByteOffset, RADIENT_STATUS ExpectedStatus) {
        RefCntAutoPtr<ScopeCheckedMorphBlob> pBlob{MakeNewRCObj<ScopeCheckedMorphBlob>()(pBytes, BlobSize)};
        Data.AttributeData[0].pDeltaBuffer = pBlob;
        Data.AttributeData[0].ByteOffset   = ByteOffset;
        RadientMorphTargetSource Source{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
        EXPECT_EQ(Source.GetStatus(), ExpectedStatus);
        EXPECT_EQ(pBlob->ReaderCount, 0u);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
        EXPECT_EQ(Source.PackData({}), ExpectedStatus);
    };

    Check(nullptr, 0, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(nullptr, DataSize, 0, RADIENT_STATUS_INVALID_DATA);
    Check(Data.PositionDeltas.data(), DataSize - 1, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(Data.PositionDeltas.data(), DataSize, 1, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(Data.PositionDeltas.data(), DataSize, DataSize, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(Data.PositionDeltas.data(), DataSize, DataSize + 1, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(Data.PositionDeltas.data(), DataSize, (std::numeric_limits<Uint64>::max)(), RADIENT_STATUS_INVALID_ARGUMENT);
    Check(Data.PositionDeltas.data(), (std::numeric_limits<Uint64>::max)(),
          (std::numeric_limits<Uint64>::max)() - DataSize + 1, RADIENT_STATUS_INVALID_ARGUMENT);
    if (sizeof(size_t) < sizeof(Uint64))
    {
        Uint64 UnaddressableOffset = (std::numeric_limits<size_t>::max)();
        ++UnaddressableOffset;
        Check(Data.PositionDeltas.data(), UnaddressableOffset + DataSize,
              UnaddressableOffset, RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientMorphTargetSourceTest, PacksSharedUnalignedBlobRangesInTargetOrder)
{
    MorphTargetSourceData Packed;
    MorphTargetSourceData Padded;
    Packed.Targets[1].Desc.pAttributes    = Packed.Attributes.data();
    Packed.Targets[1].Desc.AttributeCount = 1;
    Packed.Targets[1].pAttributeData      = Packed.AttributeData.data();
    Padded.Targets[1].Desc.pAttributes    = Padded.Attributes.data();
    Padded.Targets[1].Desc.AttributeCount = 1;
    Padded.Targets[1].pAttributeData      = Padded.AttributeData.data();

    // Physical range order differs from target order; neither padding nor alignment is part of the data.
    constexpr size_t                                                      CustomOffset   = 1;
    constexpr size_t                                                      PositionOffset = CustomOffset + sizeof(Padded.CustomDeltas) + 2;
    std::array<Uint8, PositionOffset + sizeof(Padded.PositionDeltas) + 5> Bytes;
    Bytes.fill(0xCD);
    std::memcpy(Bytes.data() + PositionOffset, Padded.PositionDeltas.data(), sizeof(Padded.PositionDeltas));
    std::memcpy(Bytes.data() + CustomOffset, Padded.CustomDeltas.data(), sizeof(Padded.CustomDeltas));
    RefCntAutoPtr<ScopeCheckedMorphBlob> pBlob{MakeNewRCObj<ScopeCheckedMorphBlob>()(Bytes.data(), Bytes.size())};
    Padded.AttributeData[0].pDeltaBuffer = pBlob;
    Padded.AttributeData[0].ByteOffset   = PositionOffset;
    Padded.AttributeData[1].pDeltaBuffer = pBlob;
    Padded.AttributeData[1].ByteOffset   = CustomOffset;

    const RadientMorphTargetSource PackedSource{Packed.Targets.data(), static_cast<Uint32>(Packed.Targets.size()), Packed.VertexCount};
    ASSERT_EQ(PackedSource.GetStatus(), RADIENT_STATUS_OK);
    std::vector<Uint8> Expected(PackedSource.GetDataSize());
    ASSERT_EQ(PackedSource.PackData({Expected.data(), static_cast<Uint32>(Expected.size())}), RADIENT_STATUS_OK);
    {
        const RadientMorphTargetSource PaddedSource{Padded.Targets.data(), static_cast<Uint32>(Padded.Targets.size()), Padded.VertexCount};
        ASSERT_EQ(PaddedSource.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_GT(pBlob->ReaderCount, 0u);
        Padded.Targets       = {};
        Padded.Attributes    = {};
        Padded.AttributeData = {};

        EXPECT_EQ(PaddedSource.GetDataSize(), 2 * sizeof(Packed.PositionDeltas) + sizeof(Packed.CustomDeltas));
        ASSERT_EQ(PaddedSource.GetTarget(1).Attributes.size(), 1u);
        EXPECT_EQ(PaddedSource.GetTarget(1).Attributes[0].DataOffset, sizeof(Packed.PositionDeltas) + sizeof(Packed.CustomDeltas));
        std::vector<Uint8> Actual(PaddedSource.GetDataSize());
        ASSERT_EQ(PaddedSource.PackData({Actual.data(), static_cast<Uint32>(Actual.size())}), RADIENT_STATUS_OK);
        EXPECT_EQ(Actual, Expected);
        EXPECT_EQ(PaddedSource.MakeCacheKey(), PackedSource.MakeCacheKey());
        EXPECT_GT(pBlob->ReaderCount, 0u);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
}

TEST(RadientMorphTargetSourceTest, RetainsMutableBlobReadAccessUntilAllSourcesAreDestroyed)
{
    MorphTargetSourceData                  Data;
    Uint32                                 ReadReleases = 0;
    RefCntAutoPtr<IRadientMutableDataBlob> pBlob        = MakeTestMutableDataBlob(
        Data.PositionDeltas.data(), sizeof(Data.PositionDeltas), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    Data.AttributeData[0].pDeltaBuffer               = pBlob;
    std::unique_ptr<RadientMorphTargetSource> Source = std::make_unique<RadientMorphTargetSource>(
        Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount);
    std::unique_ptr<RadientMorphTargetSource> OtherSource = std::make_unique<RadientMorphTargetSource>(
        Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount);
    ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(OtherSource->GetStatus(), RADIENT_STATUS_OK);

    void* pWrite = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(ReadReleases, 0u);
    Source.reset();
    EXPECT_EQ(ReadReleases, 0u);
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_INVALID_OPERATION);
    OtherSource.reset();
    EXPECT_EQ(ReadReleases, 1u);
    ASSERT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_OK);
}

TEST(RadientMorphTargetSourceTest, RetainsReferencedBlobOwnerAfterCallerReleasesIt)
{
    using DeltaArray = std::array<Float32, 9>;
    const DeltaArray                          Expected{0.f, 0.f, 0.f, 0.1f, 0.2f, 0.3f, -0.1f, -0.2f, -0.3f};
    std::weak_ptr<DeltaArray>                 WeakOwner;
    std::unique_ptr<RadientMorphTargetSource> Source;
    {
        const std::shared_ptr<DeltaArray> Deltas           = std::make_shared<DeltaArray>(Expected);
        WeakOwner                                          = Deltas;
        std::unique_ptr<std::shared_ptr<DeltaArray>> Owner = std::make_unique<std::shared_ptr<DeltaArray>>(Deltas);
        RadientDataBlobCreateInfo                    BlobCI;
        BlobCI.Size      = sizeof(*Deltas);
        BlobCI.pData     = Deltas->data();
        BlobCI.pUserData = Owner.get();
        BlobCI.OnDestroy = [](void* pContext) {
            delete static_cast<std::shared_ptr<DeltaArray>*>(pContext);
        };
        RefCntAutoPtr<IRadientDataBlob> pBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
        Owner.release();

        MorphTargetSourceData Data;
        Data.AttributeData[0].pDeltaBuffer  = pBlob;
        Data.Targets[0].Desc.AttributeCount = 1;
        Source                              = std::make_unique<RadientMorphTargetSource>(Data.Targets.data(), 1, Data.VertexCount);
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
    }

    ASSERT_FALSE(WeakOwner.expired());
    DeltaArray Actual{};
    ASSERT_EQ(Source->PackData({Actual.data(), sizeof(Actual)}), RADIENT_STATUS_OK);
    EXPECT_EQ(Actual, Expected);
    EXPECT_FALSE(Source->MakeCacheKey().empty());
    Source.reset();
    EXPECT_TRUE(WeakOwner.expired());
}

TEST(RadientMorphTargetSourceTest, RejectsBusyOrShortBlobAndReleasesEarlierReads)
{
    MorphTargetSourceData                        Data;
    Uint32                                       ReadReleases = 0;
    const RefCntAutoPtr<IRadientMutableDataBlob> pPositions   = MakeTestMutableDataBlob(
        Data.PositionDeltas.data(), sizeof(Data.PositionDeltas), CountBlobReadReleases, &ReadReleases);
    const RefCntAutoPtr<IRadientMutableDataBlob> pCustom = MakeTestMutableDataBlob(Data.CustomDeltas.data(), sizeof(Data.CustomDeltas));
    ASSERT_NE(pPositions, nullptr);
    ASSERT_NE(pCustom, nullptr);
    Data.AttributeData[0].pDeltaBuffer = pPositions;
    Data.AttributeData[1].pDeltaBuffer = pCustom;

    void* pWrite = nullptr;
    ASSERT_EQ(pCustom->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    RadientMorphTargetSource BusySource{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(BusySource.GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(BusySource.MakeCacheKey().empty());
    EXPECT_EQ(BusySource.PackData({}), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(ReadReleases, 1u);
    ASSERT_EQ(pPositions->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    EXPECT_EQ(pPositions->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(pCustom->EndWrite(), RADIENT_STATUS_OK);

    ASSERT_EQ(pCustom->Resize(sizeof(Data.CustomDeltas) - 1), RADIENT_STATUS_OK);
    RadientMorphTargetSource ShortSource{Data.Targets.data(), static_cast<Uint32>(Data.Targets.size()), Data.VertexCount};
    EXPECT_EQ(ShortSource.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(ReadReleases, 2u);
    EXPECT_EQ(pPositions->Resize(0), RADIENT_STATUS_OK);
    EXPECT_EQ(pCustom->Resize(0), RADIENT_STATUS_OK);
}
