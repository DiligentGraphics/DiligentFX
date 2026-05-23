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

#include "Radient/interface/RadientEngine.h"

void RadientEngine_C_TestMacros(IRadientEngine* pEngine)
{
    RadientSceneDesc     SceneDesc    = {0};
    RadientRendererDesc  RendererDesc = {0};
    IRadientBackend*     pBackend     = 0;
    IRadientScene*       pScene       = 0;
    IRadientSceneWriter* pWriter      = 0;
    IRadientRenderer*    pRenderer    = 0;
    RADIENT_STATUS       Status       = RADIENT_STATUS_OK;

    Status = IRadientEngine_GetBackend(pEngine, &pBackend);
    Status = IRadientEngine_CreateScene(pEngine, &SceneDesc, &pScene);
    Status = IRadientEngine_CreateSceneWriter(pEngine, pScene, &pWriter);
    Status = IRadientEngine_CreateRenderer(pEngine, &RendererDesc, &pRenderer);

    (void)pBackend;
    (void)pScene;
    (void)pWriter;
    (void)pRenderer;
    (void)Status;
}

void RadientEngine_C_TestCreateFunction(void)
{
    RadientEngineCreateInfo EngineCI = {0};
    IRadientEngine*         pEngine  = 0;
    RADIENT_STATUS          Status   = Diligent_CreateRadientEngine(&EngineCI, &pEngine);

    (void)pEngine;
    (void)Status;
}
