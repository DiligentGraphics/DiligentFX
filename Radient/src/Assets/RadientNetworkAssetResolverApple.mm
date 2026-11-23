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

#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>

namespace Diligent
{
namespace AppleNetwork
{

using namespace NetworkAssetResolverDetail;

RADIENT_STATUS CanonicalizeURL(NSURL* URL, std::string& Result)
{
    if (URL == nil)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    NSURLComponents* Components = [NSURLComponents componentsWithURL:URL.standardizedURL resolvingAgainstBaseURL:YES];
    NSString*        Scheme     = Components.scheme.lowercaseString;
    if (Scheme.length == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if ([Scheme isEqualToString:@"file"])
        return RADIENT_STATUS_UNSUPPORTED;
    if (Components.host.length == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (![Scheme isEqualToString:@"http"] && ![Scheme isEqualToString:@"https"])
        return RADIENT_STATUS_UNSUPPORTED;
    if (Components.port != nil && (Components.port.longLongValue < 0 || Components.port.longLongValue > 65535))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    Components.scheme   = Scheme;
    Components.fragment = nil;
    if (([Scheme isEqualToString:@"http"] && Components.port.integerValue == 80) ||
        ([Scheme isEqualToString:@"https"] && Components.port.integerValue == 443))
        Components.port = nil;
    if (Components.percentEncodedPath.length == 0)
        Components.percentEncodedPath = @"/";
    const char* Text = Components.URL.absoluteString.UTF8String;
    if (Text == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    Result = Text;
    return RADIENT_STATUS_OK;
}

NSURL* ParseURL(const char* Text, NSURL* Base = nil)
{
    NSString* String = Text != nullptr ? [NSString stringWithUTF8String:Text] : nil;
    // Older Foundation versions reject literal spaces. Preserve existing escapes,
    // plus signs, and URL delimiters rather than percent-encoding the whole reference.
    String = [String stringByReplacingOccurrencesOfString:@" " withString:@"%20"];
    return String != nil ? [NSURL URLWithString:String relativeToURL:Base].absoluteURL : nil;
}

RADIENT_STATUS ResolveURL(const RadientAssetResolveInfo& Info, std::string& Result)
{
    NSURL* URL = ParseURL(Info.URI);
    if (URL.scheme.length == 0)
    {
        NSURL* Base = ParseURL(Info.BaseURI);
        if (Base.scheme.length == 0 || Base.host.length == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        URL = ParseURL(Info.URI, Base);
    }
    return CanonicalizeURL(URL, Result);
}

bool ValidateContentEncoding(NSHTTPURLResponse* Response, bool& Compressed)
{
    Compressed = false;
    for (NSString* Name in Response.allHeaderFields)
    {
        if ([Name caseInsensitiveCompare:@"Content-Encoding"] != NSOrderedSame)
            continue;
        NSString* Value = [Response.allHeaderFields[Name] description];
        for (NSString* Item in [Value componentsSeparatedByString:@","])
        {
            NSString* Encoding = [Item stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].lowercaseString;
            if (Encoding.length == 0 || [Encoding isEqualToString:@"identity"])
                continue;
            if (![Encoding isEqualToString:@"gzip"] && ![Encoding isEqualToString:@"x-gzip"] && ![Encoding isEqualToString:@"deflate"])
                return false;
            Compressed = true;
        }
    }
    return true;
}

// The delegate owns this state independently of the synchronous caller. Timeout
// closes publication under the mutex; late callbacks cannot touch caller storage.
class RequestState
{
public:
    RequestState(bool HeadOnly, Uint32 MaxRedirects) :
        m_HeadOnly{HeadOnly}, m_MaxRedirects{MaxRedirects} {}

    bool AcceptRedirect(NSURL* URL)
    {
        std::string Resolved;
        if (CanonicalizeURL(URL, Resolved) != RADIENT_STATUS_OK)
            return false;
        std::lock_guard<std::mutex> Lock{m_Mutex};
        if (m_Finished || m_Redirects >= m_MaxRedirects)
            return false;
        ++m_Redirects;
        return true;
    }

    RADIENT_STATUS ReceiveResponse(NSURLResponse* Response)
    {
        if (![Response isKindOfClass:NSHTTPURLResponse.class])
            return RADIENT_STATUS_FAILED;
        NSHTTPURLResponse* HTTP   = static_cast<NSHTTPURLResponse*>(Response);
        const auto         Status = GetHTTPStatus(static_cast<long>(HTTP.statusCode));
        if (Status != RADIENT_STATUS_OK)
            return Status;
        std::string URL;
        if (CanonicalizeURL(HTTP.URL, URL) != RADIENT_STATUS_OK)
            return RADIENT_STATUS_FAILED;
        bool       Compressed = false;
        const bool HasBody    = !m_HeadOnly && HTTP.statusCode != 204 && HTTP.statusCode != 205;
        if (HasBody && !ValidateContentEncoding(HTTP, Compressed))
            return RADIENT_STATUS_FAILED;

        std::lock_guard<std::mutex> Lock{m_Mutex};
        if (m_Finished)
            return RADIENT_STATUS_FAILED;
        m_URL              = std::move(URL);
        m_ResponseReceived = true;
        // Content-Length describes encoded bytes. NSURLSession delivers decoded
        // chunks, so length comparison is only meaningful for identity responses.
        m_ExpectedLength = HasBody && !Compressed ? HTTP.expectedContentLength : -1;
        return RADIENT_STATUS_OK;
    }

    bool Append(NSData* Data)
    {
        std::lock_guard<std::mutex> Lock{m_Mutex};
        if (m_Finished)
            return false;
        if (!m_HeadOnly && Data.length != 0)
        {
            const auto* Bytes = static_cast<const Uint8*>(Data.bytes);
            m_Bytes.insert(m_Bytes.end(), Bytes, Bytes + Data.length);
        }
        return true;
    }

    void Finish(RADIENT_STATUS Status)
    {
        std::lock_guard<std::mutex> Lock{m_Mutex};
        if (m_Finished)
            return;
        const bool CompleteBody = m_ExpectedLength < 0 || static_cast<Uint64>(m_ExpectedLength) == m_Bytes.size();
        m_Status                = Status == RADIENT_STATUS_OK && (!m_ResponseReceived || !CompleteBody) ? RADIENT_STATUS_FAILED : Status;
        m_Finished              = true;
        m_Completed.notify_one();
    }

    RADIENT_STATUS Wait(std::chrono::steady_clock::time_point Deadline, std::vector<Uint8>& Bytes, std::string& URL)
    {
        std::unique_lock<std::mutex> Lock{m_Mutex};
        if (!m_Completed.wait_until(Lock, Deadline, [this] { return m_Finished; }))
        {
            m_Status   = RADIENT_STATUS_FAILED;
            m_Finished = true;
        }
        if (m_Status == RADIENT_STATUS_OK)
        {
            Bytes = std::move(m_Bytes);
            URL   = std::move(m_URL);
        }
        else
        {
            std::vector<Uint8>{}.swap(m_Bytes);
        }
        return m_Status;
    }

private:
    const bool              m_HeadOnly;
    const Uint32            m_MaxRedirects;
    std::mutex              m_Mutex;
    std::condition_variable m_Completed;
    Uint32                  m_Redirects        = 0;
    bool                    m_ResponseReceived = false;
    bool                    m_Finished         = false;
    long long               m_ExpectedLength   = -1;
    RADIENT_STATUS          m_Status           = RADIENT_STATUS_FAILED;
    std::vector<Uint8>      m_Bytes;
    std::string             m_URL;
};

} // namespace AppleNetwork
} // namespace Diligent

@interface RadientNetworkSessionDelegate : NSObject <NSURLSessionDataDelegate>
{
@public
    std::shared_ptr<Diligent::AppleNetwork::RequestState> State;
}
@end

@implementation RadientNetworkSessionDelegate

- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask*)Task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)Response
                    newRequest:(NSURLRequest*)Request
             completionHandler:(void (^)(NSURLRequest*))Completion
{
    (void)Session;
    (void)Task;
    (void)Response;
    bool Accept = false;
    try
    {
        Accept = State->AcceptRedirect(Request.URL);
    }
    catch (...)
    {
    }
    if (!Accept)
        State->Finish(Diligent::RADIENT_STATUS_FAILED);
    Completion(Accept ? Request : nil);
}

- (void)URLSession:(NSURLSession*)Session dataTask:(NSURLSessionDataTask*)Task
    didReceiveResponse:(NSURLResponse*)Response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))Completion
{
    (void)Session;
    (void)Task;
    Diligent::RADIENT_STATUS Status = Diligent::RADIENT_STATUS_FAILED;
    try
    {
        Status = State->ReceiveResponse(Response);
    }
    catch (...)
    {
    }
    if (Status != Diligent::RADIENT_STATUS_OK)
        State->Finish(Status);
    Completion(Status == Diligent::RADIENT_STATUS_OK ? NSURLSessionResponseAllow : NSURLSessionResponseCancel);
}

