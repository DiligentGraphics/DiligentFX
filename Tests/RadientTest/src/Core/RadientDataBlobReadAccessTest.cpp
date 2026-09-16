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

#include "Core/RadientDataBlobReadAccess.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <type_traits>
#include <utility>

using namespace Diligent;

namespace
{

static_assert(!std::is_copy_constructible<RadientDataBlobReadAccess>::value, "Read scopes cannot be copied");
static_assert(!std::is_copy_assignable<RadientDataBlobReadAccess>::value, "Read scopes cannot be copied");
static_assert(std::is_nothrow_move_constructible<RadientDataBlobReadAccess>::value, "Moving read scopes must not throw");
static_assert(std::is_nothrow_move_assignable<RadientDataBlobReadAccess>::value, "Moving read scopes must not throw");
static_assert(std::is_nothrow_destructible<RadientDataBlobReadAccess>::value, "Destroying read scopes must not throw");
static_assert(noexcept(std::declval<RadientDataBlobReadAccess&>().Reset()), "Resetting read scopes must not throw");

struct CallbackCounts
{
    int Reads        = 0;
    int Destructions = 0;
};

RadientDataBlobCreateInfo MakeCreateInfo(CallbackCounts& Counts, Uint64 Size = 4, const void* pData = nullptr)
{
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Size;
    CI.pData                = pData;
    CI.pUserData            = &Counts;
    CI.OnLastReaderReleased = [](IRadientDataBlob*, void* pContext) {
        ++static_cast<CallbackCounts*>(pContext)->Reads;
    };
    CI.OnDestroy = [](void* pContext) {
        ++static_cast<CallbackCounts*>(pContext)->Destructions;
    };
    return CI;
}

RefCntAutoPtr<IRadientMutableDataBlob> MakeMutableBlob(CallbackCounts& Counts)
{
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    EXPECT_EQ(CreateRadientMutableDataBlob(MakeCreateInfo(Counts), &Blob), RADIENT_STATUS_OK);
    return Blob;
}

void ExpectInactive(const RadientDataBlobReadAccess& Scope)
{
    EXPECT_FALSE(Scope);
    EXPECT_EQ(Scope.GetData(), nullptr);
    EXPECT_EQ(Scope.GetSize(), 0u);
}

enum class BlobKind
{
    Copy,
    Reference,
    Mutable
};

RefCntAutoPtr<IRadientDataBlob> MakeBlob(BlobKind Kind, const RadientDataBlobCreateInfo& CI)
{
    if (Kind == BlobKind::Mutable)
    {
        RefCntAutoPtr<IRadientMutableDataBlob> Blob;
        EXPECT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
        return RefCntAutoPtr<IRadientDataBlob>{Blob};
    }

    RefCntAutoPtr<IRadientDataBlob> Blob;
    const auto                      Mode = Kind == BlobKind::Reference ? RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE : RADIENT_DATA_BLOB_STORAGE_MODE_COPY;
    EXPECT_EQ(CreateRadientDataBlob(CI, Mode, &Blob), RADIENT_STATUS_OK);
    return Blob;
}

} // namespace

TEST(RadientDataBlobReadAccessTest, DefaultAndNullScopesAreInactive)
{
    RadientDataBlobReadAccess Default;
    ExpectInactive(Default);
    EXPECT_EQ(Default.Reset(), RADIENT_STATUS_NO_CHANGE);
    ExpectInactive(Default);

    RadientDataBlobReadAccess Null{nullptr};
    ExpectInactive(Null);
    EXPECT_EQ(Null.Reset(), RADIENT_STATUS_NO_CHANGE);
    ExpectInactive(Null);
}

TEST(RadientDataBlobReadAccessTest, FailedAcquisitionDoesNotRetainBlobOrReleaseWriter)
{
    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    void* Data = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
    RadientDataBlobReadAccess Failed{Blob};
    ExpectInactive(Failed);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Counts.Reads, 0);

    // The unsuccessful scope must not keep the object alive.
    Blob.Release();
    EXPECT_EQ(Counts.Destructions, 1);
    EXPECT_EQ(Failed.Reset(), RADIENT_STATUS_NO_CHANGE);
}

