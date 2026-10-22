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

#pragma once

#include "Assets/RadientMeshIndexSource.hpp"
#include "gtest/gtest.h"

#include <cstring>
#include <initializer_list>
#include <vector>

namespace Diligent
{

namespace Testing
{

inline void ExpectPackedIndices(const RadientMeshIndexSource& Source,
                                std::initializer_list<Uint32> ExpectedIndices)
{
    ASSERT_EQ(Source.GetIndexCount(), ExpectedIndices.size());
    ASSERT_TRUE(Source.GetIndexType() == VT_UINT16 || Source.GetIndexType() == VT_UINT32);
    const Uint32 IndexSize = Source.GetIndexType() == VT_UINT16 ? sizeof(Uint16) : sizeof(Uint32);
    ASSERT_EQ(Source.GetIndexDataSize(), Source.GetIndexCount() * IndexSize);
    std::vector<Uint8> PackedIndices(Source.GetIndexDataSize(), Uint8{0xCD});
    ASSERT_EQ(Source.PackIndexData({PackedIndices.data(), Source.GetIndexDataSize()}), RADIENT_STATUS_OK);

    size_t Offset = 0;
    for (Uint32 Expected : ExpectedIndices)
    {
        Uint32 Value = 0;
        if (Source.GetIndexType() == VT_UINT16)
        {
            Uint16 Value16 = 0;
            std::memcpy(&Value16, PackedIndices.data() + Offset, sizeof(Value16));
            Value = Value16;
        }
        else
        {
            std::memcpy(&Value, PackedIndices.data() + Offset, sizeof(Value));
        }
        EXPECT_EQ(Value, Expected);
        Offset += IndexSize;
    }
}

} // namespace Testing

} // namespace Diligent
