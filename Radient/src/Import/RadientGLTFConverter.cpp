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

#include "Import/RadientGLTFConverter.hpp"

#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "Import/RadientImportedScene.hpp"
#include "Math/RadientMath.hpp"
#include "RadientSceneWriter.h"
#include "RadientSkinning.h"

#include "Errors.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFLoader.hpp"
#include "GraphicsAccessories.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

struct MeshSourceDataOwner
{
    explicit MeshSourceDataOwner(std::shared_ptr<const GLTF::Document> pDoc) :
        pDocument{std::move(pDoc)}
    {
    }

    std::shared_ptr<const GLTF::Document> pDocument;
    std::vector<Uint32>                   GeneratedIndices;
};

RadientTransform ToRadientTransform(const GLTF::Node& Node)
{
    // GLTF nodes define either a matrix or TRS. Keep the common TRS path direct
    // and only decompose a matrix when the node actually uses one.
    if (Node.Matrix != float4x4::Identity())
        return RadientMath::MatrixToTransform(RadientMath::ToRadientMatrix(Node.Matrix));

    RadientTransform Transform{};
    Transform.Position = RadientMath::ToRadientFloat3(Node.Translation);
    Transform.Rotation = RadientMath::ToRadientQuaternion(Node.Rotation);
    Transform.Scale    = RadientMath::ToRadientFloat3(Node.Scale);
    return Transform;
}

RadientCameraComponent ToRadientCamera(const GLTF::Camera& Camera)
{
    RadientCameraComponent Result{};

    if (Camera.Type == GLTF::Camera::Projection::Orthographic)
    {
        Result.Projection         = RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
        Result.HorizontalAperture = 2.f * Camera.Orthographic.XMag;
        Result.VerticalAperture   = 2.f * Camera.Orthographic.YMag;
        Result.ClippingRange      = {Camera.Orthographic.ZNear, Camera.Orthographic.ZFar};
    }
    else if (Camera.Type == GLTF::Camera::Projection::Perspective)
    {
        Result.Projection    = RADIENT_CAMERA_PROJECTION_PERSPECTIVE;
        Result.ClippingRange = {Camera.Perspective.ZNear, Camera.Perspective.ZFar};

        if (Camera.Perspective.AspectRatio > 0.f)
            Result.HorizontalAperture = Result.VerticalAperture * Camera.Perspective.AspectRatio;

        if (Camera.Perspective.YFov > 0.f)
            Result.FocalLength = Result.VerticalAperture / (2.f * std::tan(Camera.Perspective.YFov * 0.5f));
    }

    return Result;
}

RADIENT_LIGHT_TYPE ToRadientLightType(GLTF::Light::TYPE Type)
{
    switch (Type)
    {
        case GLTF::Light::TYPE::DIRECTIONAL:
            return RADIENT_LIGHT_TYPE_DIRECTIONAL;

        case GLTF::Light::TYPE::POINT:
            return RADIENT_LIGHT_TYPE_POINT;

        case GLTF::Light::TYPE::SPOT:
            return RADIENT_LIGHT_TYPE_SPOT;

        default:
            UNEXPECTED("Unexpected GLTF light type");
            return RADIENT_LIGHT_TYPE_DIRECTIONAL;
    }
}

RadientLightComponent ToRadientLight(const GLTF::Light& Light)
{
    RadientLightComponent Result{};
    Result.Type           = ToRadientLightType(Light.Type);
    Result.Color          = RadientMath::ToRadientFloat3(Light.Color);
    Result.Intensity      = Light.Intensity;
    Result.Range          = Light.Range;
    Result.InnerConeAngle = Light.InnerConeAngle;
    Result.OuterConeAngle = Light.OuterConeAngle;
    return Result;
}

bool GetNodeIndex(const GLTF::Model& Model,
                  const GLTF::Node*  pNode,
                  Uint32&            NodeIndex)
{
    if (pNode == nullptr ||
        pNode->Index < 0 ||
        static_cast<size_t>(pNode->Index) >= Model.Nodes.size() ||
        pNode != &Model.Nodes[static_cast<size_t>(pNode->Index)])
    {
        return false;
    }

    NodeIndex = static_cast<Uint32>(pNode->Index);
    return true;
}

