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

// Include the server before platform headers to avoid Windows socket-header conflicts.
#include <httplib.h>
#include <zlib.h>

// httplib includes sys/mman.h on Linux, whose MAP_TYPE macro conflicts with our enum.
#ifdef MAP_TYPE
#    undef MAP_TYPE
#endif

#include "RadientAssetResolver.h"
#include "RadientEngine.h"
#include "Assets/RadientAssetResolver.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientImportedDocument.hpp"
#include "RadientAssetTestHelpers.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "ThreadPool.hpp"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <thread>
#include <stdexcept>

using namespace Diligent;
using namespace Diligent::Testing;

#if RADIENT_ANDROID_NETWORK_TEST
// Supplied by the JNI test entry point, never by production resolver code.
JavaVM* GetNetworkTestJavaVM();
#endif

namespace
{

RadientNetworkAssetResolverCreateInfo NetworkCreateInfo()
{
    RadientNetworkAssetResolverCreateInfo Info;
#if RADIENT_ANDROID_NETWORK_TEST
    Info.pJavaVM = GetNetworkTestJavaVM();
#endif
    return Info;
}

std::string CompressHTTPData(const std::string& Data, int WindowBits = MAX_WBITS + 16)
{
    z_stream Stream{};
    if (deflateInit2(&Stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, WindowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error{"Failed to initialize test compression encoder"};
    struct Cleanup
    {
        z_stream& Stream;
        ~Cleanup() { deflateEnd(&Stream); }
    } End{Stream};
    std::string Compressed(deflateBound(&Stream, static_cast<uLong>(Data.size())), '\0');
    Stream.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(Data.data()));
    Stream.avail_in  = static_cast<uInt>(Data.size());
    Stream.next_out  = reinterpret_cast<Bytef*>(&Compressed[0]);
    Stream.avail_out = static_cast<uInt>(Compressed.size());
    if (deflate(&Stream, Z_FINISH) != Z_STREAM_END)
        throw std::runtime_error{"Failed to encode test compression response"};
    Compressed.resize(Stream.total_out);
    return Compressed;
}

std::string MakeGLB(std::string JSON)
{
    JSON.append((4 - JSON.size() % 4) % 4, ' ');
    std::string Result;
    const auto  AppendWord = [&Result](Uint32 Value) {
        for (Uint32 Byte = 0; Byte < 4; ++Byte)
            Result.push_back(static_cast<char>((Value >> (8 * Byte)) & 0xff));
    };
    AppendWord(0x46546c67); // glTF magic, followed by the GLB 2.0 header and JSON chunk.
    AppendWord(2);
    AppendWord(static_cast<Uint32>(20 + JSON.size()));
    AppendWord(static_cast<Uint32>(JSON.size()));
    AppendWord(0x4e4f534a);
    Result += JSON;
    return Result;
}

class RadientNetworkAssetResolverTest : public ::testing::Test
{
protected:
    void ImportDependencies(IRadientAssetResolver* SelectedResolver);

    void SetUp() override
    {
#if PLATFORM_ANDROID && !RADIENT_ANDROID_NETWORK_TEST
        GTEST_SKIP() << "Run Android networking tests through the JVM test entry point";
#endif
        Server.new_task_queue = [] {
            return new httplib::ThreadPool(4);
        };
        // Idle platform-pooled connections must not occupy all four server
        // workers while the concurrent-transfer test opens eight connections.
        Server.set_keep_alive_max_count(1);
        AddResponseRoutes();
        AddCompressionRoutes();
        AddGLTFRoutes();
        const int Port = Server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(Port, 0);
        BaseURL      = "http://127.0.0.1:" + std::to_string(Port);
        ServerThread = std::thread{
            [this] {
                Server.listen_after_bind();
            }};
        Server.wait_until_ready();
        ASSERT_TRUE(Server.is_running());
        auto Info             = NetworkCreateInfo();
        Info.RequestTimeoutMs = 5000;
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Resolver), RADIENT_STATUS_OK);
    }

    ~RadientNetworkAssetResolverTest() override
    {
        // Release handlers before stopping the server, including after fatal assertions.
        {
            std::lock_guard<std::mutex> Lock{GateMutex};
            Released = true;
        }
        Gate.notify_all();
        Server.stop();
        if (ServerThread.joinable())
            ServerThread.join();
    }

