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

#include "RadientVertexLayout.h"

// Link and execute the C entry points as well as compiling their declarations.
int RadientVertexLayout_C_TestHelpers(void)
{
    RadientVertexAttributeDesc Attribute = {"POSITION", 0, 0, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False};

    if (Diligent_GetRadientVertexComponentSize(RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32) != 4 ||
        Diligent_GetRadientVertexAttributeSize(&Attribute) != 12)
        return 1;

    if (Diligent_GetRadientVertexAttributeSize(0) != 0)
        return 2;

    Attribute.Normalized = True;
    if (Diligent_GetRadientVertexAttributeSize(&Attribute) != 0)
        return 3;

    return 0;
}

// The prefixed macros are integer constant expressions for C static initializers.
const RadientVertexLayoutDesc* RadientVertexLayout_C_GetAutomaticLayout(void)
{
    static const RadientVertexAttributeDesc Attributes[] = {
        {"POSITION", 0, DILIGENT_RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, 0},
        {"TEXCOORD_0", 0, DILIGENT_RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, 0},
    };
    static const RadientVertexBufferLayoutDesc Buffer = {DILIGENT_RADIENT_VERTEX_AUTO_STRIDE};
    static const RadientVertexLayoutDesc       Layout = {Attributes, 2, &Buffer, 1};
    return &Layout;
}
