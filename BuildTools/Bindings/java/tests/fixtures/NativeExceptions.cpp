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

#include "NativeExceptions.h"
#include <stdexcept>
#include <new>
namespace Diligent
{
void ThrowNative() { throw std::runtime_error{"Fixture failure"}; }

void ThrowCounterOutputs(ITestCounter* pCounter, Bool AllocationFailure, ITestCounter** ppFirst, ITestCounter** ppSecond)
{
    pCounter->AddRef();
    *ppFirst = pCounter;
    CreateCounter(43, TEST_STATUS_OK, ppSecond);
    // Simulate native failure after acquiring resources, without exhausting memory.
    if (AllocationFailure)
        throw std::bad_alloc{};
    throw std::runtime_error{"Fixture failure after assigning outputs"};
}

} // namespace Diligent
