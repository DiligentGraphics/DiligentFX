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

#include "Scalars.h"
#include <stdexcept>

namespace Diligent
{
namespace
{
Uint32 Calls = 0;
}
Int8  EchoInt8(Int8 Value) { return Value; }
Uint8 EchoUint8(Uint8 Value)
{
    ++Calls;
    return Value;
}
Int16       EchoInt16(Int16 Value) { return Value; }
Uint16      EchoUint16(Uint16 Value) { return Value; }
Int32       EchoInt32(Int32 Value) { return Value; }
Uint32      EchoUint32(Uint32 Value) { return Value; }
Int64       EchoInt64(Int64 Value) { return Value; }
Uint64      EchoUint64(Uint64 Value) { return Value; }
Float32     EchoFloat(Float32 Value) { return Value; }
Float64     EchoDouble(Float64 Value) { return Value; }
Bool        InvertBool(Bool Value) { return !Value; }
TEST_MODE   EchoMode(TEST_MODE Value) { return Value; }
TEST_STATUS EchoStatus(TEST_STATUS Value) { return Value; }
Uint32      GetCallCount() { return Calls; }
void        ThrowNative() { throw std::runtime_error{"Fixture failure"}; }
} // namespace Diligent
