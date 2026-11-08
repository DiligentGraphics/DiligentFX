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

#include "OutputArrays.h"

#include <array>
#include <string>
#include <emscripten/heap.h>

namespace Diligent
{
namespace
{
Uint32         LiveArraySnapshots = 0;
constexpr Char UTF8Text[]         = "A\xc3\xa9\xe6\xb0\xb4\xf0\x9f\x98\x80";

class MarshalArraySnapshots final : public IMarshalArraySnapshots
{
public:
    MarshalArraySnapshots()
    {
        m_Desc.pInt8                    = m_Int8.data();
        m_Desc.pUint8                   = m_Uint8.data();
        m_Desc.pInt16                   = m_Int16.data();
        m_Desc.pUint16                  = m_Uint16.data();
        m_Desc.pInt32                   = m_Int32.data();
        m_Desc.pUint32                  = m_Uint32.data();
        m_Desc.pInt64                   = m_Int64.data();
        m_Desc.pUint64                  = m_Uint64.data();
        m_Desc.pFloat32                 = m_Float32.data();
        m_Desc.pFloat64                 = m_Float64.data();
        m_Desc.pBool                    = m_Bool.data();
        m_Desc.pMARSHAL_MODE            = m_MARSHAL_MODE.data();
        m_Desc.pChar                    = m_Char.data();
        m_Items[0].Id                   = 41;
        m_Items[0].Label                = m_Label.c_str();
        m_Items[0].Transform.Position.y = -2.5f;
        m_Items[0].pSamples             = m_Float32.data() + 1;
        m_Items[0].SampleCount          = 2;
        m_Items[1].Id                   = 0xfedcba98u;
        m_Items[1].Label                = nullptr;
        m_Groups[0]                     = {m_Items.data(), 2};
        m_Groups[1]                     = {m_Items.data() + 1, 1};
        m_Desc.pGroups                  = m_Groups.data();
        m_Desc.GroupCount               = 2;
        ++LiveArraySnapshots;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalArraySnapshots)
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
    IReferenceCounters* GetReferenceCounters() const override { return nullptr; }

