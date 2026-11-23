// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
#include "RadientAssetResolver.h"
#include "RefCntAutoPtr.hpp"
#include "gtest/gtest.h"

#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

JavaVM* GetNetworkTestJavaVM();

namespace
{
using namespace Diligent;

class Socket
{
public:
    explicit Socket(int FD) :
        m_FD{FD}
    {}

    ~Socket()
    {
        if (m_FD >= 0) close(m_FD);
    }
    int Get() const { return m_FD; }

    // clang-format off
    Socket           (const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    // clang-format on

private:
    int m_FD;
};

// Accept TCP but never send a TLS ServerHello. No certificate or TLS library is
// needed to exercise the client's handshake timeout. Cleanup also works after
// fatal assertions, and the server closes after five seconds if timeout breaks.
class StalledTLSServer
{
public:
    StalledTLSServer() :
        m_Listener{socket(AF_INET, SOCK_STREAM, 0)}
    {
        sockaddr_in Address{};
        Address.sin_family      = AF_INET;
        Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t Size          = sizeof(Address);
        if (m_Listener.Get() < 0 ||
            bind(m_Listener.Get(), reinterpret_cast<sockaddr*>(&Address), Size) != 0 ||
            getsockname(m_Listener.Get(), reinterpret_cast<sockaddr*>(&Address), &Size) != 0 ||
            listen(m_Listener.Get(), 1) != 0)
        {
            throw std::runtime_error{"Failed to start stalled TLS fixture"};
        }
        m_URL = "https://127.0.0.1:" + std::to_string(ntohs(Address.sin_port)) + "/asset";

        m_Thread = std::thread{[this] {
            Serve();
        }};
    }
    ~StalledTLSServer()
    {
        {
            std::lock_guard<std::mutex> Lock{m_Mutex};
            m_Stop = true;
        }
        m_Gate.notify_all();
        m_Thread.join();
    }
    const char* GetURL() const { return m_URL.c_str(); }
    bool        WasAccepted() const { return m_Accepted.load(); }

private:
    void Serve()
    {
        pollfd Poll{m_Listener.Get(), POLLIN, 0};
        while (!m_Stop.load())
        {
            if (poll(&Poll, 1, 100) <= 0 || !(Poll.revents & POLLIN))
                continue;
            Socket Client{accept(m_Listener.Get(), nullptr, nullptr)};
            if (Client.Get() < 0)
                return;
            m_Accepted = true;
            std::unique_lock<std::mutex> Lock{m_Mutex};
            m_Gate.wait_for(Lock, std::chrono::seconds{5}, [this] { return m_Stop.load(); });
            return;
        }
    }

    Socket                  m_Listener;
    std::string             m_URL;
    std::mutex              m_Mutex;
    std::condition_variable m_Gate;
    std::atomic<bool>       m_Stop{false};
    std::atomic<bool>       m_Accepted{false};
    std::thread             m_Thread;
};

TEST(RadientNetworkAndroidTest, TLSHandshakeUsesRequestBudget)
{
    for (bool HeadOnly : {false, true})
    {
        SCOPED_TRACE(HeadOnly ? "HEAD" : "GET");
        StalledTLSServer                      Server;
        RadientNetworkAssetResolverCreateInfo Info;
        Info.pJavaVM          = GetNetworkTestJavaVM();
        Info.ConnectTimeoutMs = 100;
        Info.RequestTimeoutMs = 1200;
        RefCntAutoPtr<IRadientAssetResolver> Resolver;
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Resolver), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({Server.GetURL()}, &Location), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetData> Data;
        const auto                       Start   = std::chrono::steady_clock::now();
        const auto                       Status  = HeadOnly ? Resolver->CheckAsset(Location) : Resolver->OpenAsset(Location, &Data);
        const auto                       Elapsed = std::chrono::steady_clock::now() - Start;
        EXPECT_EQ(Status, RADIENT_STATUS_FAILED);
        EXPECT_EQ(Data, nullptr);
        ASSERT_TRUE(Server.WasAccepted());
        // Broad margins distinguish the documented request/read budget from the
        // 100 ms TCP timeout and from the fixture's five-second forced close.
        EXPECT_GE(Elapsed, std::chrono::milliseconds{600});
        EXPECT_LT(Elapsed, std::chrono::milliseconds{4500});
    }
}
} // namespace
