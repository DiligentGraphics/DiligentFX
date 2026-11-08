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

#include "ComputedOutputs.h"

#include <emscripten/heap.h>

namespace Diligent
{
namespace
{

Uint32 LiveComputedOutputs = 0;

class ComputedOutputs final : public IMarshalComputedOutputs
{
public:
    explicit ComputedOutputs(Uint64 Count)
    {
        m_Desc.Count = Count;
        ++LiveComputedOutputs;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalComputedOutputs)
        {
            *ppObject = this;
            AddRef();
        }
    }
    ReferenceCounterValueType AddRef() override { return ++m_Refs; }
    ReferenceCounterValueType Release() override
    {
        const Uint32 Count = --m_Refs;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters*        GetReferenceCounters() const override { return nullptr; }
    const MarshalComputedDesc& GetDesc() const override { return m_Desc; }
    Uint64                     GetValueCount() const override { return m_Desc.Count; }
    void                       SetCount(Uint64 Count) override { m_Desc.Count = Count; }

    MARSHAL_STATUS FillMatrices(IMarshalComputedOutputs* pSource, MarshalComputedMatrix* pMatrices, Bool Fail, Bool Grow) override
    {
        if (Fail)
            return MARSHAL_STATUS_FAILED;
        if (Grow)
            emscripten_resize_heap(emscripten_get_heap_size() + 65536);
        const Uint64 Count = pSource->GetDesc().Count;
        for (Uint64 i = 0; i < Count; ++i)
        {
            for (Uint32 Component = 0; Component < 16; ++Component)
            {
                pMatrices[i].Data[Component] = static_cast<float>(100 * i + Component + 1);
            }
        }
        return MARSHAL_STATUS_PENDING;
    }

private:
    ~ComputedOutputs() { --LiveComputedOutputs; }

    Uint32              m_Refs = 1;
    MarshalComputedDesc m_Desc;
};

} // namespace

MARSHAL_STATUS CreateComputedOutputs(Uint64 Count, IMarshalComputedOutputs** ppObject)
{
    *ppObject = new ComputedOutputs{Count};
    return MARSHAL_STATUS_OK;
}

void FillComputedValues(Uint32* pValues, const IMarshalComputedOutputs& Source, const MarshalDesc& Options)
{
    for (Uint64 i = 0; i < Source.GetValueCount(); ++i)
    {
        pValues[i] = static_cast<Uint32>(Options.Size + i);
    }
}

Uint32 GetLiveComputedOutputs() { return LiveComputedOutputs; }

} // namespace Diligent
