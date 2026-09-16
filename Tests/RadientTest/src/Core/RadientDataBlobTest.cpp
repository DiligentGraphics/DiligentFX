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

#include "RadientDataBlob.h"

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

using namespace Diligent;

extern "C" int RadientDataBlob_C_TestAccess(void);

namespace
{

RefCntAutoPtr<IRadientMutableDataBlob> MakeMutableBlob(Uint64                                 Size      = 16,
                                                       RadientDataBlobReadReleaseCallbackType Callback  = nullptr,
                                                       void*                                  pUserData = nullptr)
{
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Size;
    CI.OnLastReaderReleased = Callback;
    CI.pUserData            = pUserData;
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    return Blob;
}

void CountNotification(IRadientDataBlob*, void* pUserData)
{
    ++*static_cast<std::atomic<int>*>(pUserData);
}

// Coordinates contenders without sleeps or scheduler-dependent timing.
struct AccessGate
{
    std::mutex              Mutex;
    std::condition_variable Changed;
    int                     Arrived = 0;
    bool                    Proceed = false;

    void ArriveAndWait()
    {
        std::unique_lock<std::mutex> Lock{Mutex};
        ++Arrived;
        Changed.notify_all();
        Changed.wait(Lock, [this] { return Proceed; });
    }

    bool WaitFor(int Count)
    {
        std::unique_lock<std::mutex> Lock{Mutex};
        return Changed.wait_for(Lock, std::chrono::seconds{5}, [this, Count] { return Arrived == Count; });
    }

    void Release()
    {
        std::lock_guard<std::mutex> Lock{Mutex};
        Proceed = true;
        Changed.notify_all();
    }
};

} // namespace

TEST(RadientDataBlobTest, CreatesEmptyBlobAndRejectsInvalidCreation)
{
    RadientDataBlobCreateInfo CI;
    EXPECT_EQ(CI.Size, 0u);
    EXPECT_EQ(CI.pData, nullptr);
    EXPECT_EQ(CI.OnLastReaderReleased, nullptr);
    EXPECT_EQ(CI.pUserData, nullptr);
    EXPECT_EQ(CI.OnDestroy, nullptr);
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    auto Blob = MakeMutableBlob(0);
    ASSERT_NE(Blob, nullptr);
    EXPECT_EQ(Blob->GetSize(), 0u);
    int   Dummy;
    void* WriteData = &Dummy;
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(WriteData, nullptr);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    const void* ReadData = &Dummy;
    EXPECT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadData, nullptr);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);

    CI.Size = (std::numeric_limits<Uint64>::max)();
    RefCntAutoPtr<IRadientMutableDataBlob> Invalid;
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Invalid, nullptr);
}

