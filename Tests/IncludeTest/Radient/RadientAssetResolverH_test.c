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

#include "Radient/interface/RadientAssetResolver.h"

void RadientAssetResolver_C_UseTypes(void)
{
    RadientAssetResolveInfo ResolveInfo    = {0};
    IRadientAssetData*      pAssetData     = 0;
    IRadientAssetResolver*  pAssetResolver = 0;

    (void)ResolveInfo;
    (void)pAssetData;
    (void)pAssetResolver;
}

void RadientAssetResolver_C_TestMacros(IRadientAssetResolver* pAssetResolver,
                                       IRadientAssetData*     pAssetData)
{
    RadientAssetResolveInfo ResolveInfo = {0};
    const void*             pData       = IRadientAssetData_GetData(pAssetData);
    size_t                  DataSize    = IRadientAssetData_GetSize(pAssetData);
    const char*             ResolvedURI = IRadientAssetData_GetResolvedURI(pAssetData);
    RADIENT_STATUS          Status      = RADIENT_STATUS_OK;

    Status = IRadientAssetResolver_CheckAsset(pAssetResolver, &ResolveInfo);
    Status = IRadientAssetResolver_ResolveAsset(pAssetResolver, &ResolveInfo, &pAssetData);

    (void)pData;
    (void)DataSize;
    (void)ResolvedURI;
    (void)Status;
}
