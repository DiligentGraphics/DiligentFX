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

#include "Radient/interface/RadientAnimation.h"

void RadientAnimation_C_UseTypes(void)
{
    RadientAnimationSchemaID               Schema             = {0};
    RadientAnimationPropertyID             Property           = 0;
    RadientAnimationObjectID               Object             = 0;
    RadientAnimationDestinationElement     DestinationElement = 0;
    RADIENT_ANIMATION_VALUE_SEMANTIC       Semantic           = RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN;
    RadientAnimationValueDesc              Value              = {0};
    RadientAnimationSamplerDesc            Sampler            = {0};
    RadientAnimationTargetDesc             ClipTarget         = {0};
    RadientAnimationChannelDesc            Channel            = {0};
    RadientAnimationClipDesc               Clip               = {0};
    RadientAnimationPropertyBindingDesc    PropertyBinding    = {0};
    RadientAnimationResolvedPropertyDesc   ResolvedProperty   = {0};
    RadientAnimationPropertyUpdateDesc     PropertyUpdate     = {0};
    RadientAnimationApplyInfo              ApplyInfo          = {0};
    RadientAnimationDestinationMappingDesc DestinationMapping = {0};
    RadientAnimationDestinationDesc        Destination        = {0};
    RadientAnimationBindingDesc            Binding            = {0};
    RadientAnimationEvaluateInfo           EvaluateInfo       = {0};
    RadientAnimationTarget                 Target             = {0};
    RadientAnimationRegistryEntry          Entry              = {0};
    RadientAnimationRegistryState          State              = {0};

    Schema                                = InvalidRadientAnimationSchemaID;
    Schema                                = RadientNodeAnimationSchemaID;
    Property                              = RadientNodeTranslationProperty;
    Property                              = RadientNodeRotationProperty;
    Property                              = RadientNodeScaleProperty;
    Schema                                = RadientMorphWeightsAnimationSchemaID;
    Property                              = RadientMorphWeightsProperty;
    Object                                = InvalidRadientAnimationObject;
    DestinationElement                    = InvalidRadientAnimationDestinationElement;
    Semantic                              = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
    Semantic                              = RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION;
    Value.Type                            = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
    Value.ArraySize                       = 1;
    Sampler.Value                         = Value;
    Sampler.Interpolation                 = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Sampler.ValueDataSize                 = 0;
    ClipTarget.Schema                     = Schema;
    ClipTarget.Object                     = Object;
    ClipTarget.Name                       = "Target";
    Channel.TargetIndex                   = InvalidRadientAnimationTargetIndex;
    Channel.Property                      = InvalidRadientAnimationPropertyID;
    Channel.FirstArrayElement             = 0;
    Channel.SamplerIndex                  = InvalidRadientAnimationSamplerIndex;
    Clip.Name                             = "Clip";
    PropertyBinding.Schema                = Schema;
    PropertyBinding.DestinationElement    = DestinationElement;
    PropertyBinding.Property              = Property;
    PropertyBinding.FirstArrayElement     = 0;
    PropertyBinding.Value                 = Value;
    ResolvedProperty.Semantic             = Semantic;
    PropertyUpdate.pValue                 = 0;
    PropertyUpdate.ValueDataSize          = 0;
    ApplyInfo.pUpdates                    = &PropertyUpdate;
    ApplyInfo.UpdateCount                 = 1;
    ApplyInfo.UpdateDerivedState          = True;
    DestinationMapping.ClipTargetIndex    = InvalidRadientAnimationTargetIndex;
    DestinationMapping.DestinationElement = DestinationElement;
    Destination.pDestination              = 0;
    Destination.pMappings                 = &DestinationMapping;
    Destination.MappingCount              = 1;
    Binding.pDestinations                 = &Destination;
    Binding.DestinationCount              = 1;
    EvaluateInfo.Time                     = 0.0;
    EvaluateInfo.UpdateDerivedState       = True;

    (void)Schema;
    (void)Property;
    (void)Object;
    (void)DestinationElement;
    (void)Semantic;
    (void)Sampler;
    (void)ClipTarget;
    (void)Channel;
    (void)Clip;
    (void)PropertyBinding;
    (void)ResolvedProperty;
    (void)ApplyInfo;
    (void)Binding;
    (void)EvaluateInfo;
    (void)Target;
    (void)Entry;
    (void)State;
}

void RadientAnimation_C_TestDestinationMacros(IRadientAnimationDestination* pDestination)
{
    RadientAnimationPropertyBindingDesc  Property            = {0};
    RadientAnimationResolvedPropertyDesc ResolvedProperty    = {0};
    IRadientAnimationDestinationBinding* pDestinationBinding = 0;
    RADIENT_STATUS                       Status              = RADIENT_STATUS_OK;

    Status = IRadientAnimationDestination_CreateBinding(pDestination, &Property, 1, &ResolvedProperty, &pDestinationBinding);

    (void)Status;
}

void RadientAnimation_C_TestDestinationBindingMacros(IRadientAnimationDestinationBinding* pDestinationBinding)
{
    RadientAnimationPropertyUpdateDesc PropertyUpdate = {0};
    RadientAnimationApplyInfo          ApplyInfo      = {0};
    RADIENT_STATUS                     Status         = RADIENT_STATUS_OK;

    ApplyInfo.pUpdates    = &PropertyUpdate;
    ApplyInfo.UpdateCount = 1;
    Status                = IRadientAnimationDestinationBinding_ApplyProperties(pDestinationBinding, &ApplyInfo);

    (void)Status;
}

void RadientAnimation_C_TestClipMacros(IRadientAnimationClipAsset* pClip)
{
    RadientAnimationBindingDesc BindingDesc = {0};
    IRadientAnimationBinding*   pBinding    = 0;
    RADIENT_STATUS              Status      = RADIENT_STATUS_OK;

    (void)IRadientAnimationClipAsset_GetDesc(pClip);
    Status = IRadientAnimationClipAsset_CreateBinding(pClip, &BindingDesc, &pBinding);

    (void)Status;
}

void RadientAnimation_C_TestBindingMacros(IRadientAnimationBinding* pBinding)
{
    RadientAnimationEvaluateInfo EvaluateInfo = {0};
    RADIENT_STATUS               Status       = RADIENT_STATUS_OK;

    (void)IRadientAnimationBinding_GetClip(pBinding);
    Status = IRadientAnimationBinding_Evaluate(pBinding, &EvaluateInfo);

    (void)Status;
}

void RadientAnimation_C_TestMacros(IRadientAnimationRegistry*      pRegistry,
                                   IRadientSkeletonAnimationAsset* pAnimation)
{
    RadientEntityID Entities[1] = {0};
    RADIENT_STATUS  Status      = RADIENT_STATUS_OK;

    (void)IRadientAnimationRegistry_GetScene(pRegistry);
    Status = IRadientAnimationRegistry_AddAnimatedEntities(pRegistry, pAnimation, Entities, 1);
    Status = IRadientAnimationRegistry_RemoveAnimatedEntities(pRegistry, pAnimation, Entities, 1);
    Status = IRadientAnimationRegistry_RemoveEntity(pRegistry, Entities[0]);
    Status = IRadientAnimationRegistry_RemoveAnimation(pRegistry, pAnimation);
    (void)IRadientAnimationRegistry_GetState(pRegistry);

    (void)Status;
}