TEST(RadientDataBlobTest, CopiesInitialDataDuringCreation)
{
    const std::array<Uint8, 5>             Expected{{3, 17, 29, 128, 255}};
    std::atomic<int>                       Calls{0};
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    {
        auto                      InitialData = Expected;
        RadientDataBlobCreateInfo CI;
        CI.Size                 = InitialData.size();
        CI.pData                = InitialData.data();
        CI.OnLastReaderReleased = CountNotification;
        CI.pUserData            = &Calls;
        ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
        InitialData.fill(0);
        CI = {};
    }
    ASSERT_NE(Blob, nullptr);
    EXPECT_EQ(Blob->GetSize(), Expected.size());
    EXPECT_EQ(Calls, 0);
    // Initialization leaves the blob available for exclusive access.
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(WriteData, Expected.data(), Expected.size()), 0);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Calls, 0);
    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(ReadData, Expected.data(), Expected.size()), 0);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Calls, 1);

    // A non-null initial-data pointer is ignored for empty allocations.
    RadientDataBlobCreateInfo EmptyCI;
    EmptyCI.pData = Expected.data();
    RefCntAutoPtr<IRadientMutableDataBlob> Empty;
    ASSERT_EQ(CreateRadientMutableDataBlob(EmptyCI, &Empty), RADIENT_STATUS_OK);
    EXPECT_EQ(Empty->GetSize(), 0u);
    ASSERT_EQ(Empty->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadData, nullptr);
    EXPECT_EQ(Empty->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, ResizePreservesPrefixAndZeroInitializesNewBytes)
{
    const std::array<Uint8, 4> InitialData{{3, 17, 29, 255}};
    RadientDataBlobCreateInfo  CI;
    CI.Size  = InitialData.size();
    CI.pData = InitialData.data();
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientDataBlob> Base{Blob, IID_RadientDataBlob};
    ASSERT_NE(Base, nullptr);

    EXPECT_EQ(Blob->Resize(InitialData.size()), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
    const void* ReadData = nullptr;
    ASSERT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->GetSize(), 8u);
    const std::array<Uint8, 8> Grown{{3, 17, 29, 255, 0, 0, 0, 0}};
    EXPECT_EQ(std::memcmp(ReadData, Grown.data(), Grown.size()), 0);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);

    ASSERT_EQ(Blob->Resize(2), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->GetSize(), 2u);
    ASSERT_EQ(Blob->Resize(4), RADIENT_STATUS_OK);
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->GetSize(), 4u);
    const std::array<Uint8, 4> Regrown{{3, 17, 0, 0}};
    EXPECT_EQ(std::memcmp(WriteData, Regrown.data(), Regrown.size()), 0);
    static_cast<Uint8*>(WriteData)[3] = 42;
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    ASSERT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(static_cast<const Uint8*>(ReadData)[3], 42);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);

    ASSERT_EQ(Blob->Resize(0), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->GetSize(), 0u);
    EXPECT_EQ(Blob->Resize(0), RADIENT_STATUS_OK);
    ASSERT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadData, nullptr);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(WriteData, nullptr);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);

    ASSERT_EQ(Blob->Resize(4), RADIENT_STATUS_OK);
    ASSERT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->GetSize(), 4u);
    const std::array<Uint8, 4> Zeros{};
    EXPECT_EQ(std::memcmp(ReadData, Zeros.data(), Zeros.size()), 0);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, ResizeRejectsEveryActiveAccessScope)
{
    auto Blob = MakeMutableBlob(4);
    ASSERT_NE(Blob, nullptr);
    RefCntAutoPtr<IRadientDataBlob> Base{Blob, IID_RadientDataBlob};
    ASSERT_NE(Base, nullptr);
    const void* First  = nullptr;
    const void* Second = nullptr;
    ASSERT_EQ(Base->BeginRead(&First), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->BeginRead(&Second), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize(4), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(0), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Base->GetSize(), 4u);
    const std::array<Uint8, 4> Zeros{};
    EXPECT_EQ(std::memcmp(First, Zeros.data(), Zeros.size()), 0);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(std::memcmp(Second, Zeros.data(), Zeros.size()), 0);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);

    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    static_cast<Uint8*>(WriteData)[0] = 73;
    EXPECT_EQ(Blob->Resize(4), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(0), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Base->GetSize(), 4u);
    EXPECT_EQ(static_cast<const Uint8*>(WriteData)[0], 73);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);

    ASSERT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
    ASSERT_EQ(Base->BeginRead(&First), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->GetSize(), 8u);
    EXPECT_EQ(static_cast<const Uint8*>(First)[0], 73);
    EXPECT_EQ(Base->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, InvalidResizePreservesDataAndAccessState)
{
    auto Blob = MakeMutableBlob(4);
    ASSERT_NE(Blob, nullptr);
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    const std::array<Uint8, 4> Expected{{1, 2, 3, 4}};
    std::memcpy(WriteData, Expected.data(), Expected.size());
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);

    EXPECT_EQ(Blob->Resize((std::numeric_limits<Uint64>::max)()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->GetSize(), Expected.size());
    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(ReadData, Expected.data(), Expected.size()), 0);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, LastReaderCallbackCanResizeWithoutAdditionalNotifications)
{
    struct CallbackCounts
    {
        int Reads        = 0;
        int Destructions = 0;
    } Counts;
    RadientDataBlobCreateInfo CI;
    CI.Size                 = 1;
    CI.pUserData            = &Counts;
    CI.OnLastReaderReleased = [](IRadientDataBlob* pBlob, void* pContext) {
        ++static_cast<CallbackCounts*>(pContext)->Reads;
        RefCntAutoPtr<IRadientMutableDataBlob> Mutable{pBlob, IID_RadientMutableDataBlob};
        ASSERT_NE(Mutable, nullptr);
        ASSERT_EQ(Mutable->Resize(8), RADIENT_STATUS_OK);
        void* WriteData = nullptr;
        ASSERT_EQ(Mutable->BeginWrite(&WriteData), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->GetSize(), 8u);
        static_cast<Uint8*>(WriteData)[0] = 42;
        EXPECT_EQ(Mutable->EndWrite(), RADIENT_STATUS_OK);
    };
    CI.OnDestroy = [](void* pContext) {
        ++static_cast<CallbackCounts*>(pContext)->Destructions;
    };
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->Resize(2), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize((std::numeric_limits<Uint64>::max)()), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Counts.Reads, 0);
    EXPECT_EQ(Counts.Destructions, 0);
    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->Resize(0), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Counts.Reads, 1);
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->GetSize(), 8u);
    EXPECT_EQ(static_cast<const Uint8*>(WriteData)[0], 42);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->Resize(0), RADIENT_STATUS_OK);
    EXPECT_EQ(Counts.Reads, 1);
    EXPECT_EQ(Counts.Destructions, 0);
    Blob.Release();
    EXPECT_EQ(Counts.Destructions, 1);
}

