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

#include "Render/Tessera/RadientTesseraFrameHistory.hpp"
#include "Scene/RadientSceneImpl.hpp"

#include "gtest/gtest.h"

using namespace Diligent;

namespace
{

RadientMatrix4x4 MakeTranslation(float X)
{
    RadientMatrix4x4 Transform;
    Transform.Data[12] = X;
    return Transform;
}

TEST(RadientTesseraFrameHistoryTest, TracksConsecutiveDrawableTransforms)
{
    RadientTesseraFrameHistory History;

    const RadientMatrix4x4 Frame0 = MakeTranslation(1.f);
    EXPECT_EQ(History.UpdateDrawableTransform(3, 1, Frame0), Frame0);

    History.CommitFrame();
    const RadientMatrix4x4 Frame1 = MakeTranslation(2.f);
    EXPECT_EQ(History.UpdateDrawableTransform(3, 1, Frame1), Frame0);

    // Repeated rendering in one frame keeps the same previous transform.
    EXPECT_EQ(History.UpdateDrawableTransform(3, 1, MakeTranslation(3.f)), Frame0);

    History.CommitFrame();
    History.CommitFrame();
    const RadientMatrix4x4 AfterGap = MakeTranslation(4.f);
    EXPECT_EQ(History.UpdateDrawableTransform(3, 1, AfterGap), AfterGap);

    History.CommitFrame();
    const RadientMatrix4x4 Recycled = MakeTranslation(5.f);
    EXPECT_EQ(History.UpdateDrawableTransform(3, 2, Recycled), Recycled);
}

TEST(RadientTesseraFrameHistoryTest, KeepsCameraHistoryPerSceneCameraAndViewport)
{
    RadientTesseraFrameHistory      History;
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();
    ASSERT_NE(pScene, nullptr);

    RadientTesseraCameraState Camera;
    Camera.Scene        = pScene.RawPtr();
    Camera.Camera       = 7;
    Camera.ViewportSize = {1280, 720};
    Camera.World        = MakeTranslation(1.f);
    Camera.IsValid      = true;

    EXPECT_EQ(History.GetPreviousCamera(Camera), nullptr);
    History.SetCurrentCamera(Camera);
    History.CommitFrame();

    RadientTesseraCameraState Current = Camera;
    Current.World                     = MakeTranslation(2.f);
    ASSERT_NE(History.GetPreviousCamera(Current), nullptr);
    EXPECT_EQ(History.GetPreviousCamera(Current)->World, Camera.World);

    Current.ViewportSize = {1920, 1080};
    EXPECT_EQ(History.GetPreviousCamera(Current), nullptr);
    History.SetCurrentCamera(Current);
    const RadientMatrix4x4 ResizedTransform = MakeTranslation(8.f);
    EXPECT_EQ(History.UpdateDrawableTransform(3, 1, ResizedTransform), ResizedTransform);

    Current        = Camera;
    Current.Camera = 8;
    EXPECT_EQ(History.GetPreviousCamera(Current), nullptr);

    RefCntAutoPtr<RadientSceneImpl> pOtherScene = RadientSceneImpl::Create();
    ASSERT_NE(pOtherScene, nullptr);
    Current       = Camera;
    Current.Scene = pOtherScene.RawPtr();
    EXPECT_EQ(History.GetPreviousCamera(Current), nullptr);

    RefCntWeakPtr<IRadientScene> WeakScene{pScene.RawPtr()};
    pScene.Release();
    EXPECT_EQ(WeakScene.Lock(), nullptr);
}

} // namespace
