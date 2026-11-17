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

namespace Diligent
{
namespace
{
Uint32 LiveObjectCount  = 0;
Uint32 DestructionCount = 0;

// A deliberately small native test double. Only strong-reference operations
// are exposed by these bindings; weak-reference counters are not exercised.
// The unrelated tagged interface has a distinct IObject subobject. Queries
// must preserve the returned interface address, not the original object address.
class MarshalObject final : public IMarshalMutableObject, public IMarshalTaggedObject
{
public:
    MarshalObject(Uint64 Size, bool Mutable) : m_Size{Size}, m_Mutable{Mutable}
    {
        ++LiveObjectCount;
    }

    void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_MarshalTaggedObject)
            *ppObject = static_cast<IMarshalTaggedObject*>(this);
        else if (IID == IID_Unknown || IID == IID_MarshalObject || (m_Mutable && IID == IID_MarshalMutableObject))
            *ppObject = static_cast<IMarshalMutableObject*>(this);
        if (*ppObject)
            AddRef();
    }
    ReferenceCounterValueType DILIGENT_CALL_TYPE AddRef() override { return ++m_RefCount; }
    ReferenceCounterValueType DILIGENT_CALL_TYPE Release() override
    {
        const Uint32 Count = --m_RefCount;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters* DILIGENT_CALL_TYPE          GetReferenceCounters() const override { return nullptr; }
    Uint64 DILIGENT_CALL_TYPE                       GetSize() const override { return m_Size; }
    const MarshalNativeOnlyDesc& DILIGENT_CALL_TYPE GetNativeDesc() const override
    {
        static const MarshalNativeOnlyDesc Desc{41};
        return Desc;
    }
    Uint64 DILIGENT_CALL_TYPE         GetTag() const override { return m_Size ^ 0xfedcba9876543210ull; }
    Uint32 DILIGENT_CALL_TYPE         GetRefCount() const override { return m_RefCount; }
    MARSHAL_STATUS DILIGENT_CALL_TYPE SetSize(Uint64 Size) override
    {
        m_Size = Size;
        return MARSHAL_STATUS_PENDING;
    }

    MARSHAL_STATUS DILIGENT_CALL_TYPE CopySizeFrom(const IMarshalObject& Source) override
    {
        return SetSize(Source.GetSize());
    }
    MARSHAL_STATUS DILIGENT_CALL_TYPE CopySizeTo(IMarshalMutableObject& Destination) const override
    {
        return Destination.SetSize(GetSize());
    }

private:
    ~MarshalObject()
    {
        --LiveObjectCount;
        ++DestructionCount;
    }
    Uint64 m_Size;
    Uint32 m_RefCount = 1;
    bool   m_Mutable;
};
} // namespace

MARSHAL_STATUS CreateMarshalObject(const MarshalDesc& Desc, MARSHAL_STATUS Status, Bool Mutable, IMarshalObject** ppObject)
{
    *ppObject = nullptr;
    if (Status != MARSHAL_STATUS_FAILED)
        *ppObject = new MarshalObject{Desc.Size, Mutable};
    return Status;
}
Uint64 GetObjectSize(IMarshalObject* pObject) { return pObject ? pObject->GetSize() : 0; }
Uint32 GetLiveObjectCount() { return LiveObjectCount; }
Uint32 GetDestructionCount() { return DestructionCount; }
} // namespace Diligent
