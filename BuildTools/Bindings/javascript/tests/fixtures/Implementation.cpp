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
Uint32 RecordedValue    = 0;
Uint32 LiveObjectCount  = 0;
Uint32 DestructionCount = 0;

// A deliberately small native test double. Only strong-reference operations
// are exposed by these bindings; weak-reference counters are not exercised.
class MarshalObject final : public IMarshalMutableObject
{
public:
    MarshalObject(Uint64 Size, bool Mutable) : m_Size{Size}, m_Mutable{Mutable}
    {
        ++LiveObjectCount;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalObject || (m_Mutable && IID == IID_MarshalMutableObject))
        {
            *ppObject = this;
            AddRef();
        }
    }
    ReferenceCounterValueType AddRef() override { return ++m_RefCount; }
    ReferenceCounterValueType Release() override
    {
        const Uint32 Count = --m_RefCount;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters*          GetReferenceCounters() const override { return nullptr; }
    Uint64                       GetSize() const override { return m_Size; }
    const MarshalNativeOnlyDesc& GetNativeDesc() const override
    {
        static const MarshalNativeOnlyDesc Desc{41};
        return Desc;
    }
    Uint32         GetRefCount() const override { return m_RefCount; }
    MARSHAL_STATUS SetSize(Uint64 Size) override
    {
        m_Size = Size;
        return MARSHAL_STATUS_PENDING;
    }

    MARSHAL_STATUS CopySizeFrom(const IMarshalObject& Source) override
    {
        return SetSize(Source.GetSize());
    }
    MARSHAL_STATUS CopySizeTo(IMarshalMutableObject& Destination) const override
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

Int8           EchoInt8(Int8 Value) { return Value; }
Uint8          EchoUint8(Uint8 Value) { return Value; }
Int16          EchoInt16(Int16 Value) { return Value; }
Uint16         EchoUint16(Uint16 Value) { return Value; }
Int32          EchoInt32(Int32 Value) { return Value; }
Uint32         EchoUint32(Uint32 Value) { return Value; }
Int64          EchoInt64(Int64 Value) { return Value; }
Uint64         EchoUint64(Uint64 Value) { return Value; }
Float32        EchoFloat(Float32 Value) { return Value; }
Float64        EchoDouble(Float64 Value) { return Value; }
Bool           InvertBool(Bool Value) { return !Value; }
MARSHAL_MODE   EchoMode(MARSHAL_MODE Mode) { return Mode; }
MARSHAL_STATUS EchoStatus(MARSHAL_STATUS Status) { return Status; }

Bool CheckScalars(Int8 SignedByte, Uint32 UnsignedWord, Int64 SignedWide, Uint64 UnsignedWide, Float32 Single, Float64 Double, Bool Enabled, MARSHAL_MODE Mode)
{
    return SignedByte == -101 && UnsignedWord == 0xFEDCBA98u &&
        SignedWide == -0x123456789ABCDEFll && UnsignedWide == 0xFEDCBA9876543210ull &&
        Single == 1.25f && Double == -2.5 && Enabled && Mode == MARSHAL_MODE_LAST;
}
Uint64         GetWideConstant() { return 0xFEDCBA9876543210ull; }
MARSHAL_STATUS ReadWideOutput(Bool Fail, Uint64* pValue)
{
    if (Fail)
        return MARSHAL_STATUS_FAILED;
    *pValue = 0xFEDCBA9876543210ull;
    return MARSHAL_STATUS_OK;
}
void   RecordValue(Uint32 Value) { RecordedValue = Value; }
Uint32 GetRecordedValue() { return RecordedValue; }

Bool CheckDefaultDesc(const MarshalDesc& Desc)
{
    return Desc.Size == 0xFEDCBA9876543210ull && Desc.Count == -13 && Desc.Scale == 1.25f &&
        Desc.Weight == -2.5 && Desc.Enabled && Desc.Mode == MARSHAL_MODE_LAST;
}
Bool CheckCustomDesc(const MarshalDesc& Desc)
{
    return Desc.Size == 0x123456789ABCDEF0ull && Desc.Count == 19 && Desc.Scale == -3.5f &&
        Desc.Weight == 0.125 && !Desc.Enabled && Desc.Mode == MARSHAL_MODE_FIRST;
}
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
