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

#include "RadientAnimation.h"

namespace Diligent
{

struct RadientAnimationClipChannelIndex
{
    // TargetCount + 1 offsets into pChannelIndices. The range for target i is
    // [pTargetOffsets[i], pTargetOffsets[i + 1]).
    const Uint32* pTargetOffsets = nullptr;

    // ChannelCount indices into RadientAnimationClipDesc::pChannels, grouped
    // by target while preserving descriptor order within every target.
    const Uint32* pChannelIndices = nullptr;

    // The pointers borrow immutable packed storage owned by the clip and use
    // these counts to verify that the index matches its descriptor.
    Uint32 TargetCount  = 0;
    Uint32 ChannelCount = 0;
};

RADIENT_STATUS CreateRadientAnimationBinding(IRadientAnimationClipAsset*             pClip,
                                             const RadientAnimationClipChannelIndex& ChannelIndex,
                                             const RadientAnimationBindingDesc&      BindingDesc,
                                             IRadientAnimationBinding**              ppBinding);

} // namespace Diligent
