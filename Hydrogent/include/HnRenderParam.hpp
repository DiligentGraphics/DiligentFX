/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include <atomic>
#include <array>
#include <vector>
#include <unordered_map>
#include <thread>

#include "HnTypes.hpp"
#include "SpinLock.hpp"

#include "pxr/imaging/hd/renderDelegate.h"
#include "../../PBR/interface/PBR_Renderer.hpp"

namespace Diligent
{

namespace USD
{

/// Hydra render param implementation in Hydrogent.
class HnRenderParam final : public pxr::HdRenderParam
{
public:
    struct Configuration
    {
        bool                              UseVertexPool          = false;
        bool                              UseIndexPool           = false;
        bool                              AsyncShaderCompilation = false;
        bool                              UseNativeStartVertex   = false;
        HN_MATERIAL_TEXTURES_BINDING_MODE TextureBindingMode     = {};
        float                             MetersPerUnit          = 1.0f;
        Uint64                            GeometryLoadBudget     = 0;
    };

    HnRenderParam(const Configuration& Config, bool UseShadows) noexcept;
    ~HnRenderParam();

    const Configuration& GetConfig() const { return m_Config; }

    HN_GEOMETRY_MODE GetGeometryMode() const { return m_GeometryMode; }
    void             SetGeometryMode(HN_GEOMETRY_MODE Mode) { m_GeometryMode = Mode; }

    const pxr::SdfPath& GetSelectedPrimId() const { return m_SelectedPrimId; }
    void                SetSelectedPrimId(const pxr::SdfPath& PrimId) { m_SelectedPrimId = PrimId; }

    void SetUseShadows(bool UseShadows) { m_UseShadows = UseShadows; }
    bool GetUseShadows() const { return m_UseShadows; }

    void SetLoadingAnimationActive(bool Active) { m_LoadingAnimationActive = Active; }
    bool GetLoadingAnimationActive() const { return m_LoadingAnimationActive; }

    enum class GlobalAttrib
    {
        // Indicates changes to geometry subset draw items.
        GeometrySubsetDrawItems,

        // Indicates changes to mesh geometry:
        //   - Mesh topology (index buffers and geometry subsets)
        //   - Any primvars (vertex buffers)
        MeshGeometry,

        // Indicates changes to mesh transforms.
        MeshTransform,

        // Indicates changes to mesh visibility.
        MeshVisibility,

        // Indicates changes to mesh culling mode (front, back, none).
        MeshCulling,

        // Indicates changes to mesh materials:
        //   - Material assignment
        //   - Display style
        //   - Double-sided
        MeshMaterial,

        // Indicates changes to material properties.
        Material,

        // Indicates changes to light properties.
        Light,

        // Indicates changes to light resources (e.g. textures).
        LightResources,

        // Indicates changes to skinning xforms
        SkinningXForms,

        // Indicates changes to OIT resources.
        //
        // \remarks OIT resources are initialized by HnBeginFrameTask. This
        //          version is tracked by HnRenderDelegate to know when to
        //          recreate main pass frame attribs SRB.
        OITResources,

        Count
    };
    uint32_t GetAttribVersion(GlobalAttrib Attrib) const { return m_GlobalAttribVersions[static_cast<size_t>(Attrib)].load(); }
    uint32_t MakeAttribDirty(GlobalAttrib Attrib) { return m_GlobalAttribVersions[static_cast<size_t>(Attrib)].fetch_add(1) + 1; }

    HN_VIEW_MODE GetViewMode() const { return m_ViewMode; }
    void         SetViewMode(HN_VIEW_MODE ViewMode) { m_ViewMode = ViewMode; }

    double GetFrameTime() const { return m_FrameTime; }
    void   SetFrameTime(double FrameTime) { m_FrameTime = FrameTime; }

    float GetElapsedTime() const { return m_ElapsedTime; }
    void  SetElapsedTime(float ElapsedTime) { m_ElapsedTime = ElapsedTime; }

    Uint32 GetFrameNumber() const { return m_FrameNumber; }
    void   SetFrameNumber(Uint32 FrameNumber) { m_FrameNumber = FrameNumber; }

    void AddDirtyRPrim(const pxr::SdfPath& RPrimId, pxr::HdDirtyBits DirtyBits);
    void CommitDirtyRPrims(pxr::HdChangeTracker& ChangeTracker);

    size_t GetLastDirtyRPrimCount() const { return m_LastDirtyRPrimCount; }

private:
    const Configuration m_Config;

    HN_GEOMETRY_MODE m_GeometryMode = HN_GEOMETRY_MODE_SOLID;

    pxr::SdfPath m_SelectedPrimId;

    std::array<std::atomic<uint32_t>, static_cast<size_t>(GlobalAttrib::Count)> m_GlobalAttribVersions{};

    HN_VIEW_MODE m_ViewMode = HN_VIEW_MODE_SHADED;

    bool m_UseShadows             = false;
    bool m_LoadingAnimationActive = false;

    double   m_FrameTime   = 0.0;
    float    m_ElapsedTime = 0.0;
    uint32_t m_FrameNumber = 0;

    using DirtyRPrimsVector = std::vector<std::pair<pxr::SdfPath, pxr::HdDirtyBits>>;

    // Note: spin lock here works much faster than std::mutex
    Threading::SpinLock                                    m_DirtyRPrimsLock;
    std::unordered_map<std::thread::id, DirtyRPrimsVector> m_DirtyRPrimsPerThread;
    // The number of dirty RPrims when CommitDirtyRPrims was last called.
    size_t m_LastDirtyRPrimCount = 0;
};

} // namespace USD

} // namespace Diligent
