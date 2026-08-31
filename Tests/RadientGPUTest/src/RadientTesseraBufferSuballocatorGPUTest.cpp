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

#include "GPUTestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

TEST(RadientTesseraBufferSuballocatorGPUTest, CreatesConfiguredInitialBufferWithoutAllocations)
{
    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    GPUTestingEnvironment::ScopedReleaseResources AutoReleaseResources;

    constexpr Uint32 InitialBufferSize = 64u << 10u;

    RadientTesseraBufferSuballocator::CreateInfo BufferCI;
    BufferCI.Desc.Name              = "Tessera initial buffer size test";
    BufferCI.Desc.Size              = InitialBufferSize;
    BufferCI.Desc.BindFlags         = BIND_SHADER_RESOURCE;
    BufferCI.Desc.Usage             = USAGE_DEFAULT;
    BufferCI.Desc.Mode              = BUFFER_MODE_STRUCTURED;
    BufferCI.Desc.ElementByteStride = 16;
    BufferCI.InitialSize            = InitialBufferSize;
    RadientTesseraBufferSuballocator Buffer{BufferCI};

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    ASSERT_NE(Buffer.GetBuffer(), nullptr);
    EXPECT_EQ(Buffer.GetBuffer()->GetDesc().Size, InitialBufferSize);
}

TEST(RadientTesseraBufferSuballocatorGPUTest, ReservesMinimumBoundRange)
{
    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    GPUTestingEnvironment::ScopedReleaseResources AutoReleaseResources;

    constexpr Uint32 MinimumBoundRange = 1024;
    const Uint32     Alignment         = std::max(pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment, 1u);

    RadientTesseraBufferSuballocator::CreateInfo BufferCI;
    BufferCI.Desc = BufferDesc{
        "Tessera minimum bound range test",
        0,
        BIND_UNIFORM_BUFFER,
        USAGE_DEFAULT,
    };
    BufferCI.InitialSize         = Alignment;
    BufferCI.AllocationAlignment = Alignment;
    BufferCI.MinimumBoundRange   = MinimumBoundRange;
    RadientTesseraBufferSuballocator Buffer{BufferCI};

    const std::array<Uint8, 16>          Data{};
    const RadientTesseraBufferAllocation Allocation =
        Buffer.Allocate(static_cast<Uint32>(Data.size()), Data.data());
    ASSERT_TRUE(Allocation);
    EXPECT_EQ(Allocation.GetSize(), Data.size());

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    ASSERT_NE(Buffer.GetBuffer(), nullptr);
    EXPECT_GE(Buffer.GetBuffer()->GetDesc().Size,
              Uint64{Allocation.GetOffset()} + MinimumBoundRange);
}

