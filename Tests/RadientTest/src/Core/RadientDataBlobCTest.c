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

int RadientDataBlob_C_TestAccess(void)
{
    Uint8                     InitialData[] = {7, 8, 9, 10};
    int                       Notifications = 0;
    RadientDataBlobCreateInfo CI            = {4, InitialData, CountNotification, &Notifications};
    IRadientDataBlob*         pBlob         = 0;
    void*                     WriteData     = 0;
    const void*               ReadData      = 0;
    int                       Result        = 0;
    if (Diligent_CreateRadientDataBlob(0, &pBlob) != RADIENT_STATUS_INVALID_ARGUMENT || pBlob != 0)
        return 1;
    if (Diligent_CreateRadientDataBlob(&CI, 0) != RADIENT_STATUS_INVALID_ARGUMENT)
        return 2;
    if (Diligent_CreateRadientDataBlob(&CI, &pBlob) != RADIENT_STATUS_OK || pBlob == 0)
        return 3;
    InitialData[0] = 0;
    if (IRadientDataBlob_BeginWrite(pBlob, &WriteData) != RADIENT_STATUS_OK || WriteData == 0)
        Result = 4;
    else
    {
        if (*(const Uint8*)WriteData != 7 || Notifications != 0)
            Result = 10;
        *(Uint8*)WriteData = 29;
        if (IRadientDataBlob_BeginRead(pBlob, &ReadData) != RADIENT_STATUS_INVALID_OPERATION || ReadData != 0)
            Result = 5;
        if (IRadientDataBlob_EndWrite(pBlob) != RADIENT_STATUS_OK)
            Result = 6;
        if (IRadientDataBlob_BeginRead(pBlob, &ReadData) != RADIENT_STATUS_OK || ReadData == 0)
            Result = 7;
        else
        {
            if (*(const Uint8*)ReadData != 29)
                Result = 8;
            if (IRadientDataBlob_EndRead(pBlob) != RADIENT_STATUS_OK || Notifications != 1)
                Result = 9;
        }
    }
    IObject_Release(pBlob);
    return Result;
}
