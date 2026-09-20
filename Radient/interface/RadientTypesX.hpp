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

/// \file
/// Owning C++ convenience wrappers for Radient descriptors.
/// Include this header explicitly; the C-compatible Radient.h remains unchanged.

#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "RadientAssets.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"

namespace Diligent
{

/// Owns a vertex layout's attribute descriptors, buffer descriptors, and semantic strings.
///
/// Construction and copying duplicate metadata, including semantic strings. Automatic
/// offsets and strides are preserved. Setters allow incremental construction; layout
/// validation still takes place in the consuming Radient API. Get() and conversion
/// expose a read-only view without allocating. Views are valid until the wrapper is
/// modified, moved, or destroyed. A consuming API copies metadata during the call.
/// Concurrent access to a wrapper requires synchronization if any access modifies it.
class RadientVertexLayoutDescX
{
public:
    /// Creates an empty layout.
    RadientVertexLayoutDescX() noexcept = default;

    /// Copies both arrays and every semantic. Nonzero counts require valid arrays.
    RadientVertexLayoutDescX(const RadientVertexLayoutDesc& Desc) :
        m_Desc{Desc}
    {
        if (Desc.AttributeCount != 0)
        {
            VERIFY_EXPR(Desc.pAttributes != nullptr);
            m_Attributes.assign(Desc.pAttributes, Desc.pAttributes + Desc.AttributeCount);
            m_Semantics.reserve(m_Attributes.size());
            for (RadientVertexAttributeDesc& Attribute : m_Attributes)
            {
                m_Semantics.push_back(CopySemantic(Attribute.Semantic));
                Attribute.Semantic = m_Semantics.back().get();
            }
        }
        if (Desc.BufferCount != 0)
        {
            VERIFY_EXPR(Desc.pBuffers != nullptr);
            m_Buffers.assign(Desc.pBuffers, Desc.pBuffers + Desc.BufferCount);
        }
        SyncDesc();
    }

    /// Copies attributes and buffers from initializer lists.
    RadientVertexLayoutDescX(std::initializer_list<RadientVertexAttributeDesc>    Attributes,
                             std::initializer_list<RadientVertexBufferLayoutDesc> Buffers) :
        RadientVertexLayoutDescX{RadientVertexLayoutDesc{Attributes.begin(), static_cast<Uint32>(Attributes.size()),
                                                         Buffers.begin(), static_cast<Uint32>(Buffers.size())}}
    {}

    /// Creates an independent metadata copy.
    RadientVertexLayoutDescX(const RadientVertexLayoutDescX& Other) :
        RadientVertexLayoutDescX{Other.Get()}
    {}

    /// Replaces this layout with an independent metadata copy.
    RadientVertexLayoutDescX& operator=(const RadientVertexLayoutDescX& Other)
    {
        RadientVertexLayoutDescX Copy{Other};
        Swap(Copy);
        return *this;
    }

    /// Transfers storage and leaves Other empty.
    RadientVertexLayoutDescX(RadientVertexLayoutDescX&& Other) noexcept
    {
        Swap(Other);
    }

    /// Transfers storage and leaves Other empty. Self-move leaves the layout unchanged.
    RadientVertexLayoutDescX& operator=(RadientVertexLayoutDescX&& Other) noexcept
    {
        RadientVertexLayoutDescX Moved{std::move(Other)};
        Swap(Moved);
        return *this;
    }

    /// Appends a logical buffer. Its index is the previous buffer count.
    RadientVertexLayoutDescX& AddBuffer(const RadientVertexBufferLayoutDesc& Buffer)
    {
        m_Buffers.push_back(Buffer);
        SyncDesc();
        return *this;
    }

    /// Appends a buffer with the specified stride; the default requests automatic stride.
    RadientVertexLayoutDescX& AddBuffer(Uint32 ByteStride = RADIENT_VERTEX_AUTO_STRIDE)
    {
        return AddBuffer(RadientVertexBufferLayoutDesc{ByteStride});
    }

