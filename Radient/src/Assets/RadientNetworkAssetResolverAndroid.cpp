// Copyright 2026 Diligent Graphics LLC
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#include "RadientAssetResolver.h"
#include "Assets/RadientAssetData.hpp"
#include "Assets/RadientNetworkAssetResolverCommon.hpp"
#include "Assets/RadientHTTPContent.hpp"
#include "RefCntAutoPtr.hpp"

#include <chrono>
#include <charconv>
#include <cstring>
#include <limits>
#include <optional>

namespace Diligent
{
namespace
{
using namespace NetworkAssetResolverDetail;

// Java exceptions are consumed at this boundary and converted into Radient status.
struct JavaError
{};

void CheckJava(JNIEnv* Env)
{
    if (Env->ExceptionCheck())
    {
        Env->ExceptionClear();
        throw JavaError{};
    }
}

template <typename T>
T Checked(JNIEnv* Env, T Value)
{
    CheckJava(Env);
    return Value;
}

class ThreadEnvironment
{
public:
    explicit ThreadEnvironment(JavaVM* VM) : m_VM{VM}
    {
        const jint Status = VM->GetEnv(reinterpret_cast<void**>(&m_Env), JNI_VERSION_1_6);
        if (Status == JNI_EDETACHED)
        {
            if (VM->AttachCurrentThread(&m_Env, nullptr) != JNI_OK)
                throw JavaError{};
            m_Attached = true;
        }
        else if (Status != JNI_OK)
            throw JavaError{};
    }
    ~ThreadEnvironment()
    {
        if (m_Attached)
            m_VM->DetachCurrentThread();
    }
    JNIEnv* Get() const { return m_Env; }
    ThreadEnvironment(const ThreadEnvironment&)            = delete;
    ThreadEnvironment& operator=(const ThreadEnvironment&) = delete;

private:
    JavaVM* const m_VM;
    JNIEnv*       m_Env      = nullptr;
    bool          m_Attached = false;
};

class LocalFrame
{
public:
    explicit LocalFrame(JNIEnv* Env) : m_Env{Env}
    {
        if (Checked(Env, Env->PushLocalFrame(64)) != JNI_OK)
            throw JavaError{};
    }
    ~LocalFrame() { m_Env->PopLocalFrame(nullptr); }
    LocalFrame(const LocalFrame&)            = delete;
    LocalFrame& operator=(const LocalFrame&) = delete;

private:
    JNIEnv* const m_Env;
};

// Only bootstrap classes are used: no Activity, application class loader, or
// packaged Java helper is needed. Metadata is local to each resolver operation.
class JavaAPI
{
public:
    explicit JavaAPI(JNIEnv* Env) : m_Env{Env} {}
    JavaAPI(const JavaAPI&)            = delete;
    JavaAPI& operator=(const JavaAPI&) = delete;

    // Returned Java handles belong to the current local frame. Callers retain
    // their existing frame scopes; only this class invokes Java methods.
    LocalFrame  MakeLocalFrame() { return LocalFrame{m_Env}; }
    jobject     CreateURI(const char* URI) { return NewObject(m_URI, m_URICtor, Text(URI)); }
    jobject     CreateURL(const char* URL) { return NewObject(m_URL, m_URLCtor, Text(URL)); }
    jobject     CreateURL(jobject Base, const char* Reference) { return NewObject(m_URL, m_RelativeURLCtor, Base, Text(Reference)); }
    jobject     ToURI(jobject URL) { return CallObject(URL, m_ToURI); }
    std::string GetScheme(jobject URI) { return StringProperty(URI, m_Scheme); }
    std::string GetAuthority(jobject URI) { return StringProperty(URI, m_Authority); }
    std::string GetHost(jobject URI) { return StringProperty(URI, m_Host); }
    std::string GetPath(jobject URI) { return StringProperty(URI, m_Path); }
    jint        GetPort(jobject URI) { return CallIntMethod(URI, m_Port); }

    std::optional<std::string> GetQuery(jobject URI)
    {
        LocalFrame Frame{m_Env};
        jobject    Query = CallObject(URI, m_Query);
        // An empty query still contributes '?' to the resource identity.
        if (Query == nullptr)
            return std::nullopt;
        return Text(Query);
    }

    jobject OpenConnection(const char* URL)
    {
        jobject Connection = CallObject(CreateURL(URL), m_Open);
        if (!m_Env->IsInstanceOf(Connection, m_HTTP))
            throw JavaError{};
        return Connection;
    }

