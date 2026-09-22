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

#include "Import/RadientSceneInstantiation.hpp"

#include "Math/RadientMath.hpp"
#include "RadientAnimation.h"
#include "RadientMorphTargets.h"
#include "RadientSceneWriter.h"
#include "RadientSkinning.h"

#include "Errors.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

namespace
{

using SkinEntityLists  = std::vector<std::vector<RadientEntityID>>;
using NodeEntityLists  = std::vector<std::vector<RadientEntityID>>;
using LightEntityLists = std::unordered_map<Uint32, std::vector<RadientEntityID>>;

struct MorphAnimationDestinationInstance
{
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    RadientEntityID                           Entity = InvalidRadientEntityID;
};

using MorphAnimationDestinationLists = std::vector<std::vector<MorphAnimationDestinationInstance>>;

RADIENT_STATUS CreateNode(IRadientSceneWriter&                              Writer,
                          const RadientImport::ImportedDocument&            Scene,
                          Uint32                                            NodeIndex,
                          RadientEntityID                                   Parent,
                          const RadientMatrix4x4&                           ParentDocumentMatrix,
                          std::vector<RefCntAutoPtr<IRadientSkeletonPose>>& SkinPoses,
                          NodeEntityLists*                                  pNodeEntities,
                          LightEntityLists*                                 pLightEntities,
                          SkinEntityLists*                                  pSkinEntities,
                          MorphAnimationDestinationLists*                   pMorphAnimationDestinations)
{
    if (NodeIndex >= Scene.Nodes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientImport::ImportedNode& Node               = Scene.Nodes[NodeIndex];
    const std::string                  FallbackName       = std::string{"GLTF Node "} + std::to_string(NodeIndex);
    const RadientMatrix4x4             NodeDocumentMatrix = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(Node.Transform), ParentDocumentMatrix);

    RadientEntityDesc NodeDesc{};
    NodeDesc.Name      = !Node.Name.empty() ? Node.Name.c_str() : FallbackName.c_str();
    NodeDesc.Parent    = Parent;
    NodeDesc.Flags     = Node.Visible ? RADIENT_ENTITY_FLAG_VISIBLE : RADIENT_ENTITY_FLAG_NONE;
    NodeDesc.Transform = Node.Transform;

    RadientEntityID NodeEntity = InvalidRadientEntityID;
    RADIENT_STATUS  Status     = Writer.CreateEntity(NodeDesc, NodeEntity);
    if (RADIENT_FAILED(Status))
        return Status;

    if (pNodeEntities != nullptr)
    {
        VERIFY_EXPR(NodeIndex < pNodeEntities->size());
        (*pNodeEntities)[NodeIndex].push_back(NodeEntity);
    }

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

        if (pLightEntities != nullptr &&
            Node.LightIndex != RadientImport::InvalidImportedLightIndex)
        {
            (*pLightEntities)[Node.LightIndex].push_back(NodeEntity);
        }
    }

    if (Node.pMesh != nullptr)
    {
        RadientMeshComponent Mesh{};
        Mesh.pMesh = Node.pMesh;
        Status     = Writer.SetMesh(NodeEntity, Mesh);
        if (RADIENT_FAILED(Status))
            return Status;

        const Uint32 MorphTargetCount = Node.pMesh->GetDesc().MorphTargetCount;
        if (MorphTargetCount != 0)
        {
            RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
            Status = Node.pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty());
            if (RADIENT_FAILED(Status) || pWeights == nullptr)
                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;

            if (!Node.MorphWeights.empty())
            {
                if (Node.MorphWeights.size() != MorphTargetCount)
                {
                    LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' provides ", Node.MorphWeights.size(),
                                        " morph weights for a mesh with ", MorphTargetCount,
                                        " targets; applying the available weights");
                }

                const Uint32 WeightCount = static_cast<Uint32>(std::min(
                    Node.MorphWeights.size(),
                    size_t{MorphTargetCount}));
                if (WeightCount != 0)
                {
                    Status = pWeights->SetWeights(0, WeightCount, Node.MorphWeights.data());
                    if (RADIENT_FAILED(Status))
                        return Status;
                }
            }

            Status = Writer.SetMorph(NodeEntity, RadientMorphComponent{pWeights});
            if (RADIENT_FAILED(Status))
                return Status;

