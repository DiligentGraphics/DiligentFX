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

// Compile the same declarations with their actual C interface definitions.
Uint64 CheckCInterface(IMarshalMutableObject* pObject)
{
    IMarshalMutableObject_SetSize(pObject, 17);
    return IMarshalObject_GetSize((IMarshalObject*)pObject);
}

Uint64 CheckCByteInterface(IMarshalBytes* pBytes)
{
    const void* pData = 0;
    if (IMarshalBytes_MapRead(pBytes, &pData) == MARSHAL_STATUS_OK)
        IMarshalBytes_UnmapRead(pBytes);
    return IMarshalBytes_Size(pBytes);
}

const Char* CheckCStringInterface(IMarshalStrings* pStrings)
{
    const Char* Text = 0;
    IMarshalStrings_SetText(pStrings, "C string");
    IMarshalStrings_ReadText(pStrings, False, &Text);
    return Text;
}

Float32 CheckCNestedStruct(void)
{
    MarshalNestedDesc Desc = {0};
    Desc.Transform.Scale.x = 1.f;
    return Diligent_ReadNestedValue(&Desc, 3);
}

const MarshalSnapshotDesc* CheckCStructResult(IMarshalSnapshots* pSnapshots)
{
    return IMarshalSnapshots_GetDesc(pSnapshots);
}

Float64 CheckCArrayInput(const MarshalArrayItem* pItems, Uint32 Count)
{
    MarshalArrayDesc Desc = {0};
    Desc.pItems           = pItems;
    Desc.ItemCount        = Count;
    return Diligent_ReadArrayDesc(&Desc, 0, 0);
}

const MarshalOutputArrayDesc* CheckCArrayOutput(IMarshalArraySnapshots* pSnapshots)
{
    const Float32* Values = IMarshalArraySnapshots_GetValues(pSnapshots, 1);
    (void)Values;
    return IMarshalArraySnapshots_GetDesc(pSnapshots);
}

Float32 CheckCFixedArrays(void)
{
    MarshalFixedArrays Desc   = {0};
    Desc.Rows[1][2]           = 3.f;
    Desc.Matrices[1].Data[15] = 1.f;
    return Diligent_ReadFixedRow(&Desc, 1, 2);
}

Uint64 CheckCInterfaceReferences(IMarshalMutableObject* pMutable, const IMarshalObject* pSource)
{
    MarshalDesc Desc = {0};
    Diligent_WriteObjectReference(pMutable, &Desc);
    IMarshalMutableObject_CopySizeFrom(pMutable, pSource);
    IMarshalMutableObject_CopySizeTo(pMutable, pMutable);
    return Diligent_ReadObjectReference(pSource);
}

void CheckCComputedOutputs(IMarshalComputedOutputs* pSource)
{
    MarshalComputedMatrix Matrix  = {0};
    MarshalDesc           Options = {0};
    Uint32                Value   = 0;
    IMarshalComputedOutputs_FillMatrices(pSource, pSource, &Matrix, False, False);
    Diligent_FillComputedValues(&Value, pSource, &Options);
}