    /// Replaces a buffer descriptor. Index must be less than GetBufferCount().
    RadientVertexLayoutDescX& SetBuffer(Uint32 Index, const RadientVertexBufferLayoutDesc& Buffer)
    {
        VERIFY_EXPR(Index < m_Buffers.size());
        m_Buffers[Index] = Buffer;
        return *this;
    }

    /// Replaces a buffer's stride. Index must be less than GetBufferCount().
    RadientVertexLayoutDescX& SetBuffer(Uint32 Index, Uint32 ByteStride)
    {
        return SetBuffer(Index, RadientVertexBufferLayoutDesc{ByteStride});
    }

    /// Appends an attribute and copies its semantic string.
    RadientVertexLayoutDescX& AddAttribute(const RadientVertexAttributeDesc& Attribute)
    {
        RadientVertexAttributeDesc Copy = Attribute;
        m_Semantics.push_back(CopySemantic(Copy.Semantic));
        Copy.Semantic = m_Semantics.back().get();
        try
        {
            m_Attributes.push_back(Copy);
        }
        catch (...)
        {
            m_Semantics.pop_back();
            throw;
        }
        SyncDesc();
        return *this;
    }

    /// Appends an attribute. Defaults select buffer zero, automatic offset, and no normalization.
    // clang-format off
    RadientVertexLayoutDescX& AddAttribute(const Char*                    Semantic,
                                         RADIENT_VERTEX_COMPONENT_TYPE ComponentType,
                                         Uint32                         ComponentCount,
                                         Uint32                         BufferIndex = 0,
                                         Uint32                         ByteOffset = RADIENT_VERTEX_AUTO_OFFSET,
                                         Bool                           Normalized = False)
    // clang-format on
    {
        return AddAttribute(RadientVertexAttributeDesc{Semantic, BufferIndex, ByteOffset, ComponentType, ComponentCount, Normalized});
    }

    /// Replaces an attribute and copies its semantic. Index must be less than GetAttributeCount().
    RadientVertexLayoutDescX& SetAttribute(Uint32 Index, const RadientVertexAttributeDesc& Attribute)
    {
        VERIFY_EXPR(Index < m_Attributes.size());
        RadientVertexAttributeDesc Copy     = Attribute;
        std::unique_ptr<Char[]>    Semantic = CopySemantic(Copy.Semantic);
        Copy.Semantic                       = Semantic.get();
        m_Attributes[Index]                 = Copy;
        m_Semantics[Index]                  = std::move(Semantic);
        return *this;
    }

    /// Returns the number of attributes.
    Uint32 GetAttributeCount() const noexcept { return m_Desc.AttributeCount; }

    /// Returns the number of logical buffers.
    Uint32 GetBufferCount() const noexcept { return m_Desc.BufferCount; }

    /// Returns an attribute by index, which must be less than GetAttributeCount().
    const RadientVertexAttributeDesc& GetAttribute(Uint32 Index) const noexcept
    {
        VERIFY_EXPR(Index < m_Attributes.size());
        return m_Attributes[Index];
    }

    /// Returns a buffer by index, which must be less than GetBufferCount().
    const RadientVertexBufferLayoutDesc& GetBuffer(Uint32 Index) const noexcept
    {
        VERIFY_EXPR(Index < m_Buffers.size());
        return m_Buffers[Index];
    }

    /// Returns the C descriptor view. Its arrays and strings belong to this wrapper.
    const RadientVertexLayoutDesc& Get() const noexcept { return m_Desc; }

    /// Allows passing this wrapper to APIs accepting a const C descriptor reference.
    operator const RadientVertexLayoutDesc&() const noexcept { return Get(); }

    /// Releases metadata and restores an empty layout.
    void Clear() noexcept
    {
        RadientVertexLayoutDescX Empty;
        Swap(Empty);
    }

