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

namespace Diligent
{

// Combines the current aggregate status with one dependency status.
// A failure is terminal and takes priority over all non-failure statuses.
// A specific failure takes priority over FAILED, while FAILED takes priority
// over CANCELLED so that cancellation does not hide an execution failure.
// If there are no failures, PENDING takes priority over OK because the owner
// cannot report completion while any dependency is still loading.
// Other non-failure statuses, such as NO_GPU_DATA, are preserved unless a
// dependency is still pending.
inline RADIENT_STATUS CombineDependencyStatus(RADIENT_STATUS Status,
                                              RADIENT_STATUS DependencyStatus) noexcept
{
    // An OK dependency does not change the aggregate result.
    if (DependencyStatus == RADIENT_STATUS_OK)
        return Status;

    const auto GetFailurePriority = [](RADIENT_STATUS FailureStatus) noexcept {
        if (FailureStatus == RADIENT_STATUS_CANCELLED)
            return 1;
        if (FailureStatus == RADIENT_STATUS_FAILED)
            return 2;
        return 3;
    };

    // Prefer a more informative failure regardless of dependency traversal
    // order. Preserve the first failure when priorities are equal.
    if (RADIENT_FAILED(Status) &&
        RADIENT_FAILED(DependencyStatus) &&
        GetFailurePriority(DependencyStatus) > GetFailurePriority(Status))
    {
        return DependencyStatus;
    }

    // Preserve the first failure of the highest priority observed so far.
    if (RADIENT_FAILED(Status))
        return Status;

    // A newly observed failure overrides any non-failed aggregate status.
    if (RADIENT_FAILED(DependencyStatus))
        return DependencyStatus;

    // Pending has priority over other non-failure statuses: completion cannot
    // be reported while any dependency is still loading.
    if (Status == RADIENT_STATUS_PENDING ||
        DependencyStatus == RADIENT_STATUS_PENDING)
        return RADIENT_STATUS_PENDING;

    // If all previous dependencies were OK, adopt the first non-OK status.
    if (Status == RADIENT_STATUS_OK)
        return DependencyStatus;

    // Preserve an earlier non-failure, non-OK status.
    return Status;
}

} // namespace Diligent
