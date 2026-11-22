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

#include "Assets/RadientNetworkAssetResolverCommon.hpp"
#include "Assets/RadientAssetData.hpp"
#include "RefCntAutoPtr.hpp"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <cstdlib>

// clang-format off
// These imports do not suspend Wasm. A worker waits on a proxy context while
// fetch and its timers run on the runtime thread's normal JavaScript event loop.
EM_JS_DEPS(RadientNetworkDeps, "$UTF8ToString,$stringToNewUTF8,$setValue");

EM_JS(int, RadientResolveNetworkURL, (const char* URI, const char* BaseURI, char** Result), {
    try {
        const reference = UTF8ToString(URI);
        let url;
        try { url = new URL(reference); }
        catch (_) { url = new URL(reference, BaseURI ? UTF8ToString(BaseURI) : undefined); }
        if (url.protocol !== 'http:' && url.protocol !== 'https:') return 1;
        url.hash = "";
        const text = stringToNewUTF8(url.href);
        if (!text) return 3;
        setValue(Result, text, '*');
        return 0;
    } catch (_) { return 2; }
});

EM_JS(void, RadientStartNetworkFetch,
      (void* Context, void* Request, const char* RequestURL, int HeadOnly, unsigned ConnectTimeout, unsigned RequestTimeout, unsigned MaxRedirects), {
    // Capture native arguments before returning to the event loop. Only the
    // completion callback accesses Request again, immediately before releasing it.
    const url = UTF8ToString(RequestURL);
    (async () => {
        let status = 0, data = 0, size = 0, resolved = 0;
        let connectionTimer, requestTimer;
        let controller;
        try {
            controller = new AbortController();
            const deadline = new Promise((_, reject) => {
                const timeout = () => {
                    controller.abort();
                    reject(new Error('Network asset request timed out'));
                };
                connectionTimer = setTimeout(timeout, ConnectTimeout);
                requestTimer = setTimeout(timeout, RequestTimeout);
            });
            const response = await Promise.race([
                fetch(url, {method: HeadOnly ? 'HEAD' : 'GET', signal: controller.signal,
                            redirect: MaxRedirects === 0 ? 'error' : 'follow', credentials: 'omit'}),
                deadline
            ]);
            clearTimeout(connectionTimer);
            status = response.status;
            if (!HeadOnly && status >= 200 && status < 300 && status !== 206) {
                const bytes = await Promise.race([response.arrayBuffer(), deadline]);
                size = bytes.byteLength;
                // Browser fetch has already decoded HTTP content encodings.
                data = size ? _malloc(size) : 0;
                if (size && !data) throw new Error('Unable to allocate network asset data');
                if (size) new Uint8Array(wasmMemory.buffer, data, size).set(new Uint8Array(bytes));
                const finalURL = new URL(response.url);
                finalURL.hash = "";
                resolved = stringToNewUTF8(finalURL.href);
                if (!resolved) throw new Error('Unable to allocate resolved asset URL');
            } else if (response.body) {
                // Do not retain unconsumed error bodies and their connection resources.
                await Promise.race([response.body.cancel(), deadline]);
            }
        } catch (_) {
            controller?.abort();
            _free(data);
            _free(resolved);
            data = resolved = size = status = 0;
        } finally {
            clearTimeout(connectionTimer);
            clearTimeout(requestTimer);
            _RadientCompleteNetworkFetch(Context, Request, status, data, size, resolved);
        }
    })();
});

// clang-format on

