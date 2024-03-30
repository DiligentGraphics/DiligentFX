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

#include "HnMaterialNetwork.hpp"
#include "HnTokens.hpp"

#include "DebugUtilities.hpp"
#include "StringTools.hpp"
#include "GraphicsAccessories.hpp"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hio/glslfx.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/sdr/shaderProperty.h"
#include "pxr/usd/sdf/valueTypeName.h"

namespace Diligent
{

namespace USD
{

HnMaterialParameter::HnMaterialParameter()
{
}

HnMaterialParameter::~HnMaterialParameter()
{
}

HnMaterialParameter::HnMaterialParameter(
    ParamType                      _Type,
    const pxr::TfToken&            _Name,
    const pxr::VtValue&            _FallbackValue,
    const pxr::TfTokenVector&      _SamplerCoords,
    pxr::HdTextureType             _TextureType,
    const TextureComponentMapping& _Swizzle,
    bool                           _IsPremultiplied,
    size_t                         _ArrayOfTexturesSize) :
    // clang-format off
    Type               {_Type},
    Name               {_Name},
    FallbackValue      {_FallbackValue},
    SamplerCoords      {_SamplerCoords},
    TextureType        {_TextureType},
    Swizzle            {_Swizzle},
    IsPremultiplied    {_IsPremultiplied},
    ArrayOfTexturesSize{_ArrayOfTexturesSize}
{}

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnMaterialPrivateTokens,
    (opacity)
    (opacityThreshold)
    (isPtex)
    (st)
    (uv)
    (fieldname)
    (diffuseColor)
    (a)

    (HwUvTexture_1)
    (textureMemory)
    (sourceColorSpace)
    (in)

