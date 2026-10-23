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

#include "Radient.h"
#include "RefCntAutoPtr.hpp"
#include "VertexPool.h"

#include <array>

namespace Diligent
{

namespace Testing
{

namespace RadientGPUTest
{

constexpr RadientFloat4 DefaultQuadColor{0.25f, 0.5f, 0.75f, 1.f};

using SampledColors = std::array<std::array<Uint8, 4>, 4>;

struct QuadSetup
{
    RadientFloat4                 Color             = DefaultQuadColor;
    RadientFloat2                 UVBias            = {0.25f, 0.5f};
    RadientFloat2                 UVScale           = {0.f, 0.f};
    Uint32                        TexCoordSet       = 0;
    RADIENT_MATERIAL_SURFACE_MODE SurfaceMode       = RADIENT_MATERIAL_SURFACE_MODE_MASKED;
    Float32                       AlphaCutoff       = 0.25f;
    Bool                          DoubleSided       = True;
    bool                          BackFacing        = false;
    bool                          UseVertexColor    = false;
    RadientFloat4                 VertexColor       = {1.f, 1.f, 1.f, 1.f};
    RADIENT_INDEX_TYPE            IndexType         = RADIENT_INDEX_TYPE_UINT16;
    Uint32                        FirstElement      = 0;
    Uint32                        VertexPrefixCount = 0;
    bool                          UseMorphTarget    = false;
};

using QuadSetups = std::array<QuadSetup, 4>;

// Renders four independently configured quads through the full Radient pipeline
// and reads back corresponding pixels for mesh and material comparisons.
class RadientQuadTestScene
{
public:
    ~RadientQuadTestScene();

    void           Initialize(const QuadSetups& Setups = {}, Bool EnableAsyncPipelineCompilation = False);
    void           RenderUntilReady(IRadientMaterialAsset* pEditedMaterial = nullptr);
    RADIENT_STATUS RenderFrame();
    void           ReadColors(SampledColors& Colors, Uint32 PixelOffsetX = 0);

    Uint32                                              IndexBufferSize     = 4096;
    Uint64                                              MaxIndexBufferSize  = RadientResourceManagerCreateInfo{}.MaxIndexBufferSize;
    Uint32                                              ReservedVertexCount = 0;
    Uint32                                              MultiDrawBatchSize  = 16;
    bool                                                ShareMaterial       = false;
    IRenderDevice*                                      pDevice             = nullptr;
    IDeviceContext*                                     pContext            = nullptr;
    RefCntAutoPtr<IRadientEngine>                       pEngine;
    RefCntAutoPtr<IRadientAssetManager>                 pAssets;
    RefCntAutoPtr<IVertexPoolAllocation>                pReservedVertices;
    RefCntAutoPtr<IRadientRenderer>                     pRenderer;
    RefCntAutoPtr<IRadientScene>                        pScene;
    RefCntAutoPtr<IRadientTextureAsset>                 pTexture;
    std::array<RefCntAutoPtr<IRadientMaterialAsset>, 4> Materials;
    std::array<RefCntAutoPtr<IRadientMeshAsset>, 4>     Meshes;
    RefCntAutoPtr<ITexture>                             pOutput;
    RefCntAutoPtr<ITexture>                             pReadback;
    RefCntAutoPtr<IRadientRenderTarget>                 pTarget;
    RefCntAutoPtr<IRadientView>                         pView;
    double                                              Time = 0.0;
};

void ExpectSameColor(const std::array<Uint8, 4>& Actual, const std::array<Uint8, 4>& Reference);

} // namespace RadientGPUTest

} // namespace Testing

} // namespace Diligent
