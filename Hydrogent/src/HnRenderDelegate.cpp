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

#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "DebugUtilities.hpp"

#include "pxr/imaging/hd/material.h"

namespace Diligent
{

namespace USD
{

std::unique_ptr<HnRenderDelegate> HnRenderDelegate::Create()
{
    return std::make_unique<HnRenderDelegate>();
}

// clang-format off
const pxr::TfTokenVector HnRenderDelegate::SupportedRPrimTypes =
{
    pxr::HdPrimTypeTokens->mesh,
    pxr::HdPrimTypeTokens->points,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedSPrimTypes =
{
    pxr::HdPrimTypeTokens->material,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedBPrimTypes =
{
};
// clang-format on

HnRenderDelegate::HnRenderDelegate()
{
}

HnRenderDelegate::~HnRenderDelegate()
{
}

const pxr::TfTokenVector& HnRenderDelegate::GetSupportedRprimTypes() const
{
    return SupportedRPrimTypes;
}

const pxr::TfTokenVector& HnRenderDelegate::GetSupportedSprimTypes() const
{
    return SupportedSPrimTypes;
}


const pxr::TfTokenVector& HnRenderDelegate::GetSupportedBprimTypes() const
{
    return SupportedBPrimTypes;
}

pxr::HdResourceRegistrySharedPtr HnRenderDelegate::GetResourceRegistry() const
{
    return {};
}

pxr::HdRenderPassSharedPtr HnRenderDelegate::CreateRenderPass(pxr::HdRenderIndex*           index,
                                                              pxr::HdRprimCollection const& collection)
{
    return {};
}


pxr::HdInstancer* HnRenderDelegate::CreateInstancer(pxr::HdSceneDelegate* delegate,
                                                    pxr::SdfPath const&   id)
{
    return nullptr;
}

void HnRenderDelegate::DestroyInstancer(pxr::HdInstancer* instancer)
{
}

pxr::HdRprim* HnRenderDelegate::CreateRprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& rprimId)
{
    return nullptr;
}

void HnRenderDelegate::DestroyRprim(pxr::HdRprim* rPrim)
{
}

pxr::HdSprim* HnRenderDelegate::CreateSprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& sprimId)
{
    return nullptr;
}

pxr::HdSprim* HnRenderDelegate::CreateFallbackSprim(pxr::TfToken const& typeId)
{
    return nullptr;
}

void HnRenderDelegate::DestroySprim(pxr::HdSprim* sprim)
{
}

pxr::HdBprim* HnRenderDelegate::CreateBprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& bprimId)
{
    return nullptr;
}

pxr::HdBprim* HnRenderDelegate::CreateFallbackBprim(pxr::TfToken const& typeId)
{
    return nullptr;
}

void HnRenderDelegate::DestroyBprim(pxr::HdBprim* bprim)
{
}

void HnRenderDelegate::CommitResources(pxr::HdChangeTracker* tracker)
{
}

} // namespace USD

} // namespace Diligent
