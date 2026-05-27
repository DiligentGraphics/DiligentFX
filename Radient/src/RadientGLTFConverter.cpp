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

#include "RadientGLTFConverter.hpp"

#include "RadientMath.hpp"
#include "RadientSceneWriter.h"

#include "GLTFLoader.hpp"
#include "Errors.hpp"

#include <cmath>
#include <string>

namespace Diligent
{

namespace
{

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
        Result.HorizontalAperture = Camera.Orthographic.XMag;
        Result.VerticalAperture   = Camera.Orthographic.YMag;
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

RADIENT_STATUS CreateNode(IRadientSceneWriter& Writer,
                          const GLTF::Node&    Node,
                          RadientEntityID      Parent)
{
    const std::string FallbackName = std::string{"GLTF Node "} + std::to_string(Node.Index);

    RadientEntityDesc NodeDesc{};
    NodeDesc.Name      = !Node.Name.empty() ? Node.Name.c_str() : FallbackName.c_str();
    NodeDesc.Parent    = Parent;
    NodeDesc.Transform = ToRadientTransform(Node);

    RadientEntityID NodeEntity = InvalidRadientEntityID;
    RADIENT_STATUS  Status     = Writer.CreateEntity(NodeDesc, NodeEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (Node.pCamera != nullptr)
    {
        Status = Writer.SetCamera(NodeEntity, ToRadientCamera(*Node.pCamera));
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (Node.pLight != nullptr)
    {
        Status = Writer.SetLight(NodeEntity, ToRadientLight(*Node.pLight));
        if (RADIENT_FAILED(Status))
            return Status;
    }

    for (const GLTF::Node* pChild : Node.Children)
    {
        if (pChild == nullptr)
            continue;

        Status = CreateNode(Writer, *pChild, NodeEntity);
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

RADIENT_STATUS ResolveSceneIndex(const GLTF::Model&                Model,
                                 const RadientGLTFInstantiateInfo& InstantiateInfo,
                                 Uint32&                           SceneIndex)
{
    if (InstantiateInfo.SceneIndex == InvalidRadientGLTFSceneIndex)
    {
        SceneIndex = GetDefaultSceneIndex(Model);
        return RADIENT_STATUS_OK;
    }

    if (InstantiateInfo.SceneIndex >= Model.Scenes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    SceneIndex = InstantiateInfo.SceneIndex;
    return RADIENT_STATUS_OK;
}

} // namespace

namespace RadientGLTFConverter
{

RADIENT_STATUS InstantiateSceneGraph(const GLTF::Model&                GLTFModel,
                                     const RadientAssetReference&      Model,
                                     const RadientGLTFInstantiateInfo& InstantiateInfo,
                                     IRadientSceneWriter&              Writer,
                                     RadientEntityID&                  RootEntity)
{
    RootEntity = InvalidRadientEntityID;

    Uint32         SceneIndex = 0;
    RADIENT_STATUS Status     = ResolveSceneIndex(GLTFModel, InstantiateInfo, SceneIndex);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientEntityDesc RootDesc{};
    RootDesc.Name      = InstantiateInfo.Name != nullptr ? InstantiateInfo.Name : Model.URI;
    RootDesc.Parent    = InstantiateInfo.Parent;
    RootDesc.Flags     = InstantiateInfo.RootFlags;
    RootDesc.Transform = InstantiateInfo.RootTransform;

    Status = Writer.CreateEntity(RootDesc, RootEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (SceneIndex < GLTFModel.Scenes.size())
    {
        for (const GLTF::Node* pNode : GLTFModel.Scenes[SceneIndex].RootNodes)
        {
            if (pNode == nullptr)
                continue;

            Status = CreateNode(Writer, *pNode, RootEntity);
            if (RADIENT_FAILED(Status))
                return Status;
        }
    }

    // Temporary rendering slice: keep the full GLTF asset on the instance root
    // until GLTF meshes are imported as individual Radient mesh assets.
    RadientMeshComponent Mesh{};
    Mesh.Mesh = Model;
    Status    = Writer.SetMesh(RootEntity, Mesh);
    if (RADIENT_FAILED(Status))
        return Status;

    RadientMeshRendererComponent Renderer{};
    Status = Writer.SetMeshRenderer(RootEntity, Renderer);
    if (RADIENT_FAILED(Status))
        return Status;

    return RADIENT_STATUS_OK;
}

} // namespace RadientGLTFConverter

} // namespace Diligent