    RADIENT_STATUS Open(const char* Path, RefCntAutoPtr<IRadientAssetData>& Data)
    {
        const std::string URL = BaseURL + Path;
        return OpenAsset(Resolver, {URL.c_str()}, Data.GetAddressOfEmpty());
    }

    static std::string CompressionPayload()
    {
        std::string Data(4096, '\0');
        for (size_t i = 0; i < Data.size(); ++i)
            Data[i] = static_cast<char>((i * 37 + i / 7) & 255);
        return Data;
    }

    void AddEncodedRoute(const char* Path, std::string Body, const char* Encoding)
    {
        Server.Get(Path, [Body = std::move(Body), Encoding](const httplib::Request&, httplib::Response& Response) {
            Response.set_header("Content-Encoding", Encoding);
            Response.set_content(Body, "application/octet-stream");
        });
    }

    void AddCompressionRoutes()
    {
        Server.Get("/no-content/(204|205)", [](const httplib::Request& Request, httplib::Response& Response) {
            Response.status = std::stoi(Request.matches[1]);
            Response.set_header("Content-Encoding", "gzip");
        });
        const auto Data = CompressionPayload();
        const auto Gzip = CompressHTTPData(Data);
        AddEncodedRoute("/complete-gzip", Gzip, "GZip");
        AddEncodedRoute("/deflate", CompressHTTPData(Data, MAX_WBITS), "deflate");
        AddEncodedRoute("/raw-deflate", CompressHTTPData(Data, -MAX_WBITS), "deflate");
        AddEncodedRoute("/empty-gzip", CompressHTTPData(""), "gzip");
        AddEncodedRoute("/large-gzip", CompressHTTPData(std::string(100000, 'a')), "gzip");
        AddEncodedRoute("/gzip-members", CompressHTTPData(Data.substr(0, 123)) + CompressHTTPData("") + CompressHTTPData(Data.substr(123)), "gzip");
        AddEncodedRoute("/stacked", CompressHTTPData(CompressHTTPData(Data, MAX_WBITS)), "deflate, gzip");
        const auto DoubleGzip = CompressHTTPData(Gzip);
        Server.Get("/repeated-encoding", [DoubleGzip](const httplib::Request&, httplib::Response& Response) {
            // Identical encodings make header order irrelevant while still requiring
            // both fields to be decoded. The /stacked case covers mixed encoding order.
            Response.set_header("Content-Encoding", "gzip");
            Response.set_header("Content-Encoding", "gzip");
            Response.set_content(DoubleGzip, "application/octet-stream");
        });
        Server.Get("/encoded-redirect", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_redirect("/complete-gzip");
            Response.set_header("Content-Encoding", "unsupported");
        });
        // Content-Length matches each deliberately incomplete compressed body.
        // Transport completeness alone must not be mistaken for decoder completion.
        Server.Get("/truncated-gzip/(\\d+)", [Gzip](const httplib::Request& Request, httplib::Response& Response) {
            const size_t Lengths[] = {0, 1, 9, Gzip.size() / 2, Gzip.size() - 8, Gzip.size() - 1};
            const size_t Index     = static_cast<size_t>(std::stoul(Request.matches[1]));
            Response.set_header("Content-Encoding", "gzip");
            if (Index >= sizeof(Lengths) / sizeof(Lengths[0]))
            {
                Response.status = 400;
                return;
            }
            Response.set_content(Gzip.substr(0, Lengths[Index]), "application/octet-stream");
        });
        auto Deflate = CompressHTTPData(Data, MAX_WBITS);
        Deflate.pop_back();
        AddEncodedRoute("/truncated-deflate", Deflate, "deflate");
        auto Raw = CompressHTTPData(Data, -MAX_WBITS);
        Raw.pop_back();
        AddEncodedRoute("/truncated-raw", Raw, "deflate");
        AddEncodedRoute("/truncated-member", Gzip + Gzip.substr(0, Gzip.size() / 2), "gzip");
        AddEncodedRoute("/gzip-garbage", Gzip + "garbage", "gzip");
    }

    void AddResponseRoutes()
    {
        const auto Gzip = CompressHTTPData(std::string{"A\0B\xff", 4});
        Server.Get("/gzip", [Gzip](const httplib::Request&, httplib::Response& Response) {
            Response.set_header("Content-Encoding", "gzip");
            Response.set_content(Gzip, "application/octet-stream");
        });
        Server.Get("/bad-gzip", [Gzip](const httplib::Request&, httplib::Response& Response) {
            auto Corrupt = Gzip;
            Corrupt[Corrupt.size() - 8] ^= 1; // Corrupt the checksum after decoded bytes have been produced.
            Response.set_header("Content-Encoding", "gzip");
            Response.set_content(Corrupt, "application/octet-stream");
        });
        Server.Get("/unknown-encoding", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_header("Content-Encoding", "unsupported");
            Response.set_content("encoded data", "application/octet-stream");
        });
        Server.Get("/bytes", [this](const httplib::Request& Request, httplib::Response& Response) {
            ++Requests;
            if (Request.method == "HEAD")
                ++HeadRequests;
            Response.set_content(std::string{"A\0B\xff", 4}, "application/octet-stream");
        });
        Server.Get("/empty", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_content("", "application/octet-stream");
        });
        Server.Get("/redirect", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_redirect("/directory/data.bin");
            Response.set_content("redirect body must not become asset data", "text/plain");
        });
        Server.Get("/directory/data.bin", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_content("final bytes", "application/octet-stream");
        });
        Server.Get("/directory/child.bin", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_content("child bytes", "application/octet-stream");
        });
        Server.Get("/loop", [](const httplib::Request&, httplib::Response& Response) { Response.set_redirect("/loop"); });
        Server.Get("/file-redirect", [](const httplib::Request&, httplib::Response& Response) { Response.set_redirect("file:///should-not-be-opened"); });
        Server.Get("/status/(\\d+)", [](const httplib::Request& Request, httplib::Response& Response) {
            Response.status = std::stoi(Request.matches[1]);
            Response.set_content("error body", "text/plain");
        });
        Server.Get("/truncated", [](const httplib::Request&, httplib::Response& Response) {
            Response.set_content_provider(64, "application/octet-stream",
                                          [](size_t, size_t, httplib::DataSink& Sink) {
                                              Sink.write("part", 4);
                                              return false; // Close before satisfying Content-Length.
                                          });
        });
        Server.Get("/blocked", [this](const httplib::Request&, httplib::Response& Response) {
            std::unique_lock<std::mutex> Lock{GateMutex};
            ++BlockedRequests;
            Gate.wait(Lock, [this] { return Released; });
            Response.set_content("released", "text/plain");
        });
        Server.Get("/partial-blocked", [this](const httplib::Request&, httplib::Response& Response) {
            Response.set_content_provider(8, "application/octet-stream",
                                          [this](size_t, size_t, httplib::DataSink& Sink) {
                                              if (!Sink.write("part", 4))
                                                  return false;
                                              std::unique_lock<std::mutex> Lock{GateMutex};
                                              ++BlockedRequests;
                                              Gate.wait(Lock, [this] { return Released; });
                                              return Sink.write("rest", 4);
                                          });
        });
    }

    void AddGLTFRoutes()
    {
        const char* Document       = R"({"asset":{"version":"2.0"},
        "scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
        "buffers":[{"uri":"mesh.bin","byteLength":36}],
        "bufferViews":[{"buffer":0,"byteLength":36}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
        "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
        "textures":[{"source":0}],"images":[{"uri":"image.tga"}]})";
        const auto  BinaryDocument = MakeGLB(Document);
        Server.Get("/entry.glb", [](const httplib::Request&, httplib::Response& Response) { Response.set_redirect("/model/123"); });
        Server.Get("/model/123", [BinaryDocument](const httplib::Request&, httplib::Response& Response) { Response.set_content(BinaryDocument, "application/octet-stream"); });
        const auto CompressedDocument = CompressHTTPData(Document);
        Server.Get("/compressed.gltf", [](const httplib::Request&, httplib::Response& Response) { Response.set_redirect("/model/compressed.gltf"); });
        Server.Get("/model/compressed.gltf", [CompressedDocument](const httplib::Request&, httplib::Response& Response) {
            Response.set_header("Content-Encoding", "gzip");
            Response.set_content(CompressedDocument, "model/gltf+json");
        });
        Server.Get("/entry.gltf", [](const httplib::Request&, httplib::Response& Response) { Response.set_redirect("/model/scene.gltf"); });
        Server.Get("/model/scene.gltf", [Document](const httplib::Request&, httplib::Response& Response) { Response.set_content(Document, "model/gltf+json"); });
        Server.Get("/model/mesh.bin", [](const httplib::Request&, httplib::Response& Response) {
            const float Positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            Response.set_content(reinterpret_cast<const char*>(Positions), sizeof(Positions), "application/octet-stream");
        });
        Server.Get("/model/image.tga", [](const httplib::Request&, httplib::Response& Response) {
            // One uncompressed, white 24-bit pixel; no encoder dependency is needed.
            std::string Image(21, '\0');
            Image[2]  = 2;
            Image[12] = 1;
            Image[14] = 1;
            Image[16] = 24;
            Image[18] = Image[19] = Image[20] = static_cast<char>(255);
            Response.set_content(Image, "image/x-tga");
        });
    }

    httplib::Server                      Server;
    std::string                          BaseURL;
    RefCntAutoPtr<IRadientAssetResolver> Resolver;
    std::atomic_uint                     Requests{0};
    std::atomic_uint                     HeadRequests{0};
    std::atomic_uint                     BlockedRequests{0};
    std::mutex                           GateMutex;
    std::condition_variable              Gate;
    bool                                 Released = false;
    std::thread                          ServerThread;
};