TEST(RadientTesseraBufferSuballocatorGPUTest, UploadsDataAndPreservesOffsetsAcrossGrowth)
{
    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    GPUTestingEnvironment::ScopedReleaseResources AutoReleaseResources;

    constexpr Uint32                             MaxMaterialAttribsSize = 1024;
    const Uint32                                 Alignment              = std::max(pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment, 1u);
    RadientTesseraBufferSuballocator::CreateInfo BufferCI;
    BufferCI.Desc = BufferDesc{
        "Tessera suballocated buffer GPU test",
        0,
        BIND_UNIFORM_BUFFER,
        USAGE_DEFAULT,
    };
    BufferCI.AllocationAlignment = Alignment;
    BufferCI.MinimumBoundRange   = MaxMaterialAttribsSize;
    RadientTesseraBufferSuballocator Buffer{BufferCI};

    std::array<Uint8, 32> FirstData;
    FirstData.fill(0x2A);
    const RadientTesseraBufferAllocation First =
        Buffer.Allocate(static_cast<Uint32>(FirstData.size()), FirstData.data());
    ASSERT_TRUE(First);
    EXPECT_FALSE(First.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    const Uint32 FirstOffset = First.GetOffset();

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    EXPECT_TRUE(First.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    IBuffer* const pInitialBuffer = Buffer.GetBuffer();
    ASSERT_NE(pInitialBuffer, nullptr);
    const Uint32 InitialVersion    = Buffer.GetVersion();
    const Uint64 InitialGeneration = Buffer.GetUploadedGeneration();
    const Uint64 InitialBufferSize = pInitialBuffer->GetDesc().Size;
    EXPECT_TRUE(First.IsUploadedThrough(InitialGeneration));

    std::array<Uint8, 32> LateData;
    LateData.fill(0x7B);
    const RadientTesseraBufferAllocation Late =
        Buffer.Allocate(static_cast<Uint32>(LateData.size()), LateData.data());
    ASSERT_TRUE(Late);
    ASSERT_LE(Uint64{Late.GetOffset()} + MaxMaterialAttribsSize, InitialBufferSize);
    EXPECT_FALSE(Late.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    EXPECT_FALSE(Late.IsUploadedThrough(InitialGeneration));

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    EXPECT_TRUE(Late.IsUploadedThrough(Buffer.GetUploadedGeneration()));
    EXPECT_EQ(Buffer.GetVersion(), InitialVersion);
    const Uint64 UpdatedGeneration = Buffer.GetUploadedGeneration();
    EXPECT_GT(UpdatedGeneration, InitialGeneration);
    EXPECT_TRUE(Late.IsUploadedThrough(UpdatedGeneration));

    std::vector<Uint8>                          LastData(MaxMaterialAttribsSize, 0xA5);
    const size_t                                AllocationCount = static_cast<size_t>(InitialBufferSize / MaxMaterialAttribsSize) + 2;
    std::vector<RadientTesseraBufferAllocation> Allocations;
    Allocations.reserve(AllocationCount);
    for (size_t Index = 0; Index < AllocationCount; ++Index)
        Allocations.push_back(Buffer.Allocate(static_cast<Uint32>(LastData.size()), LastData.data()));
    ASSERT_TRUE(Allocations.back());
    EXPECT_FALSE(Allocations.back().IsUploadedThrough(Buffer.GetUploadedGeneration()));

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    EXPECT_TRUE(Allocations.back().IsUploadedThrough(Buffer.GetUploadedGeneration()));
    IBuffer* const pGrownBuffer = Buffer.GetBuffer();
    ASSERT_NE(pGrownBuffer, nullptr);
    EXPECT_GT(Buffer.GetVersion(), InitialVersion);
    EXPECT_FALSE(Allocations.back().IsUploadedThrough(UpdatedGeneration));
    EXPECT_TRUE(Allocations.back().IsUploadedThrough(Buffer.GetUploadedGeneration()));
    EXPECT_TRUE(First.IsUploadedThrough(InitialGeneration));
    EXPECT_GE(pGrownBuffer->GetDesc().Size, InitialBufferSize * 2);
    EXPECT_EQ(First.GetOffset(), FirstOffset);

    BufferDesc StagingDesc;
    StagingDesc.Name           = "Tessera material buffer test staging buffer";
    StagingDesc.Size           = pGrownBuffer->GetDesc().Size;
    StagingDesc.Usage          = USAGE_STAGING;
    StagingDesc.BindFlags      = BIND_NONE;
    StagingDesc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> pStagingBuffer;
    pDevice->CreateBuffer(StagingDesc, nullptr, pStagingBuffer.GetAddressOfEmpty());
    ASSERT_NE(pStagingBuffer, nullptr);

    pContext->CopyBuffer(pGrownBuffer,
                         0,
                         RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                         pStagingBuffer,
                         0,
                         StagingDesc.Size,
                         RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->WaitForIdle();

    void* pMappedData = nullptr;
    pContext->MapBuffer(pStagingBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT, pMappedData);
    ASSERT_NE(pMappedData, nullptr);
    EXPECT_EQ(std::memcmp(static_cast<const Uint8*>(pMappedData) + FirstOffset,
                          FirstData.data(),
                          FirstData.size()),
              0);
    EXPECT_EQ(std::memcmp(static_cast<const Uint8*>(pMappedData) + Allocations.back().GetOffset(),
                          LastData.data(),
                          LastData.size()),
              0);
    pContext->UnmapBuffer(pStagingBuffer, MAP_READ);
}
