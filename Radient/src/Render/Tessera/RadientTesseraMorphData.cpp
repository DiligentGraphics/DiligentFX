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

#include "Render/Tessera/RadientTesseraMorphData.hpp"

#include "Render/RadientDrawableMesh.hpp"
#include "Render/Tessera/RadientTesseraFrameHistory.hpp"
#include "DebugUtilities.hpp"

#include <algorithm>
#include <cstring>
#include <exception>

namespace Diligent
{

RadientTesseraMorphData::RadientTesseraMorphData(IRadientMorphTargetWeights* pWeights,
                                                 const RadientDrawableMesh&  Mesh,
                                                 Uint32                      MaxActiveTargetCount) :
    m_pWeights{pWeights}
{
    VERIFY_EXPR(m_pWeights != nullptr);
    VERIFY_EXPR(MaxActiveTargetCount != 0);
    if (m_pWeights == nullptr || MaxActiveTargetCount == 0)
    {
        m_PreparationStatus = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    IRadientMeshAsset* const pMesh = m_pWeights->GetMesh();
    VERIFY_EXPR(pMesh != nullptr);
    if (pMesh == nullptr)
    {
        m_PreparationStatus = RADIENT_STATUS_INVALID_DATA;
        return;
    }

    const RadientMeshAssetDesc& MeshDesc    = pMesh->GetDesc();
    const Uint32                TargetCount = m_pWeights->GetWeightCount();
    VERIFY_EXPR(TargetCount == MeshDesc.MorphTargetCount);
    VERIFY_EXPR(TargetCount == 0 || MeshDesc.pMorphTargets != nullptr);
    if (TargetCount != MeshDesc.MorphTargetCount ||
        (TargetCount != 0 && MeshDesc.pMorphTargets == nullptr))
    {
        m_PreparationStatus = RADIENT_STATUS_INVALID_DATA;
        return;
    }

    try
    {
        m_TargetCount     = TargetCount;
        m_PaletteCapacity = std::min(TargetCount, MaxActiveTargetCount);
        m_ActiveTargets.resize(size_t{m_PaletteCapacity} * 2);
        m_Geometries.resize(Mesh.Geometries.size());
        for (size_t GeometryIndex = 0; GeometryIndex < Mesh.Geometries.size(); ++GeometryIndex)
        {
            const RadientDrawableMeshGeometry& Geometry = Mesh.Geometries[GeometryIndex];
            if (Geometry.pMorphTargetData == nullptr)
                continue;

            const RadientMorphTargetData& GeometryData = *Geometry.pMorphTargetData;
            const RadientMeshAssetDesc&   GeometryDesc = GeometryData.GetDesc();
            if (GeometryDesc.MorphTargetCount != m_TargetCount ||
                Geometry.MorphTargetDataOffset % sizeof(Float32) != 0)
            {
                UNEXPECTED("Morph geometry must match its weights and have a Float32-aligned allocation");
                m_PreparationStatus = RADIENT_STATUS_INVALID_DATA;
                return;
            }

            GeometryShaderData& ShaderData = m_Geometries[GeometryIndex].emplace();
            ShaderData.TargetAttribs.resize(m_TargetCount);
            ShaderData.ShaderAttribs.resize(size_t{m_PaletteCapacity} * 2);
            for (Uint32 TargetIndex = 0; TargetIndex < m_TargetCount; ++TargetIndex)
            {
                const RadientMorphTargetDesc&           Target  = GeometryDesc.pMorphTargets[TargetIndex];
                PBR_Renderer::MorphTargetShaderAttribs& Attribs = ShaderData.TargetAttribs[TargetIndex];
                for (Uint32 AttributeIndex = 0; AttributeIndex < Target.AttributeCount; ++AttributeIndex)
                {
                    const RadientMorphTargetAttributeDesc& Attribute = Target.pAttributes[AttributeIndex];
                    Uint32*                                pOffset   = nullptr;
                    if (std::strcmp(Attribute.Semantic, RadientMorphTargetPositionSemantic) == 0)
                        pOffset = &Attribs.PositionDeltaOffset;
                    else if (std::strcmp(Attribute.Semantic, RadientMorphTargetNormalSemantic) == 0)
                        pOffset = &Attribs.NormalDeltaOffset;
                    else if (std::strcmp(Attribute.Semantic, RadientMorphTargetTangentSemantic) == 0)
                        pOffset = &Attribs.TangentDeltaOffset;
                    else
                        continue;

                    const Uint64 ByteOffset = Uint64{Geometry.MorphTargetDataOffset} +
                        GeometryData.GetAttributeDataOffset(TargetIndex, AttributeIndex);
                    if (ByteOffset % sizeof(Float32) != 0 ||
                        ByteOffset / sizeof(Float32) >= PBR_Renderer::InvalidMorphTargetDataOffset)
                    {
                        UNEXPECTED("Morph attribute offset cannot be represented in the shader layout");
                        m_PreparationStatus = RADIENT_STATUS_INVALID_DATA;
                        return;
                    }
                    *pOffset = static_cast<Uint32>(ByteOffset / sizeof(Float32));
                }
            }
        }
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to initialize Tessera morph target data: ", Error.what());
        m_PreparationStatus = RADIENT_STATUS_FAILED;
        return;
    }
}

RADIENT_STATUS RadientTesseraMorphData::Prepare(RadientFrameID RenderFrameID) noexcept
{
    if (RADIENT_FAILED(m_PreparationStatus))
        return m_PreparationStatus;
    if (RenderFrameID == InvalidRadientFrameID)
    {
        UNEXPECTED("Tessera morph data requires a valid render frame ID");
        m_PreparationStatus = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_PreparationStatus;
    }

    const Uint32 WeightCount = m_pWeights->GetWeightCount();
    VERIFY_EXPR(WeightCount == m_TargetCount);
    const Float32* const pWeights = m_pWeights->GetWeights();
    VERIFY_EXPR(WeightCount == 0 || pWeights != nullptr);
    if (WeightCount != m_TargetCount ||
        (WeightCount != 0 && pWeights == nullptr))
    {
        m_PreparationStatus = RADIENT_STATUS_INVALID_DATA;
        return m_PreparationStatus;
    }

    const bool   WasPrepared    = m_PreparationStatus == RADIENT_STATUS_OK;
    const Uint64 WeightsVersion = m_pWeights->GetVersion();
    const bool   SameFrame      = WasPrepared && m_PreparedFrameID == RenderFrameID;
    if (WasPrepared && m_PreparedWeightsVersion == WeightsVersion)
    {
        m_PreparedFrameID = RenderFrameID;
        if (SameFrame || m_PreviousPalette == m_CurrentPalette)
            return RADIENT_STATUS_NO_CHANGE;

        m_PreviousPalette = m_CurrentPalette;
        return RADIENT_STATUS_OK;
    }

    const bool HasContinuousHistory =
        SameFrame || (WasPrepared && RadientTesseraFrameHistory::IsContinuous(m_PreparedFrameID, RenderFrameID));
    const bool                             ReplaceCurrent      = SameFrame && m_PreviousPalette != m_CurrentPalette;
    const Uint32                           DestinationPalette  = !WasPrepared || ReplaceCurrent ? m_CurrentPalette : 1u - m_CurrentPalette;
    PBR_Renderer::ActiveMorphTarget* const pDestinationPalette = m_PaletteCapacity != 0 ?
        m_ActiveTargets.data() + size_t{DestinationPalette} * m_PaletteCapacity :
        nullptr;

    m_ActiveTargetCounts[DestinationPalette] = PBR_Renderer::SelectActiveMorphTargets(
        pWeights,
        WeightCount,
        pDestinationPalette,
        m_PaletteCapacity);
    for (auto& Geometry : m_Geometries)
    {
        if (Geometry)
            PrepareShaderPalette(*Geometry, DestinationPalette);
    }

    if (!WasPrepared || !HasContinuousHistory)
        m_PreviousPalette = DestinationPalette;
    else if (!ReplaceCurrent)
        m_PreviousPalette = m_CurrentPalette;
    m_CurrentPalette = DestinationPalette;

    m_PreparedWeightsVersion = WeightsVersion;
    m_PreparedFrameID        = RenderFrameID;
    m_PreparationStatus      = RADIENT_STATUS_OK;
    return RADIENT_STATUS_OK;
}

void RadientTesseraMorphData::PrepareShaderPalette(GeometryShaderData& Geometry, Uint32 PaletteIndex) const noexcept
{
    const size_t PaletteOffset = size_t{PaletteIndex} * m_PaletteCapacity;
    for (Uint32 ActiveIndex = 0; ActiveIndex < m_ActiveTargetCounts[PaletteIndex]; ++ActiveIndex)
    {
        const PBR_Renderer::ActiveMorphTarget& ActiveTarget = m_ActiveTargets[PaletteOffset + ActiveIndex];
        VERIFY_EXPR(ActiveTarget.TargetIndex < Geometry.TargetAttribs.size());
        PBR_Renderer::MorphTargetShaderAttribs Attribs      = Geometry.TargetAttribs[ActiveTarget.TargetIndex];
        Attribs.Weight                                      = ActiveTarget.Weight;
        Geometry.ShaderAttribs[PaletteOffset + ActiveIndex] = Attribs;
    }
}

} // namespace Diligent