TEST_F(RadientNetworkAssetResolverTest, ResolvesURLsWithoutIO)
{
    const std::string Base = BaseURL + "/models/scene.gltf?old=1";
    struct Case
    {
        const char* URI;
        const char* Expected;
    };
    const Case Cases[] = {
        {"../bytes", "/bytes"},
        {"../../bytes", "/bytes"},
        {"textures//image.png", "/models/textures//image.png"},
        {"/bytes#part", "/bytes"},
        {"?variant=mesh%3Amain", "/models/scene.gltf?variant=mesh%3Amain"},
        {"#node", "/models/scene.gltf?old=1"},
        {"textures/a%20b.png", "/models/textures/a%20b.png"},
        {"textures/a+b.png", "/models/textures/a+b.png"},
        {"textures/a%2520b.png", "/models/textures/a%2520b.png"},
        {"textures/a b.png", "/models/textures/a%20b.png"},
        {"./image:red.png", "/models/image:red.png"},
    };
    for (const auto& Item : Cases)
    {
        SCOPED_TRACE(Item.URI);
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({Item.URI, Base.c_str()}, &Location), RADIENT_STATUS_OK);
        EXPECT_EQ(Location->GetLocation(), BaseURL + Item.Expected);
    }
    RefCntAutoPtr<IRadientAssetLocation> Absolute;
    ASSERT_EQ(Resolver->ResolveAssetLocation({"https://example.com:443/a/../b#part", "invalid base"}, &Absolute), RADIENT_STATUS_OK);
    EXPECT_STREQ(Absolute->GetLocation(), "https://example.com/b");
    EXPECT_EQ(Requests.load(), 0u);
}

