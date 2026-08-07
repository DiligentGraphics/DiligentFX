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

#include "BasicMath.hpp"
#include "GraphicsTypes.h"
#include "TestingSwapChainBase.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Diligent
{
namespace Testing
{

struct RadientRenderTestOptions;

struct RadientRenderTestCamera
{
    float3 Eye;
    float3 Target = float3{0, 0, 0};
    float3 Up     = float3{0, 1, 0};
    float  Fov    = 45;
};

struct RadientRenderTestStatistics
{
    std::optional<Uint32> MultiDrawIndexed;
    std::optional<Uint32> MapBufferMax;
    std::optional<Uint32> UpdateBufferMax;
};

struct RadientRenderTestCase
{
    std::string Name;
    std::string Model;

    RadientRenderTestCamera                                                                               Camera;
    TestImageComparisonAttribs                                                                            Comparison;
    std::array<std::optional<RadientRenderTestStatistics>, static_cast<size_t>(RENDER_DEVICE_TYPE_COUNT)> Statistics;
};

struct RadientRenderTestManifest
{
    Uint32                             Version = 0;
    std::vector<RadientRenderTestCase> Tests;
};

bool InitializeRadientRenderTestManifest(const RadientRenderTestOptions& Options);

const RadientRenderTestManifest& GetRadientRenderTestManifest();

} // namespace Testing
} // namespace Diligent