    /// Exchanges metadata and repairs both descriptor views without allocating.
    void Swap(RadientVertexLayoutDescX& Other) noexcept
    {
        m_Attributes.swap(Other.m_Attributes);
        m_Buffers.swap(Other.m_Buffers);
        m_Semantics.swap(Other.m_Semantics);
        SyncDesc();
        Other.SyncDesc();
    }

private:
    static std::unique_ptr<Char[]> CopySemantic(const Char* Semantic)
    {
        if (Semantic == nullptr)
        {
            return {};
        }
        const size_t            Size = std::strlen(Semantic) + 1;
        std::unique_ptr<Char[]> Copy{new Char[Size]};
        std::memcpy(Copy.get(), Semantic, Size);
        return Copy;
    }

    void SyncDesc() noexcept
    {
        m_Desc.pAttributes    = m_Attributes.empty() ? nullptr : m_Attributes.data();
        m_Desc.AttributeCount = static_cast<Uint32>(m_Attributes.size());
        m_Desc.pBuffers       = m_Buffers.empty() ? nullptr : m_Buffers.data();
        m_Desc.BufferCount    = static_cast<Uint32>(m_Buffers.size());
    }

    RadientVertexLayoutDesc                    m_Desc;
    std::vector<RadientVertexAttributeDesc>    m_Attributes;
    std::vector<RadientVertexBufferLayoutDesc> m_Buffers;
    // Separate string storage keeps semantic pointers stable as arrays grow.
    std::vector<std::unique_ptr<Char[]>> m_Semantics;
};

/// Owns supplied mip descriptors and retains their data blobs.
///
/// Copies duplicate descriptors and share blob references; pixel bytes are never
/// copied. Construction and modification do not acquire blob read access, so a
/// mutable blob can still be populated before LoadTexture. REFERENCE blobs retain
/// their existing external-storage lifetime requirements. Get() and conversion
/// expose a view valid until modification, move, or destruction. LoadTexture
/// captures the descriptor and blob references during the call. Concurrent access
/// requires synchronization if any access modifies the wrapper.
class RadientTextureDataX
{
public:
    /// Creates empty data with the C descriptor defaults, including GenerateMips=True.
    RadientTextureDataX() noexcept = default;

    /// Sets mip-zero dimensions and format. AddMip supplies the actual levels.
    RadientTextureDataX(Uint32 Width, Uint32 Height, RADIENT_TEXTURE_FORMAT Format) noexcept
    {
        SetDimensions(Width, Height).SetFormat(Format);
    }

    /// Copies the mip array and retains each blob. A nonzero count requires a valid array.
    RadientTextureDataX(const RadientTextureData& Data) :
        m_Desc{Data}
    {
        if (Data.MipLevelCount != 0)
        {
            VERIFY_EXPR(Data.pMipLevels != nullptr);
            m_Mips.assign(Data.pMipLevels, Data.pMipLevels + Data.MipLevelCount);
            m_Blobs.reserve(m_Mips.size());
            for (const RadientTextureMipData& Mip : m_Mips)
            {
                m_Blobs.emplace_back(Mip.pDataBlob);
            }
        }
        SyncDesc();
    }

    /// Copies metadata and shares references to the same blobs.
    RadientTextureDataX(const RadientTextureDataX& Other) :
        RadientTextureDataX{Other.Get()}
    {}

    /// Replaces metadata and retained blob references with a copy of Other.
    RadientTextureDataX& operator=(const RadientTextureDataX& Other)
    {
        RadientTextureDataX Copy{Other};
        Swap(Copy);
        return *this;
    }

    /// Transfers metadata and references, leaving Other at its default state.
    RadientTextureDataX(RadientTextureDataX&& Other) noexcept
    {
        Swap(Other);
    }