TEST_F(RadientNetworkAssetResolverTest, ReadsBytesAndRetainsStorage)
{
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(Open("/bytes", Data), RADIENT_STATUS_OK);
    ASSERT_EQ(Data->GetSize(), 4u);
    EXPECT_EQ(std::memcmp(Data->GetData(), "A\0B\xff", 4), 0);
    EXPECT_EQ(Data->GetResolvedURI(), BaseURL + "/bytes");
    RefCntAutoPtr<IRadientAssetData> Empty;
    ASSERT_EQ(Open("/empty", Empty), RADIENT_STATUS_OK);
    EXPECT_EQ(Empty->GetSize(), 0u);
    Resolver.Release();
    EXPECT_EQ(std::memcmp(Data->GetData(), "A\0B\xff", 4), 0);
}

TEST_F(RadientNetworkAssetResolverTest, DecodesCompressedResponses)
{
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(Open("/gzip", Data), RADIENT_STATUS_OK);
    ASSERT_EQ(Data->GetSize(), 4u);
    EXPECT_EQ(std::memcmp(Data->GetData(), "A\0B\xff", 4), 0);
    Data.Release();
    EXPECT_EQ(Open("/unknown-encoding", Data), RADIENT_STATUS_FAILED);
    EXPECT_EQ(Data, nullptr);
}