    void Disconnect(jobject Connection) noexcept
    {
        m_Env->CallVoidMethod(Connection, m_Disconnect);
        // Cleanup must not throw while unwinding a failed request.
        if (m_Env->ExceptionCheck())
            m_Env->ExceptionClear();
    }

    void SetRequestMethod(jobject Connection, const char* Method) { CallVoidMethod(Connection, m_SetMethod, Text(Method)); }
    void SetFollowRedirects(jobject Connection, bool Enable) { CallVoidMethod(Connection, m_SetRedirects, static_cast<jboolean>(Enable)); }
    void SetUseCaches(jobject Connection, bool Enable) { CallVoidMethod(Connection, m_SetCache, static_cast<jboolean>(Enable)); }
    void SetConnectTimeout(jobject Connection, jint Timeout) { CallVoidMethod(Connection, m_SetConnectTimeout, Timeout); }
    void SetReadTimeout(jobject Connection, jint Timeout) { CallVoidMethod(Connection, m_SetReadTimeout, Timeout); }
    void SetRequestHeader(jobject Connection, const char* Name, const char* Value) { CallVoidMethod(Connection, m_SetHeader, Text(Name), Text(Value)); }
    jint GetResponseCode(jobject Connection) { return CallIntMethod(Connection, m_ResponseCode); }

    std::string GetHeader(jobject Connection, const char* Name)
    {
        LocalFrame Frame{m_Env};
        return Text(CallObject(Connection, m_NamedHeader, Text(Name)));
    }

    bool GetHeader(jobject Connection, jint Index, std::string& Name, std::string& Value)
    {
        LocalFrame Frame{m_Env};
        jobject    JavaName  = CallObject(Connection, m_HeaderName, Index);
        jobject    JavaValue = CallObject(Connection, m_Header, Index);
        Name                 = Text(JavaName);
        Value                = Text(JavaValue);
        // The status line has no name; an empty field value is also valid.
        return JavaName != nullptr || JavaValue != nullptr;
    }

    jobject    GetInputStream(jobject Connection) { return CallObject(Connection, m_Input); }
    jbyteArray CreateByteBuffer(jsize Size) { return Checked(m_Env, m_Env->NewByteArray(Size)); }
    jint       Read(jobject Stream, jbyteArray Buffer) { return CallIntMethod(Stream, m_Read, Buffer); }
    void       Close(jobject Stream) { CallVoidMethod(Stream, m_Close); }

    void CopyBytes(jbyteArray Buffer, jint Count, Uint8* Destination)
    {
        m_Env->GetByteArrayRegion(Buffer, 0, Count, reinterpret_cast<jbyte*>(Destination));
        CheckJava(m_Env);
    }

private:
    template <typename... Args>
    jobject NewObject(jclass Type, jmethodID Constructor, Args... Arguments)
    {
        return Checked(m_Env, m_Env->NewObject(Type, Constructor, Arguments...));
    }
    template <typename... Args>
    jobject CallObject(jobject Object, jmethodID Method, Args... Arguments)
    {
        return Checked(m_Env, m_Env->CallObjectMethod(Object, Method, Arguments...));
    }
    template <typename... Args>
    jint CallIntMethod(jobject Object, jmethodID Method, Args... Arguments)
    {
        return Checked(m_Env, m_Env->CallIntMethod(Object, Method, Arguments...));
    }
    template <typename... Args>
    void CallVoidMethod(jobject Object, jmethodID Method, Args... Arguments)
    {
        m_Env->CallVoidMethod(Object, Method, Arguments...);
        CheckJava(m_Env);
    }

    std::string StringProperty(jobject Object, jmethodID Method)
    {
        LocalFrame Frame{m_Env};
        return Text(CallObject(Object, Method));
    }

    jclass    Class(const char* Name) { return Checked(m_Env, m_Env->FindClass(Name)); }
    jmethodID Method(jclass Type, const char* Name, const char* Signature)
    {
        return Checked(m_Env, m_Env->GetMethodID(Type, Name, Signature));
    }

    jstring Text(const char* Value)
    {
        const size_t Size = std::strlen(Value);
        if (Size > static_cast<size_t>((std::numeric_limits<jsize>::max)()))
            throw JavaError{};
        jbyteArray Bytes = Checked(m_Env, m_Env->NewByteArray(static_cast<jsize>(Size)));
        m_Env->SetByteArrayRegion(Bytes, 0, static_cast<jsize>(Size), reinterpret_cast<const jbyte*>(Value));
        CheckJava(m_Env);
        jstring Result = static_cast<jstring>(Checked(m_Env, m_Env->NewObject(m_String, m_StringCtor, Bytes, m_UTF8)));
        m_Env->DeleteLocalRef(Bytes);
        return Result;
    }

