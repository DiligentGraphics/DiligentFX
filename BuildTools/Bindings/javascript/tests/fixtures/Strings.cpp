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

#include "StringMarshalling.h"

#include <cstring>
#include <string>
#include <emscripten/heap.h>

namespace Diligent
{
namespace
{
Uint32 StringCallCount = 0;
Uint32 LiveStringCount = 0;

// A, e-acute, CJK water, and a supplementary-plane smile: independently authored
// UTF-8 bytes catch encoding mistakes that a simple JS/native round trip could hide.
constexpr char UTF8Text[] = "A\xc3\xa9\xe6\xb0\xb4\xf0\x9f\x98\x80";

class MarshalStrings final : public IMarshalStrings
{
public:
    explicit MarshalStrings(const Char* Text)
    {
        SetText(Text);
        ++LiveStringCount;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalStrings)
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

    void SetText(const Char* Text) override
    {
        m_IsNull = Text == nullptr;
        m_Text   = Text != nullptr ? Text : "";
    }
    const Char* GetText() const override { return m_IsNull ? nullptr : m_Text.c_str(); }

    MARSHAL_STATUS ReadText(Bool Fail, const Char*& Text) const override
    {
        // Leave the output untouched on failure to check wrapper initialization.
        if (Fail)
            return MARSHAL_STATUS_FAILED;
        Text = GetText();
        return MARSHAL_STATUS_PENDING;
    }
    MARSHAL_STATUS ReadTextPointer(Bool Fail, const Char** ppText) const override
    {
        return ReadText(Fail, *ppText);
    }

private:
    ~MarshalStrings() { --LiveStringCount; }
    std::string m_Text;
    bool        m_IsNull = false;
    Uint32      m_Refs   = 1;
};
} // namespace

MARSHAL_STATUS CreateMarshalStrings(const Char* Text, IMarshalStrings** ppObject)
{
    *ppObject = new MarshalStrings{Text};
    return MARSHAL_STATUS_OK;
}
const Char* GetStringDescName(const MarshalStringDesc& Desc)
{
    ++StringCallCount;
    return Desc.Name;
}
const char* GetStringDescLabel(const MarshalStringDesc& Desc)
{
    ++StringCallCount;
    return Desc.Label;
}
const Char* EchoString(const Char* Text, Bool GrowHeap)
{
    ++StringCallCount;
    if (GrowHeap)
        emscripten_resize_heap(emscripten_get_heap_size() + 65536);
    return Text;
}
const char* EchoNullableString(const char* Text)
{
    ++StringCallCount;
    return Text;
}
Bool CheckStringBytes(const Char* Text) { return std::strcmp(Text, UTF8Text) == 0; }
Bool CheckCharacterBytes(Uint32 ByteCount, const Char* Bytes)
{
    return ByteCount == 3 && Bytes[0] == 'A' && Bytes[1] == 0 && Bytes[2] == 'Z';
}
const Char* GetStaticString() { return UTF8Text; }
const Char* GetInvalidUTF8()
{
    static constexpr char Invalid[] = {static_cast<char>(0xc3), 'x', 0};
    return Invalid;
}
Uint32 GetStringCallCount() { return StringCallCount; }
Uint32 GetLiveStringCount() { return LiveStringCount; }
} // namespace Diligent
