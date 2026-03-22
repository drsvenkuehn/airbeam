/// Integration test: full RAOP session against shairport-sync Docker container.
/// OPTIONS → ANNOUNCE → SETUP → RECORD, each step asserts HTTP 200.
///
/// Prerequisites: docker-compose up -d  (see docker-compose.yml)
/// Disabled until RaopSession (T047) is implemented.
#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

// TODO(T047): include "protocol/RaopSession.h"

namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t    kPort = 5000;

struct RtspConn {
    SOCKET sock = INVALID_SOCKET;

    bool Connect() {
        WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        DWORD to = 3000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(kPort);
        inet_pton(AF_INET, kHost, &addr.sin_addr);
        return ::connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    std::string SendRecv(const std::string& req) {
        send(sock, req.c_str(), (int)req.size(), 0);
        std::string buf; char ch;
        while (recv(sock, &ch, 1, 0) == 1) {
            buf += ch;
            if (buf.size() >= 4 && buf.compare(buf.size()-4,4,"\r\n\r\n")==0) break;
        }
        return buf;
    }

    int Status(const std::string& r) {
        auto p = r.find(' ');
        return p == std::string::npos ? -1 : std::stoi(r.substr(p+1,3));
    }

    ~RtspConn() { if (sock != INVALID_SOCKET) { closesocket(sock); WSACleanup(); } }
};

} // namespace

TEST(RaopShairport, DISABLED_OptionsReturns200) {
    RtspConn c;
    if (!c.Connect())
        GTEST_SKIP() << "shairport-sync container not reachable at " << kHost << ":" << kPort;

    std::ostringstream req;
    req << "OPTIONS * RTSP/1.0\r\n"
        << "CSeq: 1\r\n"
        << "User-Agent: AirBeam/1.0\r\n"
        << "Client-Instance: AABBCCDDEEFF0011\r\n\r\n";
    auto resp = c.SendRecv(req.str());
    EXPECT_EQ(c.Status(resp), 200) << "OPTIONS response:\n" << resp;
}

TEST(RaopShairport, WsaStartupSucceeds) {
    WSADATA wd;
    EXPECT_EQ(WSAStartup(MAKEWORD(2,2), &wd), 0);
    WSACleanup();
}

/// T054: Verifies startup auto-reconnect: if config.lastDevice is set to a discovered
/// receiver, AppController connects automatically within 5 seconds.
/// Disabled until full pipeline is wired and shairport-sync container is running.
TEST(RaopShairport, DISABLED_AutoReconnectWithin5Seconds) {
    // TODO(T055-T057): instantiate AppController with config.lastDevice set
    // to the shairport-sync service instance name; start AppController;
    // wait up to 5s; assert WM_RAOP_CONNECTED received.
    GTEST_SKIP() << "Requires full pipeline and Docker container";
}
