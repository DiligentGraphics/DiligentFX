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
    RadientAnimationSchemaID      Schema     = {0};
    RadientAnimationValueDesc     Value      = {0};
    RadientAnimationSamplerDesc   Sampler    = {0};
    RadientAnimationTargetDesc    ClipTarget = {0};
    RadientAnimationChannelDesc   Channel    = {0};
    RadientAnimationClipDesc      Clip       = {0};
    RadientAnimationTarget        Target     = {0};
    RadientAnimationRegistryEntry Entry      = {0};
    RadientAnimationRegistryState State      = {0};

    Schema                    = InvalidRadientAnimationSchemaID;
    Value.Type                = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
    Value.ArraySize           = 1;
    Sampler.Value             = Value;
    Sampler.Interpolation     = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Sampler.ValueDataSize     = 0;
    ClipTarget.Schema         = Schema;
    ClipTarget.Object         = InvalidRadientAnimationObject;
    ClipTarget.Name           = "Target";
    Channel.TargetIndex       = InvalidRadientAnimationTargetIndex;
    Channel.Property          = InvalidRadientAnimationPropertyID;
    Channel.FirstArrayElement = 0;
    Channel.SamplerIndex      = InvalidRadientAnimationSamplerIndex;
    Clip.Name                 = "Clip";

    (void)Sampler;
    (void)ClipTarget;
    (void)Channel;
    (void)Clip;
    (void)Target;
    (void)Entry;
    (void)State;
}

void RadientAnimation_C_TestClipMacros(IRadientAnimationClipAsset* pClip)
{
    (void)IRadientAnimationClipAsset_GetDesc(pClip);
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