TEST(RadientDataBlobTest, SharesReadsAndExcludesWrites)
{
    auto Blob = MakeMutableBlob();
    ASSERT_NE(Blob, nullptr);
    EXPECT_EQ(Blob->GetSize(), 16u);
    const void* First = nullptr;
    ASSERT_EQ(Blob->BeginRead(&First), RADIENT_STATUS_OK);
    const std::array<Uint8, 16> Zeros{};
    EXPECT_EQ(std::memcmp(First, Zeros.data(), Zeros.size()), 0);
    const void* Second = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Second), RADIENT_STATUS_OK);
    EXPECT_EQ(First, Second);
    int   Dummy;
    void* WriteData = &Dummy;
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(WriteData, nullptr);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);

    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    const std::array<Uint8, 16> Values{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    std::memcpy(WriteData, Values.data(), Values.size());
    const void* RejectedRead = WriteData;
    EXPECT_EQ(Blob->BeginRead(&RejectedRead), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RejectedRead, nullptr);
    void* RejectedWrite = WriteData;
    EXPECT_EQ(Blob->BeginWrite(&RejectedWrite), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RejectedWrite, nullptr);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->GetSize(), 16u);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_INVALID_OPERATION);
    ASSERT_EQ(Blob->BeginRead(&First), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(First, Values.data(), Values.size()), 0);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, NullOutputsDoNotChangeAccessState)
{
    auto Blob = MakeMutableBlob();
    ASSERT_NE(Blob, nullptr);
    EXPECT_EQ(Blob->BeginRead(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->BeginWrite(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_INVALID_OPERATION);
    void* Data = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->BeginRead(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->BeginWrite(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->BeginRead(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientDataBlobTest, CopiesRegistrationAndNotifiesOncePerReadCycle)
{
    std::atomic<int>                       Calls{0};
    RadientDataBlobCreateInfo              CI{16, nullptr, CountNotification, &Calls};
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Blob), RADIENT_STATUS_OK);
    CI = {};
    EXPECT_EQ(Calls, 0);
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(Calls, 0);
    for (int Cycle = 0; Cycle < 3; ++Cycle)
    {
        const void* Data = nullptr;
        ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
        ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
        EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
        EXPECT_EQ(Calls, Cycle);
        EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
        EXPECT_EQ(Calls, Cycle + 1);
        EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(Calls, Cycle + 1);
    }
    Blob.Release();
    EXPECT_EQ(Calls, 3);
}

TEST(RadientDataBlobTest, CallbackCanAcquireWriteAccess)
{
    auto Blob = MakeMutableBlob(1, [](IRadientDataBlob* pBlob, void*) {
        RefCntAutoPtr<IRadientMutableDataBlob> Mutable{pBlob, IID_RadientMutableDataBlob};
        ASSERT_NE(Mutable, nullptr);
        void* Data = nullptr;
        ASSERT_EQ(Mutable->BeginWrite(&Data), RADIENT_STATUS_OK);
        *static_cast<Uint8*>(Data) = 42;
        EXPECT_EQ(Mutable->EndWrite(), RADIENT_STATUS_OK);
    });
    ASSERT_NE(Blob, nullptr);
    const void* Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(*static_cast<const Uint8*>(Data), 42);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, CallbackCanStartAnotherReadCycle)
{
    int  Calls = 0;
    auto Blob  = MakeMutableBlob(
        1, [](IRadientDataBlob* pBlob, void* Context) {
        if (++*static_cast<int*>(Context) == 1)
        {
            const void* Data = nullptr;
            ASSERT_EQ(pBlob->BeginRead(&Data), RADIENT_STATUS_OK);
            EXPECT_EQ(pBlob->EndRead(), RADIENT_STATUS_OK);
        } }, &Calls);
    ASSERT_NE(Blob, nullptr);
    const void* Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Calls, 2);
}

TEST(RadientDataBlobTest, KeepsBlobAliveWhileCallbackReleasesCallerReference)
{
    RefCntAutoPtr<IRadientMutableDataBlob> Blob;
    Blob = MakeMutableBlob(
        16, [](IRadientDataBlob* pBlob, void* Context) {
        static_cast<RefCntAutoPtr<IRadientMutableDataBlob>*>(Context)->Release();
        EXPECT_EQ(pBlob->GetSize(), 16u);
        RefCntAutoPtr<IRadientMutableDataBlob> Mutable{pBlob, IID_RadientMutableDataBlob};
        ASSERT_NE(Mutable, nullptr);
        void* Data = nullptr;
        ASSERT_EQ(Mutable->BeginWrite(&Data), RADIENT_STATUS_OK);
        EXPECT_EQ(Mutable->EndWrite(), RADIENT_STATUS_OK); }, std::addressof(Blob));
    ASSERT_NE(Blob, nullptr);
    RefCntWeakPtr<IRadientMutableDataBlob> Weak{Blob};
    const void*                            Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    IRadientDataBlob* pRawBlob = Blob;
    EXPECT_EQ(pRawBlob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob, nullptr);
    EXPECT_EQ(Weak.Lock(), nullptr);
}

TEST(RadientDataBlobTest, CallbackCanRetainBlobForReuse)
{
    RefCntAutoPtr<IRadientMutableDataBlob> Recycled;

    auto Blob = MakeMutableBlob(
        16, [](IRadientDataBlob* pBlob, void* Context) { *static_cast<RefCntAutoPtr<IRadientMutableDataBlob>*>(Context) = RefCntAutoPtr<IRadientMutableDataBlob>{pBlob, IID_RadientMutableDataBlob}; }, std::addressof(Recycled));
    ASSERT_NE(Blob, nullptr);
    const void* Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    ASSERT_EQ(Recycled, Blob);
    Blob.Release();
    void* WriteData = nullptr;
    ASSERT_EQ(Recycled->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Recycled->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, CallbackFailureLeavesReadAccessReleased)
{
    auto Blob = MakeMutableBlob(1, [](IRadientDataBlob*, void*) { throw 1; });
    ASSERT_NE(Blob, nullptr);
    const void* Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_FAILED);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
    void* WriteData = nullptr;
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, ConcurrentReadersExcludeWriter)
{
    std::atomic<int> Calls{0};
    auto             Blob = MakeMutableBlob(1, CountNotification, &Calls);
    ASSERT_NE(Blob, nullptr);
    void* WriteData = nullptr;
    ASSERT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    *static_cast<Uint8*>(WriteData) = 73;
    ASSERT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
    AccessGate                 Readers;
    std::atomic<int>           SuccessfulReads{0};
    std::array<std::thread, 4> Threads;
    for (auto& Thread : Threads)
        Thread = std::thread{[&] {
            const void* Data   = nullptr;
            const auto  Status = Blob->BeginRead(&Data);
            if (Status == RADIENT_STATUS_OK && *static_cast<const Uint8*>(Data) == 73)
                ++SuccessfulReads;
            Readers.ArriveAndWait();
            if (Status == RADIENT_STATUS_OK)
                EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
        }};
    EXPECT_TRUE(Readers.WaitFor(4));
    EXPECT_EQ(SuccessfulReads, 4);
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Blob->GetSize(), 1u);
    EXPECT_EQ(Calls, 0);
    Readers.Release();
    for (auto& Thread : Threads)
        Thread.join();
    EXPECT_EQ(Calls, 1);
    EXPECT_EQ(Blob->Resize(8), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, OnlyOneConcurrentWriterSucceeds)
{
    auto Blob = MakeMutableBlob();
    ASSERT_NE(Blob, nullptr);
    AccessGate                 Start;
    AccessGate                 Attempts;
    std::atomic<int>           Winners{0};
    std::atomic<int>           Conflicts{0};
    std::array<std::thread, 4> Threads;
    for (auto& Thread : Threads)
        Thread = std::thread{[&] {
            Start.ArriveAndWait();
            void*      Data   = nullptr;
            const auto Status = Blob->BeginWrite(&Data);
            if (Status == RADIENT_STATUS_OK)
                ++Winners;
            else if (Status == RADIENT_STATUS_INVALID_OPERATION)
                ++Conflicts;
            Attempts.ArriveAndWait();
            if (Status == RADIENT_STATUS_OK)
                EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
        }};
    EXPECT_TRUE(Start.WaitFor(4));
    Start.Release();
    EXPECT_TRUE(Attempts.WaitFor(4));
    EXPECT_EQ(Winners, 1);
    EXPECT_EQ(Conflicts, 3);
    Attempts.Release();
    for (auto& Thread : Threads)
        Thread.join();
}

TEST(RadientDataBlobTest, CallbackDoesNotReserveAccessOrHoldInternalLock)
{
    struct CallbackContext
    {
        AccessGate       FirstCallback;
        std::atomic<int> Calls{0};
        RADIENT_STATUS   WriteStatus = RADIENT_STATUS_FAILED;
    } Context;

    auto Blob = MakeMutableBlob(
        1, [](IRadientDataBlob* pBlob, void* Data) {
        auto& Context = *static_cast<CallbackContext*>(Data);
        if (++Context.Calls == 1)
        {
            Context.FirstCallback.ArriveAndWait();
            RefCntAutoPtr<IRadientMutableDataBlob> Mutable{pBlob, IID_RadientMutableDataBlob};
            ASSERT_NE(Mutable, nullptr);
            void* Bytes = nullptr;
            Context.WriteStatus = Mutable->BeginWrite(&Bytes);
            if (Context.WriteStatus == RADIENT_STATUS_OK)
                Mutable->EndWrite();
        } }, &Context);
    ASSERT_NE(Blob, nullptr);
    const void* Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    std::thread EndingThread{
        [&] {
            EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
        }};
    EXPECT_TRUE(Context.FirstCallback.WaitFor(1));
    const auto ReadStatus = Blob->BeginRead(&Data);
    EXPECT_EQ(ReadStatus, RADIENT_STATUS_OK);
    Context.FirstCallback.Release();
    EndingThread.join();
    EXPECT_EQ(Context.WriteStatus, RADIENT_STATUS_INVALID_OPERATION);
    if (ReadStatus == RADIENT_STATUS_OK)
        EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Context.Calls, 2);
}

TEST(RadientDataBlobTest, SupportsQueryInterfaceAndCEntryPoints)
{
    auto Blob = MakeMutableBlob();
    ASSERT_NE(Blob, nullptr);
    RefCntAutoPtr<IRadientDataBlob> Base{Blob, IID_RadientDataBlob};
    ASSERT_NE(Base, nullptr);
    RefCntAutoPtr<IRadientMutableDataBlob> Mutable{Base, IID_RadientMutableDataBlob};
    EXPECT_EQ(Mutable, Blob);
    const void* ReadData = nullptr;
    ASSERT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_OK);
    void* WriteData = nullptr;
    EXPECT_EQ(Mutable->BeginWrite(&WriteData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Mutable->EndRead(), RADIENT_STATUS_OK);
    ASSERT_EQ(Mutable->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Base->BeginRead(&ReadData), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Mutable->EndWrite(), RADIENT_STATUS_OK);
    Blob.Release();
    Mutable.Release();
    EXPECT_EQ(Base->GetSize(), 16u);
    EXPECT_EQ(RadientDataBlob_C_TestAccess(), 0);
}

class RadientReadOnlyDataBlobTest : public testing::TestWithParam<RADIENT_DATA_BLOB_STORAGE_MODE>
{};

TEST_P(RadientReadOnlyDataBlobTest, StorageModeDeterminesWhetherDataIsCopied)
{
    const std::array<Uint8, 5> Data{{3, 17, 29, 128, 255}};
    RadientDataBlobCreateInfo  CI;
    CI.Size  = Data.size();
    CI.pData = Data.data();
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    CI = {};
    EXPECT_EQ(Blob->GetSize(), Data.size());
    RefCntAutoPtr<IRadientDataBlob> Base{Blob, IID_RadientDataBlob};
    EXPECT_EQ(Base, Blob);
    RefCntAutoPtr<IRadientMutableDataBlob> Mutable{Blob, IID_RadientMutableDataBlob};
    EXPECT_EQ(Mutable, nullptr);

    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(ReadData, Data.data(), Data.size()), 0);
    if (GetParam() == RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE)
        EXPECT_EQ(ReadData, Data.data());
    else
        EXPECT_NE(ReadData, Data.data());
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
}