    /// Transfers ownership. Other becomes default-initialized; self-move is a no-op.
    RadientTextureDataX& operator=(RadientTextureDataX&& Other) noexcept
    {
        RadientTextureDataX Moved{std::move(Other)};
        Swap(Moved);
        return *this;
    }

    /// Sets the logical dimensions of mip zero without changing supplied levels.
    RadientTextureDataX& SetDimensions(Uint32 Width, Uint32 Height) noexcept
    {
        m_Desc.Width  = Width;
        m_Desc.Height = Height;
        return *this;
    }

    /// Sets the pixel or block-compressed format without converting bytes.
    RadientTextureDataX& SetFormat(RADIENT_TEXTURE_FORMAT Format) noexcept
    {
        m_Desc.Format = Format;
        return *this;
    }

    /// Selects the existing LoadTexture policy for generating missing levels.
    RadientTextureDataX& SetGenerateMips(Bool GenerateMips) noexcept
    {
        m_Desc.GenerateMips = GenerateMips;
        return *this;
    }

    /// Appends the next consecutive mip descriptor and retains its blob.
    RadientTextureDataX& AddMip(const RadientTextureMipData& Mip)
    {
        const RadientTextureMipData Copy = Mip;
        m_Blobs.emplace_back(Copy.pDataBlob);
        try
        {
            m_Mips.push_back(Copy);
        }
        catch (...)
        {
            m_Blobs.pop_back();
            throw;
        }
        SyncDesc();
        return *this;
    }

    /// Appends a mip stored at ByteOffset in pBlob. Zero Stride selects tight rows.
    RadientTextureDataX& AddMip(IRadientDataBlob* pBlob, Uint64 ByteOffset = 0, Uint32 Stride = 0)
    {
        return AddMip(RadientTextureMipData{pBlob, ByteOffset, Stride});
    }

    /// Replaces a mip and its retained blob. Index must be less than GetMipLevelCount().
    RadientTextureDataX& SetMip(Uint32 Index, const RadientTextureMipData& Mip)
    {
        VERIFY_EXPR(Index < m_Mips.size());
        const RadientTextureMipData     Copy = Mip;
        RefCntAutoPtr<IRadientDataBlob> Blob{Copy.pDataBlob};
        m_Mips[Index] = Copy;
        m_Blobs[Index].swap(Blob);
        return *this;
    }

    /// Replaces a mip's blob, offset, and row stride. Index must already exist.
    RadientTextureDataX& SetMip(Uint32 Index, IRadientDataBlob* pBlob, Uint64 ByteOffset = 0, Uint32 Stride = 0)
    {
        return SetMip(Index, RadientTextureMipData{pBlob, ByteOffset, Stride});
    }

    /// Returns the number of supplied mip levels.
    Uint32 GetMipLevelCount() const noexcept { return m_Desc.MipLevelCount; }

    /// Returns a mip descriptor. Index must be less than GetMipLevelCount().
    const RadientTextureMipData& GetMip(Uint32 Index) const noexcept
    {
        VERIFY_EXPR(Index < m_Mips.size());
        return m_Mips[Index];
    }

    /// Returns the C descriptor view without allocating or acquiring blob access.
    const RadientTextureData& Get() const noexcept { return m_Desc; }

    /// Allows passing this wrapper to APIs accepting a const C descriptor reference.
    operator const RadientTextureData&() const noexcept { return Get(); }

    /// Releases supplied levels and their references, preserving dimensions, format, and options.
    void ClearMips() noexcept
    {
        RadientTextureDataX Empty{m_Desc.Width, m_Desc.Height, m_Desc.Format};
        Empty.SetGenerateMips(m_Desc.GenerateMips);
        Swap(Empty);
    }

    /// Releases levels and restores all C descriptor defaults.
    void Clear() noexcept
    {
        RadientTextureDataX Empty;
        Swap(Empty);
    }

