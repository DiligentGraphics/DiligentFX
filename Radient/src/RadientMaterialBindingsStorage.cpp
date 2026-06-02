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

#include "RadientMaterialBindingsStorage.hpp"

#include <utility>

namespace Diligent
{

MaterialBindingsStorage::MaterialBindingsStorage() = default;

MaterialBindingsStorage::MaterialBindingsStorage(MaterialBindingsStorage&& Rhs) noexcept :
    Component{Rhs.Component},
    Bindings{std::move(Rhs.Bindings)},
    Materials{std::move(Rhs.Materials)}
{
    FixupPointers();
    Rhs.Component = {};
    Rhs.Bindings.clear();
    Rhs.Materials.clear();
}

MaterialBindingsStorage& MaterialBindingsStorage::operator=(MaterialBindingsStorage&& Rhs) noexcept
{
    if (this != &Rhs)
    {
        Component = Rhs.Component;
        Bindings  = std::move(Rhs.Bindings);
        Materials = std::move(Rhs.Materials);
        FixupPointers();
        Rhs.Component = {};
        Rhs.Bindings.clear();
        Rhs.Materials.clear();
    }

    return *this;
}

bool MaterialBindingsStorage::Equals(const RadientMaterialBindingsComponent& Rhs) const
{
    return Component == Rhs;
}

void MaterialBindingsStorage::Assign(const RadientMaterialBindingsComponent& Rhs)
{
    Component = Rhs;
    Bindings  = {};
    Materials = {};

    if (Rhs.BindingCount != 0)
    {
        Bindings.assign(Rhs.pBindings, Rhs.pBindings + Rhs.BindingCount);
        Materials.reserve(Rhs.BindingCount);
        for (Uint32 BindingIndex = 0; BindingIndex < Rhs.BindingCount; ++BindingIndex)
        {
            Materials.emplace_back(Rhs.pBindings[BindingIndex].pMaterial);
        }
    }

    FixupPointers();
}

void MaterialBindingsStorage::FixupPointers()
{
    Component.pBindings    = Bindings.empty() ? nullptr : Bindings.data();
    Component.BindingCount = static_cast<Uint32>(Bindings.size());

    for (Uint32 BindingIndex = 0; BindingIndex < Component.BindingCount; ++BindingIndex)
    {
        Bindings[BindingIndex].pMaterial = Materials[BindingIndex];
    }
}

} // namespace Diligent