            if (pMorphAnimationDestinations != nullptr)
            {
                VERIFY_EXPR(NodeIndex < pMorphAnimationDestinations->size());
                MorphAnimationDestinationInstance& Instance =
                    (*pMorphAnimationDestinations)[NodeIndex].emplace_back();
                Instance.pWeights = pWeights;
                Instance.Entity   = NodeEntity;
            }
        }
        else if (!Node.MorphWeights.empty())
        {
            LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name,
                                "' provides morph weights for a mesh without imported morph targets; ignoring the weights");
        }

        if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
        {
            if (Node.SkinIndex >= Scene.Skins.size() || Scene.Skins[Node.SkinIndex] == nullptr)
            {
                LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references unavailable skin ",
                                    Node.SkinIndex, "; rendering the mesh without skinning");
            }
            else
            {
                IRadientSkinAsset* const     pSkin     = Scene.Skins[Node.SkinIndex];
                IRadientSkeletonAsset* const pSkeleton = pSkin->GetDesc().pSkeleton;
                if (pSkeleton == nullptr)
                {
                    LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references skin ",
                                        Node.SkinIndex, " without a skeleton; rendering the mesh without skinning");
                }
                else
                {
                    RadientMatrix4x4 SkeletonToMeshTransform;
                    const bool       NodeDocumentMatrixIsIdentity = (NodeDocumentMatrix == SkeletonToMeshTransform);
                    if (!NodeDocumentMatrixIsIdentity &&
                        !RadientMath::TryInverseMatrix(NodeDocumentMatrix, SkeletonToMeshTransform))
                    {
                        LOG_WARNING_MESSAGE("Imported skinned node '", NodeDesc.Name,
                                            "' has a non-invertible document transform; rendering the mesh without skinning");
                    }
                    else
                    {
                        RefCntAutoPtr<IRadientSkeletonPose>& pPose = SkinPoses[Node.SkinIndex];
                        if (pPose == nullptr)
                        {
                            Status = pSkeleton->CreatePose(pPose.GetAddressOfEmpty());
                            if (RADIENT_FAILED(Status) || pPose == nullptr)
                                return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_FAILED;
                        }

                        RadientSkinComponent SkinComponent{};
                        SkinComponent.pSkin                   = pSkin;
                        SkinComponent.pPose                   = pPose;
                        SkinComponent.SkeletonToMeshTransform = SkeletonToMeshTransform;
                        Status                                = Writer.SetSkin(NodeEntity, SkinComponent);
                        if (RADIENT_FAILED(Status))
                            return Status;

                        if (pSkinEntities != nullptr)
                        {
                            VERIFY_EXPR(Node.SkinIndex < pSkinEntities->size());
                            (*pSkinEntities)[Node.SkinIndex].push_back(NodeEntity);
                        }
                    }
                }
            }
        }

        RadientMeshRendererComponent Renderer{};
        Status = Writer.SetMeshRenderer(NodeEntity, Renderer);
        if (RADIENT_FAILED(Status))
            return Status;
    }
    else if (Node.SkinIndex != RadientImport::InvalidImportedSkinIndex)
    {
        LOG_WARNING_MESSAGE("Imported node '", NodeDesc.Name, "' references skin ", Node.SkinIndex,
                            " but has no mesh; ignoring the skin reference");
    }

    for (Uint32 ChildIndex : Node.Children)
    {
        Status = CreateNode(Writer, Scene, ChildIndex, NodeEntity, NodeDocumentMatrix,
                            SkinPoses, pNodeEntities, pLightEntities, pSkinEntities,
                            pMorphAnimationDestinations);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    return RADIENT_STATUS_OK;
}