    /// Exchanges descriptors and references and repairs both views without allocating.
    void Swap(RadientTextureDataX& Other) noexcept
    {
        std::swap(m_Desc, Other.m_Desc);
        m_Mips.swap(Other.m_Mips);
        m_Blobs.swap(Other.m_Blobs);
        SyncDesc();
        Other.SyncDesc();
    }

private:
    void SyncDesc() noexcept
    {
        m_Desc.pMipLevels    = m_Mips.empty() ? nullptr : m_Mips.data();
        m_Desc.MipLevelCount = static_cast<Uint32>(m_Mips.size());
    }

    RadientTextureData                           m_Desc;
    std::vector<RadientTextureMipData>           m_Mips;
    std::vector<RefCntAutoPtr<IRadientDataBlob>> m_Blobs;
};

/// Owns texture-loading metadata and retains its source blobs.
///
/// URI strings and nested mip descriptors are copied; blobs are retained without
/// copying bytes or acquiring read access. Copies own independent metadata while
/// sharing blobs. Get() and conversion expose a read-only view valid until the
/// wrapper is modified, moved, or destroyed. LoadTexture captures this information
/// during the call, so the wrapper can be discarded afterward. Concurrent access
/// requires synchronization if any access modifies the wrapper.
class RadientTextureLoadInfoX
{
public:
    /// Creates empty load information with the C descriptor defaults.
    RadientTextureLoadInfoX() noexcept = default;

    /// Copies metadata, preserving null versus empty URI strings, and retains blobs.
    RadientTextureLoadInfoX(const RadientTextureLoadInfo& Info) :
        m_Desc{Info},
        m_URI{Info.URI != nullptr ? Info.URI : ""},
        m_BaseURI{Info.BaseURI != nullptr ? Info.BaseURI : ""},
        m_pDataBlob{Info.pDataBlob},
        m_TextureData{Info.pTextureData != nullptr ? *Info.pTextureData : RadientTextureData{}}
    {
        SyncDesc();
    }

    /// Creates an independent metadata copy and shares references to source blobs.
    RadientTextureLoadInfoX(const RadientTextureLoadInfoX& Other) :
        RadientTextureLoadInfoX{Other.Get()}
    {}

    /// Replaces this information with an independent metadata copy.
    RadientTextureLoadInfoX& operator=(const RadientTextureLoadInfoX& Other)
    {
        RadientTextureLoadInfoX Copy{Other};
        Swap(Copy);
        return *this;
    }

    /// Transfers ownership and leaves Other at its default state.
    RadientTextureLoadInfoX(RadientTextureLoadInfoX&& Other) noexcept
    {
        Swap(Other);
    }

    /// Transfers ownership. Other becomes default-initialized; self-move is a no-op.
    RadientTextureLoadInfoX& operator=(RadientTextureLoadInfoX&& Other) noexcept
    {
        RadientTextureLoadInfoX Moved{std::move(Other)};
        Swap(Moved);
        return *this;
    }

    /// Copies URI; nullptr clears it. Supplied data is preserved because URI may identify it.
    RadientTextureLoadInfoX& SetURI(const Char* URI)
    {
        std::string Copy{URI != nullptr ? URI : ""};
        m_URI.swap(Copy);
        m_Desc.URI = URI != nullptr ? m_URI.c_str() : nullptr;
        return *this;
    }

    /// Copies a URI string, including an empty string.
    RadientTextureLoadInfoX& SetURI(const std::string& URI) { return SetURI(URI.c_str()); }

    /// Copies BaseURI; nullptr clears it. Source data and URI are preserved.
    RadientTextureLoadInfoX& SetBaseURI(const Char* BaseURI)
    {
        std::string Copy{BaseURI != nullptr ? BaseURI : ""};
        m_BaseURI.swap(Copy);
        m_Desc.BaseURI = BaseURI != nullptr ? m_BaseURI.c_str() : nullptr;
        return *this;
    }