    (mtlx)
);
// clang-format on

HnMaterialNetwork::HnMaterialNetwork()
{
}

HnMaterialNetwork::~HnMaterialNetwork()
{
}

namespace
{

const pxr::HdMaterialNode2* GetTerminalNode(const pxr::HdMaterialNetwork2& Network,
                                            const pxr::TfToken&            TerminalToken,
                                            pxr::SdfPath&                  TerminalPath)
{
    // Find the node that is upstream of the terminal, e.g.
    // the node that is connected to the terminal's input.
    const auto terminal_it = Network.terminals.find(TerminalToken);
    if (terminal_it == Network.terminals.end())
        return nullptr;

    //
    //             upstreamNode
    //                 A
    //                 |
    //                 | HdMaterialConnection2
    //                 |
    //              Terminal
    //
    const pxr::HdMaterialConnection2& TerminalConnection = terminal_it->second;

    const auto node_it = Network.nodes.find(TerminalConnection.upstreamNode);
    if (node_it == Network.nodes.end())
    {
        LOG_ERROR_MESSAGE("Unable to find upstream node ", TerminalConnection.upstreamNode, " of terminal ", TerminalToken);
        return nullptr;
    }

    TerminalPath = TerminalConnection.upstreamNode;
    return &node_it->second;
}

#if 0
std::shared_ptr<pxr::HioGlslfx> GetGlslfxForTerminal(const pxr::TfToken& NodeTypeId)
{
    // If there is a URI, we will use that, otherwise we will try to use
    // the source code.
    pxr::SdrRegistry&          ShaderReg = pxr::SdrRegistry::GetInstance();
    pxr::SdrShaderNodeConstPtr SdrNode   = ShaderReg.GetShaderNodeByIdentifierAndType(NodeTypeId, pxr::HioGlslfxTokens->glslfx);

    if (!SdrNode)
        return nullptr;

    const std::string& GlslfxFilePath = SdrNode->GetResolvedImplementationURI();
    if (!GlslfxFilePath.empty())
    {
        // TODO: cache the glslfx file
        return std::make_shared<pxr::HioGlslfx>(GlslfxFilePath);
    }
    else
    {
        const std::string& SourceCode = SdrNode->GetSourceCode();
        if (!SourceCode.empty())
        {
            // Do not use the registry for the source code to avoid
            // the cost of hashing the entire source code.
            return std::make_shared<pxr::HioGlslfx>(std::istringstream{SourceCode});
        }
    }

    return nullptr;
}
#endif

pxr::TfToken GetMaterialTag(const pxr::VtDictionary&    Metadata,
                            const pxr::HdMaterialNode2& Terminal)
{
    // Strongest materialTag opinion is a hardcoded tag in glslfx meta data.
    // This can be used for masked, additive, translucent or volume materials.
    // See HdMaterialTagTokens.
    pxr::VtValue vtMetaTag = TfMapLookupByValue(Metadata, pxr::HdShaderTokens->materialTag, pxr::VtValue{});
    if (vtMetaTag.IsHolding<std::string>())
    {
        return TfToken{vtMetaTag.UncheckedGet<std::string>()};
    }

    // Next check for authored terminal.opacityThreshold value > 0
    for (const auto& param_it : Terminal.parameters)
    {
        if (param_it.first != HnMaterialPrivateTokens->opacityThreshold)
            continue;

        const pxr::VtValue& vtOpacityThreshold = param_it.second;
        if (vtOpacityThreshold.Get<float>() > 0.0f)
        {
            return HnMaterialTagTokens->masked;
        }
    }

    // Next strongest opinion is a connection to 'terminal.opacity'
    auto IsTranslucent = (Terminal.inputConnections.find(HnMaterialPrivateTokens->opacity) != Terminal.inputConnections.end());

    // Weakest opinion is an authored terminal.opacity value.
    if (!IsTranslucent)
    {
        for (const auto& param_it : Terminal.parameters)
        {
            if (param_it.first != HnMaterialPrivateTokens->opacity)
                continue;

            const pxr::VtValue& vtOpacity = param_it.second;
            IsTranslucent                 = vtOpacity.Get<float>() < 1.0f;
            break;
        }
    }

    return IsTranslucent ? HnMaterialTagTokens->translucent : HnMaterialTagTokens->defaultTag;
}

float GetTerminalOpacityThreshold(const pxr::HdMaterialNode2& Terminal)
{
    for (const auto& param_it : Terminal.parameters)
    {
        if (param_it.first == HnMaterialPrivateTokens->opacityThreshold)
        {
            const pxr::VtValue& vtOpacityThreshold = param_it.second;
            return vtOpacityThreshold.Get<float>();
        }
    }
    return 0;
}

float GetTerminalOpacity(const pxr::HdMaterialNode2& Terminal)
{
    for (const auto& param_it : Terminal.parameters)
    {
        if (param_it.first == HnMaterialPrivateTokens->opacity)
        {
            const pxr::VtValue& vtOpacity = param_it.second;
            return vtOpacity.Get<float>();
        }
    }
    return 1;
}


// Get the fallback value for material node, first consulting Sdr to find
// whether the node has an input for the fallback value and then checking
// whether the output named outputName is known to Sdr and using either
// the default value specified by the SdrShaderProperty or using a
// default constructed value of the type specified by SdrShaderProperty.
pxr::VtValue GetNodeFallbackValue(const pxr::HdMaterialNode2& Node,
                                  const pxr::TfToken&         OutputName)
{
    pxr::SdrRegistry& ShaderReg = pxr::SdrRegistry::GetInstance();

    // Find the corresponding Sdr node.
    const pxr::SdrShaderNodeConstPtr SdrNode =
        ShaderReg.GetShaderNodeByIdentifierAndType(Node.nodeTypeId, pxr::HioGlslfxTokens->glslfx);
    if (!SdrNode)
    {
        return pxr::VtValue{};
    }


    // HACK: Incorrect usage of GetDefaultInput to
    // determine what the fallback value is.
    // GetDefaultInput is meant to be used for 'disabled'
    // node where the 'default input' becomes the value
    // pass-through in the network. But there is no other
    // mechanism currently to deal with fallback values.
    if (const pxr::SdrShaderPropertyConstPtr& DefaultInput = SdrNode->GetDefaultInput())
    {
        const pxr::TfToken& DefInputName = DefaultInput->GetName();
        auto const&         def_param_it = Node.parameters.find(DefInputName);
        if (def_param_it != Node.parameters.end())
        {
            return def_param_it->second;
        }
    }

    // Sdr supports specifying default values for outputs so if we
    // did not use the GetDefaultInput hack above, we fallback to
    // using this DefaultOutput value.
    if (const pxr::SdrShaderPropertyConstPtr& Output = SdrNode->GetShaderOutput(OutputName))
    {
        const pxr::VtValue Out = Output->GetDefaultValue();
        if (!Out.IsEmpty())
        {
            return Out;
        }

        // If no default value was registered with Sdr for
        // the output, fallback to the type's default.
        return Output->GetTypeAsSdfType().first.GetDefaultValue();
    }

    return pxr::VtValue{};
}


pxr::VtValue GetParamFallbackValue(const pxr::HdMaterialNetwork2& Network,
                                   const pxr::HdMaterialNode2&    Node,
                                   const pxr::TfToken&            ParamName)
{
    // The 'fallback value' will be the value of the material param if nothing
    // is connected or what is connected is mis-configured. For example a
    // missing texture file.

    // Check if there are any connections to the terminal input.
    {
        const auto conn_it = Node.inputConnections.find(ParamName);
        if (conn_it != Node.inputConnections.end())
        {
            if (!conn_it->second.empty())
            {
                const pxr::HdMaterialConnection2& Connection = conn_it->second.front();

                const auto up_it = Network.nodes.find(Connection.upstreamNode);
                if (up_it != Network.nodes.end())
                {
                    const pxr::HdMaterialNode2& UpstreamNode = up_it->second;

                    const pxr::VtValue FallbackValue =
                        GetNodeFallbackValue(UpstreamNode, Connection.upstreamOutputName);
                    if (!FallbackValue.IsEmpty())
                    {
                        return FallbackValue;
                    }
                }
            }
        }
    }

    // If there are no connections, there may be an authored value.
    {
        const auto param_it = Node.parameters.find(ParamName);
        if (param_it != Node.parameters.end())
        {
            return param_it->second;
        }
    }

    // If we had nothing connected, but we do have an Sdr node, we can use the
    // DefaultValue for the input as specified in the Sdr schema.
    // E.g. PreviewSurface is a terminal with an Sdr schema.
    {
        pxr::SdrRegistry& ShaderReg = pxr::SdrRegistry::GetInstance();

        pxr::SdrShaderNodeConstPtr TerminalSdr =
            ShaderReg.GetShaderNodeByIdentifierAndType(Node.nodeTypeId, pxr::HioGlslfxTokens->glslfx);

        if (TerminalSdr)
        {
            if (const pxr::SdrShaderPropertyConstPtr& Input = TerminalSdr->GetShaderInput(ParamName))
            {
                pxr::VtValue Out = Input->GetDefaultValue();
                // If no default value was registered with Sdr for
                // the output, fallback to the type's default.
                if (Out.IsEmpty())
                {
                    Out = Input->GetTypeAsSdfType().first.GetDefaultValue();
                }

                if (!Out.IsEmpty())
                {
                    return Out;
                }
            }
        }
    }

    // Returning an empty value will likely result in a shader compile error,
    // because the buffer source will not be able to determine the HdTupleType.
    // Hope for the best and return a vec3.
    LOG_WARNING_MESSAGE("Couldn't determine default value for: ", ParamName.GetText(), " on nodeType: ", Node.nodeTypeId.GetText());

    return pxr::VtValue{pxr::GfVec3f{0}};
}

pxr::TfToken GetPrimvarNameAttributeValue(const pxr::SdrShaderNodeConstPtr& SdrNode,
                                          const pxr::HdMaterialNode2&       Node,
                                          const pxr::TfToken&               PropName)
{
    pxr::VtValue vtName;

    // If the name of the primvar was authored, the material adapter would have
    // put that that authored value in the node's parameter list.
    // The authored value is the strongest opinion/

    const auto& param_it = Node.parameters.find(PropName);
    if (param_it != Node.parameters.end())
    {
        vtName = param_it->second;
    }

    // If we didn't find an authored value consult Sdr for the default value.
    if (vtName.IsEmpty() && SdrNode)
    {
        if (pxr::SdrShaderPropertyConstPtr SdrPrimvarInput = SdrNode->GetShaderInput(PropName))
        {
            vtName = SdrPrimvarInput->GetDefaultValue();
        }
    }

    if (vtName.IsHolding<TfToken>())
    {
        return vtName.UncheckedGet<TfToken>();
    }
    else if (vtName.IsHolding<std::string>())
    {
        return TfToken(vtName.UncheckedGet<std::string>());
    }

    return pxr::TfToken{};
}

// Look up value from material node parameters and fallback to
// corresponding value on given SdrNode.
template <typename T>
T ResolveParameter(const pxr::HdMaterialNode2&       Node,
                   const pxr::SdrShaderNodeConstPtr& SdrNode,
                   const pxr::TfToken&               Name,
                   const T&                          DefaultValue)
{
    // First consult node parameters...
    {
        const auto param_it = Node.parameters.find(Name);
        if (param_it != Node.parameters.end())
        {
            const pxr::VtValue& Value = param_it->second;
            if (Value.IsHolding<T>())
            {
                return Value.UncheckedGet<T>();
            }
        }
    }

    // Then fallback to SdrNode.
    if (SdrNode)
    {
        if (const pxr::SdrShaderPropertyConstPtr Input = SdrNode->GetShaderInput(Name))
        {
            const pxr::VtValue& Value = Input->GetDefaultValue();
            if (Value.IsHolding<T>())
            {
                return Value.UncheckedGet<T>();
            }
        }
    }

    return DefaultValue;
}

std::string ResolveAssetPath(const pxr::VtValue& Value)
{
    // Note that the SdfAssetPath should really be resolved into an ArAsset via
    // ArGetResolver (Eg. USDZ). Using GetResolvePath directly isn't sufficient.
    // Texture loading goes via Glf, which will handle the ArAsset
    // resolution already, so we skip doing it here and simply use the string.
    if (Value.IsHolding<pxr::SdfAssetPath>())
    {
        pxr::SdfAssetPath SdfPath = Value.Get<pxr::SdfAssetPath>();
        std::string       PathStr = SdfPath.GetResolvedPath();
        if (PathStr.empty())
        {
            PathStr = SdfPath.GetAssetPath();
        }
        return PathStr;
    }
    else if (Value.IsHolding<std::string>())
    {
        return Value.UncheckedGet<std::string>();
    }

    return std::string{};
}

pxr::HdWrap ResolveWrapSamplerParameter(const pxr::SdfPath&               NodePath,
                                        const pxr::HdMaterialNode2&       Node,
                                        const pxr::SdrShaderNodeConstPtr& SdrNode,
                                        const pxr::TfToken&               Name)
{
    const TfToken value = ResolveParameter(Node, SdrNode, Name, HnTextureTokens->useMetadata);
    if (value == HnTextureTokens->repeat)
    {
        return pxr::HdWrapRepeat;
    }

    if (value == HnTextureTokens->mirror)
    {
        return pxr::HdWrapMirror;
    }

    if (value == HnTextureTokens->clamp)
    {
        return pxr::HdWrapClamp;
    }

    if (value == HnTextureTokens->black)
    {
        return pxr::HdWrapBlack;
    }

    if (value == HnTextureTokens->useMetadata)
    {
        if (Node.nodeTypeId == HnMaterialPrivateTokens->HwUvTexture_1)
        {
            return pxr::HdWrapLegacy;
        }
        return pxr::HdWrapUseMetadata;
    }

    LOG_WARNING_MESSAGE("Unknown wrap mode on prim ", NodePath.GetText(), ": ", value.GetText());

    return pxr::HdWrapUseMetadata;
}

pxr::HdMinFilter ResolveMinSamplerParameter(const pxr::SdfPath&               NodePath,
                                            const pxr::HdMaterialNode2&       Node,
                                            const pxr::SdrShaderNodeConstPtr& SdrNode)
{
    // Using linearMipmapLinear as fallback value.

    // Note that it is ambiguous whether the fallback value in the old
    // texture system (usdImagingGL/textureUtils.cpp) was linear or
    // linearMipmapLinear: when nothing was authored in USD for the
    // min filter, linearMipmapLinear was used, but when an empty
    // token was authored, linear was used.

    const pxr::TfToken value = ResolveParameter(Node, SdrNode, HnTextureTokens->minFilter, HnTextureTokens->linearMipmapLinear);

    if (value == HnTextureTokens->nearest)
    {
        return pxr::HdMinFilterNearest;
    }

    if (value == HnTextureTokens->linear)
    {
        return pxr::HdMinFilterLinear;
    }

    if (value == HnTextureTokens->nearestMipmapNearest)
    {
        return pxr::HdMinFilterNearestMipmapNearest;
    }

    if (value == HnTextureTokens->nearestMipmapLinear)
    {
        return pxr::HdMinFilterNearestMipmapLinear;
    }

    if (value == HnTextureTokens->linearMipmapNearest)
    {
        return pxr::HdMinFilterLinearMipmapNearest;
    }

    if (value == HnTextureTokens->linearMipmapLinear)
    {
        return pxr::HdMinFilterLinearMipmapLinear;
    }

    return pxr::HdMinFilterLinearMipmapLinear;
}

pxr::HdMagFilter ResolveMagSamplerParameter(const pxr::SdfPath&               NodePath,
                                            const pxr::HdMaterialNode2&       Node,
                                            const pxr::SdrShaderNodeConstPtr& SdrNode)
{
    const TfToken value = ResolveParameter(Node, SdrNode, HnTextureTokens->magFilter, HnTextureTokens->linear);

    if (value == HnTextureTokens->nearest)
    {
        return pxr::HdMagFilterNearest;
    }

    return pxr::HdMagFilterLinear;
}

// Resolve sampling parameters for texture node by
// looking at material node parameters and falling back to
// fallback values from Sdr.
pxr::HdSamplerParameters GetSamplerParameters(const pxr::SdfPath&               NodePath,
                                              const pxr::HdMaterialNode2&       Node,
                                              const pxr::SdrShaderNodeConstPtr& SdrNode)
{
    return {
        ResolveWrapSamplerParameter(NodePath, Node, SdrNode, HnTextureTokens->wrapS),
        ResolveWrapSamplerParameter(NodePath, Node, SdrNode, HnTextureTokens->wrapT),
        ResolveWrapSamplerParameter(NodePath, Node, SdrNode, HnTextureTokens->wrapR),
        ResolveMinSamplerParameter(NodePath, Node, SdrNode),
        ResolveMagSamplerParameter(NodePath, Node, SdrNode),
        pxr::HdBorderColorTransparentBlack,
        false, // enableCompare
        pxr::HdCmpFuncNever,
    };
}

HnSubTextureIdentifier GetSubtextureIdentifier(const pxr::TfToken&            ParamName,
                                               const pxr::HdTextureType&      TextureType,
                                               const pxr::TfToken&            NodeType,
                                               bool                           PremultiplyAlpha,
                                               const pxr::TfToken&            SourceColorSpace,
                                               const TextureComponentMapping& Swizzle)
{
    HnSubTextureIdentifier TextureId;
    TextureId.Type             = TextureType;
    TextureId.PremultiplyAlpha = PremultiplyAlpha;

    if (SourceColorSpace == HnTokens->sRGB)
    {
        TextureId.IsSRGB = true;
    }
    else if (SourceColorSpace == "auto")
    {
        TextureId.IsSRGB = ParamName == HnTokens->diffuseColor || ParamName == HnTokens->emissiveColor;
    }

    if (TextureType == pxr::HdTextureType::Uv)
    {
        TextureId.FlipVertically = NodeType == HnMaterialPrivateTokens->HwUvTexture_1;
    }

    TextureId.Swizzle = Swizzle;

    return TextureId;
}

HnMaterialParameter GetPrimvarReaderParam(const pxr::HdMaterialNetwork2& Network,
                                          const pxr::HdMaterialNode2&    Node,
                                          const pxr::SdfPath&            NodePath,
                                          const pxr::TfToken&            ParamName)
{
    pxr::SdrRegistry& ShaderReg = pxr::SdrRegistry::GetInstance();

    pxr::SdrShaderNodeConstPtr SdrNode =
        ShaderReg.GetShaderNodeByIdentifierAndType(Node.nodeTypeId, pxr::HioGlslfxTokens->glslfx);

    HnMaterialParameter Param{HnMaterialParameter::ParamType::PrimvarRedirect, ParamName};

    // A node may require 'additional primvars' to function correctly.
    for (auto const& PropName : SdrNode->GetAdditionalPrimvarProperties())
    {
        pxr::TfToken PrimvarName = GetPrimvarNameAttributeValue(SdrNode, Node, PropName);
        if (!PrimvarName.IsEmpty())
        {
            Param.SamplerCoords.push_back(PrimvarName);
        }
    }

    return Param;
}

static HnMaterialParameter GetTransform2dParam(
    const pxr::HdMaterialNetwork2& Network,
    const pxr::HdMaterialNode2&    Node,
    const pxr::SdfPath&            NodePath,
    const pxr::TfToken&            ParamName)
{
    HnMaterialParameter Tr2dParam;
    Tr2dParam.Type          = HnMaterialParameter::ParamType::Transform2d;
    Tr2dParam.Name          = ParamName;
    Tr2dParam.FallbackValue = GetParamFallbackValue(Network, Node, HnMaterialPrivateTokens->in);

    // Find the input connection to the transform2d node
    auto in_it = Node.inputConnections.find(HnMaterialPrivateTokens->in);
    if (in_it != Node.inputConnections.end())
    {
        if (!in_it->second.empty())
        {
            const pxr::HdMaterialConnection2& Conn = in_it->second.front();

            const auto                  pn_it       = Network.nodes.find(Conn.upstreamNode);
            const pxr::HdMaterialNode2& PrimvarNode = pn_it->second;
            pxr::SdrRegistry&           ShaderReg   = pxr::SdrRegistry::GetInstance();
            if (pxr::SdrShaderNodeConstPtr PrimvarSdr = ShaderReg.GetShaderNodeByIdentifierAndType(PrimvarNode.nodeTypeId, pxr::HioGlslfxTokens->glslfx))
            {
                auto PrimvarParam       = GetPrimvarReaderParam(Network, PrimvarNode, Conn.upstreamNode, in_it->first);
                Tr2dParam.SamplerCoords = std::move(PrimvarParam.SamplerCoords);
            }
        }
    }
    else
    {
        // See if input value was directly authored as value.
        auto it = Node.parameters.find(HnMaterialPrivateTokens->in);
        if (it != Node.parameters.end())
        {
            if (it->second.IsHolding<pxr::TfToken>())
            {
                const pxr::TfToken& SamplerCoord = it->second.UncheckedGet<pxr::TfToken>();
                Tr2dParam.SamplerCoords.push_back(SamplerCoord);
            }
        }
    }

    pxr::VtValue RotationVal = GetParamFallbackValue(Network, Node, HnTokens->rotation);
    if (RotationVal.IsHolding<float>())
    {
        Tr2dParam.Transform2d.Rotation = RotationVal.UncheckedGet<float>();
    }

    pxr::VtValue ScaleVal = GetParamFallbackValue(Network, Node, HnTokens->scale);
    if (ScaleVal.IsHolding<pxr::GfVec2f>())
    {
        Tr2dParam.Transform2d.Scale = ScaleVal.UncheckedGet<pxr::GfVec2f>();
    }

    pxr::VtValue TranslationVal = GetParamFallbackValue(Network, Node, HnTokens->translation);
    if (TranslationVal.IsHolding<pxr::GfVec2f>())
    {
        Tr2dParam.Transform2d.Translation = TranslationVal.UncheckedGet<pxr::GfVec2f>();
    }

    return Tr2dParam;
}

} // namespace

HnMaterialNetwork::HnMaterialNetwork(const pxr::SdfPath&              SdfPath,
                                     const pxr::HdMaterialNetworkMap& hdNetworkMap) noexcept(false)
{
    bool                    IsVolume      = false;
    pxr::HdMaterialNetwork2 Network2      = HdConvertToHdMaterialNetwork2(hdNetworkMap, &IsVolume);
    const pxr::TfToken&     TerminalToken = IsVolume ? pxr::HdMaterialTerminalTokens->volume : pxr::HdMaterialTerminalTokens->surface;

    pxr::SdfPath                TerminalPath;
    const pxr::HdMaterialNode2* TerminalNode = GetTerminalNode(Network2, TerminalToken, TerminalPath);
    if (TerminalNode == nullptr)
    {
        return;
    }

    // Glslfx is only used to extract metadata, which is in turn used
    // to determine the material tag.
#if 0
    // Extract the glslfx and metadata for surface/volume.
    if (auto Glslfx = GetGlslfxForTerminal(TerminalNode->nodeTypeId))
    {
        if (Glslfx->IsValid())
        {
            m_Metadata = Glslfx->GetMetadata();
        }
    }
#endif
    m_Tag              = GetMaterialTag(m_Metadata, *TerminalNode);
    m_OpacityThreshold = GetTerminalOpacityThreshold(*TerminalNode);
    m_Opacity          = GetTerminalOpacity(*TerminalNode);

    LoadParams(Network2, *TerminalNode);
}

void HnMaterialNetwork::LoadParams(const pxr::HdMaterialNetwork2& Network,
                                   const pxr::HdMaterialNode2&    Node)
{
    // Hydrogent currently supports two material configurations.
    // A custom glslfx file or a PreviewSurface material network.
    // Either configuration consists of a terminal (Shader or PreviewSurface)
    // with its input values authored or connected to a primvar, texture or
    // volume node. The texture may have a primvar connected to provide UVs.
    //
    // The following code is made to process one of these two material configs
    // exclusively. It cannot convert arbitrary material networks by
    // generating the appropriate glsl code.

    pxr::SdrRegistry& ShaderReg = pxr::SdrRegistry::GetInstance();

    const pxr::SdrShaderNodeConstPtr SdrNode =
        ShaderReg.GetShaderNodeByIdentifierAndType(Node.nodeTypeId, pxr::HioGlslfxTokens->glslfx);

    if (SdrNode)
    {
        pxr::SdfPathSet VisitedNodes;
        for (const TfToken& InputName : SdrNode->GetInputNames())
        {
            ProcessInputParameter(Network, Node, InputName, VisitedNodes);
        }
    }
    else
    {
        LOG_WARNING_MESSAGE("Unrecognized node: ", Node.nodeTypeId.GetText());
    }

    // Set fallback values for the inputs on the terminal (excepting
    // referenced sampler coords).
    for (auto& Param : m_Parameters)
    {
        if (Param.Type != HnMaterialParameter::ParamType::AdditionalPrimvar &&
            Param.FallbackValue.IsEmpty())
        {
            Param.FallbackValue = GetParamFallbackValue(Network, Node, Param.Name);
        }
    }

    if (SdrNode)
    {
        // Create HnMaterialParameter for each primvar the terminal says it
        // needs.
        // Primvars come from 'attributes' in the glslfx and are separate from
        // the input 'parameters'. We need to create a material param for them
        // so that these primvars survive 'primvar filtering' that discards any
        // unused primvars on the mesh.
        // If the network lists additional primvars, we add those too.
        pxr::NdrTokenVec Primvars = SdrNode->GetPrimvars();
        Primvars.insert(Primvars.end(), Network.primvars.begin(), Network.primvars.end());
        std::sort(Primvars.begin(), Primvars.end());
        Primvars.erase(std::unique(Primvars.begin(), Primvars.end()), Primvars.end());

        for (const TfToken& PrimvarName : Primvars)
        {
            AddAdditionalPrimvarParameter(PrimvarName);
        }
    }
}

void HnMaterialNetwork::AddAdditionalPrimvarParameter(const pxr::TfToken& PrimvarName)
{
    m_Parameters.emplace_back(HnMaterialParameter::ParamType::AdditionalPrimvar, PrimvarName);
}


void HnMaterialNetwork::AddUnconnectedParam(const pxr::TfToken& ParamName)
{
    m_Parameters.emplace_back(HnMaterialParameter::ParamType::Fallback, ParamName);
}


void HnMaterialNetwork::ProcessInputParameter(const pxr::HdMaterialNetwork2& Network,
                                              const pxr::HdMaterialNode2&    Node,
                                              const pxr::TfToken&            ParamName,
                                              pxr::SdfPathSet&               VisitedNodes)
{
    pxr::SdrRegistry& ShaderReg = pxr::SdrRegistry::GetInstance();

    // Resolve what is connected to this param (eg. primvar, texture, nothing)
    // and then make the correct HdSt_MaterialParam for it.
    auto const& conn_it = Node.inputConnections.find(ParamName);
    if (conn_it != Node.inputConnections.end())
    {
        const std::vector<pxr::HdMaterialConnection2>& Connections = conn_it->second;
        if (!Connections.empty())
        {
            // Find the node that is connected to this input
            const pxr::HdMaterialConnection2& Conn  = Connections.front();
            auto const&                       up_it = Network.nodes.find(Conn.upstreamNode);

            if (up_it != Network.nodes.end())
            {
                const pxr::SdfPath&         UpstreamPath       = up_it->first;
                const pxr::TfToken&         UpstreamOutputName = Conn.upstreamOutputName;
                const pxr::HdMaterialNode2& UpstreamNode       = up_it->second;

                pxr::SdrShaderNodeConstPtr UpstreamSdr =
                    ShaderReg.GetShaderNodeByIdentifier(
                        UpstreamNode.nodeTypeId,
                        {pxr::HioGlslfxTokens->glslfx, HnMaterialPrivateTokens->mtlx});

                if (UpstreamSdr)
                {
                    pxr::TfToken SdrRole{UpstreamSdr->GetRole()};
                    if (SdrRole == pxr::SdrNodeRole->Texture)
                    {
                        AddTextureParam(
                            Network,
                            UpstreamNode,
                            Node,
                            UpstreamPath,
                            UpstreamOutputName,
                            ParamName,
                            VisitedNodes);
                        return;
                    }
                    else if (SdrRole == pxr::SdrNodeRole->Primvar)
                    {
                        AddPrimvarReaderParam(
                            Network,
                            UpstreamNode,
                            UpstreamPath,
                            ParamName,
                            VisitedNodes);
                        return;
                    }
                    else if (SdrRole == pxr::SdrNodeRole->Field)
                    {
                        AddFieldReaderParam(
                            Network,
                            UpstreamNode,
                            UpstreamPath,
                            ParamName,
                            VisitedNodes);
                        return;
                    }
                    else if (SdrRole == pxr::SdrNodeRole->Math)
                    {
                        AddTransform2dParam(
                            Network,
                            UpstreamNode,
                            UpstreamPath,
                            ParamName,
                            VisitedNodes);
                        return;
                    }
                }
                else
                {
                    LOG_WARNING_MESSAGE("Unrecognized connected node: ", UpstreamNode.nodeTypeId.GetText());
                }
            }
        }
    }

    // Nothing (supported) was connected, output a fallback material param
    AddUnconnectedParam(ParamName);
}

static TextureComponentMapping SwizzleStringToComponentMapping(std::string SwizzleStr)
{
    for (auto& c : SwizzleStr)
    {
        if (c == 'x' || c == 'X' || c == 'r' || c == 'R')
            c = 'r';
        else if (c == 'y' || c == 'Y' || c == 'g' || c == 'G')
            c = 'g';
        else if (c == 'z' || c == 'Z' || c == 'b' || c == 'B')
            c = 'b';
        else if (c == 'w' || c == 'W' || c == 'a' || c == 'A')
            c = 'a';
        else
            LOG_WARNING_MESSAGE("Unknown texture swizzle component: ", c);
    }
    TextureComponentMapping Mapping;
    TextureComponentMappingFromString(SwizzleStr, Mapping);
    return Mapping;
}

void HnMaterialNetwork::AddTextureParam(const pxr::HdMaterialNetwork2& Network,
                                        const pxr::HdMaterialNode2&    Node,
                                        const pxr::HdMaterialNode2&    DownstreamNode, // needed to determine def value
                                        const pxr::SdfPath&            NodePath,
                                        const pxr::TfToken&            OutputName,
                                        const pxr::TfToken&            ParamName,
                                        pxr::SdfPathSet&               VisitedNodes)
{
    // Make sure to add output name as the same texture may be used multiple times
    // with different swizzles. For example, Metallic-Roughness.g, Metallic-Roughness.b.
    if (!VisitedNodes.emplace(NodePath.AppendProperty(OutputName)).second)
        return;

    pxr::SdrRegistry&          ShaderReg = pxr::SdrRegistry::GetInstance();
    pxr::SdrShaderNodeConstPtr SdrNode =
        ShaderReg.GetShaderNodeByIdentifier(Node.nodeTypeId, {pxr::HioGlslfxTokens->glslfx, HnMaterialPrivateTokens->mtlx});

    HnMaterialParameter TexParam{HnMaterialParameter::ParamType::Texture, ParamName};

    // Get swizzle metadata if possible
    if (pxr::SdrShaderPropertyConstPtr SdrProperty = SdrNode->GetShaderOutput(OutputName))
    {
        const pxr::NdrTokenMap& PropMetadata = SdrProperty->GetMetadata();

        const auto& it = PropMetadata.find(HnSdrMetadataTokens->swizzle);
        if (it != PropMetadata.end())
        {
            TexParam.Swizzle = SwizzleStringToComponentMapping(it->second);
        }
    }

    // Determine the texture type
    TexParam.TextureType = pxr::HdTextureType::Uv;
    if (SdrNode && SdrNode->GetMetadata().count(HnMaterialPrivateTokens->isPtex))
    {
        LOG_ERROR_MESSAGE("PTex textures are not currently supported");
        TexParam.TextureType = pxr::HdTextureType::Ptex;
    }

    // Determine if texture should be pre-multiplied on CPU
    // Currently, this will only happen if the texture param is called
    // "diffuseColor" and if there is another param "opacity" connected to the
    // same texture node via output "a", as long as the material tag is not
    // "masked"
    if (ParamName == HnMaterialPrivateTokens->diffuseColor &&
        m_Tag != HnMaterialTagTokens->masked)
    {
        auto const& opacity_conn_it = DownstreamNode.inputConnections.find(HnMaterialPrivateTokens->opacity);
        if (opacity_conn_it != DownstreamNode.inputConnections.end())
        {
            const pxr::HdMaterialConnection2& Conn = opacity_conn_it->second.front();
            TexParam.IsPremultiplied =
                (NodePath == Conn.upstreamNode) &&
                (Conn.upstreamOutputName == HnMaterialPrivateTokens->a);
        }
    }


    // Get texture's sourceColorSpace hint
    // XXX: This is a workaround for Presto. If there's no colorspace token,
    // check if there's a colorspace string.
    pxr::TfToken SourceColorSpace = ResolveParameter(Node, SdrNode, HnMaterialPrivateTokens->sourceColorSpace, TfToken{});
    if (SourceColorSpace.IsEmpty())
    {
        const std::string SourceColorSpaceStr = ResolveParameter(Node, SdrNode, HnMaterialPrivateTokens->sourceColorSpace, HnTokens->colorSpaceAuto.GetString());
        SourceColorSpace                      = TfToken{SourceColorSpaceStr};
    }

    // Extract texture file path
    bool UseTexturePrimToFindTexture = true;

    pxr::SdfPath TexturePrimPathForSceneDelegate;

    HnTextureIdentifier TextureId;
    TextureId.SubtextureId.Type = TexParam.TextureType;

    const pxr::NdrTokenVec& AssetIdentifierPropertyNames = SdrNode->GetAssetIdentifierInputNames();

    if (AssetIdentifierPropertyNames.size() == 1)
    {
        const pxr::TfToken& FileProp = AssetIdentifierPropertyNames[0];
        const auto          param_it = Node.parameters.find(FileProp);
        if (param_it != Node.parameters.end())
        {
            const pxr::VtValue& v = param_it->second;
            // We use the nodePath, not the filePath, for the 'connection'.
            // Based on the connection path we will do a texture lookup via
            // the scene delegate. The scene delegate will lookup this texture
            // prim (by path) to query the file attribute value for filepath.
            // The reason for this re-direct is to support other texture uses
            // such as render-targets.
            TexturePrimPathForSceneDelegate = NodePath;

            // Use the type of the filePath attribute to determine
            // whether to use the texture system (for
            // SdfAssetPath/std::string/ HdStTextureIdentifier) or use
            // the render buffer associated to a draw target.
            if (v.IsHolding<HnTextureIdentifier>())
            {
                //
                // Clients can explicitly give an HdStTextureIdentifier for
                // more direct control since they can give an instance of
                // HdStSubtextureIdentifier.
                //
                // Examples are, e.g., HdStUvAssetSubtextureIdentifier
                // allowing clients to flip the texture. Clients can even
                // subclass from HdStDynamicUvSubtextureIdentifier and
                // HdStDynamicUvTextureImplementation to implement their own
                // texture loading and commit.
                //
                UseTexturePrimToFindTexture = false;
                TextureId                   = v.UncheckedGet<HnTextureIdentifier>();
            }
            else if (v.IsHolding<std::string>() ||
                     v.IsHolding<pxr::SdfAssetPath>())
            {
                const std::string filePath = ResolveAssetPath(v);

#if 0
                if (HdStIsSupportedUdimTexture(filePath))
                {
                    texParam.textureType = HdTextureType::Udim;
                }
#endif

                UseTexturePrimToFindTexture = false;

                TextureId = HnTextureIdentifier{
                    TfToken{filePath},
                    GetSubtextureIdentifier(
                        ParamName,
                        TexParam.TextureType,
                        Node.nodeTypeId,
                        TexParam.IsPremultiplied,
                        SourceColorSpace,
                        TexParam.Swizzle),
                };

                // If the file attribute is an SdfPath, interpret it as path
                // to a prim holding the texture resource (e.g., a render buffer).
            }
            else if (v.IsHolding<pxr::SdfPath>())
            {
                TexturePrimPathForSceneDelegate = v.UncheckedGet<pxr::SdfPath>();
            }
        }
    }
    else
    {
        LOG_WARNING_MESSAGE("Invalid number of asset identifier input names: ", NodePath.GetText());
    }


    // Check to see if a primvar or transform2d node is connected to 'st' or
    // 'uv'.
    // Instead of looking for a st inputs by name we could traverse all
    // connections to inputs and pick one that has a 'primvar' or 'transform2d'
    // node attached. That could also be problematic if you connect a primvar or
    // transform2d to one of the other inputs of the texture node.
    auto st_it = Node.inputConnections.find(HnMaterialPrivateTokens->st);
    if (st_it == Node.inputConnections.end())
    {
        st_it = Node.inputConnections.find(HnMaterialPrivateTokens->uv);
    }

    if (st_it != Node.inputConnections.end())
    {
        if (!st_it->second.empty())
        {
            const pxr::HdMaterialConnection2& Conn             = st_it->second.front();
            const pxr::SdfPath&               UpstreamNodePath = Conn.upstreamNode;

            const auto                  up_it        = Network.nodes.find(UpstreamNodePath);
            const pxr::HdMaterialNode2& UpstreamNode = up_it->second;

            pxr::SdrShaderNodeConstPtr UpstreamSdr =
                ShaderReg.GetShaderNodeByIdentifierAndType(UpstreamNode.nodeTypeId, pxr::HioGlslfxTokens->glslfx);

            if (UpstreamSdr)
            {
                pxr::TfToken SdrRole{UpstreamSdr->GetRole()};
                if (SdrRole == pxr::SdrNodeRole->Primvar)
                {
                    auto PrimvarParam = GetPrimvarReaderParam(Network, UpstreamNode, UpstreamNodePath, st_it->first);
                    // Extract the referenced primvar(s) for use in the texture
                    // sampler coords.
                    TexParam.SamplerCoords = PrimvarParam.SamplerCoords;
                }
                else if (SdrRole == pxr::SdrNodeRole->Math)
                {
                    HnMaterialParameter Transform2dParam = GetTransform2dParam(Network, UpstreamNode, UpstreamNodePath, ParamName);

                    // The texure's sampler coords should come from the output of the transform2d
                    TexParam.SamplerCoords = Transform2dParam.SamplerCoords;

                    m_Parameters.push_back(std::move(Transform2dParam));
                }

                // For any referenced primvars, add them as "additional primvars"
                // to make sure they pass primvar filtering.
                for (const auto& PrimvarName : TexParam.SamplerCoords)
                {
                    AddAdditionalPrimvarParameter(PrimvarName);
                }
            }
        }
    }
    else
    {
        // See if ST value was directly authored as value.
        auto param_it = Node.parameters.find(HnMaterialPrivateTokens->st);
        if (param_it == Node.parameters.end())
        {
            param_it = Node.parameters.find(HnMaterialPrivateTokens->uv);
        }

        if (param_it != Node.parameters.end())
        {
            if (param_it->second.IsHolding<pxr::TfToken>())
            {
                const pxr::TfToken& SamplerCoord = param_it->second.UncheckedGet<TfToken>();
                TexParam.SamplerCoords.push_back(SamplerCoord);
            }
        }
    }

    // Input scale (e.g., for a normal map, this can be (2, 2, 2, 2))
    TexParam.InputScale = ResolveParameter(Node, SdrNode, HnTokens->scale, pxr::GfVec4f{1.0f});

    // Input bias (e.g., for a normal map, this can be (-1, -1, -1, -1))
    TexParam.InputBias = ResolveParameter(Node, SdrNode, HnTokens->bias, pxr::GfVec4f{0.0f});

    // Attribute is in Mebibytes, convert to bytes.
    const size_t memoryRequest = 1048576 * ResolveParameter<float>(Node, SdrNode, HnMaterialPrivateTokens->textureMemory, 0.0f);

    {
        TextureDescriptor TexDescr;
        TexDescr.Name                        = ParamName;
        TexDescr.TextureId                   = TextureId;
        TexDescr.SamplerParams               = GetSamplerParameters(NodePath, Node, SdrNode);
        TexDescr.MemoryRequest               = memoryRequest;
        TexDescr.UseTexturePrimToFindTexture = UseTexturePrimToFindTexture;
        TexDescr.TexturePrim                 = TexturePrimPathForSceneDelegate;

        m_Textures.emplace_back(std::move(TexDescr));
    }

    m_Parameters.push_back(std::move(TexParam));
}

void HnMaterialNetwork::AddPrimvarReaderParam(const pxr::HdMaterialNetwork2& Network,
                                              const pxr::HdMaterialNode2&    Node,
                                              const pxr::SdfPath&            NodePath,
                                              const pxr::TfToken&            ParamName,
                                              pxr::SdfPathSet&               VisitedNodes)
{
    if (!VisitedNodes.emplace(NodePath).second)
        return;

    m_Parameters.emplace_back(GetPrimvarReaderParam(Network, Node, NodePath, ParamName));
}

void HnMaterialNetwork::AddFieldReaderParam(const pxr::HdMaterialNetwork2& Network,
                                            const pxr::HdMaterialNode2&    Node,
                                            const pxr::SdfPath&            NodePath,
                                            const pxr::TfToken&            ParamName,
                                            pxr::SdfPathSet&               VisitedNodes)
{
    if (!VisitedNodes.emplace(NodePath).second)
        return;

    // Volume Fields act more like a primvar then a texture.
    // There is a `Volume` prim with 'fields' that may point to a
    // OpenVDB file. We have to find the 'inputs:fieldname' on the
    // HWFieldReader in the material network to know what 'field' to use.
    // See also HdStVolume and HdStField for how volume textures are
    // inserted into Storm.

    HnMaterialParameter Param{HnMaterialParameter::ParamType::FieldRedirect, ParamName};

    // XXX Why HnMaterialPrivateTokens->fieldname:
    // Hard-coding the name of the attribute of HwFieldReader identifying
    // the field name for now.
    // The equivalent of the generic mechanism Sdr provides for primvars
    // is missing for fields: UsdPrimvarReader.inputs:varname is tagged with
    // SdrMetadata as primvarProperty="1" so that we can use
    // SdrNode->GetAdditionalPrimvarProperties to know what attribute to use.
    const pxr::TfToken& VarName = HnMaterialPrivateTokens->fieldname;

    auto const& param_it = Node.parameters.find(VarName);
    if (param_it != Node.parameters.end())
    {
        pxr::VtValue FieldName = param_it->second;
        if (FieldName.IsHolding<pxr::TfToken>())
        {
            // Stashing name of field in _samplerCoords.
            Param.SamplerCoords.push_back(FieldName.UncheckedGet<pxr::TfToken>());
        }
        else if (FieldName.IsHolding<std::string>())
        {
            Param.SamplerCoords.push_back(pxr::TfToken{FieldName.UncheckedGet<std::string>()});
        }
    }

    m_Parameters.emplace_back(std::move(Param));
}

void HnMaterialNetwork::AddTransform2dParam(const pxr::HdMaterialNetwork2& Network,
                                            const pxr::HdMaterialNode2&    Node,
                                            const pxr::SdfPath&            NodePath,
                                            const pxr::TfToken&            ParamName,
                                            pxr::SdfPathSet&               VisitedNodes)
{
    if (!VisitedNodes.emplace(NodePath).second)
        return;

    m_Parameters.emplace_back(GetTransform2dParam(Network, Node, NodePath, pxr::TfToken{ParamName.GetString()}));
}

const HnMaterialParameter* HnMaterialNetwork::GetParameter(HnMaterialParameter::ParamType Type, const pxr::TfToken& Name) const
{
    for (const auto& Param : m_Parameters)
    {
        if (Param.Type == Type && Param.Name == Name)
        {
            return &Param;
        }
    }

    return nullptr;
}

} // namespace USD

} // namespace Diligent