RADIENT_STATUS CreateImportedSkin(const GLTF::Model&                Model,
                                  Uint32                            SkinIndex,
                                  IRadientAssetManager&             AssetManager,
                                  RefCntAutoPtr<IRadientSkinAsset>& pSkin)
{
    VERIFY_EXPR(SkinIndex < Model.Skins.size());
    const GLTF::Skin& Skin = Model.Skins[SkinIndex];

    if (Skin.Joints.empty() || Skin.Joints.size() > std::numeric_limits<Uint32>::max())
    {
        LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " must contain at least one joint");
        return RADIENT_STATUS_INVALID_DATA;
    }

    if (!Skin.InverseBindMatrices.empty() &&
        Skin.InverseBindMatrices.size() != Skin.Joints.size())
    {
        LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains ", Skin.Joints.size(),
                          " joints, but ", Skin.InverseBindMatrices.size(), " inverse-bind matrices");
        return RADIENT_STATUS_INVALID_DATA;
    }

    // A glTF skin only lists palette joints. Preserve every ancestor as well,
    // because non-joint nodes may contribute transforms to the joint hierarchy.
    std::vector<Uint8> IncludedNodes(Model.Nodes.size(), 0);
    std::vector<Uint8> PaletteNodes(Model.Nodes.size(), 0);
    for (size_t PaletteIndex = 0; PaletteIndex < Skin.Joints.size(); ++PaletteIndex)
    {
        const GLTF::Node* pJoint = Skin.Joints[PaletteIndex];
        Uint32            JointNodeIndex;
        if (!GetNodeIndex(Model, pJoint, JointNodeIndex))
        {
            LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " references an invalid joint node at palette index ", PaletteIndex);
            return RADIENT_STATUS_INVALID_DATA;
        }
        if (PaletteNodes[JointNodeIndex] != 0)
        {
            LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " references node ", JointNodeIndex, " more than once");
            return RADIENT_STATUS_INVALID_DATA;
        }
        PaletteNodes[JointNodeIndex] = 1;

        for (const GLTF::Node* pAncestor = pJoint; pAncestor != nullptr; pAncestor = pAncestor->Parent)
        {
            Uint32 AncestorIndex;
            if (!GetNodeIndex(Model, pAncestor, AncestorIndex))
            {
                LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains an invalid joint ancestor");
                return RADIENT_STATUS_INVALID_DATA;
            }

            if (IncludedNodes[AncestorIndex] != 0)
                break;

            IncludedNodes[AncestorIndex] = 1;
        }
    }

    std::vector<Uint32> NodeToSkeletonJoint(Model.Nodes.size(), InvalidRadientJointIndex);
    Uint32              SkeletonJointCount = 0;
    for (size_t NodeIndex = 0; NodeIndex < IncludedNodes.size(); ++NodeIndex)
    {
        if (IncludedNodes[NodeIndex] != 0)
            NodeToSkeletonJoint[NodeIndex] = SkeletonJointCount++;
    }

    std::vector<RadientSkeletonJointDesc> SkeletonJoints(SkeletonJointCount);
    for (size_t NodeIndex = 0; NodeIndex < IncludedNodes.size(); ++NodeIndex)
    {
        if (IncludedNodes[NodeIndex] == 0)
            continue;

        const GLTF::Node&         Node  = Model.Nodes[NodeIndex];
        RadientSkeletonJointDesc& Joint = SkeletonJoints[NodeToSkeletonJoint[NodeIndex]];
        Joint.Name                      = Node.Name.c_str();
        Joint.LocalRestTransform        = ToRadientTransform(Node);

        if (Node.Parent != nullptr)
        {
            Uint32 ParentNodeIndex;
            if (!GetNodeIndex(Model, Node.Parent, ParentNodeIndex) ||
                NodeToSkeletonJoint[ParentNodeIndex] == InvalidRadientJointIndex)
            {
                LOG_ERROR_MESSAGE("GLTF skin ", SkinIndex, " contains an invalid joint parent");
                return RADIENT_STATUS_INVALID_DATA;
            }
            Joint.ParentJointIndex = NodeToSkeletonJoint[ParentNodeIndex];
        }
    }

    RadientSkeletonDesc SkeletonDesc{};
    SkeletonDesc.Name       = Skin.Name.c_str();
    SkeletonDesc.pJoints    = SkeletonJoints.data();
    SkeletonDesc.JointCount = SkeletonJointCount;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    RADIENT_STATUS                       Status = AssetManager.CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty());
    if (RADIENT_FAILED(Status) || pSkeleton == nullptr)
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

    std::vector<RadientSkinJointBindingDesc> SkinJoints(Skin.Joints.size());
    for (size_t PaletteIndex = 0; PaletteIndex < Skin.Joints.size(); ++PaletteIndex)
    {
        Uint32 JointNodeIndex;
        if (!GetNodeIndex(Model, Skin.Joints[PaletteIndex], JointNodeIndex))
        {
            UNEXPECTED("A validated GLTF skin joint became invalid");
            return RADIENT_STATUS_INVALID_DATA;
        }

        RadientSkinJointBindingDesc& Joint = SkinJoints[PaletteIndex];
        Joint.SkeletonJointIndex           = NodeToSkeletonJoint[JointNodeIndex];
        Joint.InverseBindMatrix            = RadientMath::ToRadientMatrix(
            Skin.InverseBindMatrices.empty() ?
                float4x4::Identity() :
                Skin.InverseBindMatrices[PaletteIndex]);
    }

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = Skin.Name.c_str();
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = SkinJoints.data();
    SkinDesc.JointCount = static_cast<Uint32>(SkinJoints.size());

    Status = AssetManager.CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty());
    return RADIENT_FAILED(Status) || pSkin == nullptr ?
        (RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED) :
        RADIENT_STATUS_OK;
}

