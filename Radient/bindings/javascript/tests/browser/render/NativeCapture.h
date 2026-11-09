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

#include "../../../BrowserNative.h"

namespace Diligent
{

// Test-only image operations. Scene loading, animation and rendering are driven
// exclusively by the JavaScript fixture through the production bindings.
struct BrowserImageComparison
{
    bool   HasReference    = false;
    bool   SameDimensions  = false;
    bool   Passed          = false;
    Uint32 DifferentPixels = 0;
    Uint32 BadPixels       = 0;
    Uint32 MaxDifference   = 0;
};

RADIENT_STATUS BeginBrowserImageCapture(IRadientEngine* pEngine);
RADIENT_STATUS SaveBrowserImageCapture(const char* Path);
void           ReleaseBrowserImageCapture();
RADIENT_STATUS CompareBrowserImages(const char* ActualPath, const char* ReferencePath, const char* DifferencePath, Uint32 MaxChannelError, float MaxBadPixelRatio, BrowserImageComparison& Result);

} // namespace Diligent
