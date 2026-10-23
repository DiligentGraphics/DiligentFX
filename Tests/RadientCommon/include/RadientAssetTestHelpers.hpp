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

#include "RadientAssets.h"
#include "ThreadPool.h"

#include <thread>

namespace Diligent
{

namespace Testing
{

// Waits for CPU-only asset loading, executing queued work on the calling thread.
// The fixture must keep the pool running until this function returns. Do not use
// this for GPU-backed loading: tasks may block on staging space while this thread
// is unable to advance uploads. GPU tests must pump uploads while workers run.
inline RADIENT_STATUS WaitForAssetLoad(IRadientAsset* pAsset, IThreadPool& ThreadPool)
{
    for (;;)
    {
        const RADIENT_STATUS Status = GetRadientAssetLoadStatus(pAsset);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        if (!ThreadPool.ProcessTask(0, false))
            std::this_thread::yield();
    }
}

} // namespace Testing

} // namespace Diligent