    /// Copies a base URI string, including an empty string.
    RadientTextureLoadInfoX& SetBaseURI(const std::string& BaseURI) { return SetBaseURI(BaseURI.c_str()); }

    /// Retains an encoded blob and clears supplied mip data, preserving URI and options.
    /// Passing nullptr clears both data sources, allowing URI-based loading.
    RadientTextureLoadInfoX& SetDataBlob(IRadientDataBlob* pBlob) noexcept
    {
        RefCntAutoPtr<IRadientDataBlob> Blob{pBlob};
        RadientTextureDataX             PreviousData;
        m_TextureData.Swap(PreviousData);
        m_pDataBlob.swap(Blob);
        m_Desc.pTextureData = nullptr;
        m_Desc.pDataBlob    = m_pDataBlob;
        return *this;
    }

    /// Copies supplied mip metadata, retains its blobs, and clears encoded data.
    /// URI, BaseURI, and IsSRGB are preserved.
    RadientTextureLoadInfoX& SetTextureData(const RadientTextureData& Data)
    {
        RadientTextureDataX Copy{Data};
        return SetTextureData(std::move(Copy));
    }

    /// Transfers supplied mip metadata and blob references, clearing encoded data.
    /// Data becomes default-initialized; URI, BaseURI, and IsSRGB are preserved.
    RadientTextureLoadInfoX& SetTextureData(RadientTextureDataX&& Data) noexcept
    {
        RadientTextureDataX             Moved{std::move(Data)};
        RefCntAutoPtr<IRadientDataBlob> PreviousBlob;
        m_TextureData.Swap(Moved);
        m_pDataBlob.swap(PreviousBlob);
        m_Desc.pDataBlob    = nullptr;
        m_Desc.pTextureData = &m_TextureData.Get();
        return *this;
    }

    /// Selects the existing LoadTexture sRGB interpretation option.
    RadientTextureLoadInfoX& SetSRGB(Bool IsSRGB) noexcept
    {
        m_Desc.IsSRGB = IsSRGB;
        return *this;
    }

    /// Releases both data sources while preserving URI, BaseURI, and IsSRGB.
    RadientTextureLoadInfoX& ClearSourceData() noexcept { return SetDataBlob(nullptr); }

    /// Returns the C descriptor view without allocating or acquiring blob access.
    const RadientTextureLoadInfo& Get() const noexcept { return m_Desc; }

    /// Allows passing this wrapper directly to LoadTexture.
    operator const RadientTextureLoadInfo&() const noexcept { return Get(); }

    /// Releases all owned metadata and blob references and restores C descriptor defaults.
    void Clear() noexcept
    {
        RadientTextureLoadInfoX Empty;
        Swap(Empty);
    }

    /// Exchanges ownership and repairs both string and nested descriptor pointers.
    void Swap(RadientTextureLoadInfoX& Other) noexcept
    {
        std::swap(m_Desc, Other.m_Desc);
        m_URI.swap(Other.m_URI);
        m_BaseURI.swap(Other.m_BaseURI);
        m_pDataBlob.swap(Other.m_pDataBlob);
        m_TextureData.Swap(Other.m_TextureData);
        SyncDesc();
        Other.SyncDesc();
    }

private:
    void SyncDesc() noexcept
    {
        m_Desc.URI          = m_Desc.URI != nullptr ? m_URI.c_str() : nullptr;
        m_Desc.BaseURI      = m_Desc.BaseURI != nullptr ? m_BaseURI.c_str() : nullptr;
        m_Desc.pDataBlob    = m_pDataBlob;
        m_Desc.pTextureData = m_Desc.pTextureData != nullptr ? &m_TextureData.Get() : nullptr;
    }

    RadientTextureLoadInfo          m_Desc;
    std::string                     m_URI;
    std::string                     m_BaseURI;
    RefCntAutoPtr<IRadientDataBlob> m_pDataBlob;
    RadientTextureDataX             m_TextureData;
};

} // namespace Diligent
