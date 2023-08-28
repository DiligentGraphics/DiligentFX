/*
 *  Copyright 2023 Diligent Graphics LLC
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


#include "Mesh.hpp"

namespace Diligent
{

namespace USD
{

std::shared_ptr<HnMesh> HnMesh::Create(pxr::TfToken const& typeId, pxr::SdfPath const& id)
{
    return std::shared_ptr<HnMesh>(new HnMesh{typeId, id});
}

HnMesh::HnMesh(pxr::TfToken const& typeId,
               pxr::SdfPath const& id) :
    pxr::HdMesh{id}
{
}

HnMesh::~HnMesh()
{
}

pxr::HdDirtyBits HnMesh::GetInitialDirtyBitsMask() const
{
    // Set all bits except the varying flag
    return pxr::HdChangeTracker::AllSceneDirtyBits & ~pxr::HdChangeTracker::Varying;
}

void HnMesh::Sync(pxr::HdSceneDelegate* delegate,
                  pxr::HdRenderParam*   renderParam,
                  pxr::HdDirtyBits*     dirtyBits,
                  pxr::TfToken const&   reprToken)
{
    if (*dirtyBits == pxr::HdChangeTracker::Clean)
        return;

    *dirtyBits &= ~pxr::HdChangeTracker::AllSceneDirtyBits;
}

pxr::TfTokenVector const& HnMesh::GetBuiltinPrimvarNames() const
{
    static const pxr::TfTokenVector names{};
    return names;
}

pxr::HdDirtyBits HnMesh::_PropagateDirtyBits(pxr::HdDirtyBits bits) const
{
    return bits;
}

void HnMesh::_InitRepr(pxr::TfToken const& reprToken, pxr::HdDirtyBits* dirtyBits)
{
    auto it = std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));
    if (it == _reprs.end())
    {
        _reprs.emplace_back(reprToken, pxr::HdReprSharedPtr());
    }
}

} // namespace USD

} // namespace Diligent
