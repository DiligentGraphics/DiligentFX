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

#include "Assets/RadientAssetResolver.hpp"
#include "Assets/RadientFilesystemAssetResolver.hpp"

namespace Diligent
{

RADIENT_STATUS CheckAsset(IRadientAssetResolver*         pResolver,
                          const RadientAssetResolveInfo& ResolveInfo)
{
    if (pResolver == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientAssetLocation> pLocation;
    const RADIENT_STATUS                 Status =
        pResolver->ResolveAssetLocation(ResolveInfo, pLocation.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return pLocation != nullptr ?
        pResolver->CheckAsset(pLocation) :
        RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS OpenAsset(IRadientAssetResolver*         pResolver,
                         const RadientAssetResolveInfo& ResolveInfo,
                         IRadientAssetData**            ppData)
{
    if (ppData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppData = nullptr;

    if (pResolver == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<IRadientAssetLocation> pLocation;
    const RADIENT_STATUS                 Status =
        pResolver->ResolveAssetLocation(ResolveInfo, pLocation.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return pLocation != nullptr ?
        pResolver->OpenAsset(pLocation, ppData) :
        RADIENT_STATUS_INVALID_OPERATION;
}

// Creates the built-in resolver for filesystem paths and file:// URIs.
RefCntAutoPtr<IRadientAssetResolver> CreateDefaultRadientAssetResolver()
{
    return RefCntAutoPtr<RadientFilesystemAssetResolver>{
        MakeNewRCObj<RadientFilesystemAssetResolver>()()};
}

// Preserve a user resolver when supplied, otherwise provide the built-in resolver.
RefCntAutoPtr<IRadientAssetResolver> GetRadientAssetResolverOrDefault(IRadientAssetResolver* pResolver)
{
    if (pResolver != nullptr)
        return RefCntAutoPtr<IRadientAssetResolver>{pResolver};

    return CreateDefaultRadientAssetResolver();
}

} // namespace Diligent

extern "C"
{

    Diligent::RADIENT_STATUS Diligent_CheckAsset(
        Diligent::IRadientAssetResolver*         pResolver,
        const Diligent::RadientAssetResolveInfo& ResolveInfo)
    {
        return Diligent::CheckAsset(pResolver, ResolveInfo);
    }

    Diligent::RADIENT_STATUS Diligent_OpenAsset(
        Diligent::IRadientAssetResolver*         pResolver,
        const Diligent::RadientAssetResolveInfo& ResolveInfo,
        Diligent::IRadientAssetData**            ppData)
    {
        return Diligent::OpenAsset(pResolver, ResolveInfo, ppData);
    }
}