TEST_F(RadientNetworkAssetResolverTest, DecodesStandardCompressedResponses)
{
    const auto Expected = CompressionPayload();
    for (const char* Path : {"/complete-gzip", "/deflate"})
    {
        SCOPED_TRACE(Path);
        RefCntAutoPtr<IRadientAssetData> Data;
        ASSERT_EQ(Open(Path, Data), RADIENT_STATUS_OK);
        ASSERT_EQ(Data->GetSize(), Expected.size());
        EXPECT_EQ(std::memcmp(Data->GetData(), Expected.data(), Expected.size()), 0);
    }
    for (const char* Path : {"/empty-gzip", "/no-content/204", "/no-content/205"})
    {
        SCOPED_TRACE(Path);
        RefCntAutoPtr<IRadientAssetData> Empty;
        ASSERT_EQ(Open(Path, Empty), RADIENT_STATUS_OK);
        EXPECT_EQ(Empty->GetSize(), 0u);
    }
    RefCntAutoPtr<IRadientAssetData> Large;
    ASSERT_EQ(Open("/large-gzip", Large), RADIENT_STATUS_OK);
    ASSERT_EQ(Large->GetSize(), 100000u);
    EXPECT_EQ(std::string(static_cast<const char*>(Large->GetData()), static_cast<size_t>(Large->GetSize())), std::string(100000, 'a'));
}