void RegisterAnimationDestination(
    IRadientAnimationClipAsset&            Clip,
    const RadientAnimationDestinationDesc& DestinationDesc,
    const RadientEntityID*                 pEntities,
    Uint32                                 EntityCount,
    const char*                            DestinationDescription,
    IRadientAnimationRegistry&             Registry)
{
    const RadientAnimationClipDesc& ClipDesc = Clip.GetDesc();

    RadientAnimationBindingDesc BindingDesc{};
    BindingDesc.pDestinations    = &DestinationDesc;
    BindingDesc.DestinationCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    RADIENT_STATUS                          Status = Clip.CreateBinding(BindingDesc, pBinding.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK || pBinding == nullptr)
    {
        LOG_WARNING_MESSAGE("Skipping ", DestinationDescription, " binding for imported animation clip '",
                            ClipDesc.Name, "' because it could not be created");
        return;
    }

    Status = Registry.AddAnimationBinding(pBinding, pEntities, EntityCount);
    if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE)
        return;

    LOG_WARNING_MESSAGE("Skipping ", DestinationDescription, " binding for imported animation clip '",
                        ClipDesc.Name, "' because it could not be registered");

    // AddAnimationBinding implementations may have changed part of the
    // registry before reporting a failure. Remove this binding as a
    // best-effort cleanup without disturbing earlier bindings.
    const RADIENT_STATUS CleanupStatus = Registry.RemoveAnimationBinding(
        pBinding, pEntities, EntityCount);
    if (CleanupStatus != RADIENT_STATUS_OK && CleanupStatus != RADIENT_STATUS_NO_CHANGE)
    {
        LOG_WARNING_MESSAGE("Unable to clean up the failed ", DestinationDescription,
                            " animation binding for clip '", ClipDesc.Name, "'");
    }
}

