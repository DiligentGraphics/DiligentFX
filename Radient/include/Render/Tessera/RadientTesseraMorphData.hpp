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

#include "Assets/RadientMorphTargetData.hpp"
#include "PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"

#include <optional>
#include <vector>

namespace Diligent
{

struct RadientDrawableMesh;

/// Tessera-side active target palettes for one morph-target weights object.
///
/// The object retains the weights and prepares their bounded current and
/// previous palettes once per renderer frame. It also owns shader records for
/// each mesh geometry, shared by all entities and primitives using that geometry.
class RadientTesseraMorphData final
{
public:
    /// Initializes all morph-bearing geometries from a fully resolved mesh.
    /// Copies their immutable stream offsets without retaining source metadata.
    RadientTesseraMorphData(IRadientMorphTargetWeights* pWeights,
                            const RadientDrawableMesh&  Mesh,
                            Uint32                      MaxActiveTargetCount);

    RadientTesseraMorphData(const RadientTesseraMorphData&)            = delete;
    RadientTesseraMorphData& operator=(const RadientTesseraMorphData&) = delete;

    /// Selects the strongest target weights and prepares all morph geometries
    /// together. Unchanged weights do not rebuild shader records. Repeated calls
    /// for the same frame are idempotent unless weights change. A skipped frame
    /// resets the shared current/previous palette history.
    RADIENT_STATUS Prepare(RadientFrameID RenderFrameID) noexcept;

    /// Returns a prepared geometry palette without copying. Requires an OK
    /// preparation status. Use the current palette for both frames
    /// when the view has no history. Pointers remain valid until destruction;
    /// Prepare() may update contents.
    const PBR_Renderer::MorphTargetShaderAttribs* GetShaderAttribs(Uint32 GeometryIndex,
                                                                   bool   UsePreviousPalette) const noexcept
    {
        VERIFY_EXPR(GeometryIndex < m_Geometries.size() && m_Geometries[GeometryIndex]);
        const Uint32 PaletteIndex = UsePreviousPalette ? m_PreviousPalette : m_CurrentPalette;
        return m_PaletteCapacity != 0 ?
            m_Geometries[GeometryIndex]->ShaderAttribs.data() + size_t{PaletteIndex} * m_PaletteCapacity :
            nullptr;
    }

    bool Matches(IRadientMorphTargetWeights* pWeights) const noexcept
    {
        return m_pWeights == pWeights;
    }

    IRadientMorphTargetWeights* GetWeights() const noexcept
    {
        return m_pWeights;
    }

    /// Readiness of all morph geometries. PENDING until the first successful
    /// Prepare(); initialization failures apply to the entire morph data.
    RADIENT_STATUS GetPreparationStatus() const noexcept
    {
        return m_PreparationStatus;
    }

    Uint64 GetPreparedWeightsVersion() const noexcept
    {
        return m_PreparedWeightsVersion;
    }

    /// Returns the active target count for the selected current or previous palette.
    Uint32 GetActiveTargetCount(bool UsePreviousPalette) const noexcept
    {
        const Uint32 PaletteIndex = UsePreviousPalette ? m_PreviousPalette : m_CurrentPalette;
        return m_ActiveTargetCounts[PaletteIndex];
    }

private:
    struct GeometryShaderData
    {
        // Immutable shader-record templates for all targets, indexed by target
        // index. PositionDeltaOffset, NormalDeltaOffset, and TangentDeltaOffset
        // address the GPU delta buffer in Float32 elements; absent streams retain
        // InvalidMorphTargetDataOffset. Weight is unused and remains zero.
        std::vector<PBR_Renderer::MorphTargetShaderAttribs> TargetAttribs;

        // Prepared current/previous active palettes in two fixed halves.
        // Preparation copies selected templates and sets their actual weights.
        std::vector<PBR_Renderer::MorphTargetShaderAttribs> ShaderAttribs;
    };

    void PrepareShaderPalette(GeometryShaderData& Geometry, Uint32 PaletteIndex) const noexcept;

    RefCntAutoPtr<IRadientMorphTargetWeights> m_pWeights;

    // Fixed at construction and indexed by mesh geometry. Only non-morph
    // geometries have no entry.
    std::vector<std::optional<GeometryShaderData>> m_Geometries;

    std::vector<PBR_Renderer::ActiveMorphTarget> m_ActiveTargets;

    Uint32 m_TargetCount           = 0;
    Uint32 m_PaletteCapacity       = 0;
    Uint32 m_ActiveTargetCounts[2] = {};
    Uint32 m_CurrentPalette        = 0;
    Uint32 m_PreviousPalette       = 0;

    Uint64         m_PreparedWeightsVersion = 0;
    RadientFrameID m_PreparedFrameID        = InvalidRadientFrameID;
    RADIENT_STATUS m_PreparationStatus      = RADIENT_STATUS_PENDING;
};

/// A drawable's binding to one geometry in the shared morph data. All shader
/// records and palette history belong to MorphData, not to the attachment.
struct RadientTesseraMorphAttachment
{
    RadientTesseraMorphData& MorphData;
    Uint32                   GeometryIndex = 0;
};

} // namespace Diligent
