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

#include "Threading.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Diligent
{
namespace
{
std::mutex              GateMutex;
std::condition_variable GateChanged;
bool                    Entered  = false;
bool                    Released = false;
} // namespace
void ResetCounterCallGate()
{
    std::lock_guard<std::mutex> Lock{GateMutex};
    Entered  = false;
    Released = false;
}
Bool WaitForCounterCall(Uint32 Milliseconds)
{
    std::unique_lock<std::mutex> Lock{GateMutex};
    return GateChanged.wait_for(Lock, std::chrono::milliseconds{Milliseconds}, [] { return Entered; });
}
void ReleaseCounterCall()
{
    std::lock_guard<std::mutex> Lock{GateMutex};
    Released = true;
    GateChanged.notify_all();
}
Uint64 ReadCounterBlocked(ITestCounter* pCounter)
{
    std::unique_lock<std::mutex> Lock{GateMutex};
    Entered = true;
    GateChanged.notify_all();
    GateChanged.wait(Lock, [] { return Released; });
    // Read only after Java has closed the wrapper on another thread.
    return pCounter->GetValue();
}
} // namespace Diligent
