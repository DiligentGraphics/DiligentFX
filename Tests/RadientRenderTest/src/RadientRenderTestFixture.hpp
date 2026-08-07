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

#include "gtest/gtest.h"

namespace Diligent
{

typedef struct IRadientAssetManager IRadientAssetManager;
typedef struct IRadientEngine       IRadientEngine;
typedef struct IRadientRenderer     IRadientRenderer;
typedef struct IRadientTextureAsset IRadientTextureAsset;
typedef struct IRadientView         IRadientView;

namespace Testing
{

/// Base fixture for Radient render tests. The engine, renderer, and view are
/// created once for the complete suite so tests in different source files can
/// reuse renderer caches and persistent view resources such as IBL cubemaps.
class RadientRenderTestFixture : public ::testing::Test
{
public:
    static void SetUpTestSuite();
    static void TearDownTestSuite();

protected:
    static IRadientEngine*       GetEngine();
    static IRadientAssetManager* GetAssetManager();
    static IRadientRenderer*     GetRenderer();
    static IRadientTextureAsset* GetEnvironmentMap();
    static IRadientView*         GetView();
};

// TEST_F uses the fixture token as the suite name. This alias lets regular and
// dynamically registered tests share the RadientRender suite and fixture.
using RadientRender = RadientRenderTestFixture;

} // namespace Testing
} // namespace Diligent
