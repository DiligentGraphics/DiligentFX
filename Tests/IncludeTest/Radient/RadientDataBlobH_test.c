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

#include "Radient/interface/RadientDataBlob.h"

static void OnLastReaderReleased(IRadientDataBlob* pBlob, void* pUserData)
{
    (void)pBlob;
    (void)pUserData;
}

void RadientDataBlob_C_UseTypes(void)
{
    static const Uint8                     InitialData[16] = {1, 2, 3, 4};
    static const RadientDataBlobCreateInfo CI              = {sizeof(InitialData), InitialData, OnLastReaderReleased, 0};
    IRadientDataBlob*                      pBlob           = 0;
    const void*                            pReadData       = 0;
    void*                                  pWriteData      = 0;
    if (Diligent_CreateRadientDataBlob(&CI, &pBlob) != RADIENT_STATUS_OK)
        return;
    (void)IRadientDataBlob_GetSize(pBlob);
    if (IRadientDataBlob_BeginWrite(pBlob, &pWriteData) == RADIENT_STATUS_OK)
        (void)IRadientDataBlob_EndWrite(pBlob);
    if (IRadientDataBlob_BeginRead(pBlob, &pReadData) == RADIENT_STATUS_OK)
        (void)IRadientDataBlob_EndRead(pBlob);
    IObject_Release(pBlob);
}
