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

#include "RadientAssetResolver.h"


#include "Assets/RadientAssetData.hpp"
#include "Assets/RadientNetworkAssetResolverCommon.hpp"
#include "RefCntAutoPtr.hpp"

#include <curl/curl.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <string_view>

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Diligent
{
namespace
{

using namespace NetworkAssetResolverDetail;

// Each request owns its easy handle. Global initialization happens before any
// transfer and stays alive until process teardown, rather than per request.
struct CurlRuntime
{
    const CURLcode Status = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CurlRuntime()
    {
        if (Status == CURLE_OK)
            curl_global_cleanup();
    }
};

using CurlURL      = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
using CurlString   = std::unique_ptr<char, decltype(&curl_free)>;
using CurlTransfer = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

RADIENT_STATUS ResolveURL(const RadientAssetResolveInfo& Info, std::string& Result)
{
    CurlURL URL{curl_url(), curl_url_cleanup};
    if (!URL)
        return RADIENT_STATUS_FAILED;

    // Try an absolute URL first: an explicit URL overrides BaseURI.
    constexpr unsigned Flags = CURLU_NON_SUPPORT_SCHEME | CURLU_ALLOW_SPACE | CURLU_URLENCODE;
    if (curl_url_set(URL.get(), CURLUPART_URL, Info.URI, Flags) != CURLUE_OK)
    {
        if (Info.BaseURI == nullptr || Info.BaseURI[0] == 0 ||
            curl_url_set(URL.get(), CURLUPART_URL, Info.BaseURI, Flags) != CURLUE_OK ||
            curl_url_set(URL.get(), CURLUPART_URL, Info.URI, Flags) != CURLUE_OK)
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    char* RawScheme = nullptr;
    if (curl_url_get(URL.get(), CURLUPART_SCHEME, &RawScheme, 0) != CURLUE_OK)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    CurlString Scheme{RawScheme, curl_free};
    if (std::strcmp(Scheme.get(), "http") != 0 && std::strcmp(Scheme.get(), "https") != 0)
        return RADIENT_STATUS_UNSUPPORTED;

    // Fragments select content within a resource; they are not sent to the server
    // and must not create duplicate locations for the same downloaded bytes.
    if (curl_url_set(URL.get(), CURLUPART_FRAGMENT, nullptr, 0) != CURLUE_OK)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    char* RawURL = nullptr;
    if (curl_url_get(URL.get(), CURLUPART_URL, &RawURL, CURLU_NO_DEFAULT_PORT) != CURLUE_OK)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    CurlString Text{RawURL, curl_free};
    Result = Text.get();
    return RADIENT_STATUS_OK;
}

size_t AppendBytes(char* Bytes, size_t Size, size_t Count, void* UserData) noexcept
{
    // libcurl invokes this through C: allocation exceptions must not escape.
    try
    {
        auto& Data = *static_cast<std::vector<Uint8>*>(UserData);
        if (Size != 0 && Count > (Data.max_size() - Data.size()) / Size)
            return 0;
        const size_t ByteCount = Size * Count;
        if (ByteCount != 0)
            Data.insert(Data.end(), Bytes, Bytes + ByteCount);
        return ByteCount;
    }
    catch (...)
    {
        return 0;
    }
}

// curl can report a successful transfer even when its content decoder never
// reached the end of a compressed stream. Decode the received body ourselves so
// truncated streams (including missing trailers) cannot publish partial assets.
bool InflateContent(std::vector<Uint8>& Bytes, int WindowBits, bool Gzip)
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

bool DecodeContentEncoding(std::string_view Encoding, std::vector<Uint8>& Bytes)
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

void AppendContentEncodings(std::string_view Value, std::vector<std::string>& Encodings)
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

bool DecodeHTTPContent(CURL* Handle, std::vector<Uint8>& Bytes)
{
    curl_header* Header = nullptr;
    const auto   Result = curl_easy_header(Handle, "Content-Encoding", 0, CURLH_HEADER, -1, &Header);
    if (Result == CURLHE_MISSING || Result == CURLHE_NOHEADERS)
        return true;
    if (Result != CURLHE_OK)
        return false;

    // Use only the final response's headers, and undo stacked encodings in reverse
    // application order, including when they are split across repeated headers.
    const size_t             HeaderCount = Header->amount;
    std::vector<std::string> Encodings;
    for (size_t Index = 0; Index < HeaderCount; ++Index)
    {
        if (curl_easy_header(Handle, "Content-Encoding", Index, CURLH_HEADER, -1, &Header) != CURLHE_OK)
            return false;
        AppendContentEncodings(Header->value, Encodings);
    }
    for (auto It = Encodings.rbegin(); It != Encodings.rend(); ++It)
        if (!DecodeContentEncoding(*It, Bytes))
            return false;
    return true;
}

class NetworkAssetResolver final : public ObjectBase<IRadientAssetResolver>
{
public:
    using TBase = ObjectBase<IRadientAssetResolver>;
    NetworkAssetResolver(IReferenceCounters* Counters, const RadientNetworkAssetResolverCreateInfo& Info) :
        TBase{Counters}, m_Info{Info} {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetResolver, TBase)

    RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAssetLocation(const RadientAssetResolveInfo& Info, IRadientAssetLocation** Location) override
    {
        if (Location == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *Location = nullptr;
        if (Info.URI == nullptr || Info.URI[0] == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        try
        {
            std::string URL;
            const auto  Status = ResolveURL(Info, URL);
            if (Status != RADIENT_STATUS_OK)
                return Status;
            RefCntAutoPtr<IRadientAssetLocation> Result{MakeNewRCObj<NetworkAssetLocation>()(std::move(URL))};
            *Location = Result.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
        }
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(IRadientAssetLocation* Location) override
    {
        return Transfer(Location, nullptr);
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE OpenAsset(IRadientAssetLocation* Location, IRadientAssetData** Data) override
    {
        if (Data == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *Data = nullptr;
        return Transfer(Location, Data);
    }

private:
    bool ConfigureTransfer(CURL* Handle, const char* URL, bool HeadOnly, std::vector<Uint8>& Bytes) const
    {
        // Keep signal handling out of worker threads and restrict both initial
        // requests and redirects to HTTP(S), even when libcurl supports files.
        return curl_easy_setopt(Handle, CURLOPT_URL, URL) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_MAXREDIRS, static_cast<long>(m_Info.MaxRedirects)) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_Info.ConnectTimeoutMs)) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_TIMEOUT_MS, static_cast<long>(m_Info.RequestTimeoutMs)) == CURLE_OK &&
            // Advertise only encodings whose completion we validate below.
            curl_easy_setopt(Handle, CURLOPT_ACCEPT_ENCODING, "gzip, deflate") == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_HTTP_CONTENT_DECODING, 0L) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_FAILONERROR, 1L) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_NOBODY, HeadOnly ? 1L : 0L) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, &AppendBytes) == CURLE_OK &&
            curl_easy_setopt(Handle, CURLOPT_WRITEDATA, &Bytes) == CURLE_OK;
    }

    RADIENT_STATUS Transfer(IRadientAssetLocation* Location, IRadientAssetData** Data) const
    {
        if (Location == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        try
        {
            RefCntAutoPtr<NetworkAssetLocation> NetworkLocation{Location, IID_NetworkAssetLocation};
            if (!NetworkLocation)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            CurlTransfer Request{curl_easy_init(), curl_easy_cleanup};
            if (!Request)
                return RADIENT_STATUS_FAILED;
            std::vector<Uint8> Bytes;
            CURL*              Handle = Request.get();
            if (!ConfigureTransfer(Handle, NetworkLocation->GetLocation(), Data == nullptr, Bytes))
                return RADIENT_STATUS_FAILED;

            const CURLcode TransferStatus = curl_easy_perform(Handle);
            long           HTTPCode       = 0;
            if (curl_easy_getinfo(Handle, CURLINFO_RESPONSE_CODE, &HTTPCode) != CURLE_OK)
                return RADIENT_STATUS_FAILED;
            if (TransferStatus != CURLE_OK && TransferStatus != CURLE_HTTP_RETURNED_ERROR)
                return RADIENT_STATUS_FAILED;
            const auto Status = GetHTTPStatus(HTTPCode);
            if (Status != RADIENT_STATUS_OK || Data == nullptr)
                return Status;

            // No-content responses have no representation body to decompress.
            if (HTTPCode != 204 && HTTPCode != 205 && !DecodeHTTPContent(Handle, Bytes))
                return RADIENT_STATUS_FAILED;

            char* EffectiveURL = nullptr;
            if (curl_easy_getinfo(Handle, CURLINFO_EFFECTIVE_URL, &EffectiveURL) != CURLE_OK || EffectiveURL == nullptr)
                return RADIENT_STATUS_FAILED;
            std::string ResolvedURL;
            if (ResolveURL({EffectiveURL}, ResolvedURL) != RADIENT_STATUS_OK)
                return RADIENT_STATUS_FAILED;
            RefCntAutoPtr<IRadientAssetData> Result{MakeNewRCObj<RadientAssetDataImpl>()(std::move(Bytes), std::move(ResolvedURL))};
            *Data = Result.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
        }
    }

    const RadientNetworkAssetResolverCreateInfo m_Info;
};

} // namespace

RADIENT_STATUS CreateRadientNetworkAssetResolver(const RadientNetworkAssetResolverCreateInfo& Info, IRadientAssetResolver** Resolver)
{
    if (Resolver == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *Resolver                   = nullptr;
    constexpr Uint32 MaxTimeout = static_cast<Uint32>((std::numeric_limits<Int32>::max)());
    if (Info.ConnectTimeoutMs == 0 || Info.ConnectTimeoutMs > MaxTimeout ||
        Info.RequestTimeoutMs == 0 || Info.RequestTimeoutMs > MaxTimeout || Info.MaxRedirects > MaxTimeout)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    try
    {
        static CurlRuntime Runtime;
        if (Runtime.Status != CURLE_OK)
            return RADIENT_STATUS_FAILED;
        RefCntAutoPtr<IRadientAssetResolver> Result{MakeNewRCObj<NetworkAssetResolver>()(Info)};
        *Resolver = Result.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}
} // namespace Diligent
