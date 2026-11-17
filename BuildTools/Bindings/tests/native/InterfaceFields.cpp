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

#include "InterfaceFields.h"
#include "Interfaces.h"

namespace Diligent
{

Uint64 SumInterfaceArray(IMarshalObject* const* ppObjects, Uint32 Count)
{
    Uint64 Sum = 0;
    for (Uint32 i = 0; i < Count; ++i)
    {
        if (ppObjects[i])
            Sum += ppObjects[i]->GetSize();
    }
    return Sum;
}
Uint64 SumInterfaces(const MarshalInterfaceDesc& Desc)
{
    return (Desc.pObject ? Desc.pObject->GetSize() : 0) + SumInterfaceArray(Desc.ppObjects, Desc.Count);
}
const MarshalInterfaceDesc& EchoInterfaces(const MarshalInterfaceDesc& Desc)
{
    return Desc;
}


const MarshalInterfaceDesc& GetInvalidInterfaceSnapshot(const MarshalInterfaceDesc& Desc)
{
    // Snapshot conversion retains the interface field and array elements before
    // reaching the malformed trailing string. All must be released on failure.
    static MarshalInterfaceDesc Result;
    Result       = Desc;
    Result.Label = "\xED\xA0\x80";
    return Result;
}

IMarshalObject* const* EchoInterfaceArray(IMarshalObject* const* ppObjects, Uint32 Count)
{
    (void)Count;
    return ppObjects;
}
IMarshalObject* const* GetRequiredInterfaceArray(IMarshalObject* const* ppObjects, Uint32 Count)
{
    (void)Count;
    return ppObjects;
}
Bool InterfaceArrayHasStorage(IMarshalObject* const* ppObjects, Uint32 Count)
{
    (void)Count;
    return ppObjects != nullptr;
}
const MarshalInterfaceGroup& EchoInterfaceGroup(const MarshalInterfaceGroup& Group, Bool InvalidLabel)
{
    if (!InvalidLabel)
        return Group;
    static MarshalInterfaceGroup Result;
    Result       = Group;
    Result.Label = "\xED\xA0\x80";
    return Result;
}
const MarshalRequiredInterfaceDesc& EchoRequiredInterfaces(const MarshalRequiredInterfaceDesc& Desc)
{
    return Desc;
}
const MarshalRequiredInterfaceDesc& GetRequiredInterfaceSnapshot(const MarshalInterfaceDesc& Desc)
{
    static MarshalRequiredInterfaceDesc Result;
    Result.pObject   = Desc.pObject;
    Result.ppObjects = Desc.ppObjects;
    Result.Count     = Desc.Count;
    return Result;
}

void CopyInterfaceArray(IMarshalObject* const* ppObjects, Uint32 Count, IMarshalObject** ppOutput, Uint32 Capacity, Uint32& Written)
{
    Written = Count < Capacity ? Count : Capacity;
    for (Uint32 Index = 0; Index < Written; ++Index)
    {
        // Output elements are borrowed; the binding owns any references it acquires.
        ppOutput[Index] = ppObjects[Index];
    }
}

} // namespace Diligent