class RadientDataBlobReadAccessStorageTest : public testing::TestWithParam<BlobKind>
{};

TEST_P(RadientDataBlobReadAccessStorageTest, AcquiresDataAndReleasesExactlyOneReader)
{
    const std::array<Uint8, 4> Bytes{{3, 17, 29, 255}};
    CallbackCounts             Counts;
    auto                       Blob = MakeBlob(GetParam(), MakeCreateInfo(Counts, Bytes.size(), Bytes.data()));
    ASSERT_NE(Blob, nullptr);
    {
        RadientDataBlobReadAccess Scope{Blob};
        ASSERT_TRUE(Scope);
        EXPECT_EQ(Scope.GetSize(), Bytes.size());
        ASSERT_NE(Scope.GetData(), nullptr);
        EXPECT_EQ(std::memcmp(Scope.GetData(), Bytes.data(), Bytes.size()), 0);
        if (GetParam() == BlobKind::Reference)
            EXPECT_EQ(Scope.GetData(), Bytes.data());
        else
            EXPECT_NE(Scope.GetData(), Bytes.data());

        RadientDataBlobReadAccess Other{Blob};
        ASSERT_TRUE(Other);
        EXPECT_EQ(Other.GetData(), Scope.GetData());
        EXPECT_EQ(Other.Reset(), RADIENT_STATUS_OK);
        ExpectInactive(Other);
        EXPECT_EQ(Other.Reset(), RADIENT_STATUS_NO_CHANGE);
        EXPECT_EQ(Counts.Reads, 0);
    }
    EXPECT_EQ(Counts.Reads, 1);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
}

TEST_P(RadientDataBlobReadAccessStorageTest, EmptyBlobHasSuccessfulScopeWithNullData)
{
    CallbackCounts Counts;
    auto           Blob = MakeBlob(GetParam(), MakeCreateInfo(Counts, 0));
    ASSERT_NE(Blob, nullptr);
    {
        RadientDataBlobReadAccess Scope{Blob};
        EXPECT_TRUE(Scope);
        EXPECT_EQ(Scope.GetData(), nullptr);
        EXPECT_EQ(Scope.GetSize(), 0u);
        EXPECT_EQ(Counts.Reads, 0);
    }
    EXPECT_EQ(Counts.Reads, 1);
}

TEST_P(RadientDataBlobReadAccessStorageTest, KeepsBlobAliveAfterLastExternalReferenceIsReleased)
{
    const std::array<Uint8, 4> Bytes{{11, 12, 13, 14}};
    CallbackCounts             Counts;
    auto                       Blob = MakeBlob(GetParam(), MakeCreateInfo(Counts, Bytes.size(), Bytes.data()));
    ASSERT_NE(Blob, nullptr);
    {
        RadientDataBlobReadAccess Scope{Blob};
        ASSERT_TRUE(Scope);
        Blob.Release();
        EXPECT_EQ(Counts.Destructions, 0);
        EXPECT_EQ(Scope.GetSize(), Bytes.size());
        EXPECT_EQ(std::memcmp(Scope.GetData(), Bytes.data(), Bytes.size()), 0);
    }
    EXPECT_EQ(Counts.Reads, 1);
    EXPECT_EQ(Counts.Destructions, 1);
}

INSTANTIATE_TEST_SUITE_P(StorageModes, RadientDataBlobReadAccessStorageTest, testing::Values(BlobKind::Copy, BlobKind::Reference, BlobKind::Mutable));

TEST(RadientDataBlobReadAccessTest, MutableBlobExcludesWritesAndResizingUntilReset)
{
    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    RadientDataBlobReadAccess Scope{Blob};
    ASSERT_TRUE(Scope);
    void* Data = nullptr;
    EXPECT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Scope.GetSize(), 4u);
    EXPECT_EQ(Scope.Reset(), RADIENT_STATUS_OK);
    ExpectInactive(Scope);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Counts.Reads, 1);
}

