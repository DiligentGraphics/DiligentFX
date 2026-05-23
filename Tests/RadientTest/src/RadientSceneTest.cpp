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

#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include "RadientEngine.h"
#include "RadientSceneImpl.hpp"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

TEST(RadientSceneTest, Create)
{
    RefCntAutoPtr<IRadientScene> pScene = RadientSceneImpl::Create();
    EXPECT_NE(pScene, nullptr);
}

TEST(RadientEngineTest, CreateObjects)
{
    RadientEngineCreateInfo EngineCI{};

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientBackend> pBackend;
    EXPECT_EQ(pEngine->GetBackend(&pBackend), RADIENT_STATUS_OK);
    ASSERT_NE(pBackend, nullptr);
    EXPECT_EQ(pBackend->GetDesc().Type, RADIENT_BACKEND_TYPE_LOCAL);

    RadientSceneDesc SceneDesc{};
    SceneDesc.Name = "Radient test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    EXPECT_EQ(pEngine->CreateScene(SceneDesc, &pScene), RADIENT_STATUS_OK);
    ASSERT_NE(pScene, nullptr);
    EXPECT_STREQ(pScene->GetDesc().Name, SceneDesc.Name);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    EXPECT_EQ(pEngine->CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    RadientRendererDesc RendererDesc{};
    RendererDesc.Name = "Radient test renderer";

    RefCntAutoPtr<IRadientRenderer> pRenderer;
    EXPECT_EQ(pEngine->CreateRenderer(RendererDesc, &pRenderer), RADIENT_STATUS_OK);
    ASSERT_NE(pRenderer, nullptr);
    EXPECT_STREQ(pRenderer->GetDesc().Name, RendererDesc.Name);

    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name = "Radient test target";
    TargetDesc.Size = {640, 480};

    RefCntAutoPtr<IRadientRenderTarget> pTarget;
    EXPECT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_OK);
    ASSERT_NE(pTarget, nullptr);
    EXPECT_STREQ(pTarget->GetDesc().Name, TargetDesc.Name);
    EXPECT_EQ(pTarget->GetDesc().Size, TargetDesc.Size);

    RadientRenderAttribs RenderAttribs{};
    RenderAttribs.pScene        = pScene;
    RenderAttribs.pRenderTarget = pTarget;

    EXPECT_EQ(pRenderer->Render(RenderAttribs), RADIENT_STATUS_OK);
}

} // namespace
