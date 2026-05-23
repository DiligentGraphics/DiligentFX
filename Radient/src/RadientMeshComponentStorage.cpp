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

#include "RadientMeshComponentStorage.hpp"

#include <utility>

namespace Diligent
{

MeshComponentStorage::MeshComponentStorage() = default;

MeshComponentStorage::MeshComponentStorage(MeshComponentStorage&& Rhs) noexcept :
    Component{Rhs.Component},
    MeshURI{std::move(Rhs.MeshURI)}
{
    FixupURI();
    Rhs.Component = {};
    Rhs.MeshURI.clear();
}

MeshComponentStorage& MeshComponentStorage::operator=(MeshComponentStorage&& Rhs) noexcept
{
    if (this != &Rhs)
    {
        Component = Rhs.Component;
        MeshURI   = std::move(Rhs.MeshURI);
        FixupURI();
        Rhs.Component = {};
        Rhs.MeshURI.clear();
    }

    return *this;
}

void MeshComponentStorage::Assign(const RadientMeshComponent& Mesh)
{
    Component = Mesh;
    MeshURI   = Mesh.Mesh.URI != nullptr ? Mesh.Mesh.URI : "";
    FixupURI();
}

void MeshComponentStorage::FixupURI()
{
    // Component.Mesh.URI points into MeshURI and must be repaired after move.
    Component.Mesh.URI = Component.Mesh.URI != nullptr ? MeshURI.c_str() : nullptr;
}

} // namespace Diligent
