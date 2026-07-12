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

#pragma once

#include "RadientTypes.h"

#include <atomic>
#include <string>

namespace Diligent
{

inline std::string MakeRadientAssetURI(const char*   Type,
                                       RadientHandle AssetID)
{
    return std::string{"radient://session/"} + Type + "/" + std::to_string(AssetID);
}

inline RadientHandle AllocateRadientAssetID() noexcept
{
    static std::atomic<RadientHandle> NextAssetID{1};
    return NextAssetID.fetch_add(1, std::memory_order_relaxed);
}

inline std::string MakeRadientAssetURI(const char* Type)
{
    return MakeRadientAssetURI(Type, AllocateRadientAssetID());
}

inline std::string MakeRadientAssetCacheURI(const char*        Type,
                                            const std::string& CacheKey)
{
    return std::string{"radient://cache/"} + Type + "/" + CacheKey;
}

} // namespace Diligent
