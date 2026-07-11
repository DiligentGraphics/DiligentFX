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

using namespace Diligent;

void RadientAssetResolver_CPP_UseTypes()
{
    RadientAssetResolveInfo ResolveInfo;
    IRadientAssetResolver*  pAssetResolver = nullptr;
    IRadientAssetLocation*  pLocation      = nullptr;
    IRadientAssetData*      pAssetData     = nullptr;
    const char*             LocationURI    = pLocation != nullptr ? pLocation->GetLocation() : nullptr;
    const void*             pData          = pAssetData != nullptr ? pAssetData->GetData() : nullptr;
    size_t                  DataSize       = pAssetData != nullptr ? pAssetData->GetSize() : 0;
    const char*             ResolvedURI    = pAssetData != nullptr ? pAssetData->GetResolvedURI() : nullptr;
    RADIENT_STATUS          Status         = RADIENT_STATUS_INVALID_OPERATION;

    if (pAssetResolver != nullptr)
    {
        Status = pAssetResolver->ResolveAssetLocation(ResolveInfo, &pLocation);
        Status = pAssetResolver->CheckAsset(pLocation);
        Status = pAssetResolver->OpenAsset(pLocation, &pAssetData);
        Status = CheckAsset(pAssetResolver, ResolveInfo);
        Status = OpenAsset(pAssetResolver, ResolveInfo, &pAssetData);
    }

    (void)ResolveInfo;
    (void)pAssetResolver;
    (void)pLocation;
    (void)pAssetData;
    (void)LocationURI;
    (void)pData;
    (void)DataSize;
    (void)ResolvedURI;
    (void)Status;
}
