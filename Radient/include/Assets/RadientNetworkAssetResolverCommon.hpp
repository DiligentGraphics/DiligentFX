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

#include "RadientAssetResolver.h"
#include "ObjectBase.hpp"

#include <limits>
#include <string>
#include <utility>

namespace Diligent
{
namespace NetworkAssetResolverDetail
{

// {A316F08D-BB3D-4BF1-9C92-7AEC3E321931}
constexpr INTERFACE_ID IID_NetworkAssetLocation =
    {0xa316f08d, 0xbb3d, 0x4bf1, {0x9c, 0x92, 0x7a, 0xec, 0x3e, 0x32, 0x19, 0x31}};

class NetworkAssetLocation final : public ObjectBase<IRadientAssetLocation>
{
public:
    using TBase = ObjectBase<IRadientAssetLocation>;
    NetworkAssetLocation(IReferenceCounters* Counters, std::string URL) :
        TBase{Counters}, m_URL{std::move(URL)} {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientAssetLocation, IID_NetworkAssetLocation, TBase)

    const Char* DILIGENT_CALL_TYPE GetLocation() const override { return m_URL.c_str(); }

private:
    const std::string m_URL;
};

inline RADIENT_STATUS GetHTTPStatus(long Code)
{
    if (Code == 404 || Code == 410)
        return RADIENT_STATUS_NOT_FOUND;
    // No range was requested. A partial response is not a complete asset.
    return Code >= 200 && Code < 300 && Code != 206 ? RADIENT_STATUS_OK : RADIENT_STATUS_FAILED;
}

inline bool IsValidCreateInfo(const RadientNetworkAssetResolverCreateInfo& Info)
{
    constexpr Uint32 MaxTimeout = static_cast<Uint32>((std::numeric_limits<Int32>::max)());
    return Info.ConnectTimeoutMs != 0 && Info.ConnectTimeoutMs <= MaxTimeout &&
        Info.RequestTimeoutMs != 0 && Info.RequestTimeoutMs <= MaxTimeout && Info.MaxRedirects <= MaxTimeout;
}

} // namespace NetworkAssetResolverDetail
} // namespace Diligent
