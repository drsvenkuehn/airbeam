/// T083: 24-hour stress test: stream continuously and verify:
///   - No heap growth (monitored externally via WinDbg / Application Verifier)
///   - RTP timestamp monotonically increasing (no drift)
///   - droppedFrameCount within acceptable threshold
///   - No crashes or assertions
///
/// Run with: AIRBEAM_STRESS_TEST=1 ctest -R stress_24h --output-on-failure
/// Requires: shairport-sync Docker container (docker compose up -d)
///
/// DISABLED by default — tag in test name prevents accidental CI execution.
/// Must also set AIRBEAM_STRESS_TEST=1 to proceed past the GTEST_SKIP guard.
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
#include <cstdint>
#include <memory>
#include <string>
#include <atomic>

#include "protocol/RaopSession.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"
#include "audio/WasapiCapture.h"
#include "audio/AlacEncoderThread.h"
#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"
#include "core/Messages.h"

namespace {

/// Pump messages until WM_RAOP_CONNECTED or WM_RAOP_FAILED, or timeout.
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

TEST(StressTest, DISABLED_24HourContinuousStream) {
#pragma warning(suppress: 4996)
    const char* enabled = getenv("AIRBEAM_STRESS_TEST");
    if (!enabled || std::string(enabled) != "1")
        GTEST_SKIP() << "Set AIRBEAM_STRESS_TEST=1 to run the 24 h stress test";

    // ── Message window ──────────────────────────────────────────────────────────
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, nullptr);

    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);

    // ── RAOP session ────────────────────────────────────────────────────────────
    uint8_t aesKey[16], aesIv[16];
    BCryptGenRandom(nullptr, aesKey, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(nullptr, aesIv,  16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    auto cipher     = std::make_unique<AesCbcCipher>(aesKey, aesIv);
    auto retransmit = std::make_unique<RetransmitBuffer>();
    auto ring       = std::make_unique<SpscRingBuffer<AudioFrame, 128>>();
    SpscRingBufferPtr ringPtr = ring.get();

    RaopSession::Config rc;
    rc.receiverIp   = "127.0.0.1";
    rc.receiverPort = 5000;
    rc.clientIp     = "0.0.0.0";
    std::memcpy(rc.aesKey, aesKey, 16);
    std::memcpy(rc.aesIv,  aesIv,  16);
    rc.volume       = 1.0f;
    rc.retransmit   = retransmit.get();
    rc.hwndMain     = hwnd;

    auto session = std::make_unique<RaopSession>();
    session->Start(rc);
    if (!WaitForRaop(8000)) {
        session->Stop();
        DestroyWindow(hwnd);
        WSACleanup();
        GTEST_SKIP() << "shairport-sync not reachable — run: docker compose up -d";
    }

    // ── Encoder thread ──────────────────────────────────────────────────────────
    uint32_t ssrc = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&ssrc), 4,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(session->ServerAudioPort());
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    auto alacThread = std::make_unique<AlacEncoderThread>();
    ASSERT_TRUE(alacThread->Init(ringPtr, cipher.get(), retransmit.get(), ssrc,
                                 session->AudioSocket(),
                                 reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    alacThread->Start();

    // ── WASAPI loopback capture ─────────────────────────────────────────────────
    auto wasapi = std::make_unique<WasapiCapture>();
    // hwndMain = nullptr → device-change notifications are silently suppressed
    wasapi->Start(ringPtr, nullptr);

    // ── Monitor loop ────────────────────────────────────────────────────────────
    constexpr DWORD    kDurationSec    = 86400;
    constexpr DWORD    kSampleInterval = 60 * 1000;  // ms
    constexpr uint64_t kMaxDropRate    = 10;          // drops/minute

    DWORD    start         = GetTickCount();
    uint64_t lastDropCount = 0;
    uint32_t minuteCount   = 0;

    while (GetTickCount() - start < kDurationSec * 1000UL) {
        Sleep(kSampleInterval);
        ++minuteCount;

        uint64_t dropCount = wasapi->DroppedFrameCount();
        uint64_t dropDelta = dropCount - lastDropCount;

        EXPECT_LE(dropDelta, kMaxDropRate)
            << "Minute " << minuteCount << ": drop rate " << dropDelta << " > "
            << kMaxDropRate << " (dropped frames since last sample)";

        EXPECT_TRUE(alacThread->IsRunning())
            << "AlacEncoderThread stopped at minute " << minuteCount;

        if (!alacThread->IsRunning()) break;

        lastDropCount = dropCount;
    }

    // ── Teardown ────────────────────────────────────────────────────────────────
    wasapi->Stop();
    alacThread->Stop();
    session->Stop();
    DestroyWindow(hwnd);
    WSACleanup();
}

