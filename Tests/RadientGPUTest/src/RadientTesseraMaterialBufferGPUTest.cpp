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


#include "Render/Tessera/RadientTesseraMaterialBuffer.hpp"

#include "GPUTestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

TEST(RadientTesseraMaterialBufferGPUTest, UploadsDataAndPreservesOffsetsAcrossGrowth)
{
    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    GPUTestingEnvironment::ScopedReleaseResources AutoReleaseResources;

    constexpr Uint32             MaxMaterialAttribsSize = 1024;
    const Uint32                 Alignment              = std::max(pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment, 1u);
    RadientTesseraMaterialBuffer Buffer{{Alignment, MaxMaterialAttribsSize}};

    std::array<Uint8, 32> FirstData;
    FirstData.fill(0x2A);
    const RadientTesseraMaterialBufferAllocation First =
        Buffer.Allocate(FirstData.data(), static_cast<Uint32>(FirstData.size()));
    ASSERT_TRUE(First);
    const Uint32 FirstOffset = First.GetOffset();

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    IBuffer* const pInitialBuffer = Buffer.GetBuffer();
    ASSERT_NE(pInitialBuffer, nullptr);
    const Uint32 InitialVersion    = Buffer.GetVersion();
    const Uint64 InitialBufferSize = pInitialBuffer->GetDesc().Size;

    std::vector<Uint8>                                  LastData(MaxMaterialAttribsSize, 0xA5);
    const size_t                                        AllocationCount = static_cast<size_t>(InitialBufferSize / MaxMaterialAttribsSize) + 2;
    std::vector<RadientTesseraMaterialBufferAllocation> Allocations;
    Allocations.reserve(AllocationCount);
    for (size_t Index = 0; Index < AllocationCount; ++Index)
        Allocations.push_back(Buffer.Allocate(LastData.data(), static_cast<Uint32>(LastData.size())));
    ASSERT_TRUE(Allocations.back());

    ASSERT_EQ(Buffer.Prepare(pDevice, pContext), RADIENT_STATUS_OK);
    IBuffer* const pGrownBuffer = Buffer.GetBuffer();
    ASSERT_NE(pGrownBuffer, nullptr);
    EXPECT_GT(Buffer.GetVersion(), InitialVersion);
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
