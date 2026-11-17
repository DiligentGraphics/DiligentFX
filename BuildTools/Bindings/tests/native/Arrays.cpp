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

#include "Arrays.h"
#include <algorithm>

namespace Diligent
{
Int64 SumValues(const Int32* pValues, Uint32 Count)
{
    Int64 Sum = 0;
    for (Uint32 Index = 0; Index < Count; ++Index)
    {
        Sum += pValues[Index];
    }
    return Sum;
}
Uint64 SumBytes(const Uint8* pValues, Uint32 Count)
{
    Uint64 Sum = 0;
    for (Uint32 Index = 0; Index < Count; ++Index)
    {
        Sum += pValues[Index];
    }
    return Sum;
}
const Int32*     EchoValues(const Int32* pValues, Uint32) { return pValues; }
const Bool*      EchoBools(const Bool* pValues, Uint32) { return pValues; }
const Float64*   EchoDoubles(const Float64* pValues, Uint32) { return pValues; }
const Uint64*    EchoWideValues(const Uint64* pValues, Uint32) { return pValues; }
const Int8*      EchoSignedBytes(const Int8* pValues, Uint32) { return pValues; }
const Float32*   EchoFloats(const Float32* pValues, Uint32) { return pValues; }
const TEST_MODE* EchoModes(const TEST_MODE* pValues, Uint32) { return pValues; }
TEST_STATUS      FillValues(Uint32 Capacity, Uint16* pValues, Uint32& Written)
{
    Written = std::min(Capacity, 3u);
    for (Uint32 Index = 0; Index < Written; ++Index)
    {
        pValues[Index] = static_cast<Uint16>(65535u - Index);
    }
    return Capacity < 3 ? TEST_STATUS_PENDING : TEST_STATUS_OK;
}
void         InvalidWrittenCount(Uint32 Capacity, Uint16*, Uint32& Written) { Written = Capacity + 1; }
Bool         HasRequiredArrayStorage(const Int32* pValues, Uint32) { return pValues != nullptr; }
Bool         IsNullArray(const Int32* pValues, Uint32) { return pValues == nullptr; }
const Int32* EchoOptionalValues(const Int32* pValues, Uint32) { return pValues; }
const Int32* GetNullArray(Uint32) { return nullptr; }

TEST_STATUS FillCapacityValues(Uint32 Capacity, Bool Partial, Uint32* pValues)
{
    // Deliberately leave the whole output or its tail untouched. The binding
    // must initialize storage before calling an API that may not fill it.
    if (!Partial)
        return TEST_STATUS_FAILED;
    for (Uint32 Index = 0; Index < std::min(Capacity, 2u); ++Index)
        pValues[Index] = 31u + 16u * Index;
    return TEST_STATUS_OK;
}

} // namespace Diligent
