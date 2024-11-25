/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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
#include <unordered_map>
#include <vector>

#include "HnTextureRegistry.hpp"

#include "pxr/imaging/hd/material.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentTools/AssetLoader/interface/GLTFLoader.hpp"
#include "../../../DiligentFX/PBR/interface/PBR_Renderer.hpp"

namespace Diligent
{

class USD_Renderer;

namespace GLTF
{
class ResourceManager;
}

namespace USD
{

class HnRenderDelegate;
class HnMaterialNetwork;
class HnMaterialSRBCache;

/// Hydra material implementation in Hydrogent.
class HnMaterial final : public pxr::HdMaterial
{
public:
    static HnMaterial* Create(const pxr::SdfPath& id);
    static HnMaterial* CreateFallback(HnRenderDelegate& RenderDelegate);

    ~HnMaterial();

    // Synchronizes state from the delegate to this object.
    virtual void Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    // Returns the minimal set of dirty bits to place in the
    // change tracker for use in the first sync of this prim.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    /// Creates an SRB cache that should be passed to UpdateSRB().
    static RefCntAutoPtr<IObject> CreateSRBCache();

    static Uint32 GetResourceCacheVersion(HnRenderDelegate& RenderDelegate);
    static void   InitSRBCache(HnRenderDelegate& RenderDelegate);
    static void   BeginResourceUpdate(HnRenderDelegate& RenderDelegate);
    bool          UpdateSRB(HnRenderDelegate& RenderDelegate);
    static void   EndResourceUpdate(HnRenderDelegate& RenderDelegate);

    IShaderResourceBinding* GetSRB() const { return m_SRB; }
    IShaderResourceBinding* GetSRB(Uint32 PrimitiveAttribsOffset) const
    {
        VERIFY_EXPR(m_SRBVars.PrimitiveAttribs != nullptr);
        m_SRBVars.PrimitiveAttribs->SetBufferOffset(PrimitiveAttribsOffset);
        return m_SRB;
    }
    void ApplyMaterialAttribsBufferOffset(Uint32& CurrentOffset) const
    {
        VERIFY_EXPR(m_SRBVars.MaterialAttribs != nullptr);
        VERIFY_EXPR(m_PBRMaterialAttribsBufferOffset != ~0u);
        if (CurrentOffset != m_PBRMaterialAttribsBufferOffset)
        {
            m_SRBVars.MaterialAttribs->SetBufferOffset(m_PBRMaterialAttribsBufferOffset);
            CurrentOffset = m_PBRMaterialAttribsBufferOffset;
        }
    }
    void SetJointsBufferOffset(Uint32 Offset) const
    {
        if (m_SRBVars.JointTransforms == nullptr)
        {
            UNEXPECTED("Joint transforms variable is not initialized, which indicates that skinning is not enabled in the renderer.");
            return;
        }
        m_SRBVars.JointTransforms->SetBufferOffset(Offset);
    }

    const GLTF::Material& GetMaterialData() const { return m_MaterialData; }

    /// Texture coordinate set info
    struct TextureCoordinateSetInfo
    {
        /// Texture coordinate set primvar name (e.g. "st")
        pxr::TfToken PrimVarName;
    };
    // Returns an array of unique texture coordinate sets used by this material, for example:
    // { { "st0" }, { "st1" } }
    const auto& GetTextureCoordinateSets() const { return m_TexCoords; }

    const pxr::TfToken& GetTag() const { return m_Tag; }

    /// Static shader texture indexing identifier, for example:
    ///    0 -> {0, 0, 0, 1, 1, 2}
    ///    1 -> {0, 1, 0, 1, 2, 2}
    using ShaderTextureIndexingIdType = Uint32;
    /// Returns the static shader texture indexing for the given identifier.
    static const PBR_Renderer::StaticShaderTextureIdsArrayType& GetStaticShaderTextureIds(IObject* SRBCache, ShaderTextureIndexingIdType Id);