namespace Diligent
{
namespace
{
using namespace NetworkAssetResolverDetail;

using OwnedBuffer = RadientAssetDataImpl::OwnedBuffer;
struct BrowserRequest
{
    const char*                                  URL;
    bool                                         HeadOnly;
    const RadientNetworkAssetResolverCreateInfo& Info;
    int                                          HTTPStatus = 0;
    OwnedBuffer                                  Bytes{nullptr, std::free};
    size_t                                       Size = 0;
    OwnedBuffer                                  ResolvedURL{nullptr, std::free};
};

void StartRequest(em_proxying_ctx* Context, void* Argument)
{
    const auto& Request = *static_cast<BrowserRequest*>(Argument);
    RadientStartNetworkFetch(Context, Argument, Request.URL, Request.HeadOnly,
                             Request.Info.ConnectTimeoutMs, Request.Info.RequestTimeoutMs, Request.Info.MaxRedirects);
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
            char*       URL    = nullptr;
            const int   Result = RadientResolveNetworkURL(Info.URI, Info.BaseURI, &URL);
            OwnedBuffer OwnedURL{URL, std::free};
            if (Result != 0)
                return Result == 1 ? RADIENT_STATUS_UNSUPPORTED : (Result == 2 ? RADIENT_STATUS_INVALID_ARGUMENT : RADIENT_STATUS_FAILED);
            RefCntAutoPtr<IRadientAssetLocation> Resolved{MakeNewRCObj<NetworkAssetLocation>()(std::string{URL})};
            *Location = Resolved.Detach();
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
    RADIENT_STATUS Transfer(IRadientAssetLocation* Location, IRadientAssetData** Data)
    {
        if (Location == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        // The runtime thread services fetch promises and deadlines. Waiting on it
        // would prevent completion, even in a build that supports JSPI.
        if (emscripten_is_main_runtime_thread())
            return RADIENT_STATUS_INVALID_OPERATION;
        try
        {
            RefCntAutoPtr<NetworkAssetLocation> NetworkLocation{Location, IID_NetworkAssetLocation};
            if (!NetworkLocation)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            if (m_Queue.queue == nullptr)
                return RADIENT_STATUS_FAILED;
            BrowserRequest Request{NetworkLocation->GetLocation(), Data == nullptr, m_Info};
            if (!emscripten_proxy_sync_with_ctx(m_Queue.queue, emscripten_main_runtime_thread_id(), StartRequest, &Request))
                return RADIENT_STATUS_FAILED;
            const auto Status = GetHTTPStatus(Request.HTTPStatus);
            if (Status != RADIENT_STATUS_OK || Data == nullptr)
                return Status;
            // Fetch already copied into Wasm memory; transfer that allocation
            // directly to the asset instead of allocating and copying it again.
            RefCntAutoPtr<IRadientAssetData> Result{MakeNewRCObj<RadientAssetDataImpl>()(
                std::move(Request.Bytes), Request.Size, std::string{static_cast<const char*>(Request.ResolvedURL.get())})};
            *Data = Result.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
        }
    }

    // The caller retains the resolver until the synchronous operation completes.
    // No request or proxy context remains when this queue is destroyed.
    emscripten::ProxyingQueue                   m_Queue;
    const RadientNetworkAssetResolverCreateInfo m_Info;
};
} // namespace

RADIENT_STATUS CreateRadientNetworkAssetResolver(const RadientNetworkAssetResolverCreateInfo& Info, IRadientAssetResolver** Resolver)
{
    if (Resolver == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *Resolver = nullptr;
    if (!NetworkAssetResolverDetail::IsValidCreateInfo(Info))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    try
    {
        RefCntAutoPtr<IRadientAssetResolver> Result{MakeNewRCObj<NetworkAssetResolver>()(Info)};
        *Resolver = Result.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void RadientCompleteNetworkFetch(em_proxying_ctx* Context, void* Argument, int Status, void* Bytes, size_t Size, char* ResolvedURL)
{
    auto& Request      = *static_cast<BrowserRequest*>(Argument);
    Request.HTTPStatus = Status;
    Request.Bytes.reset(Bytes);
    Request.Size = Size;
    Request.ResolvedURL.reset(ResolvedURL);
    // This publishes the result and wakes the worker. Do not access Request afterward.
    emscripten_proxy_finish(Context);
}

} // namespace Diligent
