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

#include "StructPointers.h"

namespace Diligent
{

Uint64 ReadStructPointer(const MarshalDesc* pData)
{
    return pData ? pData->Size : 0;
}
Uint64 ReadRequiredPointer(const MarshalDesc* pData)
{
    return pData->Size;
}
const MarshalDesc* EchoStructPointer(const MarshalDesc* pData)
{
    return pData;
}
const MarshalPointerDesc& EchoPointerDesc(const MarshalPointerDesc& Desc)
{
    return Desc;
}

const MarshalDesc* GetDefaultPointerData()
{
    static const MarshalDesc Data{31, -2, 2.f, 3.0, False, MARSHAL_MODE_FIRST};
    return &Data;
}

const MarshalDefaultPointerDesc& EchoDefaultPointerDesc(const MarshalDefaultPointerDesc& Desc)
{
    return Desc;
}

const MarshalRequiredPointerDesc& EchoRequiredPointerDesc(const MarshalRequiredPointerDesc& Desc)
{
    return Desc;
}

const MarshalRequiredPointerDesc& GetRequiredPointerSnapshot(const MarshalPointerDesc& Desc)
{
    static MarshalRequiredPointerDesc Result;
    Result.pData = Desc.pData;
    return Result;
}

Float64 ReadNestedPointerValue(const MarshalNestedPointerDesc& Desc, Uint32 Field)
{
    switch (Field)
    {
        case 0: return Desc.pNested && Desc.pNested->pData ? static_cast<Float64>(Desc.pNested->pData->Size) : -1;
        case 1: return Desc.pInterfaces ? static_cast<Float64>(SumInterfaceDescriptors(Desc.pInterfaces->pItems, Desc.pInterfaces->Count)) : -1;
        case 2: return Desc.pArrays && Desc.pArrays->pItems && Desc.pArrays->ItemCount ? static_cast<Float64>(Desc.pArrays->pItems[0].Id) : -1;
        case 3: return Desc.pArrays && Desc.pArrays->pValues && Desc.pArrays->ValueCount ? Desc.pArrays->pValues[0] : -1;
        case 4: return Desc.pImage ? Desc.pImage->Pixels[sizeof(Desc.pImage->Pixels) - 1] : -1;
        default: return -2;
    }
}

const MarshalNestedPointerDesc& EchoNestedPointerDesc(const MarshalNestedPointerDesc& Desc, Bool InvalidLabel)
{
    if (!InvalidLabel)
        return Desc;
    // Copy all pointed-to snapshots before rejecting the trailing malformed string.
    static MarshalNestedPointerDesc Result;
    Result       = Desc;
    Result.Label = "\xED\xA0\x80";
    return Result;
}

const MarshalNestedPointerDesc* EchoNestedPointerArray(const MarshalNestedPointerDesc* pItems, Uint32 Count)
{
    (void)Count;
    return pItems;
}

MarshalReadOnlyPointerDesc GetReadOnlyPointerDesc(Bool Empty)
{
    static const MarshalReadOnlyChild Child;
    return {Empty ? nullptr : &Child};
}

MarshalReadOnlyArrayDesc GetReadOnlyArrayDesc(Bool Empty)
{
    static const MarshalReadOnlyChild Children[] = {{11}, {23}};
    return {Empty ? nullptr : Children, Empty ? 0u : 2u};
}

MarshalReadOnlyPointerGroup GetReadOnlyPointerGroup()
{
    static const MarshalReadOnlyPointerDesc Pointer = GetReadOnlyPointerDesc(False);
    static const MarshalReadOnlyArrayDesc   Array   = GetReadOnlyArrayDesc(False);
    return {&Pointer, &Array};
}

} // namespace Diligent