void RegisterSceneAnimations(
    const RadientImport::ImportedDocument&                  Scene,
    const std::vector<RefCntAutoPtr<IRadientSkeletonPose>>& SkinPoses,
    IRadientAnimationDestination*                           pSceneDestination,
    const NodeEntityLists&                                  NodeEntities,
    RadientEntityID                                         SceneRootEntity,
    const LightEntityLists&                                 LightEntities,
    const SkinEntityLists&                                  SkinEntities,
    const MorphAnimationDestinationLists&                   MorphAnimationDestinations,
    IRadientAnimationRegistry&                              Registry)
{
    for (const RadientImport::ImportedAnimation& ImportedAnimation : Scene.Animations)
    {
        if (ImportedAnimation.pClip == nullptr)
        {
            LOG_WARNING_MESSAGE("Skipping a null imported animation clip");
            continue;
        }

        const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
        for (const RadientImport::ImportedAnimationSkinMapping& SkinMapping : ImportedAnimation.SkinMappings)
        {
            if (SkinMapping.SkinIndex >= SkinPoses.size() || SkinMapping.SkinIndex >= SkinEntities.size())
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because its skin mapping is invalid");
                continue;
            }

            IRadientSkeletonPose* const         pPose    = SkinPoses[SkinMapping.SkinIndex];
            const std::vector<RadientEntityID>& Entities = SkinEntities[SkinMapping.SkinIndex];
            if (pPose == nullptr || Entities.empty())
                continue;

            RefCntAutoPtr<IRadientAnimationDestination> pDestination;
            pPose->QueryInterface(IID_RadientAnimationDestination, pDestination.GetAddressOfEmpty());
            if (pDestination == nullptr)
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because the skeleton pose does not expose an animation destination");
                continue;
            }

            RadientAnimationDestinationDesc DestinationDesc{};
            DestinationDesc.pDestination = pDestination;
            DestinationDesc.pMappings    = SkinMapping.JointMappings.data();
            DestinationDesc.MappingCount = static_cast<Uint32>(SkinMapping.JointMappings.size());

            RegisterAnimationDestination(
                *ImportedAnimation.pClip,
                DestinationDesc,
                Entities.data(),
                static_cast<Uint32>(Entities.size()),
                "an instantiated skeleton pose",
                Registry);
        }

        for (Uint32 ClipTargetIndex = 0; ClipTargetIndex < ClipDesc.TargetCount; ++ClipTargetIndex)
        {
            const RadientAnimationTargetDesc& Target = ClipDesc.pTargets[ClipTargetIndex];
            if (Target.Schema != RadientMorphWeightsAnimationSchemaID)
                continue;

            if (Target.Object >= MorphAnimationDestinations.size())
            {
                LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                    "' because a morph target references an invalid source node");
                continue;
            }

            const Uint32 NodeIndex = static_cast<Uint32>(Target.Object);
            for (const MorphAnimationDestinationInstance& Instance :
                 MorphAnimationDestinations[NodeIndex])
            {
                if (Instance.pWeights == nullptr || Instance.Entity == InvalidRadientEntityID)
                    continue;

                RefCntAutoPtr<IRadientAnimationDestination> pDestination;
                Instance.pWeights->QueryInterface(
                    IID_RadientAnimationDestination, pDestination.GetAddressOfEmpty());
                if (pDestination == nullptr)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' because the morph weights do not expose an animation destination");
                    continue;
                }

                const RadientAnimationDestinationMappingDesc WeightMapping{
                    ClipTargetIndex,
                    0,
                };
                RadientAnimationDestinationDesc DestinationDesc{};
                DestinationDesc.pDestination = pDestination;
                DestinationDesc.pMappings    = &WeightMapping;
                DestinationDesc.MappingCount = 1;

                RegisterAnimationDestination(
                    *ImportedAnimation.pClip,
                    DestinationDesc,
                    &Instance.Entity,
                    1,
                    "instantiated morph weights",
                    Registry);
            }
        }

        std::vector<bool> HasScenePropertyChannel(ClipDesc.TargetCount, false);
        for (Uint32 ChannelIndex = 0; ChannelIndex < ClipDesc.ChannelCount; ++ChannelIndex)
        {
            const RadientAnimationChannelDesc& Channel = ClipDesc.pChannels[ChannelIndex];
            const RadientAnimationTargetDesc&  Target  = ClipDesc.pTargets[Channel.TargetIndex];
            if (Target.Schema == RadientLightAnimationSchemaID)
            {
                HasScenePropertyChannel[Channel.TargetIndex] = true;
            }
            else if (Target.Schema == RadientNodeAnimationSchemaID)
            {
                switch (Channel.Property)
                {
                    case RadientNodeTranslationProperty:
                    case RadientNodeRotationProperty:
                    case RadientNodeScaleProperty:
                    case RadientNodeVisibilityProperty:
                        HasScenePropertyChannel[Channel.TargetIndex] = true;
                        break;

                    default:
                        break;
                }
            }
        }

        std::vector<RadientAnimationDestinationMappingDesc> NodeMappings;
        std::vector<RadientAnimationDestinationMappingDesc> LightMappings;
        NodeMappings.reserve(ClipDesc.TargetCount);
        LightMappings.reserve(ClipDesc.TargetCount);
        bool       NodeMappingOverflow  = false;
        bool       LightMappingOverflow = false;
        const auto AppendEntityMappings =
            [](const std::vector<RadientEntityID>&                  Entities,
               Uint32                                               ClipTargetIndex,
               std::vector<RadientAnimationDestinationMappingDesc>& Mappings,
               bool&                                                MappingOverflow) {
                if (MappingOverflow)
                    return;

                for (RadientEntityID Entity : Entities)
                {
                    if (Entity == InvalidRadientEntityID)
                        continue;
                    if (Mappings.size() >= std::numeric_limits<Uint32>::max())
                    {
                        MappingOverflow = true;
                        return;
                    }

                    RadientAnimationDestinationMappingDesc& Mapping = Mappings.emplace_back();
                    Mapping.ClipTargetIndex                         = ClipTargetIndex;
                    Mapping.DestinationElement                      = Entity;
                }
            };

        for (Uint32 ClipTargetIndex = 0; ClipTargetIndex < ClipDesc.TargetCount; ++ClipTargetIndex)
        {
            const RadientAnimationTargetDesc& Target = ClipDesc.pTargets[ClipTargetIndex];
            if (!HasScenePropertyChannel[ClipTargetIndex])
                continue;

            if (Target.Schema == RadientNodeAnimationSchemaID)
            {
                if (Target.Object >= NodeEntities.size())
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' target because it references an invalid source node");
                    continue;
                }

                AppendEntityMappings(
                    NodeEntities[static_cast<size_t>(Target.Object)],
                    ClipTargetIndex,
                    NodeMappings,
                    NodeMappingOverflow);
            }
            else if (Target.Schema == RadientLightAnimationSchemaID)
            {
                if (Target.Object >= std::numeric_limits<Uint32>::max())
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' target because it references an invalid source light");
                    continue;
                }

                const auto LightIt = LightEntities.find(static_cast<Uint32>(Target.Object));
                if (LightIt == LightEntities.end())
                    continue;

                AppendEntityMappings(
                    LightIt->second,
                    ClipTargetIndex,
                    LightMappings,
                    LightMappingOverflow);
            }
        }

        const auto RegisterSceneMappings =
            [&](const std::vector<RadientAnimationDestinationMappingDesc>& Mappings,
                bool                                                       MappingOverflow,
                const char*                                                DestinationDescription) {
                if (MappingOverflow)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' ", DestinationDescription,
                                        " binding because it contains too many mappings");
                    return;
                }
                if (Mappings.empty())
                    return;
                if (pSceneDestination == nullptr)
                {
                    LOG_WARNING_MESSAGE("Skipping imported animation clip '", ClipDesc.Name,
                                        "' ", DestinationDescription,
                                        " binding because the scene writer does not expose an animation destination");
                    return;
                }

                RadientAnimationDestinationDesc DestinationDesc{};
                DestinationDesc.pDestination = pSceneDestination;
                DestinationDesc.pMappings    = Mappings.data();
                DestinationDesc.MappingCount = static_cast<Uint32>(Mappings.size());

                RegisterAnimationDestination(
                    *ImportedAnimation.pClip,
                    DestinationDesc,
                    &SceneRootEntity,
                    1,
                    DestinationDescription,
                    Registry);
            };

        // Keep independently mutable component families in separate bindings. A
        // deleted light can then invalidate its binding without suppressing node
        // transforms or visibility from the same clip.
        RegisterSceneMappings(NodeMappings, NodeMappingOverflow, "instantiated node properties");
        RegisterSceneMappings(LightMappings, LightMappingOverflow, "instantiated light properties");
    }
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

