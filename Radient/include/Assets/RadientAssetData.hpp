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

#pragma once

#include "RadientAssetResolver.h"
#include "ObjectBase.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

// Owns resolved bytes together with the canonical URI that identifies them.
class RadientAssetDataImpl final : public ObjectBase<IRadientAssetData>
{
public:
    using TBase       = ObjectBase<IRadientAssetData>;
    using OwnedBuffer = std::unique_ptr<void, decltype(&std::free)>;

    RadientAssetDataImpl(IReferenceCounters* pRefCounters,
                         std::vector<Uint8>  Data,
                         std::string         ResolvedURI) :
        TBase{pRefCounters},
        m_Data{std::move(Data)},
        m_Size{m_Data.size()},
        m_ResolvedURI{std::move(ResolvedURI)}
    {
    }

    // Adopts an existing allocation without copying its contents. The deleter
    // remains responsible for releasing the bytes when the last reference expires.
    RadientAssetDataImpl(IReferenceCounters* pRefCounters,
                         OwnedBuffer         Data,
                         size_t              Size,
                         std::string         ResolvedURI) :
        TBase{pRefCounters},
        m_ExternalData{std::move(Data)},
        m_Size{Size},
        m_ResolvedURI{std::move(ResolvedURI)}
    {
        VERIFY_EXPR(Size == 0 || m_ExternalData != nullptr);
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetData, TBase)

    virtual const void* DILIGENT_CALL_TYPE GetData() const override final
    {
        return m_Size != 0 ? (m_ExternalData ? m_ExternalData.get() : m_Data.data()) : nullptr;
    }

    virtual size_t DILIGENT_CALL_TYPE GetSize() const override final
    {
        return m_Size;
    }

    virtual const Char* DILIGENT_CALL_TYPE GetResolvedURI() const override final
    {
        return m_ResolvedURI.c_str();
    }

private:
    // Only one storage owner is populated by each constructor.
    std::vector<Uint8> m_Data;
    OwnedBuffer        m_ExternalData{nullptr, std::free};
    const size_t       m_Size;
    const std::string  m_ResolvedURI;
};

} // namespace Diligent
