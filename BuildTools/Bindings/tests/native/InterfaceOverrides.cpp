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

#include "InterfaceOverrides.h"

namespace Diligent
{
namespace
{
class MarshalOverrides final : public IMarshalDerivedOverrides
{
public:
    void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalBaseOverrides || IID == IID_MarshalDerivedOverrides)
        {
            *ppObject = this;
            AddRef();
        }
    }
    ReferenceCounterValueType DILIGENT_CALL_TYPE AddRef() override { return ++m_RefCount; }
    ReferenceCounterValueType DILIGENT_CALL_TYPE Release() override
    {
        const Uint32 Count = --m_RefCount;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters* DILIGENT_CALL_TYPE      GetReferenceCounters() const override { return nullptr; }
    const MarshalObjectDesc& DILIGENT_CALL_TYPE GetDesc() const override { return m_Desc; }
    void DILIGENT_CALL_TYPE                     GetValue(Uint32& Value) const override { Value = m_Desc.Value; }
    MARSHAL_STATUS DILIGENT_CALL_TYPE           GetValues(Uint32& Value, Uint64& Size) const override
    {
        Value = m_Desc.Value;
        Size  = m_Desc.Size;
        return MARSHAL_STATUS_PENDING;
    }
    void DILIGENT_CALL_TYPE SetValue(Uint32 Value) override { m_Desc.Value = Value; }

private:
    MarshalObjectDesc m_Desc;
    Uint32            m_RefCount = 1;
};
} // namespace

void CreateMarshalOverrides(IMarshalBaseOverrides** ppObject)
{
    *ppObject = new MarshalOverrides;
}

const MarshalObjectDesc& EchoOverrideDesc(const MarshalObjectDesc& Desc)
{
    return Desc;
}

Uint32 ReadOverrideBase(const MarshalObjectAttribs& Desc)
{
    return Desc.Value;
}
} // namespace Diligent