TEST(RadientDataBlobReadAccessTest, MoveConstructionTransfersOneReader)
{
    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    RadientDataBlobReadAccess Source{Blob};
    ASSERT_TRUE(Source);
    const void* Data = Source.GetData();
    {
        RadientDataBlobReadAccess Target{std::move(Source)};
        ExpectInactive(Source);
        EXPECT_EQ(Source.Reset(), RADIENT_STATUS_NO_CHANGE);
        EXPECT_TRUE(Target);
        EXPECT_EQ(Target.GetData(), Data);
        EXPECT_EQ(Target.GetSize(), 4u);
        EXPECT_EQ(Counts.Reads, 0);
    }
    EXPECT_EQ(Counts.Reads, 1);
}

TEST(RadientDataBlobReadAccessTest, MoveAssignmentReleasesPreviousReader)
{
    CallbackCounts OldCounts;
    CallbackCounts NewCounts;
    auto           OldBlob = MakeMutableBlob(OldCounts);
    auto           NewBlob = MakeMutableBlob(NewCounts);
    ASSERT_NE(OldBlob, nullptr);
    ASSERT_NE(NewBlob, nullptr);
    RadientDataBlobReadAccess Target{OldBlob};
    RadientDataBlobReadAccess Source{NewBlob};
    ASSERT_TRUE(Target);
    ASSERT_TRUE(Source);
    const void* Data = Source.GetData();

    Target = std::move(Source);
    ExpectInactive(Source);
    EXPECT_TRUE(Target);
    EXPECT_EQ(Target.GetData(), Data);
    EXPECT_EQ(Target.GetSize(), 4u);
    EXPECT_EQ(OldCounts.Reads, 1);
    EXPECT_EQ(NewCounts.Reads, 0);
    void* WriteData = nullptr;
    EXPECT_EQ(OldBlob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(OldBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(NewBlob->BeginWrite(&WriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Target.Reset(), RADIENT_STATUS_OK);
    EXPECT_EQ(NewCounts.Reads, 1);
}

TEST(RadientDataBlobReadAccessTest, MoveAssignmentOnSameBlobAndSelfMoveKeepOneReader)
{
    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    RadientDataBlobReadAccess Target{Blob};
    RadientDataBlobReadAccess Source{Blob};
    ASSERT_TRUE(Target);
    ASSERT_TRUE(Source);
    const void* Data = Target.GetData();

    Target = std::move(Source);
    ExpectInactive(Source);
    EXPECT_EQ(Counts.Reads, 0);
    RadientDataBlobReadAccess& Self = Target;
    Target                          = std::move(Self);
    EXPECT_TRUE(Target);
    EXPECT_EQ(Target.GetData(), Data);
    EXPECT_EQ(Target.GetSize(), 4u);
    EXPECT_EQ(Counts.Reads, 0);
    EXPECT_EQ(Target.Reset(), RADIENT_STATUS_OK);
    EXPECT_EQ(Counts.Reads, 1);
}

TEST(RadientDataBlobReadAccessTest, MovingInactiveScopeReleasesTargetAndEmptiesSource)
{
    RadientDataBlobReadAccess Failed{nullptr};
    RadientDataBlobReadAccess Moved{std::move(Failed)};
    ExpectInactive(Failed);
    ExpectInactive(Moved);

    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    RadientDataBlobReadAccess Target{Blob};
    ASSERT_TRUE(Target);
    Target = std::move(Moved);
    ExpectInactive(Moved);
    ExpectInactive(Target);
    EXPECT_EQ(Counts.Reads, 1);
    EXPECT_EQ(Target.Reset(), RADIENT_STATUS_NO_CHANGE);
    ExpectInactive(Target);
}

TEST(RadientDataBlobReadAccessTest, ExceptionUnwindingReleasesReadAccess)
{
    CallbackCounts Counts;
    auto           Blob = MakeMutableBlob(Counts);
    ASSERT_NE(Blob, nullptr);
    EXPECT_THROW(
        [&] {
            RadientDataBlobReadAccess Scope{Blob};
            EXPECT_TRUE(Scope);
            throw 42;
        }(),
        int);
    EXPECT_EQ(Counts.Reads, 1);
    void* Data = nullptr;
    EXPECT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobReadAccessTest, CallbackFailureIsReturnedByResetAndIgnoredByDestructor)
{
    CallbackCounts Counts;
    auto           CI       = MakeCreateInfo(Counts);
    CI.OnLastReaderReleased = [](IRadientDataBlob*, void* pContext) {
        ++static_cast<CallbackCounts*>(pContext)->Reads;
        throw 1;
    };
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    {
        RadientDataBlobReadAccess Scope{Blob};
        ASSERT_TRUE(Scope);
        EXPECT_EQ(Scope.Reset(), RADIENT_STATUS_FAILED);
        ExpectInactive(Scope);
        EXPECT_EQ(Scope.Reset(), RADIENT_STATUS_NO_CHANGE);
        EXPECT_EQ(Counts.Reads, 1);
        void* Data = nullptr;
        EXPECT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
        EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    }
    EXPECT_EQ(Counts.Reads, 1);
    EXPECT_NO_THROW({
        RadientDataBlobReadAccess Scope{Blob};
        EXPECT_TRUE(Scope);
    });
    EXPECT_EQ(Counts.Reads, 2);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobReadAccessTest, ResetClearsStateBeforeReentrantCallback)
{
    struct CallbackContext
    {
        RadientDataBlobReadAccess* Scope = nullptr;
        int                        Calls = 0;
    } Context;
    RadientDataBlobCreateInfo CI;
    CI.Size                 = 4;
    CI.pUserData            = &Context;
    CI.OnLastReaderReleased = [](IRadientDataBlob*, void* pContext) {
        auto& Context = *static_cast<CallbackContext*>(pContext);
        ++Context.Calls;
        ExpectInactive(*Context.Scope);
        EXPECT_EQ(Context.Scope->Reset(), RADIENT_STATUS_NO_CHANGE);
    };
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    RadientDataBlobReadAccess Scope{Blob};
    ASSERT_TRUE(Scope);
    Context.Scope = &Scope;
    EXPECT_EQ(Scope.Reset(), RADIENT_STATUS_OK);
    ExpectInactive(Scope);
    EXPECT_EQ(Context.Calls, 1);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobReadAccessTest, MoveAssignmentPublishesNewStateBeforeReentrantRelease)
{
    struct CallbackContext
    {
        RadientDataBlobReadAccess* Target = nullptr;
        RadientDataBlobReadAccess* Source = nullptr;
        const void*                Data   = nullptr;
        int                        Calls  = 0;
    } Context;
    RadientDataBlobCreateInfo CI;
    CI.Size                 = 1;
    CI.pUserData            = &Context;
    CI.OnLastReaderReleased = [](IRadientDataBlob*, void* pContext) {
        auto& Context = *static_cast<CallbackContext*>(pContext);
        ++Context.Calls;
        ExpectInactive(*Context.Source);
        EXPECT_TRUE(*Context.Target);
        EXPECT_EQ(Context.Target->GetData(), Context.Data);
        EXPECT_EQ(Context.Target->GetSize(), 4u);
        EXPECT_EQ(Context.Target->Reset(), RADIENT_STATUS_OK);
    };
    RefCntAutoPtr<IRadientMutableDataBlob> OldBlob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &OldBlob), RADIENT_STATUS_OK);
    CallbackCounts NewCounts;
    auto           NewBlob = MakeMutableBlob(NewCounts);
    ASSERT_NE(NewBlob, nullptr);
    RadientDataBlobReadAccess Target{OldBlob};
    RadientDataBlobReadAccess Source{NewBlob};
    ASSERT_TRUE(Target);
    ASSERT_TRUE(Source);
    Context.Target = &Target;
    Context.Source = &Source;
    Context.Data   = Source.GetData();

    Target = std::move(Source);
    ExpectInactive(Target);
    ExpectInactive(Source);
    EXPECT_EQ(Context.Calls, 1);
    EXPECT_EQ(NewCounts.Reads, 1);
}
