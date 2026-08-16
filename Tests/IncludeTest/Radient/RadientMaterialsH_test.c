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

void RadientMaterials_C_UseTypes(void)
{
    RadientMaterialParameterID          ParameterID = InvalidRadientMaterialParameterID;
    RADIENT_MATERIAL_DOMAIN             Domain      = RADIENT_MATERIAL_DOMAIN_SURFACE;
    RADIENT_MATERIAL_PARAMETER_TYPE     Type        = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    RADIENT_MATERIAL_PARAMETER_FLAGS    Flags       = RADIENT_MATERIAL_PARAMETER_FLAG_SPECIALIZATION;
    RadientMaterialParameterHandle      Handle      = {0};
    RadientMaterialDefinitionDesc       Desc        = {0};
    RadientMaterialParameterDesc        Parameter   = {0};
    RadientMaterialDefinitionCreateInfo CreateInfo  = {0};
    IRadientMaterialDefinition*         pDefinition = 0;
    IRadientMaterialInstance*           pInstance   = 0;
    IRadientMaterialInstanceWriter*     pWriter     = 0;

    (void)ParameterID;
    (void)Domain;
    (void)Type;
    (void)Flags;
    (void)Handle;
    (void)Desc;
    (void)Parameter;
    (void)CreateInfo;
    (void)pDefinition;
    (void)pInstance;
    (void)pWriter;
}

void RadientMaterials_C_TestDefinitionMacros(IRadientMaterialDefinition* pDefinition)
{
    const RadientMaterialDefinitionDesc* pDesc      = IRadientMaterialDefinition_GetDesc(pDefinition);
    RADIENT_STATUS                       Status     = IRadientMaterialDefinition_GetStatus(pDefinition);
    Uint32                               ParamCount = IRadientMaterialDefinition_GetParameterCount(pDefinition);
    const RadientMaterialParameterDesc*  pParamDesc = IRadientMaterialDefinition_GetParameterDesc(pDefinition, 0);
    RadientMaterialParameterHandle       Handle     = {0};
    IRadientMaterialInstance*            pInstance  = 0;

    Status = IRadientMaterialDefinition_GetParameterHandle(pDefinition, 0, &Handle);
    Status = IRadientMaterialDefinition_FindParameter(pDefinition, "Parameter", &Handle);
    Status = IRadientMaterialDefinition_CreateInstance(pDefinition, &pInstance);

    (void)pDesc;
    (void)Status;
    (void)ParamCount;
    (void)pParamDesc;
    (void)Handle;
    (void)pInstance;
}

void RadientMaterials_C_TestInstanceMacros(IRadientMaterialInstance*       pInstance,
                                           IRadientMaterialInstanceWriter* pWriter,
                                           IRadientTextureAsset*           pTexture)
{
    RadientMaterialParameterHandle Handle      = {0};
    IRadientMaterialDefinition*    pDefinition = IRadientMaterialInstance_GetDefinition(pInstance);
    Uint64                         Version     = IRadientMaterialInstance_GetVersion(pInstance);
    float                          Value       = 0;
    IRadientMaterialInstance*      pClone      = 0;
    RADIENT_STATUS                 Status      = RADIENT_STATUS_OK;

    Status = IRadientMaterialInstance_GetParameter(pInstance, Handle, &Value, (Uint32)sizeof(Value));
    Status = IRadientMaterialInstance_GetTexture(pInstance, Handle, 0, &pTexture);
    Status = IRadientMaterialInstance_CreateWriter(pInstance, &pWriter);
    Status = IRadientMaterialInstance_Clone(pInstance, &pClone);
    Status = IRadientMaterialInstanceWriter_SetParameter(pWriter, Handle, &Value, (Uint32)sizeof(Value));
    Status = IRadientMaterialInstanceWriter_SetTexture(pWriter, Handle, 0, pTexture);
    Status = IRadientMaterialInstanceWriter_Commit(pWriter);

    (void)pDefinition;
    (void)Version;
    (void)pClone;
    (void)Status;
}

void RadientMaterials_C_TestAssetManagerMacros(IRadientAssetManager* pAssetManager)
{
    RadientMaterialDefinitionCreateInfo CreateInfo  = {0};
    IRadientMaterialDefinition*         pDefinition = 0;
    RADIENT_STATUS                      Status      = IRadientAssetManager_CreateMaterialDefinition(pAssetManager, &CreateInfo, &pDefinition);

    (void)pDefinition;
    (void)Status;
}
