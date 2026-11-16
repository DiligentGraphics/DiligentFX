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

#include "Interfaces.h"
#include <atomic>

namespace Diligent
{
namespace
{
std::atomic<Uint32> LiveCounters{0};
class TestCounter final : public ITestCounter
{
public:
    explicit TestCounter(Uint64 Value) : m_Value{Value} { ++LiveCounters; }
    void DILIGENT_CALL_TYPE                      QueryInterface(const INTERFACE_ID&, IObject** ppObject) override { *ppObject = nullptr; }
    ReferenceCounterValueType DILIGENT_CALL_TYPE AddRef() override { return ++m_References; }
    ReferenceCounterValueType DILIGENT_CALL_TYPE Release() override
    {
        const Uint32 Count = --m_References;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters* DILIGENT_CALL_TYPE GetReferenceCounters() const override { return nullptr; }
    Uint64 DILIGENT_CALL_TYPE              GetValue() const override { return m_Value; }
    void DILIGENT_CALL_TYPE                SetValue(Uint64 Value) override { m_Value = Value; }
    Uint32 DILIGENT_CALL_TYPE              GetRefCount() const override { return m_References; }

private:
    ~TestCounter() { --LiveCounters; }
    std::atomic<Uint32> m_References{1};
    Uint64              m_Value;
};
} // namespace
TEST_STATUS CreateCounter(Uint64 Value, TEST_STATUS Status, ITestCounter** ppCounter)
{
    *ppCounter = Status == TEST_STATUS_FAILED ? nullptr : new TestCounter{Value};
    return Status;
}
ITestCounter* BorrowCounter(ITestCounter* pCounter) { return pCounter; }
Uint64        ReadCounter(ITestCounter* pCounter) { return pCounter->GetValue(); }
Uint64        ReadCounterReference(const ITestCounter& Counter) { return Counter.GetValue(); }
void          BorrowCounterOutput(ITestCounter* pCounter, ITestCounter** ppCounter) { *ppCounter = pCounter; }
Uint32        GetLiveCounterCount() { return LiveCounters.load(); }
void          FailingCounterOutputs(ITestCounter** ppCounter, const Char** ppText)
{
    *ppCounter = new TestCounter{42};
    *ppText    = "\xED\xA0\x80";
}
} // namespace Diligent
