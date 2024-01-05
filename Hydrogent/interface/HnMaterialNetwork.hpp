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

#include "pxr/base/vt/dictionary.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/imaging/hd/material.h"

#include "HnTextureIdentifier.hpp"

namespace Diligent
{

namespace USD
{

struct HnMaterialParameter final
{
    // Indicates the kind of material parameter.
    enum class ParamType
    {
        Unknown,

        // A shader-specified fallback value that is
        // not connected to either a primvar or texture.
        Fallback,

        // A parameter that is connected to a texture.
        Texture,

        // Creates an accessor HdGet_name() that either reads a
        // primvar with a potentially different name (given in
        // samplerCoords) if it exists or uses the fallback value.
        // It corresponds to a primvar reader shading node.
        PrimvarRedirect,

        // Creates an accessor HdGet_name(vec3) that either reads
        // from a field texture with a potentially different name (given
        // in samplerCoords) if it exists or uses the fallback value.
        // It corresponds to a field reader shading node.
        FieldRedirect,

        // Additional primvar needed by material. One that is not connected to
        // a input parameter (ParamTypePrimvar).
        AdditionalPrimvar,

        // This is a parameter that is connected to a transform2d node
        Transform2d
    };

    HnMaterialParameter();
    ~HnMaterialParameter();

    HnMaterialParameter(ParamType                      _Type,
                        const pxr::TfToken&            _Name,
                        const pxr::VtValue&            _FallbackValue       = pxr::VtValue{},
                        const pxr::TfTokenVector&      _SamplerCoords       = pxr::TfTokenVector{},
                        pxr::HdTextureType             _TextureType         = pxr::HdTextureType::Uv,
                        const TextureComponentMapping& _Swizzle             = TextureComponentMapping::Identity(),
                        bool                           _IsPremultiplied     = false,
                        size_t                         _ArrayOfTexturesSize = 0);

    bool IsTexture() const
    {
        return Type == ParamType::Texture;
    }
    bool IsPrimvarRedirect() const
    {
        return Type == ParamType::PrimvarRedirect;
    }
    bool IsFieldRedirect() const
    {
        return Type == ParamType::FieldRedirect;
    }
    bool IsFallback() const
    {
        return Type == ParamType::Fallback;
    }
    bool IsAdditionalPrimvar() const
    {
        return Type == ParamType::AdditionalPrimvar;
    }
    bool IsTransform2d() const
    {
        return Type == ParamType::Transform2d;
    }
    bool IsArrayOfTextures() const
    {
        return IsTexture() && ArrayOfTexturesSize > 0;
    }

    ParamType               Type = ParamType::Unknown;
    pxr::TfToken            Name;
    pxr::VtValue            FallbackValue;
    pxr::TfTokenVector      SamplerCoords;
    pxr::HdTextureType      TextureType = pxr::HdTextureType::Uv;
    TextureComponentMapping Swizzle;
    bool                    IsPremultiplied = false;

    // Scale and bias that are applied to the input values
    pxr::GfVec4f InputScale{1.f};
    pxr::GfVec4f InputBias{0.f};

    struct TextureTransform2d
    {
        pxr::GfVec2f Scale{1, 1};
        pxr::GfVec2f Translation{0, 0};
        float        Rotation{0};
    };
    TextureTransform2d Transform2d;

    // If paramType is ParamTypeTexture, this indicates both if the textures
    // should be bound as an array of textures and the size of the array. If
    // arrayOfTexturesSize is 0, then do not bind as an array of textures, but
    // rather a single texture (whereas arrayOfTexturesSize = 1 indicates an
    // array of textures of size 1).
    size_t ArrayOfTexturesSize = 0;
};


class HnMaterialNetwork final
{
public:
    HnMaterialNetwork();

    HnMaterialNetwork(const pxr::SdfPath&              SdfPath,
                      const pxr::HdMaterialNetworkMap& hdNetworkMap) noexcept(false);


    HnMaterialNetwork(HnMaterialNetwork&&) = default;
    HnMaterialNetwork& operator=(HnMaterialNetwork&&) = default;

    ~HnMaterialNetwork();

    // Information necessary to allocate a texture.
    struct TextureDescriptor
    {
        // Name by which the texture will be accessed, i.e., the name
        // of the accessor for thexture will be HdGet_name(...).
        // It is generated from the input name the corresponding texture
        // node is connected to.
        pxr::TfToken Name;

        HnTextureIdentifier      TextureId;
        pxr::HdSamplerParameters SamplerParams;

        // Memory request in bytes.
        size_t MemoryRequest = 0;

        // The texture is not just identified by a file path attribute
        // on the texture prim but there is special API to texture prim
        // to obtain the texture.
        //
        // This is used for draw targets.
        bool UseTexturePrimToFindTexture = false;

        // This is used for draw targets and hashing.
        pxr::SdfPath TexturePrim;
    };

    const pxr::TfToken&      GetTag() const { return m_Tag; }
    const pxr::VtDictionary& GetMetadata() const { return m_Metadata; }
    const auto&              GetParameters() const { return m_Parameters; }
    const auto&              GetTextures() const { return m_Textures; }

    const HnMaterialParameter* GetParameter(HnMaterialParameter::ParamType Type, const pxr::TfToken& Name) const;

    float GetOpacity() const { return m_Opacity; }
    float GetOpacityThreshold() const { return m_OpacityThreshold; }

private:
    void LoadParams(const pxr::HdMaterialNetwork2& Network,
                    const pxr::HdMaterialNode2&    Node);

    void AddAdditionalPrimvarParameter(const pxr::TfToken& PrimvarName);
    void AddUnconnectedParam(const pxr::TfToken& ParamName);

    void ProcessInputParameter(const pxr::HdMaterialNetwork2& Network,
                               const pxr::HdMaterialNode2&    Node,
                               const pxr::TfToken&            ParamName,
                               pxr::SdfPathSet&               VisitedNodes);

    void AddTextureParam(const pxr::HdMaterialNetwork2& Network,
                         const pxr::HdMaterialNode2&    Node,
                         const pxr::HdMaterialNode2&    DownstreamNode, // needed to determine def value
                         const pxr::SdfPath&            NodePath,
                         const pxr::TfToken&            OutputName,
                         const pxr::TfToken&            ParamName,
                         pxr::SdfPathSet&               VisitedNodes);

    void AddPrimvarReaderParam(const pxr::HdMaterialNetwork2& Network,
                               const pxr::HdMaterialNode2&    Node,
                               const pxr::SdfPath&            NodePath,
                               const pxr::TfToken&            ParamName,
                               pxr::SdfPathSet&               VisitedNodes);

    void AddFieldReaderParam(const pxr::HdMaterialNetwork2& Network,
                             const pxr::HdMaterialNode2&    Node,
                             const pxr::SdfPath&            NodePath,
                             const pxr::TfToken&            ParamName,
                             pxr::SdfPathSet&               VisitedNodes);

    void AddTransform2dParam(const pxr::HdMaterialNetwork2& Network,
                             const pxr::HdMaterialNode2&    Node,
                             const pxr::SdfPath&            NodePath,
                             const pxr::TfToken&            ParamName,
                             pxr::SdfPathSet&               VisitedNodes);

private:
    // Material tag is used to sort draw items by material tag.
    pxr::TfToken                     m_Tag;
    pxr::VtDictionary                m_Metadata;
    std::vector<HnMaterialParameter> m_Parameters;
    std::vector<TextureDescriptor>   m_Textures;

    float m_OpacityThreshold = 0.0f;
    float m_Opacity          = 1.0f;
};

} // namespace USD

} // namespace Diligent
