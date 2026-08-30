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


#include "Render/Tessera/RadientTesseraBufferSuballocator.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <thread>
#include <vector>

using namespace Diligent;

namespace
{

RadientTesseraBufferSuballocator::CreateInfo MakeBufferCreateInfo()
{
    RadientTesseraBufferSuballocator::CreateInfo CI;
    CI.Desc = BufferDesc{
        "Tessera suballocated buffer test",
        0,
        BIND_UNIFORM_BUFFER,
        USAGE_DEFAULT,
    };
    CI.AllocationAlignment = 256;
    CI.MinimumBoundRange   = 1024;
    return CI;
}

RadientTesseraBufferSuballocator MakeBuffer()
{
    return RadientTesseraBufferSuballocator{MakeBufferCreateInfo()};
}

struct DirectUpdateAttribs
{
    const Uint8* pExpectedData = nullptr;
    Uint32       ExpectedSize  = 0;
    bool         WasCalled     = false;
};

RADIENT_STATUS WriteDirectUpdate(void* pData, Uint32 Size, void* pUserData)
{
    auto& Attribs = *static_cast<DirectUpdateAttribs*>(pUserData);
    EXPECT_EQ(Size, Attribs.ExpectedSize);
    if (Size != Attribs.ExpectedSize)
        return RADIENT_STATUS_FAILED;

    std::memcpy(pData, Attribs.pExpectedData, Size);
    Attribs.WasCalled = true;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS VerifyDirectUpdate(void* pData, Uint32 Size, void* pUserData)
{
    auto& Attribs = *static_cast<DirectUpdateAttribs*>(pUserData);
    EXPECT_EQ(Size, Attribs.ExpectedSize);
    if (Size != Attribs.ExpectedSize)
        return RADIENT_STATUS_FAILED;

    EXPECT_EQ(std::memcmp(pData, Attribs.pExpectedData, Size), 0);
    Attribs.WasCalled = true;
    return RADIENT_STATUS_NO_CHANGE;
}

} // namespace

TEST(RadientTesseraBufferSuballocatorTest, AllocatesAlignedPersistentRegions)
{
    RadientTesseraBufferSuballocator Buffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    const auto First  = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    const auto Second = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());