    const MarshalOutputArrayDesc& GetDesc() const override { return m_Desc; }
    const MarshalOutputArrayDesc& GetDescAfterGrowingMemory() const override
    {
        emscripten_resize_heap(emscripten_get_heap_size() + 65536);
        return m_Desc;
    }
    const Float32* GetValues(Uint32 Count) const override
    {
        return Count <= m_Float32.size() ? m_Float32.data() : nullptr;
    }
    const MarshalOutputArrayItem* GetItems(Uint32 Count) const override
    {
        return Count != 0 && Count <= m_Items.size() ? m_Items.data() : nullptr;
    }
    void Update() override
    {
        m_Float32[1]                 = 64.f;
        m_Items[0].Id                = 99;
        m_Items[0].Transform.Scale.y = 5.f;
        m_Label                      = "changed label";
        m_Items[0].Label             = m_Label.c_str();
    }

private:
    ~MarshalArraySnapshots() { --LiveArraySnapshots; }
    Uint32                                 m_Refs = 1;
    std::array<Int8, 3>                    m_Int8{{-128, 17, 127}};
    std::array<Uint8, 3>                   m_Uint8{{0, 128, 255}};
    std::array<Int16, 3>                   m_Int16{{-32768, 17, 32767}};
    std::array<Uint16, 3>                  m_Uint16{{0, 32768, 65535}};
    std::array<Int32, 3>                   m_Int32{{(-2147483647 - 1), 17, 2147483647}};
    std::array<Uint32, 3>                  m_Uint32{{0, 0x80000000u, 0xffffffffu}};
    std::array<Int64, 3>                   m_Int64{{-0x7000000000000001ll, 17, 0x7123456789abcdefll}};
    std::array<Uint64, 3>                  m_Uint64{{0, 0x8000000000000000ull, 0xfedcba9876543210ull}};
    std::array<Float32, 3>                 m_Float32{{-1.5f, 0.25f, 123.5f}};
    std::array<Float64, 3>                 m_Float64{{-1.25, 0.1, 123456.789}};
    std::array<Bool, 3>                    m_Bool{{True, False, True}};
    std::array<MARSHAL_MODE, 3>            m_MARSHAL_MODE{{MARSHAL_MODE_FIRST, MARSHAL_MODE_LAST, MARSHAL_MODE_FIRST}};
    std::array<Char, 3>                    m_Char{{'A', 0, static_cast<Char>(0xff)}};
    std::string                            m_Label{UTF8Text};
    std::array<MarshalOutputArrayItem, 2>  m_Items;
    std::array<MarshalOutputArrayGroup, 2> m_Groups;
    MarshalOutputArrayDesc                 m_Desc;
};
} // namespace

MARSHAL_STATUS CreateMarshalArraySnapshots(IMarshalArraySnapshots** ppObject)
{
    *ppObject = new MarshalArraySnapshots{};
    return MARSHAL_STATUS_OK;
}

const MarshalArrayRoundTrip& EchoArrayDescriptor(const MarshalArrayRoundTrip& Desc, Bool InvalidUTF8)
{
    if (InvalidUTF8)
    {
        // Fail after converting an earlier element to exercise output conversion
        // cleanup while all of the caller's temporary inputs are still retained.
        static constexpr Char               Invalid[] = {static_cast<Char>(0xc3), 'x', 0};
        static const MarshalOutputArrayItem Items[]   = {{1, "valid"}, {2, Invalid}};
        static MarshalArrayRoundTrip        Result;
        Result           = Desc;
        Result.pItems    = Items;
        Result.ItemCount = 2;
        return Result;
    }
    return Desc;
}

const MarshalArrayRoundTrip& GetInvalidOutputArray(Uint32 Case)
{
    static const Float64         Values[] = {-1.25, 0.1, 123456.789};
    static MarshalArrayRoundTrip Result;
    Result            = {};
    Result.pValues    = Values;
    Result.ValueCount = 1;
    // These cases return malformed metadata without dereferencing it or
    // allocating large storage. The wrapper must reject it before reading.
    switch (Case)
    {
        case 0: Result.pValues = nullptr; break;
        case 1: Result.ValueCount = Uint64{1} << 32; break;
        case 2: Result.pValues = reinterpret_cast<const Float64*>(emscripten_get_heap_size()); break;
        case 3: Result.pValues = reinterpret_cast<const Float64*>(reinterpret_cast<const Uint8*>(Values) + 1); break;
        case 4: Result.ValueCount = emscripten_get_heap_size() / sizeof(Float64) + 1; break;
        case 5:
            Result.pItems    = reinterpret_cast<const MarshalOutputArrayItem*>(emscripten_get_heap_size());
            Result.ItemCount = 1;
            break;
        case 6:
            Result.pItems    = reinterpret_cast<const MarshalOutputArrayItem*>(Values);
            Result.ItemCount = static_cast<Uint32>(emscripten_get_heap_size() / sizeof(MarshalOutputArrayItem) + 1);
            break;
        case 7:
            Result.pItems    = reinterpret_cast<const MarshalOutputArrayItem*>(reinterpret_cast<const Uint8*>(Values) + 1);
            Result.ItemCount = 1;
            break;
        case 8:
            Result.pItems    = nullptr;
            Result.ItemCount = 1;
            break;
        case 9:
            Result.pValues    = nullptr;
            Result.ValueCount = 0;
            break;
        case 10: Result.ValueCount = 0; break;
        default: break;
    }
    return Result;
}

const Float64* GetStaticOutputArray(Uint64 Count)
{
    static const Float64 Values[] = {-1.25, 0.1, 123456.789};
    return Count == 0 || Count > 3 ? nullptr : Values;
}

Uint32 GetLiveArraySnapshotCount() { return LiveArraySnapshots; }
} // namespace Diligent
