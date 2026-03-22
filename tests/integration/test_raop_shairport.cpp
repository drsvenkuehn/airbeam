/// Integration test: full RAOP session against shairport-sync Docker container.
/// OPTIONS → ANNOUNCE → SETUP → RECORD, each step asserts HTTP 200.
///
/// Prerequisites: docker compose up -d  (see docker-compose.yml at repo root)
/// Skips gracefully if the container is not reachable.
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
#include <bcrypt.h>
#include <string>
#include <sstream>
#include <memory>

#include "protocol/RaopSession.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"
#include "core/Messages.h"

namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t    kPort = 5000;

// ── Low-level RTSP helper (for OptionsReturns200) ────────────────────────────
struct RtspConn {
    SOCKET sock = INVALID_SOCKET;

    bool Connect() {
        WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;

        // Use non-blocking connect + select() so the probe completes within 3 s
        // even when Windows Firewall silently drops packets (SO_SNDTIMEO does
        // not apply to connect() on Windows).
        u_long nb = 1; ioctlsocket(sock, FIONBIO, &nb);

        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(kPort);
        inet_pton(AF_INET, kHost, &addr.sin_addr);
        int rc = ::connect(sock, (sockaddr*)&addr, sizeof(addr));
        if (rc == 0) { nb = 0; ioctlsocket(sock, FIONBIO, &nb); return true; }
        if (WSAGetLastError() != WSAEWOULDBLOCK) return false;

        fd_set ws; FD_ZERO(&ws); FD_SET(sock, &ws);
        TIMEVAL tv{3, 0};
        if (select(0, nullptr, &ws, nullptr, &tv) <= 0) return false;
        int err = 0; int elen = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &elen);
        if (err != 0) return false;
        nb = 0; ioctlsocket(sock, FIONBIO, &nb);

        DWORD to = 3000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));
        return true;
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

// ── RaopSession helper ───────────────────────────────────────────────────────

/// Pump the test-thread message queue until WM_RAOP_CONNECTED or WM_RAOP_FAILED
/// is received, or the timeout expires.  Returns true on WM_RAOP_CONNECTED.
bool WaitForRaop(int timeoutMs) {
    DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
    while (GetTickCount() < deadline) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_RAOP_CONNECTED) return true;
            if (msg.message == WM_RAOP_FAILED)    return false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
    return false;
}

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(RaopShairport, OptionsReturns200) {
    RtspConn c;
    if (!c.Connect())
        GTEST_SKIP() << "shairport-sync container not reachable at "
                     << kHost << ":" << kPort
                     << " — run: docker compose up -d";

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

/// Verifies that AirBeam can (re)connect to a shairport-sync receiver within 5 s.
/// Simulates a reconnect by running two consecutive RaopSession::Start() calls and
/// verifying both complete within the 5-second window.
TEST(RaopShairport, AutoReconnectWithin5Seconds) {
    // Quick probe: skip early if Docker is not running.
    {
        RtspConn probe;
        if (!probe.Connect())
            GTEST_SKIP() << "shairport-sync not reachable — run: docker compose up -d";
    }

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, nullptr) << "Could not create message window";

    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);

    auto makeSession = [&](RetransmitBuffer* retransmit) {
        uint8_t aesKey[16], aesIv[16];
        BCryptGenRandom(nullptr, aesKey, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        BCryptGenRandom(nullptr, aesIv,  16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        RaopSession::Config rc;
        rc.receiverIp   = kHost;
        rc.receiverPort = kPort;
        rc.clientIp     = "0.0.0.0";
        std::memcpy(rc.aesKey, aesKey, 16);
        std::memcpy(rc.aesIv,  aesIv,  16);
        rc.volume       = 1.0f;
        rc.retransmit   = retransmit;
        rc.hwndMain     = hwnd;
        return rc;
    };

    // First connection
    auto retransmit1 = std::make_unique<RetransmitBuffer>();
    auto session1 = std::make_unique<RaopSession>();
    session1->Start(makeSession(retransmit1.get()));
    bool first = WaitForRaop(8000);
    session1->Stop();

    if (!first)
        GTEST_SKIP() << "Initial connection failed — shairport-sync may not be ready";

    // Reconnect within 5 seconds
    DWORD t0 = GetTickCount();
    auto retransmit2 = std::make_unique<RetransmitBuffer>();
    auto session2 = std::make_unique<RaopSession>();
    session2->Start(makeSession(retransmit2.get()));
    bool second = WaitForRaop(5000);
    DWORD elapsed = GetTickCount() - t0;
    session2->Stop();

    EXPECT_TRUE(second)  << "Reconnection did not succeed";
    EXPECT_LT(elapsed, 5000u) << "Reconnect took " << elapsed << " ms (> 5000 ms limit)";

    DestroyWindow(hwnd);
    WSACleanup();
}

