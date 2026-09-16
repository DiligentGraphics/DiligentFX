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

RefCntAutoPtr<IRadientDataBlob> MakeBlob(Uint64                                 Size      = 16,
                                         RadientDataBlobReadReleaseCallbackType Callback  = nullptr,
                                         void*                                  pUserData = nullptr)
{
    RadientDataBlobCreateInfo CI;
    CI.Size                 = Size;
    CI.OnLastReaderReleased = Callback;
    CI.pUserData            = pUserData;
    RefCntAutoPtr<IRadientDataBlob> Blob;
    EXPECT_EQ(CreateRadientDataBlob(CI, &Blob), RADIENT_STATUS_OK);
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
    EXPECT_EQ(CI.pInitialData, nullptr);
    EXPECT_EQ(CI.OnLastReaderReleased, nullptr);
    EXPECT_EQ(CI.pUserData, nullptr);
    EXPECT_EQ(CreateRadientDataBlob(CI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    auto Blob = MakeBlob(0);
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
    RefCntAutoPtr<IRadientDataBlob> Invalid;
    EXPECT_EQ(CreateRadientDataBlob(CI, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Invalid, nullptr);
}

TEST(RadientDataBlobTest, CopiesInitialDataDuringCreation)
{
    const std::array<Uint8, 5>      Expected{{3, 17, 29, 128, 255}};
    std::atomic<int>                Calls{0};
    RefCntAutoPtr<IRadientDataBlob> Blob;
    {
        auto                      InitialData = Expected;
        RadientDataBlobCreateInfo CI;
        CI.Size                 = InitialData.size();
        CI.pInitialData         = InitialData.data();
        CI.OnLastReaderReleased = CountNotification;
        CI.pUserData            = &Calls;
        ASSERT_EQ(CreateRadientDataBlob(CI, &Blob), RADIENT_STATUS_OK);
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
    EmptyCI.pInitialData = Expected.data();
    RefCntAutoPtr<IRadientDataBlob> Empty;
    ASSERT_EQ(CreateRadientDataBlob(EmptyCI, &Empty), RADIENT_STATUS_OK);
    EXPECT_EQ(Empty->GetSize(), 0u);
    ASSERT_EQ(Empty->BeginRead(&ReadData), RADIENT_STATUS_OK);
    EXPECT_EQ(ReadData, nullptr);
    EXPECT_EQ(Empty->EndRead(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, SharesReadsAndExcludesWrites)
{
    auto Blob = MakeBlob();
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
    auto Blob = MakeBlob();
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
    std::atomic<int>                Calls{0};
    RadientDataBlobCreateInfo       CI{16, nullptr, CountNotification, &Calls};
    RefCntAutoPtr<IRadientDataBlob> Blob;
    ASSERT_EQ(CreateRadientDataBlob(CI, &Blob), RADIENT_STATUS_OK);
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
    auto Blob = MakeBlob(1, [](IRadientDataBlob* pBlob, void*) {
        void* Data = nullptr;
        ASSERT_EQ(pBlob->BeginWrite(&Data), RADIENT_STATUS_OK);
        *static_cast<Uint8*>(Data) = 42;
        EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
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
    auto Blob  = MakeBlob(
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
    RefCntAutoPtr<IRadientDataBlob> Blob;
    Blob = MakeBlob(
        16, [](IRadientDataBlob* pBlob, void* Context) {
        static_cast<RefCntAutoPtr<IRadientDataBlob>*>(Context)->Release();
        EXPECT_EQ(pBlob->GetSize(), 16u);
        void* Data = nullptr;
        ASSERT_EQ(pBlob->BeginWrite(&Data), RADIENT_STATUS_OK);
        EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK); }, std::addressof(Blob));
    ASSERT_NE(Blob, nullptr);
    RefCntWeakPtr<IRadientDataBlob> Weak{Blob};
    const void*                     Data = nullptr;
    ASSERT_EQ(Blob->BeginRead(&Data), RADIENT_STATUS_OK);
    IRadientDataBlob* pRawBlob = Blob;
    EXPECT_EQ(pRawBlob->EndRead(), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob, nullptr);
    EXPECT_EQ(Weak.Lock(), nullptr);
}

TEST(RadientDataBlobTest, CallbackCanRetainBlobForReuse)
{
    RefCntAutoPtr<IRadientDataBlob> Recycled;

    auto Blob = MakeBlob(
        16, [](IRadientDataBlob* pBlob, void* Context) { *static_cast<RefCntAutoPtr<IRadientDataBlob>*>(Context) = pBlob; }, std::addressof(Recycled));
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
    auto Blob = MakeBlob(1, [](IRadientDataBlob*, void*) { throw 1; });
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
    auto             Blob = MakeBlob(1, CountNotification, &Calls);
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
    EXPECT_EQ(Calls, 0);
    Readers.Release();
    for (auto& Thread : Threads)
        Thread.join();
    EXPECT_EQ(Calls, 1);
    EXPECT_EQ(Blob->BeginWrite(&WriteData), RADIENT_STATUS_OK);
    EXPECT_EQ(Blob->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientDataBlobTest, OnlyOneConcurrentWriterSucceeds)
{
    auto Blob = MakeBlob();
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

    auto Blob = MakeBlob(
        1, [](IRadientDataBlob* pBlob, void* Data) {
        auto& Context = *static_cast<CallbackContext*>(Data);
        if (++Context.Calls == 1)
        {
            Context.FirstCallback.ArriveAndWait();
            void* Bytes = nullptr;
            Context.WriteStatus = pBlob->BeginWrite(&Bytes);
            if (Context.WriteStatus == RADIENT_STATUS_OK)
                pBlob->EndWrite();
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
    auto Blob = MakeBlob();
    ASSERT_NE(Blob, nullptr);
    RefCntAutoPtr<IRadientDataBlob> Interface{Blob, IID_RadientDataBlob};
    EXPECT_EQ(Interface, Blob);
    EXPECT_EQ(RadientDataBlob_C_TestAccess(), 0);
}