TEST_P(RadientReadOnlyDataBlobTest, EmptyAndNullSourceData)
{
    Uint8                     Data = 42;
    RadientDataBlobCreateInfo CI;
    CI.pData = &Data;
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->GetSize(), 0u);
    const void* ReadData = &Data;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadData, nullptr);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    Blob.Release();

    CI.pData = nullptr;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->GetSize(), 0u);
    Blob.Release();
    CI.Size = 4;
    if (GetParam() == RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE)
    {
        EXPECT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(Blob, nullptr);
    }
    else
    {
        ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
        ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
        const std::array<Uint8, 4> Zeros{};
        EXPECT_EQ(std::memcmp(ReadData, Zeros.data(), Zeros.size()), 0);
        EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
    }
}

TEST_P(RadientReadOnlyDataBlobTest, KeepsBackingOwnerUntilFinalReferenceIsReleased)
{
    struct CallbackCounts
    {
        int Reads        = 0;
        int Destructions = 0;
    } Counts;
    struct BackingOwner
    {
        std::array<Uint8, 4> Bytes{{11, 12, 13, 14}};
        CallbackCounts*      Counts = nullptr;
    };
    auto Owner    = std::make_unique<BackingOwner>();
    Owner->Counts = &Counts;
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Owner->Bytes.size();
    CI.pData                = Owner->Bytes.data();
    CI.pUserData            = Owner.get();
    CI.OnLastReaderReleased = [](IRadientDataBlob*, void* pContext) {
        ++static_cast<BackingOwner*>(pContext)->Counts->Reads;
    };
    CI.OnDestroy = [](void* pContext) {
        auto* Owner = static_cast<BackingOwner*>(pContext);
        ++Owner->Counts->Destructions;
        delete Owner;
    };
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    Owner.release();
    CI = {};
    RefCntAutoPtr<IRadientDataBlob> Retained{Blob};
    Blob.Release();
    EXPECT_EQ(Counts.Destructions, 0);
    for (int Cycle = 0; Cycle < 2; ++Cycle)
    {
        EXPECT_EQ(Retained->BeginRead(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
        const void* First  = nullptr;
        const void* Second = nullptr;
        ASSERT_EQ(Retained->BeginRead(&First), RADIENT_STATUS_OK);
        ASSERT_EQ(Retained->BeginRead(&Second), RADIENT_STATUS_OK);
        EXPECT_EQ(First, Second);
        EXPECT_EQ(*static_cast<const Uint8*>(First), 11);
        EXPECT_EQ(Retained->EndRead(), RADIENT_STATUS_OK);
        EXPECT_EQ(Counts.Reads, Cycle);
        EXPECT_EQ(Retained->EndRead(), RADIENT_STATUS_OK);
        EXPECT_EQ(Counts.Reads, Cycle + 1);
        EXPECT_EQ(Retained->EndRead(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(Counts.Destructions, 0);
    }
    Retained.Release();
    EXPECT_EQ(Counts.Reads, 2);
    EXPECT_EQ(Counts.Destructions, 1);
}

TEST_P(RadientReadOnlyDataBlobTest, AllowsConcurrentReaders)
{
    Uint8                           Data = 73;
    std::atomic<int>                Calls{0};
    RadientDataBlobCreateInfo       CI{1, &Data, CountNotification, &Calls};
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    AccessGate                 Readers;
    std::atomic<int>           SuccessfulReads{0};
    std::array<std::thread, 4> Threads;
    for (auto& Thread : Threads)
        Thread = std::thread{[&] {
            const void* ReadData = nullptr;
            const auto  Status   = Blob->BeginRead(&ReadData);
            if (Status == RADIENT_STATUS_OK && *static_cast<const Uint8*>(ReadData) == Data)
                ++SuccessfulReads;
            Readers.ArriveAndWait();
            if (Status == RADIENT_STATUS_OK)
                EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
        }};
    EXPECT_TRUE(Readers.WaitFor(4));
    EXPECT_EQ(SuccessfulReads, 4);
    EXPECT_EQ(Calls, 0);
    Readers.Release();
    for (auto& Thread : Threads)
        Thread.join();
    EXPECT_EQ(Calls, 1);
}

TEST_P(RadientReadOnlyDataBlobTest, LastReaderCallbackRetainsBlobUntilItReturns)
{
    struct CallbackContext
    {
        RefCntAutoPtr<IRadientDataBlob> Blob;
        int                             Destructions = 0;
    } Context;
    RadientDataBlobCreateInfo CI;
    CI.pUserData            = &Context;
    CI.OnLastReaderReleased = [](IRadientDataBlob* pBlob, void* pContext) {
        auto& Context = *static_cast<CallbackContext*>(pContext);
        Context.Blob.Release();
        EXPECT_EQ(Context.Destructions, 0);
        EXPECT_EQ(pBlob->GetSize(), 0u);
    };
    CI.OnDestroy = [](void* pContext) {
        ++static_cast<CallbackContext*>(pContext)->Destructions;
    };
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Context.Blob), RADIENT_STATUS_OK);
    const void* ReadData = nullptr;
    ASSERT_EQ(Context.Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    auto* RawBlob = Context.Blob.RawPtr();
    EXPECT_EQ(RawBlob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Context.Blob, nullptr);
    EXPECT_EQ(Context.Destructions, 1);
}

TEST_P(RadientReadOnlyDataBlobTest, DestructionCallbackExceptionsDoNotEscape)
{
    int                       Calls = 0;
    RadientDataBlobCreateInfo CI;
    CI.pUserData = &Calls;
    CI.OnDestroy = [](void* pContext) {
        ++*static_cast<int*>(pContext);
        throw 1;
    };
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, GetParam(), &Blob), RADIENT_STATUS_OK);
    Testing::TestingEnvironment::ErrorScope ExpectedError{"Radient data blob destruction callback threw an exception"};
    EXPECT_NO_THROW(Blob.Release());
    EXPECT_EQ(Calls, 1);
}

INSTANTIATE_TEST_SUITE_P(StorageModes, RadientReadOnlyDataBlobTest, testing::Values(RADIENT_DATA_BLOB_STORAGE_MODE_COPY, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE));

TEST(RadientDataBlobTest, ReadOnlyCopyOutlivesSourceStorage)
{
    RefCntAutoPtr<IRadientDataBlob> Blob;
    {
        std::array<Uint8, 4>      Data{{11, 12, 13, 14}};
        RadientDataBlobCreateInfo CI;
        CI.Size  = Data.size();
        CI.pData = Data.data();
        ASSERT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &Blob), RADIENT_STATUS_OK);
        Data.fill(0);
    }
    const void* ReadData = nullptr;
    ASSERT_EQ(Blob->BeginRead(&ReadData), RADIENT_STATUS_OK);
    const std::array<Uint8, 4> Expected{{11, 12, 13, 14}};
    EXPECT_EQ(std::memcmp(ReadData, Expected.data(), Expected.size()), 0);
    EXPECT_EQ(Blob->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, CreationFailuresDoNotTransferCallbackContext)
{
    std::atomic<int>          Calls{0};
    RadientDataBlobCreateInfo CI;
    CI.OnLastReaderReleased = CountNotification;
    CI.pUserData            = &Calls;
    CI.OnDestroy            = [](void* pContext) {
        ++*static_cast<std::atomic<int>*>(pContext);
    };
    auto Existing = MakeMutableBlob();
    ASSERT_NE(Existing, nullptr);
    Testing::TestingEnvironment::ErrorScope ExpectedErrors{
        "Output data blob pointer must be null",
        "Output data blob pointer must be null",
        "Output data blob pointer must be null",
        "Output data blob pointer must be null"};
    IRadientDataBlob* Invalid     = Existing;
    const auto        InvalidMode = static_cast<RADIENT_DATA_BLOB_STORAGE_MODE>(255);
    EXPECT_EQ(CreateRadientDataBlob(CI, InvalidMode, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Invalid, nullptr);
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    CI.Size = 1;
    Invalid = Existing;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Invalid, nullptr);
    CI.Size = (std::numeric_limits<Uint64>::max)();
    Invalid = Existing;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Invalid, nullptr);
    IRadientMutableDataBlob* InvalidMutable = Existing;
    EXPECT_EQ(CreateRadientMutableDataBlob(CI, &InvalidMutable), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(InvalidMutable, nullptr);
    EXPECT_EQ(Calls, 0);

    CI.Size = 0;
    RefCntAutoPtr<IRadientMutableDataBlob> Mutable;
    ASSERT_EQ(CreateRadientMutableDataBlob(CI, &Mutable), RADIENT_STATUS_OK);
    CI = {};
    RefCntAutoPtr<IRadientDataBlob> Base{Mutable};
    Mutable.Release();
    EXPECT_EQ(Calls, 0);
    Base.Release();
    EXPECT_EQ(Calls, 1);
}
