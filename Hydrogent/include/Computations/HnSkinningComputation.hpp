/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include <memory>
#include <array>

#include "BasicMath.hpp"

#include "HnExtComputationImpl.hpp"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/vt/types.h"

namespace Diligent
{

namespace USD
{

class HnSkinningComputation final : public HnExtComputationImpl
{
public:
    static constexpr ImplType Type = ImplType::Skinning;

    static std::unique_ptr<HnSkinningComputation> Create(HnExtComputation& Owner);

    HnSkinningComputation(HnExtComputation& Owner);
    ~HnSkinningComputation();

    virtual void Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    static bool IsCompatible(const HnExtComputation& Owner);

    const pxr::VtMatrix4fArray& GetXforms() const { return m_Xforms[m_CurrXformsIdx]; }
    const pxr::VtMatrix4fArray& GetLastFrameXforms() const { return m_Xforms[1 - m_CurrXformsIdx]; }
    size_t                      GetXformsHash() const { return m_XformsHash; }

    const float4x4& GetPrimWorldToLocal() const { return m_PrimWorldToLocal; }
    const float4x4& GetSkelLocalToWorld() const { return m_SkelLocalToWorld; }
    const float4x4& GetSkelLocalToPrimLocal() const { return m_SkelLocalToPrimLocal; }

private:
    // Keep two transforms to allow render passes reference previous-frame transforms
    std::array<pxr::VtMatrix4fArray, 2> m_Xforms;
    size_t                              m_CurrXformsIdx = 0;
    size_t                              m_XformsHash    = 0;

    float4x4 m_PrimWorldToLocal     = float4x4::Identity();
    float4x4 m_SkelLocalToWorld     = float4x4::Identity();
    float4x4 m_SkelLocalToPrimLocal = float4x4::Identity();
};

} // namespace USD

} // namespace Diligent
