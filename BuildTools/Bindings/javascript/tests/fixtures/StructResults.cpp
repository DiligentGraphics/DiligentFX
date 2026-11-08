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

#include "StructResults.h"

#include <string>
#include <emscripten/heap.h>

namespace Diligent
{
namespace
{
Uint32 LiveSnapshotCount = 0;
// Independently authored UTF-8 ensures that output tests are not JS/native
// round trips whose encoding errors could cancel each other out.
constexpr char UTF8Text[] = "A\xc3\xa9\xe6\xb0\xb4\xf0\x9f\x98\x80";

class MarshalSnapshots final : public IMarshalSnapshots
{
public:
    MarshalSnapshots()
    {
        m_Desc.Name        = m_Name.c_str();
        m_Desc.Child.Label = m_Label.c_str();
        ++LiveSnapshotCount;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalSnapshots)
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

    const MarshalSnapshotDesc& GetDesc() const override { return m_Desc; }
    const MarshalSnapshotDesc& GetDescAfterGrowingMemory() const override
    {
        emscripten_resize_heap(emscripten_get_heap_size() + 65536);
        return m_Desc;
    }
    void Update(Int32 Count, const Char* Name, const Char* Label) override
    {
        m_Name                = Name != nullptr ? Name : "";
        m_Label               = Label != nullptr ? Label : "";
        m_Desc.Values.Count   = Count;
        m_Desc.Child.Offset.x = static_cast<float>(Count);
        // Allow a null Name to exercise a native violation of the getter's
        // non-nullable string contract. The nullable child label remains valid.
        m_Desc.Name        = Name != nullptr ? m_Name.c_str() : nullptr;
        m_Desc.Child.Label = Label != nullptr ? m_Label.c_str() : nullptr;
    }

private:
    ~MarshalSnapshots() { --LiveSnapshotCount; }

    Uint32              m_Refs = 1;
    std::string         m_Name{UTF8Text};
    std::string         m_Label{"native label"};
    MarshalSnapshotDesc m_Desc;
};
} // namespace

MARSHAL_STATUS CreateMarshalSnapshots(IMarshalSnapshots** ppObject)
{
    *ppObject = new MarshalSnapshots{};
    return MARSHAL_STATUS_OK;
}

const MarshalDesc& GetStaticDescriptor()
{
    static const MarshalDesc Desc;
    return Desc;
}

const MarshalStringDesc& EchoStringDescriptor(const MarshalStringDesc& Desc, Bool InvalidUTF8)
{
    if (InvalidUTF8)
    {
        static constexpr char          Invalid[] = {static_cast<char>(0xc3), 'x', 0};
        static const MarshalStringDesc InvalidDesc{Invalid, nullptr, True};
        return InvalidDesc;
    }
    // The wrapper owns this input. Output conversion must finish before the
    // descriptor and its temporary strings are destroyed.
    return Desc;
}

Uint32 GetLiveSnapshotCount() { return LiveSnapshotCount; }
} // namespace Diligent
