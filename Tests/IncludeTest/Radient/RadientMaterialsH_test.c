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
    RADIENT_MATERIAL_DEFINITION_TYPE            DefinitionType   = RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE;
    RADIENT_MATERIAL_PARAMETER_TYPE             Type             = RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4;
    RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE       AddressMode      = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP;
    RADIENT_SURFACE_SHADING_MODEL               ShadingModel     = RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS;
    RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS      Features         = RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;
    RADIENT_MATERIAL_SURFACE_MODE               SurfaceMode      = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    RadientMaterialParameterHandle              Handle           = {0};
    RadientMaterialDefinitionDesc               Desc             = {0};
    RadientSurfaceMaterialDefinitionDesc        SurfaceDesc      = {0};
    RadientPostProcessMaterialDefinitionDesc    PostProcessDesc  = {0};
    RadientComputeMaterialDefinitionDesc        ComputeDesc      = {0};
    RadientMaterialParameterDesc                Parameter        = {0};
    RadientStandardMaterialDefinitionCreateInfo StandardCI       = {0};
    IRadientMaterialDefinitionAsset*            pDefinition      = 0;
    IRadientMaterialAsset*                      pMaterial        = 0;
    IRadientSurfaceMaterialAsset*               pSurfaceMaterial = 0;
    IRadientMaterialWriter*                     pWriter          = 0;
    IRadientSurfaceMaterialWriter*              pSurfaceWriter   = 0;

    (void)DefinitionType;
    (void)Type;
    (void)AddressMode;
    (void)ShadingModel;
    (void)Features;
    (void)SurfaceMode;
    (void)Handle;
    (void)Desc;
    (void)SurfaceDesc;
    (void)PostProcessDesc;
    (void)ComputeDesc;
    (void)Parameter;
    (void)StandardCI;
    (void)pDefinition;
    (void)pMaterial;
    (void)pSurfaceMaterial;
    (void)pWriter;
    (void)pSurfaceWriter;
}

void RadientMaterials_C_TestDefinitionMacros(IRadientMaterialDefinitionAsset* pDefinition)
{
    const RadientMaterialDefinitionDesc* pDesc      = IRadientMaterialDefinitionAsset_GetDesc(pDefinition);
    RADIENT_STATUS                       Status     = IRadientMaterialDefinitionAsset_GetStatus(pDefinition);
    Uint32                               ParamCount = IRadientMaterialDefinitionAsset_GetParameterCount(pDefinition);
    const RadientMaterialParameterDesc*  pParamDesc = IRadientMaterialDefinitionAsset_GetParameterDesc(pDefinition, 0);
    RadientMaterialParameterHandle       Handle     = {0};

    Status = IRadientMaterialDefinitionAsset_GetParameterHandle(pDefinition, 0, &Handle);
    Status = IRadientMaterialDefinitionAsset_FindParameter(pDefinition, "Parameter", &Handle);

    (void)pDesc;
    (void)Status;
    (void)ParamCount;
    (void)pParamDesc;
    (void)Handle;
}

void RadientMaterials_C_TestMaterialMacros(IRadientMaterialAsset*         pMaterial,
                                           IRadientSurfaceMaterialAsset*  pSurfaceMaterial,
                                           IRadientMaterialWriter*        pWriter,
                                           IRadientSurfaceMaterialWriter* pSurfaceWriter,
                                           IRadientTextureAsset*          pTexture)
{
    RadientMaterialParameterHandle   Handle      = {0};
    IRadientMaterialDefinitionAsset* pDefinition = IRadientMaterialAsset_GetDefinition(pMaterial);
    Uint64                           Version     = IRadientMaterialAsset_GetVersion(pMaterial);
    float                            Value       = 0;
    RADIENT_STATUS                   Status      = RADIENT_STATUS_OK;

    Status = IRadientMaterialAsset_GetParameter(pMaterial, Handle, &Value, (Uint32)sizeof(Value));
    Status = IRadientMaterialAsset_GetTexture(pMaterial, Handle, 0, &pTexture);
    Status = IRadientMaterialAsset_CreateWriter(pMaterial, &pWriter);
    Status = IRadientMaterialWriter_SetParameter(pWriter, Handle, &Value, (Uint32)sizeof(Value));
    Status = IRadientMaterialWriter_SetTexture(pWriter, Handle, 0, pTexture);
    Status = IRadientMaterialWriter_Commit(pWriter);
    {
        RADIENT_MATERIAL_SURFACE_MODE SurfaceMode = IRadientSurfaceMaterialAsset_GetSurfaceMode(pSurfaceMaterial);
        Float32                       AlphaCutoff = IRadientSurfaceMaterialAsset_GetAlphaCutoff(pSurfaceMaterial);
        Bool                          DoubleSided = IRadientSurfaceMaterialAsset_IsDoubleSided(pSurfaceMaterial);
        Status                                    = IRadientSurfaceMaterialWriter_SetSurfaceMode(pSurfaceWriter, SurfaceMode);
        Status                                    = IRadientSurfaceMaterialWriter_SetAlphaCutoff(pSurfaceWriter, AlphaCutoff);
        Status                                    = IRadientSurfaceMaterialWriter_SetDoubleSided(pSurfaceWriter, DoubleSided);
    }

    (void)pDefinition;
    (void)Version;
    (void)Status;
}

void RadientMaterials_C_TestAssetManagerMacros(IRadientAssetManager* pAssetManager)
{
    RadientStandardMaterialDefinitionCreateInfo StandardCI          = {0};
    IRadientMaterialDefinitionAsset*            pStandardDefinition = 0;
    RADIENT_STATUS                              Status              = IRadientAssetManager_CreateStandardMaterialDefinition(pAssetManager, &StandardCI, &pStandardDefinition);

    (void)pStandardDefinition;
    (void)Status;
}
