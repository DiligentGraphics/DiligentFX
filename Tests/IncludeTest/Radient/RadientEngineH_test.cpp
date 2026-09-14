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

static_assert(Diligent::RadientEngineCreateInfo{}.Resources.IndexBufferSize == 4u * 1024u * 1024u, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.MaxIndexBufferSize == 256ull * 1024ull * 1024ull, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.MorphTargetBufferSize == 1024u * 1024u, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.MaxMorphTargetBufferSize == 256ull * 1024ull * 1024ull, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.VertexPoolSize == 64u * 1024u, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.TextureAtlasSize == 2048, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.TextureAtlasMipLevel0Size == 16ull * 1024ull * 1024ull, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.TextureAtlasSlices == 1, "Resource default changed");
static_assert(Diligent::RadientEngineCreateInfo{}.Resources.TextureAtlasMaxSlices == 2048, "Resource default changed");

void RadientEngine_CPP_UseAnimationRegistry(Diligent::IRadientEngine* pEngine,
                                            Diligent::IRadientScene*  pScene)
{
    Diligent::IRadientAnimationRegistry* pRegistry = nullptr;
    const Diligent::RADIENT_STATUS       Status    = pEngine->CreateAnimationRegistry(pScene, &pRegistry);

    (void)pRegistry;
    (void)Status;
}
