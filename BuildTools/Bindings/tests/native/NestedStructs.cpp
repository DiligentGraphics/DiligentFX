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

#include "NestedStructs.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

namespace
{
Uint32 CallCount = 0;
}

Float32 DILIGENT_GLOBAL_FUNCTION(ReadNestedValue)(const MarshalNestedDesc& Desc, Uint32 Index)
{
    ++CallCount;
    // Expose the received values individually: the tests do not need struct
    // outputs or a second marshalled descriptor as their reference.
    const Float32 Values[] = {
        Desc.Transform.Position.x, Desc.Transform.Position.y, Desc.Transform.Position.z,
        Desc.Transform.Scale.x, Desc.Transform.Scale.y, Desc.Transform.Scale.z,
        Desc.Alternate.Position.x, Desc.Alternate.Position.y, Desc.Alternate.Position.z,
        Desc.Alternate.Scale.x, Desc.Alternate.Scale.y, Desc.Alternate.Scale.z};
    return Index < sizeof(Values) / sizeof(Values[0]) ? Values[Index] : -1.f;
}

Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedTag)(const MarshalNestedDesc& Desc)
{
    ++CallCount;
    return Desc.Tag;
}

const Char* DILIGENT_GLOBAL_FUNCTION(GetNestedName)(const MarshalNestedDesc& Desc)
{
    ++CallCount;
    return Desc.Data.Name;
}

Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedChecksum)(const MarshalNestedDesc& Desc)
{
    ++CallCount;
    if (Desc.Data.ByteCount == 0)
        return 0;
    if (Desc.Data.pBytes == nullptr)
        return ~0u;
    Uint32 Checksum = 0;
    for (Uint32 i = 0; i < Desc.Data.ByteCount; ++i)
    {
        Checksum += (i + 1) * Desc.Data.pBytes[i];
    }
    return Checksum;
}

Uint32 DILIGENT_GLOBAL_FUNCTION(GetNestedCallCount)()
{
    return CallCount;
}

DILIGENT_END_NAMESPACE
