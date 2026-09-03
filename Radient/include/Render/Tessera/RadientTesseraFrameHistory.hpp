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

#include "Render/RadientDrawList.hpp"
#include "RadientScene.h"
#include "BasicMath.hpp"
#include "RefCntAutoPtr.hpp"

#include <vector>

namespace Diligent
{

/// Camera state captured at the beginning of a Tessera view frame.
struct RadientTesseraCameraState
{
    /// Identifies the scene without extending its lifetime. Camera entity IDs
    /// are scene-local, so changing scenes invalidates temporal history.
    RefCntWeakPtr<IRadientScene> Scene;
    RadientEntityID              Camera = InvalidRadientEntityID;
    RadientCameraComponent       Attribs;
    RadientMatrix4x4             World;
    RadientExtent2D              ViewportSize;
    float2                       Jitter;
    bool                         IsValid = false;
};

/// Per-view temporal state used to generate camera and object motion vectors.
/// Global render frame IDs invalidate all history when the view skips a frame.
/// Drawable generations prevent recycled IDs from inheriting old transforms,
/// while view frame numbers invalidate history after a drawable was not
/// rendered by an otherwise continuously rendered view.
class RadientTesseraFrameHistory
{
public:
    /// Starts history tracking for a view rendered during RenderFrameID.
    /// Temporal history is reset unless the view was last rendered during the
    /// immediately preceding global render frame.
    void BeginFrame(RadientFrameID RenderFrameID) noexcept;

    Uint32 GetFrameIndex() const noexcept;
    bool   HasCameraHistory() const noexcept;

    const RadientTesseraCameraState* GetPreviousCamera(const RadientTesseraCameraState& CurrentCamera) const noexcept;
    void                             SetCurrentCamera(const RadientTesseraCameraState& CurrentCamera) noexcept;

    RadientMatrix4x4 UpdateDrawableTransform(RadientDrawableID       DrawableID,
                                             Uint32                  Generation,
                                             const RadientMatrix4x4& CurrentTransform);

    void CommitFrame() noexcept;

private:
    static constexpr Uint64 InvalidFrameNumber = ~Uint64{0};

    void ResetTemporalHistory() noexcept;

    struct DrawableState
    {
        RadientMatrix4x4 PreviousTransform;
        RadientMatrix4x4 CurrentTransform;
        Uint64           LastFrame  = InvalidFrameNumber;
        Uint32           Generation = 0;
    };

    RadientTesseraCameraState  m_PreviousCamera;
    RadientTesseraCameraState  m_CurrentCamera;
    std::vector<DrawableState> m_Drawables;
    Uint64                     m_FrameNumber          = 0;
    bool                       m_HasCameraHistory     = false;
    RadientFrameID             m_CurrentRenderFrameID = InvalidRadientFrameID;
    RadientFrameID             m_LastRenderFrameID    = InvalidRadientFrameID;
};

} // namespace Diligent
