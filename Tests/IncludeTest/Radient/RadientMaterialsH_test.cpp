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

#include "Radient/interface/RadientMaterials.h"

using namespace Diligent;

static_assert(RADIENT_MATERIAL_DOMAIN_SURFACE == 0, "Unexpected RADIENT_MATERIAL_DOMAIN_SURFACE value");
static_assert(RADIENT_MATERIAL_DOMAIN_POST_PROCESS == 1, "Unexpected RADIENT_MATERIAL_DOMAIN_POST_PROCESS value");
static_assert(RADIENT_MATERIAL_DOMAIN_COMPUTE == 2, "Unexpected RADIENT_MATERIAL_DOMAIN_COMPUTE value");

static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN == 0, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_BOOL == 1, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_BOOL value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_INT == 2, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_INT value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_INT2 == 3, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_INT2 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_INT3 == 4, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_INT3 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_INT4 == 5, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_INT4 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_UINT == 6, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_UINT value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_UINT2 == 7, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_UINT2 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_UINT3 == 8, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_UINT3 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_UINT4 == 9, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_UINT4 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT == 10, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2 == 11, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3 == 12, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4 == 13, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2 == 14, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3 == 15, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4 == 16, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4 value");
static_assert(RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE == 17, "Unexpected RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE value");

void RadientMaterials_CPP_UseTypes()
{
    RadientMaterialParameterHandle              Handle;
    RadientMaterialDefinitionDesc               Desc;
    RadientMaterialParameterDesc                Parameter;
    RadientStandardMaterialDefinitionCreateInfo StandardCI;

    Desc.pParameters    = &Parameter;
    Desc.ParameterCount = 1;

    StandardCI.Features = RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT |
        RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_SHEEN |
        RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_IRIDESCENCE;
    StandardCI.Textures = RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_ALL |
        RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_ALL |
        RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE_ALL;

    const bool IsValid = static_cast<bool>(Handle);
    const bool IsEqual = Handle == RadientMaterialParameterHandle{};

    (void)Desc;
    (void)Parameter;
    (void)StandardCI;
    (void)IsValid;
    (void)IsEqual;
}

void RadientMaterials_CPP_TestInterfaces(IRadientAssetManager*           pAssetManager,
                                         IRadientMaterialDefinition*     pDefinition,
                                         IRadientMaterialInstance*       pInstance,
                                         IRadientMaterialInstanceWriter* pWriter,
                                         IRadientTextureAsset*           pTexture)
{
    const RadientMaterialDefinitionDesc& Desc       = pDefinition->GetDesc();
    RADIENT_STATUS                       Status     = pDefinition->GetStatus();
    const Uint32                         ParamCount = pDefinition->GetParameterCount();
    const RadientMaterialParameterDesc&  ParamDesc  = pDefinition->GetParameterDesc(0);
    RadientMaterialParameterHandle       Handle;
    IRadientMaterialInstance*            pClone = nullptr;
    float                                Value  = 0;

    Status               = pDefinition->GetParameterHandle(0, &Handle);
    Status               = pDefinition->FindParameter("Parameter", &Handle);
    Status               = pDefinition->CreateInstance(&pInstance);
    pDefinition          = pInstance->GetDefinition();
    const Uint64 Version = pInstance->GetVersion();
    Status               = pInstance->GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value)));
    Status               = pInstance->GetTexture(Handle, 0, &pTexture);
    Status               = pInstance->CreateWriter(&pWriter);
    Status               = pInstance->Clone(&pClone);
    Status               = pWriter->SetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value)));
    Status               = pWriter->SetTexture(Handle, 0, pTexture);
    Status               = pWriter->Commit();

    RadientStandardMaterialDefinitionCreateInfo StandardCI;
    IRadientMaterialDefinition*                 pStandardDefinition = nullptr;
    Status                                                          = pAssetManager->CreateStandardMaterialDefinition(StandardCI, &pStandardDefinition);

    (void)Desc;
    (void)Status;
    (void)ParamCount;
    (void)ParamDesc;
    (void)pStandardDefinition;
    (void)pClone;
    (void)Version;
}
