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

#include "Assets/RadientDrawableMeshConverter.hpp"

#include "GLTFLoader.hpp"

namespace Diligent
{

RADIENT_STATUS CreateGLTFDrawableMesh(const GLTF::Model&      Model,
                                      Uint32                  MeshIndex,
                                      PBR_Renderer::PSO_FLAGS VertexAttribFlags,
                                      RadientDrawableMesh&    Mesh)
{
    Mesh = {};

    if (MeshIndex >= Model.Meshes.size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    Mesh.Geometries.push_back(RadientDrawableMeshGeometry{
        Model.GetVertexPool(),
        VertexAttribFlags,
        Model.GetFirstIndexLocation(),
        Model.GetBaseVertex()});

    return ConvertGLTFDrawableMeshPrimitives(Model.Meshes[MeshIndex].Primitives,
                                             Model.Materials,
                                             Mesh.Primitives);
}

RADIENT_STATUS ConvertGLTFDrawableMeshPrimitives(const std::vector<GLTF::Primitive>&        Primitives,
                                                 const std::vector<GLTF::Material>&         Materials,
                                                 std::vector<RadientDrawableMeshPrimitive>& DrawablePrimitives)
{
    DrawablePrimitives.clear();
    DrawablePrimitives.reserve(Primitives.size());

    for (const GLTF::Primitive& Primitive : Primitives)
    {
        const bool   IsIndexed    = Primitive.HasIndices();
        const Uint32 FirstElement = IsIndexed ? Primitive.FirstIndex : Primitive.FirstVertex;
        const Uint32 ElementCount = IsIndexed ? Primitive.IndexCount : Primitive.VertexCount;

        const GLTF::Material* pMaterial =
            Primitive.MaterialId < Materials.size() ?
            &Materials[Primitive.MaterialId] :
            nullptr;

        DrawablePrimitives.push_back(RadientDrawableMeshPrimitive{
            pMaterial,
            nullptr,
            0,
            IsIndexed,
            FirstElement,
            ElementCount});
    }

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