RADIENT_STATUS ExtractSkins(const GLTF::Model&               Model,
                            IRadientAssetManager*            pAssetManager,
                            RadientImport::ImportedDocument& Scene)
{
    Scene.Skins.clear();
    if (Model.Skins.empty())
        return RADIENT_STATUS_OK;

    if (pAssetManager == nullptr)
    {
        LOG_ERROR_MESSAGE("A Radient asset manager is required to import GLTF skins");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Model.Skins.size() > std::numeric_limits<Uint32>::max())
        return RADIENT_STATUS_INVALID_DATA;

    Scene.Skins.resize(Model.Skins.size());
    for (Uint32 SkinIndex = 0; SkinIndex < static_cast<Uint32>(Model.Skins.size()); ++SkinIndex)
    {
        const RADIENT_STATUS Status = CreateImportedSkin(Model, SkinIndex, *pAssetManager, Scene.Skins[SkinIndex]);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

Uint32 FindSkinIndex(const GLTF::Model& Model, const GLTF::Skin* pSkin)
{
    for (size_t SkinIndex = 0; SkinIndex < Model.Skins.size(); ++SkinIndex)
    {
        if (&Model.Skins[SkinIndex] == pSkin)
            return static_cast<Uint32>(SkinIndex);
    }

    return RadientImport::InvalidImportedSkinIndex;
}

RADIENT_STATUS CreateNode(IRadientSceneWriter&                              Writer,
                          const RadientImport::ImportedDocument&            Scene,
                          Uint32                                            NodeIndex,
                          RadientEntityID                                   Parent,
                          std::vector<RefCntAutoPtr<IRadientSkeletonPose>>& SkinPoses)
{
    if (NodeIndex >= Scene.Nodes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientImport::ImportedNode& Node         = Scene.Nodes[NodeIndex];
    const std::string                  FallbackName = std::string{"GLTF Node "} + std::to_string(NodeIndex);

    RadientEntityDesc NodeDesc{};
    NodeDesc.Name      = !Node.Name.empty() ? Node.Name.c_str() : FallbackName.c_str();
    NodeDesc.Parent    = Parent;
    NodeDesc.Transform = Node.Transform;

    RadientEntityID NodeEntity = InvalidRadientEntityID;
    RADIENT_STATUS  Status     = Writer.CreateEntity(NodeDesc, NodeEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (Node.Camera)
    {
        Status = Writer.SetCamera(NodeEntity, *Node.Camera);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (Node.Light)
    {
        Status = Writer.SetLight(NodeEntity, *Node.Light);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (Node.pMesh != nullptr)
    {
        RadientMeshComponent Mesh{};
        Mesh.pMesh = Node.pMesh;
        Status     = Writer.SetMesh(NodeEntity, Mesh);
        if (RADIENT_FAILED(Status))
            return Status;

        if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
        {
            if (Node.SkinIndex >= Scene.Skins.size() || Scene.Skins[Node.SkinIndex] == nullptr)
                return RADIENT_STATUS_INVALID_DATA;

            RefCntAutoPtr<IRadientSkeletonPose>& pPose = SkinPoses[Node.SkinIndex];
            if (pPose == nullptr)
            {
                IRadientSkeletonAsset* const pSkeleton = Scene.Skins[Node.SkinIndex]->GetDesc().pSkeleton;
                if (pSkeleton == nullptr)
                    return RADIENT_STATUS_INVALID_DATA;

                Status = pSkeleton->CreatePose(pPose.GetAddressOfEmpty());
                if (RADIENT_FAILED(Status) || pPose == nullptr)
                    return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
            }

            RadientSkinComponent SkinComponent{};
            SkinComponent.pSkin = Scene.Skins[Node.SkinIndex];
            SkinComponent.pPose = pPose;
            Status              = Writer.SetSkin(NodeEntity, SkinComponent);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        RadientMeshRendererComponent Renderer{};
        Status = Writer.SetMeshRenderer(NodeEntity, Renderer);
        if (RADIENT_FAILED(Status))
            return Status;
    }
    else if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
    {
        return RADIENT_STATUS_INVALID_DATA;
    }

    for (Uint32 ChildIndex : Node.Children)
    {
        Status = CreateNode(Writer, Scene, ChildIndex, NodeEntity, SkinPoses);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

Uint32 GetDefaultSceneIndex(const GLTF::Model& Model)
{
    return Model.DefaultSceneId >= 0 && static_cast<size_t>(Model.DefaultSceneId) < Model.Scenes.size() ?
        static_cast<Uint32>(Model.DefaultSceneId) :
        0;
}

RefCntAutoPtr<IRadientMeshAsset> GetRadientMeshAsset(const GLTF::Mesh& Mesh)
{
    return RefCntAutoPtr<IRadientMeshAsset>{Mesh.pUserData.RawPtr(), IID_RadientMeshAsset};
}

Uint32 GetDefaultSceneIndex(const RadientImport::ImportedDocument& Scene)
{
    return Scene.DefaultSceneId < Scene.Scenes.size() ? Scene.DefaultSceneId : 0u;
}

RADIENT_STATUS ResolveSceneIndex(const RadientImport::ImportedDocument& Scene,
                                 Uint32                                 RequestedSceneIndex,
                                 Uint32&                                SceneIndex)
{
    if (RequestedSceneIndex == InvalidRadientSceneIndex)
    {
        SceneIndex = GetDefaultSceneIndex(Scene);
        return RADIENT_STATUS_OK;
    }

    if (RequestedSceneIndex >= Scene.Scenes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    SceneIndex = RequestedSceneIndex;
    return RADIENT_STATUS_OK;
}

} // namespace

namespace RadientGLTFConverter
{

MeshVertexSourceResult CreateMeshVertexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                              const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                              const std::shared_ptr<const GLTF::Document>& pDocument)
{
    if (pDocument == nullptr)
    {
        MeshVertexSourceResult Result;
        Result.Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return Result;
    }

    const int* pPositionAccessor = GltfPrimitive.GetAttribute(GLTF::PositionAttributeName);
    if (pPositionAccessor == nullptr)
        return {};

    const auto PositionData = GLTF::GetGltfDataInfo(GltfModel, *pPositionAccessor);
    if (PositionData.pData == nullptr ||
        PositionData.ByteStride <= 0 ||
        PositionData.Count == 0)
    {
        return {};
    }

    float3 BBMin;
    float3 BBMax;
    if (!GLTF::ComputePrimitiveBoundingBox(PositionData, BBMin, BBMax))
        return {};

    std::shared_ptr<MeshSourceDataOwner>                  pOwner = std::make_shared<MeshSourceDataOwner>(pDocument);
    std::vector<RadientMeshVertexSource::SourceAttribute> SourceAttributes;
    SourceAttributes.reserve(GLTF::DefaultVertexAttributes.size());

    const Uint32 VertexCount = static_cast<Uint32>(PositionData.Count);

    for (size_t AttribIndex = 0; AttribIndex < GLTF::DefaultVertexAttributes.size(); ++AttribIndex)
    {
        const GLTF::VertexAttributeDesc& DstAttrib = GLTF::DefaultVertexAttributes[AttribIndex];
        const int*                       pAccessor = GltfPrimitive.GetAttribute(DstAttrib.Name);
        if (pAccessor == nullptr)
            continue;

        const auto GltfData = GLTF::GetGltfDataInfo(GltfModel, *pAccessor);
        if (GltfData.pData == nullptr ||
            GltfData.ByteStride <= 0 ||
            static_cast<Uint32>(GltfData.Count) != VertexCount)
        {
            return {};
        }

        RadientMeshVertexSource::SourceAttribute& SrcAttrib = SourceAttributes.emplace_back();

        SrcAttrib.Name          = DstAttrib.Name;
        SrcAttrib.Type          = GltfData.Accessor.GetComponentType();
        SrcAttrib.NumComponents = static_cast<Uint8>(GltfData.Accessor.GetNumComponents());
        SrcAttrib.IsNormalized  = GltfData.Accessor.IsNormalized();
        SrcAttrib.pData         = GltfData.pData;
        SrcAttrib.Stride        = static_cast<Uint32>(GltfData.ByteStride);
    }

    if (SourceAttributes.empty())
        return {};

    RadientMeshVertexSource::CreateInfo VertexCI;
    VertexCI.pAttributes      = SourceAttributes.data();
    VertexCI.AttributeCount   = static_cast<Uint32>(SourceAttributes.size());
    VertexCI.VertexCount      = VertexCount;
    VertexCI.pSourceDataOwner = pOwner;

    std::unique_ptr<RadientMeshVertexSource> pSource = std::make_unique<RadientMeshVertexSource>(VertexCI);
    if (pSource == nullptr || pSource->GetStatus() != RADIENT_STATUS_OK)
        return {};

    MeshVertexSourceResult Result;
    Result.Status  = RADIENT_STATUS_OK;
    Result.pSource = std::move(pSource);
    Result.BBMin   = BBMin;
    Result.BBMax   = BBMax;
    return Result;
}

MeshIndexSourceResult CreateMeshIndexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                            const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                            const std::shared_ptr<const GLTF::Document>& pDocument,
                                            Uint32                                       VertexCount)
{
    if (pDocument == nullptr)
    {
        MeshIndexSourceResult Result;
        Result.Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return Result;
    }

    std::shared_ptr<MeshSourceDataOwner> pOwner = std::make_shared<MeshSourceDataOwner>(pDocument);
    RadientMeshIndexSource::CreateInfo   IndexCI;
    Uint32                               IndexCount    = 0;
    const int                            IndexAccessor = GltfPrimitive.GetIndicesId();
    if (IndexAccessor >= 0)
    {
        const auto GltfIndexData = GLTF::GetGltfDataInfo(GltfModel, IndexAccessor);
        if (GltfIndexData.pData == nullptr ||
            GltfIndexData.ByteStride <= 0)
        {
            return {};
        }

        const VALUE_TYPE IndexType       = GltfIndexData.Accessor.GetComponentType();
        const Uint32     IndexValueSize  = GetValueSize(IndexType);
        const Uint32     IndexByteStride = static_cast<Uint32>(GltfIndexData.ByteStride);
        if (!RadientMeshIndexSource::IsSupportedIndexType(IndexType) ||
            IndexValueSize == 0 ||
            IndexByteStride != IndexValueSize)
        {
            return {};
        }

        IndexCI.pData = GltfIndexData.pData;
        IndexCI.Type  = IndexType;
        IndexCount    = static_cast<Uint32>(GltfIndexData.Count);
    }
    else
    {
        pOwner->GeneratedIndices.resize(VertexCount);
        for (Uint32 Index = 0; Index < VertexCount; ++Index)
            pOwner->GeneratedIndices[Index] = Index;

        IndexCI.pData = pOwner->GeneratedIndices.data();
        IndexCI.Type  = VT_UINT32;
        IndexCount    = VertexCount;
    }

    if (IndexCount == 0)
        return {};

    IndexCI.IndexCount       = IndexCount;
    IndexCI.pSourceDataOwner = pOwner;

    std::unique_ptr<RadientMeshIndexSource> pSource = std::make_unique<RadientMeshIndexSource>(IndexCI);
    if (pSource == nullptr || pSource->GetStatus() != RADIENT_STATUS_OK)
        return {};

    MeshIndexSourceResult Result;
    Result.Status  = RADIENT_STATUS_OK;
    Result.pSource = std::move(pSource);
    return Result;
}

RADIENT_STATUS ExtractSceneGraph(const GLTF::Model&               GLTFModel,
                                 RadientImport::ImportedDocument& Scene,
                                 IRadientAssetManager*            pAssetManager)
{
    RADIENT_STATUS Status = ExtractSkins(GLTFModel, pAssetManager, Scene);
    if (RADIENT_FAILED(Status))
        return Status;

    Scene.DefaultSceneId = GetDefaultSceneIndex(GLTFModel);

    Scene.Nodes.clear();
    Scene.Nodes.resize(GLTFModel.Nodes.size());
    for (const GLTF::Node& SrcNode : GLTFModel.Nodes)
    {
        if (SrcNode.Index < 0 || static_cast<size_t>(SrcNode.Index) >= Scene.Nodes.size())
            return RADIENT_STATUS_INVALID_DATA;

        RadientImport::ImportedNode& DstNode = Scene.Nodes[static_cast<size_t>(SrcNode.Index)];
        DstNode.Name                         = SrcNode.Name;
        DstNode.Transform                    = ToRadientTransform(SrcNode);

        if (SrcNode.pMesh != nullptr)
        {
            DstNode.pMesh = GetRadientMeshAsset(*SrcNode.pMesh);
            if (DstNode.pMesh == nullptr)
                return RADIENT_STATUS_INVALID_DATA;
        }

        if (SrcNode.pSkin != nullptr)
        {
            if (SrcNode.pMesh == nullptr)
            {
                LOG_ERROR_MESSAGE("GLTF node ", SrcNode.Index, " references a skin without a mesh");
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstNode.SkinIndex = FindSkinIndex(GLTFModel, SrcNode.pSkin);
            if (DstNode.SkinIndex == RadientImport::InvalidImportedSkinIndex)
            {
                LOG_ERROR_MESSAGE("GLTF node ", SrcNode.Index, " references an invalid skin");
                return RADIENT_STATUS_INVALID_DATA;
            }
        }

        if (SrcNode.pCamera != nullptr)
        {
            DstNode.Camera = ToRadientCamera(*SrcNode.pCamera);
        }

        if (SrcNode.pLight != nullptr)
        {
            DstNode.Light = ToRadientLight(*SrcNode.pLight);
        }

        DstNode.Children.reserve(SrcNode.Children.size());
        for (const GLTF::Node* pChild : SrcNode.Children)
        {
            if (pChild == nullptr ||
                pChild->Index < 0 ||
                static_cast<size_t>(pChild->Index) >= Scene.Nodes.size())
            {
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstNode.Children.push_back(static_cast<Uint32>(pChild->Index));
        }
    }

    Scene.Scenes.clear();
    Scene.Scenes.resize(GLTFModel.Scenes.size());
    for (size_t SceneIndex = 0; SceneIndex < GLTFModel.Scenes.size(); ++SceneIndex)
    {
        const GLTF::Scene&            SrcScene = GLTFModel.Scenes[SceneIndex];
        RadientImport::ImportedScene& DstScene = Scene.Scenes[SceneIndex];
        DstScene.Name                          = SrcScene.Name;
        DstScene.RootNodes.reserve(SrcScene.RootNodes.size());

        for (const GLTF::Node* pRootNode : SrcScene.RootNodes)
        {
            if (pRootNode == nullptr ||
                pRootNode->Index < 0 ||
                static_cast<size_t>(pRootNode->Index) >= Scene.Nodes.size())
            {
                return RADIENT_STATUS_INVALID_DATA;
            }

            DstScene.RootNodes.push_back(static_cast<Uint32>(pRootNode->Index));
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS InstantiateSceneGraph(const RadientImport::ImportedDocument& Scene,
                                     Uint32                                 SceneIndex,
                                     IRadientSceneWriter&                   Writer,
                                     RadientEntityID                        RootEntity)
{
    Uint32         ResolvedSceneIndex = 0;
    RADIENT_STATUS Status             = ResolveSceneIndex(Scene, SceneIndex, ResolvedSceneIndex);
    if (RADIENT_FAILED(Status))
        return Status;

    if (ResolvedSceneIndex < Scene.Scenes.size())
    {
        std::vector<RefCntAutoPtr<IRadientSkeletonPose>> SkinPoses(Scene.Skins.size());
        for (Uint32 NodeIndex : Scene.Scenes[ResolvedSceneIndex].RootNodes)
        {
            Status = CreateNode(Writer, Scene, NodeIndex, RootEntity, SkinPoses);
            if (RADIENT_FAILED(Status))
                return Status;
        }
    }

    return RADIENT_STATUS_OK;
}

} // namespace RadientGLTFConverter

} // namespace Diligent
