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

#include "BasicTypes.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent
{
namespace NetworkAssetResolverDetail
{
// curl can report a successful transfer even when its content decoder never
// reached the end of a compressed stream. Decode the received body ourselves so
// truncated streams (including missing trailers) cannot publish partial assets.
inline bool InflateContent(std::vector<Uint8>& Bytes, int WindowBits, bool Gzip)
{
    z_stream Stream{};
    if (inflateInit2(&Stream, WindowBits) != Z_OK)
        return false;
    struct Cleanup
    {
        z_stream& Stream;
        ~Cleanup() { inflateEnd(&Stream); }
    } End{Stream};

    std::vector<Uint8>       Decoded;
    std::array<Uint8, 16384> Buffer;
    size_t                   InputOffset = 0;
    int                      Status      = Z_OK;
    do
    {
        if (Stream.avail_in == 0 && InputOffset < Bytes.size())
        {
            Stream.avail_in = static_cast<uInt>((std::min)(Bytes.size() - InputOffset, size_t{(std::numeric_limits<uInt>::max)()}));
            Stream.next_in  = Bytes.data() + InputOffset;
            InputOffset += Stream.avail_in;
        }
        Stream.next_out  = Buffer.data();
        Stream.avail_out = static_cast<uInt>(Buffer.size());
        Status           = inflate(&Stream, Z_NO_FLUSH);
        if (Status != Z_OK && Status != Z_STREAM_END)
            return false;
        Decoded.insert(Decoded.end(), Buffer.data(), Buffer.data() + Buffer.size() - Stream.avail_out);
        const bool HasInput = Stream.avail_in != 0 || InputOffset < Bytes.size();
        if (Status == Z_STREAM_END && HasInput)
        {
            // A gzip representation may contain concatenated members. Every member
            // must finish successfully; trailing garbage is not silently ignored.
            if (!Gzip || inflateReset2(&Stream, WindowBits) != Z_OK)
                return false;
            Status = Z_OK;
        }
    } while (Status != Z_STREAM_END);
    Bytes.swap(Decoded);
    return true;
}

inline bool DecodeContentEncoding(std::string_view Encoding, std::vector<Uint8>& Bytes)
{
    if (Encoding == "identity")
        return true;
    if (Encoding == "gzip" || Encoding == "x-gzip")
        return InflateContent(Bytes, MAX_WBITS + 16, true);
    if (Encoding != "deflate")
        return false;

    // HTTP deflate uses a zlib wrapper, but some servers send raw deflate.
    // Preserve curl's compatibility with those servers without accepting an
    // incomplete stream in either representation.
    const bool HasZlibHeader = Bytes.size() >= 2 && (Bytes[0] & 0x0f) == Z_DEFLATED &&
        (Bytes[0] >> 4) <= 7 && ((Uint32{Bytes[0]} << 8) | Bytes[1]) % 31 == 0;
    return InflateContent(Bytes, HasZlibHeader ? MAX_WBITS : -MAX_WBITS, false);
}

inline void AppendContentEncodings(std::string_view Value, std::vector<std::string>& Encodings)
{
    while (!Value.empty())
    {
        const size_t Comma = Value.find(',');
        const auto   Token = Value.substr(0, Comma);
        const size_t Begin = Token.find_first_not_of(" \t");
        if (Begin != std::string_view::npos)
        {
            std::string Encoding{Token.substr(Begin, Token.find_last_not_of(" \t") - Begin + 1)};
            for (char& Ch : Encoding)
                if (Ch >= 'A' && Ch <= 'Z')
                    Ch = static_cast<char>(Ch + ('a' - 'A'));
            Encodings.emplace_back(std::move(Encoding));
        }
        Value = Comma == std::string_view::npos ? std::string_view{} : Value.substr(Comma + 1);
    }
}


inline bool DecodeContentEncodings(const std::vector<std::string>& Encodings, std::vector<Uint8>& Bytes)
{
    for (auto It = Encodings.rbegin(); It != Encodings.rend(); ++It)
        if (!DecodeContentEncoding(*It, Bytes))
            return false;
    return true;
}
} // namespace NetworkAssetResolverDetail
} // namespace Diligent