TEST_F(RadientNetworkAssetResolverTest, RequiresCompleteCompressedStreams)
{
#if PLATFORM_APPLE
    GTEST_SKIP() << "NSURLSession owns decompression; curl-specific stream validation is not available";
#else
    const auto Expected = CompressionPayload();
    for (const char* Path : {"/raw-deflate", "/gzip-members", "/stacked", "/repeated-encoding", "/encoded-redirect"})
    {
        SCOPED_TRACE(Path);
        RefCntAutoPtr<IRadientAssetData> Data;
        ASSERT_EQ(Open(Path, Data), RADIENT_STATUS_OK);
        ASSERT_EQ(Data->GetSize(), Expected.size());
        EXPECT_EQ(std::memcmp(Data->GetData(), Expected.data(), Expected.size()), 0);
    }
    for (const char* Path : {"/bad-gzip", "/truncated-gzip/0", "/truncated-gzip/1", "/truncated-gzip/2", "/truncated-gzip/3", "/truncated-gzip/4", "/truncated-gzip/5",
                             "/truncated-deflate", "/truncated-raw", "/truncated-member", "/gzip-garbage"})
    {
        SCOPED_TRACE(Path);
        RefCntAutoPtr<IRadientAssetData> Data;
        EXPECT_EQ(Open(Path, Data), RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
    }
#endif
}

TEST_F(RadientNetworkAssetResolverTest, ChecksWithHead)
{
    const std::string URL = BaseURL + "/bytes";
    EXPECT_EQ(CheckAsset(Resolver, {URL.c_str()}), RADIENT_STATUS_OK);
    EXPECT_EQ(HeadRequests.load(), 1u);
    EXPECT_EQ(Requests.load(), 1u);
}

TEST_F(RadientNetworkAssetResolverTest, RedirectsPreserveRequestAndUseFinalDependencyBase)
{
    const std::string                    URL = BaseURL + "/redirect";
    RefCntAutoPtr<IRadientAssetLocation> Location;
    ASSERT_EQ(Resolver->ResolveAssetLocation({URL.c_str()}, &Location), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(Resolver->OpenAsset(Location, &Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Location->GetLocation(), URL);
    EXPECT_EQ(Data->GetResolvedURI(), BaseURL + "/directory/data.bin");
    EXPECT_EQ(std::string(static_cast<const char*>(Data->GetData()), Data->GetSize()), "final bytes");
    RefCntAutoPtr<IRadientAssetData> Child;
    ASSERT_EQ(OpenAsset(Resolver, {"child.bin", Data->GetResolvedURI()}, &Child), RADIENT_STATUS_OK);
    EXPECT_EQ(std::string(static_cast<const char*>(Child->GetData()), Child->GetSize()), "child bytes");
}

TEST_F(RadientNetworkAssetResolverTest, RejectsErrorsWithoutPublishingPartialData)
{
    for (const auto Code : {404, 410, 403, 500, 206, 302})
    {
        SCOPED_TRACE(Code);
        const std::string                Path = "/status/" + std::to_string(Code);
        RefCntAutoPtr<IRadientAssetData> Data;
        EXPECT_EQ(Open(Path.c_str(), Data), Code == 404 || Code == 410 ? RADIENT_STATUS_NOT_FOUND : RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
    }
    for (const char* Path : {"/loop", "/file-redirect", "/truncated"})
    {
        SCOPED_TRACE(Path);
        RefCntAutoPtr<IRadientAssetData> Data;
        EXPECT_EQ(Open(Path, Data), RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
    }
}

TEST_F(RadientNetworkAssetResolverTest, ValidatesArgumentsAndLocationType)
{
    EXPECT_EQ(CreateRadientNetworkAssetResolver({}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
#if RADIENT_ANDROID_NETWORK_TEST
    RefCntAutoPtr<IRadientAssetResolver> MissingVM;
    EXPECT_EQ(CreateRadientNetworkAssetResolver({}, &MissingVM), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(MissingVM, nullptr);
#endif
    for (Uint32 Value : {0u, (std::numeric_limits<Uint32>::max)()})
    {
        auto Info             = NetworkCreateInfo();
        Info.RequestTimeoutMs = Value;
        RefCntAutoPtr<IRadientAssetResolver> Invalid;
        EXPECT_EQ(CreateRadientNetworkAssetResolver(Info, &Invalid), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(Invalid, nullptr);
    }
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(Resolver->ResolveAssetLocation({nullptr}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"bytes"}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"http://"}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"file:///bytes"}, &Location), RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"ftp://example.com/bytes", BaseURL.c_str()}, &Location), RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"memory:bytes", BaseURL.c_str()}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"http://", BaseURL.c_str()}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"https://example.com"}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<IRadientAssetResolver> FileResolver;
    ASSERT_EQ(CreateRadientFilesystemAssetResolver({}, &FileResolver), RADIENT_STATUS_OK);
    ASSERT_EQ(FileResolver->ResolveAssetLocation({"bytes"}, &Location), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetData> Data;
    EXPECT_EQ(Resolver->OpenAsset(Location, &Data), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->CheckAsset(Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->OpenAsset(nullptr, &Data), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->OpenAsset(nullptr, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

#if RADIENT_ANDROID_NETWORK_TEST
TEST_F(RadientNetworkAssetResolverTest, PreservesJavaThreadAttachmentAndClearsExceptions)
{
    JavaVM* VM  = GetNetworkTestJavaVM();
    JNIEnv* Env = nullptr;
    ASSERT_EQ(VM->GetEnv(reinterpret_cast<void**>(&Env), JNI_VERSION_1_6), JNI_OK);
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(Resolver->ResolveAssetLocation({"http://[invalid"}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(Env->ExceptionCheck());
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(Open("/bytes", Data), RADIENT_STATUS_OK);
    JNIEnv* After = nullptr;
    EXPECT_EQ(VM->GetEnv(reinterpret_cast<void**>(&After), JNI_VERSION_1_6), JNI_OK);
    EXPECT_EQ(After, Env);
    auto Worker = std::async(std::launch::async, [this, VM] {
        JNIEnv* WorkerEnv = nullptr;
        if (VM->GetEnv(reinterpret_cast<void**>(&WorkerEnv), JNI_VERSION_1_6) != JNI_EDETACHED)
            return false;
        RefCntAutoPtr<IRadientAssetData> Bytes;
        const bool                       Loaded = Open("/bytes", Bytes) == RADIENT_STATUS_OK;
        return Loaded && VM->GetEnv(reinterpret_cast<void**>(&WorkerEnv), JNI_VERSION_1_6) == JNI_EDETACHED;
    });
    EXPECT_TRUE(Worker.get());
}
#endif

TEST_F(RadientNetworkAssetResolverTest, TimesOutAndRecovers)
{
    auto Info             = NetworkCreateInfo();
    Info.RequestTimeoutMs = 500;
    Resolver.Release();
    ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Resolver), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetData> Data;
    for (const char* Path : {"/blocked", "/partial-blocked"})
    {
        SCOPED_TRACE(Path);
        EXPECT_EQ(Open(Path, Data), RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
    }
    EXPECT_EQ(BlockedRequests.load(), 2u);
    EXPECT_EQ(Open("/bytes", Data), RADIENT_STATUS_OK);
    // Fixture teardown releases the blocked handler even if an assertion fails.
}

TEST_F(RadientNetworkAssetResolverTest, EnforcesRedirectLimit)
{
    for (Uint32 Limit : {0u, 1u})
    {
        SCOPED_TRACE(Limit);
        auto Info         = NetworkCreateInfo();
        Info.MaxRedirects = Limit;
        Resolver.Release();
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Resolver), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetData> Data;
        EXPECT_EQ(Open("/redirect", Data), Limit == 0 ? RADIENT_STATUS_FAILED : RADIENT_STATUS_OK);
        EXPECT_EQ(Data != nullptr, Limit != 0);
        Data.Release();
        EXPECT_EQ(Open("/loop", Data), RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
    }
}

TEST_F(RadientNetworkAssetResolverTest, SupportsConcurrentTransfers)
{
    std::vector<std::future<bool>> Tasks;
    for (unsigned i = 0; i < 8; ++i)
        Tasks.emplace_back(std::async(std::launch::async, [this] {
            RefCntAutoPtr<IRadientAssetData> Data;
            return Open("/bytes", Data) == RADIENT_STATUS_OK && Data->GetSize() == 4 &&
                std::memcmp(Data->GetData(), "A\0B\xff", 4) == 0;
        }));
    for (auto& Task : Tasks)
        EXPECT_TRUE(Task.get());
    EXPECT_EQ(Requests.load(), 8u);
}

void RadientNetworkAssetResolverTest::ImportDependencies(IRadientAssetResolver* SelectedResolver)
{
    struct PoolScope
    {
        RefCntAutoPtr<IThreadPool> Pool = CreateThreadPool(ThreadPoolCreateInfo{2});
        ~PoolScope()
        {
            Pool->WaitForAllTasks();
            Pool->StopThreads();
        }
    } Work;
    RadientEngineCreateInfo Info;
    Info.pThreadPool           = Work.Pool;
    Info.Assets.pAssetResolver = SelectedResolver;
    RefCntAutoPtr<IRadientEngine> Engine;
    ASSERT_EQ(CreateRadientEngine(Info, &Engine), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetManager> Manager;
    ASSERT_EQ(Engine->GetAssetManager(&Manager), RADIENT_STATUS_OK);
    const struct
    {
        const char* Entry;
        const char* Final;
    } Cases[] = {
        {"/entry.gltf", "/model/scene.gltf"},
        {"/entry.glb", "/model/123"},
        {"/compressed.gltf", "/model/compressed.gltf"},
    };
    for (const auto& Item : Cases)
    {
        SCOPED_TRACE(Item.Entry);
        const std::string    URL = BaseURL + Item.Entry;
        RadientSceneLoadInfo LoadInfo;
        LoadInfo.URI = URL.c_str();
        RefCntAutoPtr<IRadientSceneAsset> Scene;
        const auto                        Status = Manager->LoadScene(LoadInfo, &Scene);
        ASSERT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
        ASSERT_EQ(WaitForAssetLoad(Scene, *Work.Pool), RADIENT_STATUS_OK);
        Work.Pool->WaitForAllTasks();
        const auto* Imported = RadientAssetManagerImpl::GetImportedScene(Scene);
        ASSERT_NE(Imported, nullptr);
        ASSERT_EQ(Imported->Meshes.size(), 1u);
        ASSERT_EQ(Imported->Textures.size(), 1u);
        EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Imported->Meshes[0]), RADIENT_STATUS_OK);
        EXPECT_EQ(Imported->Meshes[0]->GetDesc().pGeometries[0].VertexCount, 3u);
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Imported->Textures[0]), RADIENT_STATUS_OK);
        EXPECT_EQ(Imported->Textures[0]->GetDesc().Width, 1u);
        EXPECT_EQ(Imported->Textures[0]->GetReference().BaseURI, BaseURL + Item.Final);
    }
    Manager->Stop(nullptr);
}

TEST_F(RadientNetworkAssetResolverTest, ImportsGLTFDependenciesAfterRedirect)
{
    ImportDependencies(Resolver);
}

#if PLATFORM_ANDROID
TEST_F(RadientNetworkAssetResolverTest, DefaultEngineDoesNotRequireJavaVM)
{
    RefCntAutoPtr<IRadientEngine> Engine;
    ASSERT_EQ(CreateRadientEngine({}, &Engine), RADIENT_STATUS_OK);
    auto                                 DefaultResolver = CreateDefaultRadientAssetResolver();
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(DefaultResolver->ResolveAssetLocation({"asset.bin"}, &Location), RADIENT_STATUS_OK);
    EXPECT_EQ(DefaultResolver->ResolveAssetLocation({"https://example.test/asset.bin"}, &Location), RADIENT_STATUS_UNSUPPORTED);
}
#else
TEST_F(RadientNetworkAssetResolverTest, DefaultEngineImportsNetworkDependencies)
{
    ImportDependencies(nullptr);
}
#endif

} // namespace
