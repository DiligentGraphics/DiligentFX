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

#pragma once

#include <memory>

// NoteL tbb.h must be included before mesh.h to avoid compilation errors in tbb headers.
#include "tbb/tbb.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/tf/token.h"

namespace Diligent
{

namespace USD
{

/// Hydra mesh implementation in Hydrogent.
class HnMesh final : public pxr::HdMesh
{
public:
    static std::shared_ptr<HnMesh> Create(pxr::TfToken const& typeId,
                                          pxr::SdfPath const& id);

    ~HnMesh();

    // Returns the set of dirty bits that should be
    // added to the change tracker for this prim, when this prim is inserted.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    /// Pull invalidated scene data and prepare/update the renderable
    /// representation.
    virtual void Sync(pxr::HdSceneDelegate* delegate,
                      pxr::HdRenderParam*   renderParam,
                      pxr::HdDirtyBits*     dirtyBits,
                      pxr::TfToken const&   reprToken) override final;

    // Returns the names of built-in primvars, i.e. primvars that
    // are part of the core geometric schema for this prim.
    virtual pxr::TfTokenVector const& GetBuiltinPrimvarNames() const override final;

protected:
    // This callback from Rprim gives the prim an opportunity to set
    // additional dirty bits based on those already set.
    virtual pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override final;

    // Initialize the given representation of this Rprim.
    // This is called prior to syncing the prim, the first time the repr
    // is used.
    virtual void _InitRepr(pxr::TfToken const& reprToken, pxr::HdDirtyBits* dirtyBits) override final;

private:
    HnMesh(pxr::TfToken const& typeId,
           pxr::SdfPath const& id);
};

} // namespace USD

} // namespace Diligent