    ASSERT_TRUE(First);
    ASSERT_TRUE(Second);
    EXPECT_FALSE(First.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    EXPECT_FALSE(Second.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    EXPECT_EQ(First.GetOffset() % 256, 0u);
    EXPECT_EQ(Second.GetOffset() % 256, 0u);
    EXPECT_NE(First.GetOffset(), Second.GetOffset());
    EXPECT_EQ(First.GetSize(), Data.size());
}

TEST(RadientTesseraBufferSuballocatorTest, ReusesReleasedRegion)
{
    RadientTesseraBufferSuballocator Buffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    Uint32 ReleasedOffset = 0;
    {
        const auto Allocation = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
        ASSERT_TRUE(Allocation);
        ReleasedOffset = Allocation.GetOffset();
    }

    const auto Reused = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    ASSERT_TRUE(Reused);
    EXPECT_EQ(Reused.GetOffset(), ReleasedOffset);
}

TEST(RadientTesseraBufferSuballocatorTest, UpdatesPersistentRegion)
{
    RadientTesseraBufferSuballocator Buffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    const auto Allocation = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    ASSERT_TRUE(Allocation);
    const auto AllocationAlias = Allocation;

    // The first allocation owns generation 1. Updating it advances the shared
    // allocation state without changing its stable region.
    ASSERT_TRUE(Allocation.IsUploadedThrough(1));
    const Uint32 OriginalOffset = Allocation.GetOffset();
    const Uint32 OriginalSize   = Allocation.GetSize();

    const std::array<Uint8, 4> UpdateData{{1, 2, 3, 4}};
    EXPECT_EQ(Buffer.Update(Allocation, 6, UpdateData.data(), static_cast<Uint32>(UpdateData.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(Allocation.GetOffset(), OriginalOffset);
    EXPECT_EQ(Allocation.GetSize(), OriginalSize);
    EXPECT_FALSE(Allocation.IsUploadedThrough(1));
    EXPECT_FALSE(AllocationAlias.IsUploadedThrough(1));
    EXPECT_TRUE(Allocation.IsUploadedThrough(2));
    EXPECT_TRUE(AllocationAlias.IsUploadedThrough(2));
}

TEST(RadientTesseraBufferSuballocatorTest, PopulatesReservedRegionDirectly)
{
    RadientTesseraBufferSuballocator Buffer     = MakeBuffer();
    const auto                       Allocation = Buffer.Allocate(16);
    ASSERT_TRUE(Allocation);
    EXPECT_FALSE(Allocation.IsUploadedThrough(Buffer.GetUploadedGeneration()));

    const std::array<Uint8, 8> Data{{1, 2, 3, 4, 5, 6, 7, 8}};
    DirectUpdateAttribs        WriteAttribs{Data.data(), static_cast<Uint32>(Data.size())};
    EXPECT_EQ(Buffer.Update(Allocation,
                            4,
                            static_cast<Uint32>(Data.size()),
                            WriteDirectUpdate,
                            &WriteAttribs),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(WriteAttribs.WasCalled);
    EXPECT_TRUE(Allocation.IsUploadedThrough(1));

    DirectUpdateAttribs VerifyAttribs{Data.data(), static_cast<Uint32>(Data.size())};
    EXPECT_EQ(Buffer.Update(Allocation,
                            4,
                            static_cast<Uint32>(Data.size()),
                            VerifyDirectUpdate,
                            &VerifyAttribs),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_TRUE(VerifyAttribs.WasCalled);

    // A callback that reports no change does not advance the allocation generation.
    EXPECT_TRUE(Allocation.IsUploadedThrough(1));
}

TEST(RadientTesseraBufferSuballocatorTest, RejectsInvalidPersistentRegionUpdates)
{
    RadientTesseraBufferSuballocator Buffer      = MakeBuffer();
    RadientTesseraBufferSuballocator OtherBuffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    const auto Allocation = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    ASSERT_TRUE(Allocation);

    RadientTesseraBufferAllocation EmptyAllocation;
    EXPECT_EQ(Buffer.Update(EmptyAllocation, 0, Data.data(), 1),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Buffer.Update(Allocation, 0, nullptr, 1),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Buffer.Update(Allocation, Allocation.GetSize(), Data.data(), 1),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Buffer.Update(Allocation, Allocation.GetSize() - 1, Data.data(), 2),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(OtherBuffer.Update(Allocation, 0, Data.data(), 1),
              RADIENT_STATUS_INVALID_ARGUMENT);

    DirectUpdateAttribs DirectUpdate{Data.data(), static_cast<Uint32>(Data.size())};
    EXPECT_EQ(Buffer.Update(EmptyAllocation, 0, 1, WriteDirectUpdate, &DirectUpdate),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Buffer.Update(Allocation, 0, 1, nullptr, &DirectUpdate),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Buffer.Update(Allocation,
                            Allocation.GetSize(),
                            1,
                            WriteDirectUpdate,
                            &DirectUpdate),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(OtherBuffer.Update(Allocation, 0, 1, WriteDirectUpdate, &DirectUpdate),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(DirectUpdate.WasCalled);

    // Rejected updates do not advance the allocation generation.
    EXPECT_TRUE(Allocation.IsUploadedThrough(1));

    EXPECT_EQ(Buffer.Update(Allocation,
                            0,
                            Data.data(),
                            static_cast<Uint32>(Data.size())),
              RADIENT_STATUS_OK);
    EXPECT_FALSE(Allocation.IsUploadedThrough(1));
    EXPECT_TRUE(Allocation.IsUploadedThrough(2));
}

TEST(RadientTesseraBufferSuballocatorTest, IgnoresEmptyPersistentRegionUpdates)
{
    RadientTesseraBufferSuballocator Buffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    const auto Allocation = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    ASSERT_TRUE(Allocation);

    EXPECT_EQ(Buffer.Update(Allocation, 0, nullptr, 0), RADIENT_STATUS_OK);
    EXPECT_EQ(Buffer.Update(Allocation, Allocation.GetSize(), nullptr, 0), RADIENT_STATUS_OK);
    EXPECT_EQ(Buffer.Update(Allocation, Allocation.GetSize() + 1, nullptr, 0),
              RADIENT_STATUS_INVALID_ARGUMENT);

    // Empty updates do not dirty the buffer or advance the allocation generation.
    EXPECT_TRUE(Allocation.IsUploadedThrough(1));
}

TEST(RadientTesseraBufferSuballocatorTest, SupportsConcurrentAllocations)
{
    RadientTesseraBufferSuballocator Buffer = MakeBuffer();
    const std::array<Uint8, 16>      Data{};

    constexpr size_t                                        ThreadCount = 16;
    std::array<RadientTesseraBufferAllocation, ThreadCount> Allocations;
    std::vector<std::thread>                                Threads;
    Threads.reserve(ThreadCount);
    for (size_t ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        Threads.emplace_back([&, ThreadIndex] {
            Allocations[ThreadIndex] = Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
        });
    }
    for (std::thread& Thread : Threads)
        Thread.join();

    for (size_t Index = 0; Index < ThreadCount; ++Index)
    {
        ASSERT_TRUE(Allocations[Index]);
        EXPECT_EQ(Allocations[Index].GetOffset() % 256, 0u);
        for (size_t Previous = 0; Previous < Index; ++Previous)
            EXPECT_NE(Allocations[Index].GetOffset(), Allocations[Previous].GetOffset());
    }
}