namespace RadientImport
{

RADIENT_STATUS InstantiateSceneGraph(const RadientImport::ImportedDocument& Scene,
                                     Uint32                                 SceneIndex,
                                     IRadientSceneWriter&                   Writer,
                                     RadientEntityID                        RootEntity,
                                     IRadientAnimationRegistry*             pAnimationRegistry)
{
    Uint32         ResolvedSceneIndex = 0;
    RADIENT_STATUS Status             = ResolveSceneIndex(Scene, SceneIndex, ResolvedSceneIndex);
    if (RADIENT_FAILED(Status))
        return Status;

    if (ResolvedSceneIndex < Scene.Scenes.size())
    {
        std::vector<RefCntAutoPtr<IRadientSkeletonPose>> SkinPoses(Scene.Skins.size());
        NodeEntityLists                                  NodeEntities;
        LightEntityLists                                 LightEntities;
        SkinEntityLists                                  SkinEntities;
        MorphAnimationDestinationLists                   MorphAnimationDestinations;
        RefCntAutoPtr<IRadientAnimationDestination>      pSceneAnimationDestination;
        if (pAnimationRegistry != nullptr)
        {
            NodeEntities.resize(Scene.Nodes.size());
            SkinEntities.resize(Scene.Skins.size());
            MorphAnimationDestinations.resize(Scene.Nodes.size());
            Writer.QueryInterface(
                IID_RadientAnimationDestination,
                pSceneAnimationDestination.GetAddressOfEmpty());
        }

        for (Uint32 NodeIndex : Scene.Scenes[ResolvedSceneIndex].RootNodes)
        {
            Status = CreateNode(Writer, Scene, NodeIndex, RootEntity, RadientMatrix4x4{}, SkinPoses,
                                pAnimationRegistry != nullptr ? &NodeEntities : nullptr,
                                pAnimationRegistry != nullptr ? &LightEntities : nullptr,
                                pAnimationRegistry != nullptr ? &SkinEntities : nullptr,
                                pAnimationRegistry != nullptr ? &MorphAnimationDestinations : nullptr);
            if (RADIENT_FAILED(Status))
                return Status;
        }

        if (pAnimationRegistry != nullptr)
        {
            RegisterSceneAnimations(
                Scene, SkinPoses, pSceneAnimationDestination, NodeEntities, RootEntity,
                LightEntities, SkinEntities, MorphAnimationDestinations, *pAnimationRegistry);
        }
    }

    return RADIENT_STATUS_OK;
}

} // namespace RadientImport

} // namespace Diligent