    std::string Text(jobject Value)
    {
        if (Value == nullptr)
            return {};
        auto        Bytes = static_cast<jbyteArray>(Checked(m_Env, m_Env->CallObjectMethod(Value, m_StringBytes, m_UTF8)));
        const jsize Size  = m_Env->GetArrayLength(Bytes);
        std::string Result(static_cast<size_t>(Size), '\0');
        m_Env->GetByteArrayRegion(Bytes, 0, Size, reinterpret_cast<jbyte*>(Result.data()));
        CheckJava(m_Env);
        m_Env->DeleteLocalRef(Bytes);
        return Result;
    }

    JNIEnv* const m_Env;
    jclass        m_String            = Class("java/lang/String");
    jmethodID     m_StringCtor        = Method(m_String, "<init>", "([BLjava/lang/String;)V");
    jmethodID     m_StringBytes       = Method(m_String, "getBytes", "(Ljava/lang/String;)[B");
    jstring       m_UTF8              = Checked(m_Env, m_Env->NewStringUTF("UTF-8"));
    jclass        m_URI               = Class("java/net/URI");
    jmethodID     m_URICtor           = Method(m_URI, "<init>", "(Ljava/lang/String;)V");
    jmethodID     m_Scheme            = Method(m_URI, "getScheme", "()Ljava/lang/String;");
    jmethodID     m_Authority         = Method(m_URI, "getRawAuthority", "()Ljava/lang/String;");
    jmethodID     m_Host              = Method(m_URI, "getHost", "()Ljava/lang/String;");
    jmethodID     m_Port              = Method(m_URI, "getPort", "()I");
    jmethodID     m_Path              = Method(m_URI, "getRawPath", "()Ljava/lang/String;");
    jmethodID     m_Query             = Method(m_URI, "getRawQuery", "()Ljava/lang/String;");
    jclass        m_URL               = Class("java/net/URL");
    jmethodID     m_URLCtor           = Method(m_URL, "<init>", "(Ljava/lang/String;)V");
    jmethodID     m_RelativeURLCtor   = Method(m_URL, "<init>", "(Ljava/net/URL;Ljava/lang/String;)V");
    jmethodID     m_ToURI             = Method(m_URL, "toURI", "()Ljava/net/URI;");
    jmethodID     m_Open              = Method(m_URL, "openConnection", "()Ljava/net/URLConnection;");
    jclass        m_HTTP              = Class("java/net/HttpURLConnection");
    jmethodID     m_SetMethod         = Method(m_HTTP, "setRequestMethod", "(Ljava/lang/String;)V");
    jmethodID     m_SetRedirects      = Method(m_HTTP, "setInstanceFollowRedirects", "(Z)V");
    jmethodID     m_SetCache          = Method(m_HTTP, "setUseCaches", "(Z)V");
    jmethodID     m_SetHeader         = Method(m_HTTP, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID     m_SetConnectTimeout = Method(m_HTTP, "setConnectTimeout", "(I)V");
    jmethodID     m_SetReadTimeout    = Method(m_HTTP, "setReadTimeout", "(I)V");
    jmethodID     m_ResponseCode      = Method(m_HTTP, "getResponseCode", "()I");
    jmethodID     m_Header            = Method(m_HTTP, "getHeaderField", "(I)Ljava/lang/String;");
    jmethodID     m_HeaderName        = Method(m_HTTP, "getHeaderFieldKey", "(I)Ljava/lang/String;");
    jmethodID     m_NamedHeader       = Method(m_HTTP, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
    jmethodID     m_Input             = Method(m_HTTP, "getInputStream", "()Ljava/io/InputStream;");
    jmethodID     m_Disconnect        = Method(m_HTTP, "disconnect", "()V");
    jclass        m_Stream            = Class("java/io/InputStream");
    jmethodID     m_Read              = Method(m_Stream, "read", "([B)I");
    jmethodID     m_Close             = Method(m_Stream, "close", "()V");
};

std::string EscapeReference(const char* Text)
{
    std::string    Result;
    constexpr char Hex[] = "0123456789ABCDEF";
    for (const auto* P = reinterpret_cast<const unsigned char*>(Text); *P; ++P)
    {
        if (*P < 0x20 || *P == 0x7f)
            throw JavaError{};
        if (*P == ' ' || *P >= 0x80)
        {
            Result += '%';
            Result += Hex[*P >> 4];
            Result += Hex[*P & 15];
        }
        else
            Result += static_cast<char>(*P);
    }
    return Result;
}

RADIENT_STATUS CanonicalizeURL(JavaAPI& API, jobject URI, std::string& Result)
{
    std::string Scheme = API.GetScheme(URI);
    for (char& C : Scheme)
        if (C >= 'A' && C <= 'Z')
            C = static_cast<char>(C + ('a' - 'A'));
    if (Scheme == "file")
        return RADIENT_STATUS_UNSUPPORTED;
    if (Scheme.empty() || API.GetHost(URI).empty())
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (Scheme != "http" && Scheme != "https")
        return RADIENT_STATUS_UNSUPPORTED;
    const jint Port = API.GetPort(URI);
    if (Port < -1 || Port > 65535)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    std::string Authority = API.GetAuthority(URI);
    if ((Scheme == "http" && Port == 80) || (Scheme == "https" && Port == 443))
        Authority.erase(Authority.rfind(':'));
    const auto Path  = API.GetPath(URI);
    Result           = Scheme + "://" + Authority + (Path.empty() ? "/" : Path);
    const auto Query = API.GetQuery(URI);
    if (Query)
        Result += "?" + *Query;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ResolveURL(JavaAPI& API, const RadientAssetResolveInfo& Info, std::string& Result)
{
    std::string Reference = EscapeReference(Info.URI);
    jobject     URI       = API.CreateURI(Reference.c_str());
    jobject     URL       = nullptr;
    if (API.GetScheme(URI).empty())
    {
        if (Info.BaseURI == nullptr || Info.BaseURI[0] == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        std::string Base    = EscapeReference(Info.BaseURI);
        jobject     BaseURL = API.CreateURL(Base.c_str());
        URL                 = API.CreateURL(BaseURL, Reference.c_str());
    }
    else
        URL = API.CreateURL(Reference.c_str());
    // Android's URL resolver removes dot segments without collapsing repeated
    // slashes. URI.normalize would change resource identities such as /a//b.
    URI = API.ToURI(URL);
    return CanonicalizeURL(API, URI, Result);
}

class RequestDeadline
{
public:
    explicit RequestDeadline(Uint32 Timeout) : m_End{std::chrono::steady_clock::now() + std::chrono::milliseconds{Timeout}} {}
    jint Remaining() const
    {
        const auto MS = std::chrono::duration_cast<std::chrono::milliseconds>(m_End - std::chrono::steady_clock::now()).count();
        if (MS <= 0)
            throw JavaError{};
        return static_cast<jint>(MS);
    }

private:
    const std::chrono::steady_clock::time_point m_End;
};

class Connection
{
public:
    Connection(JavaAPI& API, const char* URL) : m_API{API}, m_Object{API.OpenConnection(URL)} {}
    ~Connection() { m_API.Disconnect(m_Object); }
    jobject Get() const { return m_Object; }
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

private:
    JavaAPI&      m_API;
    const jobject m_Object;
};

void Configure(JavaAPI& API, jobject Connection, const RadientNetworkAssetResolverCreateInfo& Info, bool HeadOnly, const RequestDeadline& Deadline)
{
    API.SetRequestMethod(Connection, HeadOnly ? "HEAD" : "GET");
    API.SetFollowRedirects(Connection, false);
    API.SetUseCaches(Connection, false);
    API.SetConnectTimeout(Connection, (std::min)(static_cast<jint>(Info.ConnectTimeoutMs), Deadline.Remaining()));
    // Android uses the socket read timeout for TLS negotiation as well as response
    // reads. ConnectTimeoutMs covers TCP establishment, not the TLS handshake.
    API.SetReadTimeout(Connection, Deadline.Remaining());
    // Explicit negotiation disables Android's transparent gzip decoding. Reuse
    // the same complete-stream validation as curl, including repeated encodings.
    API.SetRequestHeader(Connection, "Accept-Encoding", "gzip, deflate");
}

std::vector<std::string> ReadEncodings(JavaAPI& API, jobject Connection)
{
    std::vector<std::string> Encodings;
    for (jint Index = 0;; ++Index)
    {
        std::string Key, Value;
        if (!API.GetHeader(Connection, Index, Key, Value))
            break;
        for (char& C : Key)
            if (C >= 'A' && C <= 'Z')
                C = static_cast<char>(C + ('a' - 'A'));
        if (Key == "content-encoding")
            AppendContentEncodings(Value, Encodings);
    }
    return Encodings;
}

void ReadBody(JavaAPI& API, jobject Connection, const RequestDeadline& Deadline, std::vector<Uint8>& Bytes)
{
    const auto Length = API.GetHeader(Connection, "Content-Length");
    jobject    Stream = API.GetInputStream(Connection);
    jbyteArray Buffer = API.CreateByteBuffer(16384);
    for (;;)
    {
        API.SetReadTimeout(Connection, Deadline.Remaining());
        const jint Count = API.Read(Stream, Buffer);
        Deadline.Remaining();
        if (Count < 0)
            break;
        if (Count > 16384 || static_cast<size_t>(Count) > Bytes.max_size() - Bytes.size())
            throw JavaError{};
        const size_t Offset = Bytes.size();
        Bytes.resize(Offset + static_cast<size_t>(Count));
        if (Count != 0)
            API.CopyBytes(Buffer, Count, Bytes.data() + Offset);
    }
    if (!Length.empty())
    {
        Uint64     Expected = 0;
        const auto Parsed   = std::from_chars(Length.data(), Length.data() + Length.size(), Expected);
        if (Parsed.ec != std::errc{} || Parsed.ptr != Length.data() + Length.size() || Expected != Bytes.size())
            throw JavaError{};
    }
    API.Close(Stream);
}

RADIENT_STATUS Download(JavaAPI& API, const RadientNetworkAssetResolverCreateInfo& Info, std::string URL, IRadientAssetData** Data)
{
    const RequestDeadline Deadline{Info.RequestTimeoutMs};
    for (Uint32 Redirects = 0;; ++Redirects)
    {
        auto       Frame = API.MakeLocalFrame();
        Connection Request{API, URL.c_str()};
        Configure(API, Request.Get(), Info, Data == nullptr, Deadline);
        const jint Code = API.GetResponseCode(Request.Get());
        Deadline.Remaining();
        if (Code == 301 || Code == 302 || Code == 303 || Code == 307 || Code == 308)
        {
            const auto  Location = API.GetHeader(Request.Get(), "Location");
            std::string Next;
            if (Location.empty() || Redirects >= Info.MaxRedirects || ResolveURL(API, {Location.c_str(), URL.c_str()}, Next) != RADIENT_STATUS_OK)
                return RADIENT_STATUS_FAILED;
            URL = std::move(Next);
            continue;
        }
        const auto Status = GetHTTPStatus(Code);
        if (Status != RADIENT_STATUS_OK || Data == nullptr)
            return Status;
        std::vector<Uint8> Bytes;
        if (Code != 204 && Code != 205)
        {
            const auto Encodings = ReadEncodings(API, Request.Get());
            ReadBody(API, Request.Get(), Deadline, Bytes);
            if (!DecodeContentEncodings(Encodings, Bytes))
                return RADIENT_STATUS_FAILED;
        }
        Deadline.Remaining();
        RefCntAutoPtr<IRadientAssetData> Result{MakeNewRCObj<RadientAssetDataImpl>()(std::move(Bytes), std::move(URL))};
        *Data = Result.Detach();
        return RADIENT_STATUS_OK;
    }
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
            ThreadEnvironment Thread{m_Info.pJavaVM};
            LocalFrame        Frame{Thread.Get()};
            JavaAPI           API{Thread.Get()};
            std::string       URL;
            const auto        Status = ResolveURL(API, Info, URL);
            if (Status != RADIENT_STATUS_OK)
                return Status;
            RefCntAutoPtr<IRadientAssetLocation> Result{MakeNewRCObj<NetworkAssetLocation>()(std::move(URL))};
            *Location = Result.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (const JavaError&)
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
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
        try
        {
            RefCntAutoPtr<NetworkAssetLocation> NetworkLocation{Location, IID_NetworkAssetLocation};
            if (!NetworkLocation)
                return RADIENT_STATUS_INVALID_ARGUMENT;
            ThreadEnvironment Thread{m_Info.pJavaVM};
            LocalFrame        Frame{Thread.Get()};
            JavaAPI           API{Thread.Get()};
            return Download(API, m_Info, NetworkLocation->GetLocation(), Data);
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
    if (Info.pJavaVM == nullptr || Info.ConnectTimeoutMs == 0 || Info.ConnectTimeoutMs > MaxTimeout ||
        Info.RequestTimeoutMs == 0 || Info.RequestTimeoutMs > MaxTimeout || Info.MaxRedirects > MaxTimeout)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    try
    {
        ThreadEnvironment                    Thread{Info.pJavaVM};
        LocalFrame                           Frame{Thread.Get()};
        JavaAPI                              API{Thread.Get()};
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
