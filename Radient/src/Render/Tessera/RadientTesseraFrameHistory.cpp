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

namespace Diligent
{

namespace
{

bool IsImmediatelyFollowingFrame(RadientFrameID PreviousFrameID,
                                 RadientFrameID CurrentFrameID) noexcept
{
    if (PreviousFrameID == InvalidRadientFrameID || CurrentFrameID == InvalidRadientFrameID)
        return false;

    RadientFrameID NextFrameID = PreviousFrameID + 1;
    if (NextFrameID == InvalidRadientFrameID)
        NextFrameID = 1;

    return CurrentFrameID == NextFrameID;
}

} // namespace

void RadientTesseraFrameHistory::BeginFrame(RadientFrameID RenderFrameID) noexcept
{
    VERIFY(RenderFrameID != InvalidRadientFrameID, "The global render frame ID must be valid");

    if (!IsImmediatelyFollowingFrame(m_LastRenderFrameID, RenderFrameID))
        ResetTemporalHistory();

    m_CurrentRenderFrameID = RenderFrameID;
}

Uint32 RadientTesseraFrameHistory::GetFrameIndex() const noexcept
{
    return static_cast<Uint32>(m_FrameNumber);
}

bool RadientTesseraFrameHistory::HasCameraHistory() const noexcept
{
    return m_HasCameraHistory;
}

const RadientTesseraCameraState* RadientTesseraFrameHistory::GetPreviousCamera(const RadientTesseraCameraState& CurrentCamera) const noexcept
{
    if (!m_PreviousCamera.IsValid ||
        m_PreviousCamera.Scene != CurrentCamera.Scene ||
        m_PreviousCamera.Camera != CurrentCamera.Camera ||
        m_PreviousCamera.ViewportSize != CurrentCamera.ViewportSize)
    {
        return nullptr;
    }

    return &m_PreviousCamera;
}

void RadientTesseraFrameHistory::SetCurrentCamera(const RadientTesseraCameraState& CurrentCamera) noexcept
{
    // A different scene, camera, or viewport has no compatible temporal
    // history. Object transforms must be reset with the camera so the first
    // frame in the new view configuration produces zero motion.
    m_HasCameraHistory = GetPreviousCamera(CurrentCamera) != nullptr;
    if (!m_HasCameraHistory)
        m_Drawables.clear();

    m_CurrentCamera = CurrentCamera;
}

RadientMatrix4x4 RadientTesseraFrameHistory::UpdateDrawableTransform(RadientDrawableID       DrawableID,
                                                                     Uint32                  Generation,
                                                                     const RadientMatrix4x4& CurrentTransform)
{
    if (DrawableID == InvalidRadientDrawableID)
        return CurrentTransform;

    if (DrawableID >= m_Drawables.size())
        m_Drawables.resize(static_cast<size_t>(DrawableID) + 1);

    DrawableState& State = m_Drawables[DrawableID];
    if (State.Generation != Generation ||
        (State.LastFrame != m_FrameNumber &&
         (State.LastFrame == InvalidFrameNumber || State.LastFrame + 1 != m_FrameNumber)))
    {
        State.PreviousTransform = CurrentTransform;
    }
    else if (State.LastFrame != m_FrameNumber)
    {
        State.PreviousTransform = State.CurrentTransform;
    }

    State.CurrentTransform = CurrentTransform;
    State.LastFrame        = m_FrameNumber;
    State.Generation       = Generation;
    return State.PreviousTransform;
}

void RadientTesseraFrameHistory::CommitFrame() noexcept
{
    VERIFY(m_CurrentRenderFrameID != InvalidRadientFrameID,
           "BeginFrame() must be called before committing Tessera frame history");

    m_PreviousCamera       = m_CurrentCamera;
    m_CurrentCamera        = {};
    m_HasCameraHistory     = false;
    m_LastRenderFrameID    = m_CurrentRenderFrameID;
    m_CurrentRenderFrameID = InvalidRadientFrameID;
    ++m_FrameNumber;
}

void RadientTesseraFrameHistory::ResetTemporalHistory() noexcept
{
    m_PreviousCamera   = {};
    m_CurrentCamera    = {};
    m_HasCameraHistory = false;
    m_Drawables.clear();
}

} // namespace Diligent