- (void)URLSession:(NSURLSession*)Session dataTask:(NSURLSessionDataTask*)Task didReceiveData:(NSData*)Data
{
    (void)Session;
    bool Accepted = false;
    try
    {
        Accepted = State->Append(Data);
    }
    catch (...)
    {
    }
    if (!Accepted)
    {
        State->Finish(Diligent::RADIENT_STATUS_FAILED);
        [Task cancel];
    }
}

- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask*)Task didCompleteWithError:(NSError*)Error
{
    (void)Session;
    (void)Task;
    State->Finish(Error == nil ? Diligent::RADIENT_STATUS_OK : Diligent::RADIENT_STATUS_FAILED);
}

@end

namespace Diligent
{
namespace AppleNetwork
{

class SessionScope
{
public:
    explicit SessionScope(NSURLSession* Session) : m_Session{Session} {}
    ~SessionScope() { [m_Session invalidateAndCancel]; }

private:
    NSURLSession* m_Session;
};

RADIENT_STATUS Download(const char* URL, const RadientNetworkAssetResolverCreateInfo& Info, IRadientAssetData** Data)
{
    const auto                     Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{Info.RequestTimeoutMs};
    const auto                     State    = std::make_shared<RequestState>(Data == nullptr, Info.MaxRedirects);
    RadientNetworkSessionDelegate* Delegate = [[RadientNetworkSessionDelegate alloc] init];
    Delegate->State                         = State;
    NSURLSessionConfiguration* Config       = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    Config.requestCachePolicy               = NSURLRequestReloadIgnoringLocalCacheData;
    Config.URLCache                         = nil;
    Config.URLCredentialStorage             = nil;
    Config.HTTPCookieStorage                = nil;
    Config.HTTPShouldSetCookies             = NO;
    // NSURLSession exposes an inactivity timeout, not a connection-only timeout.
    Config.timeoutIntervalForRequest  = static_cast<double>(Info.ConnectTimeoutMs) / 1000.0;
    Config.timeoutIntervalForResource = static_cast<double>(Info.RequestTimeoutMs) / 1000.0;
    NSOperationQueue* Queue           = [[NSOperationQueue alloc] init];
    Queue.maxConcurrentOperationCount = 1;
    NSURLSession*        Session      = [NSURLSession sessionWithConfiguration:Config delegate:Delegate delegateQueue:Queue];
    SessionScope         Cleanup{Session};
    NSMutableURLRequest* Request = [NSMutableURLRequest requestWithURL:ParseURL(URL)];
    Request.HTTPMethod           = Data == nullptr ? @"HEAD" : @"GET";
    Request.timeoutInterval      = Config.timeoutIntervalForRequest;
    [Request setValue:@"gzip, deflate" forHTTPHeaderField:@"Accept-Encoding"];
    NSURLSessionDataTask* Task = [Session dataTaskWithRequest:Request];
    if (Task == nil)
        return RADIENT_STATUS_FAILED;
    [Task resume];

    std::vector<Uint8> Bytes;
    std::string        ResolvedURL;
    const auto         Status = State->Wait(Deadline, Bytes, ResolvedURL);
    if (Status != RADIENT_STATUS_OK || Data == nullptr)
        return Status;
    RefCntAutoPtr<IRadientAssetData> Result{MakeNewRCObj<RadientAssetDataImpl>()(std::move(Bytes), std::move(ResolvedURL))};
    *Data = Result.Detach();
    return RADIENT_STATUS_OK;
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
        @autoreleasepool
        {
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
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(IRadientAssetLocation* Location) override { return Transfer(Location, nullptr); }

    RADIENT_STATUS DILIGENT_CALL_TYPE OpenAsset(IRadientAssetLocation* Location, IRadientAssetData** Data) override
    {
        if (Data == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *Data = nullptr;
        return Transfer(Location, Data);
    }

private:
    RADIENT_STATUS Transfer(IRadientAssetLocation* Location, IRadientAssetData** Data) const
    {
        if (Location == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        @autoreleasepool
        {
            try
            {
                RefCntAutoPtr<NetworkAssetLocation> NetworkLocation{Location, IID_NetworkAssetLocation};
                if (!NetworkLocation)
                    return RADIENT_STATUS_INVALID_ARGUMENT;
                return Download(NetworkLocation->GetLocation(), m_Info, Data);
            }
            catch (...)
            {
                return RADIENT_STATUS_FAILED;
            }
        }
    }

    const RadientNetworkAssetResolverCreateInfo m_Info;
};

} // namespace AppleNetwork

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
        RefCntAutoPtr<IRadientAssetResolver> Result{MakeNewRCObj<AppleNetwork::NetworkAssetResolver>()(Info)};
        *Resolver = Result.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
