/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include "HnRenderParam.hpp"

namespace Diligent
{

namespace USD
{

HnRenderParam::HnRenderParam(const Configuration& Config, bool UseShadows) noexcept :
    m_Config{Config},
    m_UseShadows{UseShadows}
{
    for (auto& Version : m_GlobalAttribVersions)
        Version.store(0);
}

HnRenderParam::~HnRenderParam()
{
}

void HnRenderParam::AddDirtyRPrim(const pxr::SdfPath& RPrimId, pxr::HdDirtyBits DirtyBits)
{
    DirtyRPrimsVector* pDirtyRPrims = nullptr;
    {
        Threading::SpinLockGuard Guard{m_DirtyRPrimsLock};

        auto it = m_DirtyRPrimsPerThread.find(std::this_thread::get_id());
        if (it == m_DirtyRPrimsPerThread.end())
        {
            it = m_DirtyRPrimsPerThread.emplace(std::this_thread::get_id(), DirtyRPrimsVector{}).first;
            it->second.reserve(16);
        }
        pDirtyRPrims = &it->second;
    }
    pDirtyRPrims->emplace_back(RPrimId, DirtyBits);
}

void HnRenderParam::CommitDirtyRPrims(pxr::HdChangeTracker& ChangeTracker)
{
    Threading::SpinLockGuard Guard{m_DirtyRPrimsLock};

    m_LastDirtyRPrimCount = 0;
    for (const auto& DirtyRPrims : m_DirtyRPrimsPerThread)
    {
        for (const auto& RPrim : DirtyRPrims.second)
        {
            ChangeTracker.MarkRprimDirty(RPrim.first, RPrim.second);
        }
        m_LastDirtyRPrimCount += DirtyRPrims.second.size();
    }
    m_DirtyRPrimsPerThread.clear();
}

} // namespace USD

} // namespace Diligent
