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

#include "RadientDataBlob.h"

static void CountNotification(IRadientDataBlob* pBlob, void* pUserData)
{
    if (IRadientDataBlob_GetSize(pBlob) == 4)
        ++*(int*)pUserData;
}

static void CountDestruction(void* pUserData)
{
    ++*(int*)pUserData;
}

int RadientDataBlob_C_TestAccess(void)
{
    Uint8                     InitialData[] = {7, 8, 9, 10};
    int                       Notifications = 0;
    RadientDataBlobCreateInfo CI            = {4, InitialData, CountNotification, &Notifications, CountDestruction};
    IRadientDataBlob*         pReadOnly     = 0;
    IRadientMutableDataBlob*  pMutable      = 0;
    void*                     WriteData     = 0;
    const void*               ReadData      = 0;
    int                       Result        = 0;
    if (Diligent_CreateRadientDataBlob(0, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pReadOnly) != RADIENT_STATUS_INVALID_ARGUMENT || pReadOnly != 0)
        return 1;
    if (Diligent_CreateRadientDataBlob(&CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, 0) != RADIENT_STATUS_INVALID_ARGUMENT)
        return 2;
    if (Diligent_CreateRadientMutableDataBlob(0, &pMutable) != RADIENT_STATUS_INVALID_ARGUMENT || pMutable != 0)
        return 3;
    if (Diligent_CreateRadientMutableDataBlob(&CI, 0) != RADIENT_STATUS_INVALID_ARGUMENT || Notifications != 0)
        return 4;
    if (Diligent_CreateRadientMutableDataBlob(&CI, &pMutable) != RADIENT_STATUS_OK || pMutable == 0)
        return 5;
    InitialData[0] = 0;
    if (IRadientMutableDataBlob_GetSize(pMutable) != 4)
        Result = 6;
    if (IRadientMutableDataBlob_BeginWrite(pMutable, &WriteData) != RADIENT_STATUS_OK || WriteData == 0)
        Result = 7;
    else
    {
        if (*(const Uint8*)WriteData != 7 || Notifications != 0)
            Result = 8;
        *(Uint8*)WriteData = 29;
        if (IRadientMutableDataBlob_Resize(pMutable, 4) != RADIENT_STATUS_INVALID_OPERATION)
            Result = 25;
        if (IRadientMutableDataBlob_BeginRead(pMutable, &ReadData) != RADIENT_STATUS_INVALID_OPERATION || ReadData != 0)
            Result = 9;
        if (IRadientMutableDataBlob_EndWrite(pMutable) != RADIENT_STATUS_OK)
            Result = 10;
        if (IRadientMutableDataBlob_BeginRead(pMutable, &ReadData) != RADIENT_STATUS_OK || ReadData == 0)
            Result = 11;
        else
        {
            if (*(const Uint8*)ReadData != 29)
                Result = 12;
            if (IRadientMutableDataBlob_Resize(pMutable, 8) != RADIENT_STATUS_INVALID_OPERATION)
                Result = 26;
            if (IRadientMutableDataBlob_EndRead(pMutable) != RADIENT_STATUS_OK || Notifications != 1)
                Result = 13;
        }
    }
    if (IRadientMutableDataBlob_Resize(pMutable, 8) != RADIENT_STATUS_OK || IRadientMutableDataBlob_GetSize(pMutable) != 8 || Notifications != 1)
        Result = 27;
    if (IRadientMutableDataBlob_BeginWrite(pMutable, &WriteData) != RADIENT_STATUS_OK || WriteData == 0)
        Result = 28;
    else
    {
        const Uint8* Bytes = (const Uint8*)WriteData;
        if (IRadientMutableDataBlob_GetSize(pMutable) != 8 || Bytes[0] != 29 || Bytes[1] != 8 || Bytes[2] != 9 || Bytes[3] != 10 || Bytes[4] != 0 || Bytes[5] != 0 || Bytes[6] != 0 || Bytes[7] != 0)
            Result = 29;
        if (IRadientMutableDataBlob_EndWrite(pMutable) != RADIENT_STATUS_OK)
            Result = 30;
    }
    if (IRadientMutableDataBlob_Resize(pMutable, 0) != RADIENT_STATUS_OK || IRadientMutableDataBlob_GetSize(pMutable) != 0 || Notifications != 1)
        Result = 31;
    IObject_Release(pMutable);
    pMutable = 0;
    if (Notifications != 2)
        Result = 14;
    if (Result != 0)
        return Result;

    if (Diligent_CreateRadientDataBlob(&CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pReadOnly) != RADIENT_STATUS_OK || pReadOnly == 0)
        return 15;
    if (IRadientDataBlob_BeginRead(pReadOnly, &ReadData) != RADIENT_STATUS_OK)
        Result = 16;
    else
    {
        if (ReadData != InitialData || IRadientDataBlob_GetSize(pReadOnly) != 4)
            Result = 17;
        if (IRadientDataBlob_EndRead(pReadOnly) != RADIENT_STATUS_OK || Notifications != 3)
            Result = 18;
    }
    IObject_Release(pReadOnly);
    pReadOnly = 0;
    if (Notifications != 4)
        Result = 19;
    if (Result != 0)
        return Result;

    if (Diligent_CreateRadientDataBlob(&CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pReadOnly) != RADIENT_STATUS_OK || pReadOnly == 0)
        return 20;
    InitialData[0] = 57;
    if (IRadientDataBlob_BeginRead(pReadOnly, &ReadData) != RADIENT_STATUS_OK)
        Result = 21;
    else
    {
        if (ReadData == InitialData || *(const Uint8*)ReadData != 0)
            Result = 22;
        if (IRadientDataBlob_EndRead(pReadOnly) != RADIENT_STATUS_OK || Notifications != 5)
            Result = 23;
    }
    IObject_Release(pReadOnly);
    pReadOnly = 0;
    if (Notifications != 6)
        Result = 24;
    return Result;
}