    /// Returns the static shader texture indexing identifier that can be passed to
    /// GetStaticShaderTextureIds() to get the shader texture indices for this material.
    ShaderTextureIndexingIdType GetStaticShaderTextureIndexingId() const
    {
        return m_ShaderTextureIndexingId;
    }

    Uint32 GetPBRPrimitiveAttribsBufferRange() const { return m_PBRPrimitiveAttribsBufferRange; }
    Uint32 GetPBRMaterialAttribsSize() const { return m_PBRMaterialAttribsSize; }

private:
    HnMaterial(pxr::SdfPath const& id);

    // Special constructor for the fallback material.
    //
    // \remarks     Sync() is not called on fallback material,
    //  	        but we need to initialize default textures,
    //  	        so we have to use this special constructor.
    HnMaterial(HnRenderDelegate& RenderDelegate);

    // A mapping from the texture name to the texture coordinate set index in m_TexCoords array (e.g. "diffuseColor" -> 0)
    // The same index is set in m_ShaderTextureAttribs[].UVSelector for the corresponding texture.
    // The name of the primvar that contains the texture coordinates is given by m_TexCoords[index].PrimVarName (e.g. "st0").
    using TexNameToCoordSetMapType = std::unordered_map<pxr::TfToken, size_t, pxr::TfToken::HashFunctor>;
    void AllocateTextures(const HnMaterialNetwork& Network,
                          HnRenderDelegate&        RenderDelegate);
    void InitTextureAttribs(const HnMaterialNetwork&        Network,
                            HnTextureRegistry&              TexRegistry,
                            const USD_Renderer&             UsdRenderer,
                            const TexNameToCoordSetMapType& TexNameToCoordSetMap);
    bool InitTextureAddressingAttribs(const USD_Renderer& UsdRenderer,
                                      HnTextureRegistry&  TexRegistry);

    HnTextureRegistry::TextureHandleSharedPtr GetDefaultTexture(HnTextureRegistry& TexRegistry, const pxr::TfToken& Name);

    void ProcessMaterialNetwork(const HnMaterialNetwork& Network);
    void AllocateBufferSpace(HnRenderDelegate& RenderDelegate);

private:
    pxr::TfToken m_Tag;

    std::unordered_map<pxr::TfToken, HnTextureRegistry::TextureHandleSharedPtr, pxr::TfToken::HashFunctor> m_Textures;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    struct SRBVariables
    {
        IShaderResourceVariable* PrimitiveAttribs = nullptr; // cbPrimitiveAttribs
        IShaderResourceVariable* MaterialAttribs  = nullptr; // cbMaterialAttribs
        IShaderResourceVariable* JointTransforms  = nullptr; // cbJointTransforms
    };
    SRBVariables m_SRBVars;

    GLTF::Material m_MaterialData;

    std::atomic<bool> m_TextureAddressingAttribsDirty{false};
    std::atomic<bool> m_GPUDataDirty{true};

    // The names of the primvars that contain unique texture coordinate sets for this material (e.g. "st0", "st1").
    // The index in this array for texture N is given by m_ShaderTextureAttribs[N].UVSelector.
    std::vector<TextureCoordinateSetInfo> m_TexCoords;

    // The range that is used to bind the cbPrimitiveAttribs buffer.
    Uint32 m_PBRPrimitiveAttribsBufferRange = 0;

    PBR_Renderer::PSO_FLAGS m_PSOFlags = PBR_Renderer::PSO_FLAG_NONE;

    // Material attribs data resides in a single buffer shared by all SRBs.
    //
    //                 Size
    // |         |-------------| x x x x |
    //        Offset            Alignment

    // Material attribs shader data size in bytes.
    Uint32 m_PBRMaterialAttribsSize = 0;

    // The offset in the cbMaterialAttribs buffer.
    Uint32 m_PBRMaterialAttribsBufferOffset = ~0u;

    // Texture registry storage version + material attribs buffer version.
    Uint32 m_ResourceCacheVersion = ~0u;

    ShaderTextureIndexingIdType m_ShaderTextureIndexingId = 0;

    RefCntAutoPtr<HnMaterialSRBCache> m_SRBCache;
};

} // namespace USD

} // namespace Diligent
