/// End-to-end test: stream 1 kHz sine through the full AirBeam pipeline
/// (RaopSession + AlacEncoderThread) to shairport-sync Docker, verify no errors.
///
/// Prerequisites: docker compose up -d  (see docker-compose.yml at repo root)
/// Skips gracefully if the container is not reachable.
///
/// Note: receiver-side frequency analysis (Goertzel) is deferred until a
/// shairport-sync stdout-capture setup is available.
#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include <vector>
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

#include "protocol/RaopSession.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"
#include "audio/AlacEncoderThread.h"
#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"
#include "core/Messages.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<int16_t> Gen1kHz(int durationMs) {
    const int sr = 44100, n = sr * durationMs / 1000;
    std::vector<int16_t> buf; buf.reserve(static_cast<size_t>(n)*2);
    for (int i = 0; i < n; ++i) {
        auto s = static_cast<int16_t>(16000.0 * std::sin(2.0*kPi*1000.0*i/sr));
        buf.push_back(s); buf.push_back(s);
    }
    return buf;
}

// Goertzel algorithm: O(N) magnitude at a single frequency bin.
double Goertzel(const std::vector<double>& x, double hz, double sr) {
    const int n = (int)x.size();
    const double w = 2.0*kPi*hz/sr, c = 2.0*std::cos(w);
    double s0=0, s1=0, s2=0;
    for (int i=0;i<n;++i){s0=x[i]+c*s1-s2;s2=s1;s1=s0;}
    return std::sqrt(s1*s1+s2*s2-c*s1*s2);
}

double PeakFreq(const std::vector<double>& s, double sr, double lo, double hi) {
    double best=-1, freq=lo;
    for (double f=lo;f<=hi;f+=1.0){double m=Goertzel(s,f,sr);if(m>best){best=m;freq=f;}}
    return freq;
}

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

// ── Unit-level self-check (always runs) ──────────────────────────────────────

TEST(E2E1kHz, GoertzelSelfCheck) {
    auto pcm = Gen1kHz(200);
    std::vector<double> mono; mono.reserve(pcm.size()/2);
    for (size_t i=0;i<pcm.size();i+=2) mono.push_back(pcm[i]);
    EXPECT_NEAR(PeakFreq(mono, 44100.0, 800.0, 1200.0), 1000.0, 5.0);
}

// ── Full pipeline E2E test ────────────────────────────────────────────────────

/// Streams 2 seconds of 1 kHz sine via the full pipeline (RaopSession +
/// AlacEncoderThread) to shairport-sync.  Verifies that the session
/// establishes and streams without errors.
TEST(E2E1kHz, FullPipeline1kHzAtReceiver) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) GTEST_SKIP() << "Could not create message window";

    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);

    // ── Session setup ─────────────────────────────────────────────────────────
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

    // Wait up to 8 s (covers 3 retries: 1 s + 2 s + 4 s + connect time)
    if (!WaitForRaop(8000)) {
        session->Stop();
        DestroyWindow(hwnd);
        WSACleanup();
        GTEST_SKIP() << "shairport-sync not reachable — run: docker compose up -d";
    }

    // ── Encoder setup ─────────────────────────────────────────────────────────
    uint32_t ssrc = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&ssrc), 4,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(session->ServerAudioPort());
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    auto alacThread = std::make_unique<AlacEncoderThread>();
    bool ok = alacThread->Init(ringPtr, cipher.get(), retransmit.get(), ssrc,
                               session->AudioSocket(),
                               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ASSERT_TRUE(ok) << "AlacEncoderThread::Init failed";
    alacThread->Start();

    // ── Stream 2 seconds of 1 kHz sine ───────────────────────────────────────
    // Generate frames and push them into the ring buffer.
    // AlacEncoderThread pulls them, ALAC-encodes, AES-encrypts, and sends RTP.
    constexpr int kStreamMs       = 2000;
    constexpr int kSamplesPerFrame = 352;
    const     int kTotalFrames    = (44100 * kStreamMs / 1000) / kSamplesPerFrame;

    for (int f = 0; f < kTotalFrames; ++f) {
        AudioFrame frame{};
        frame.frameCount = static_cast<uint32_t>(f);
        for (int i = 0; i < kSamplesPerFrame; ++i) {
            int gs = f * kSamplesPerFrame + i;
            auto s = static_cast<int16_t>(
                16000.0 * std::sin(2.0 * kPi * 1000.0 * gs / 44100.0));
            frame.samples[i * 2]     = s;
            frame.samples[i * 2 + 1] = s;
        }
        // Retry push while full (encoder may not keep up initially)
        DWORD pushDeadline = GetTickCount() + 500;
        while (!RingTryPush(ringPtr, frame)) {
            ASSERT_LT(GetTickCount(), pushDeadline)
                << "Ring buffer full for >500 ms at frame " << f;
            Sleep(1);
        }
    }

    // Let the encoder drain the ring before teardown
    DWORD drainDeadline = GetTickCount() + 3000;
    while (!ring->IsEmpty() && GetTickCount() < drainDeadline) Sleep(10);

    EXPECT_TRUE(alacThread->IsRunning()) << "AlacEncoderThread stopped unexpectedly";

    // ── Teardown ──────────────────────────────────────────────────────────────
    alacThread->Stop();
    session->Stop();
    DestroyWindow(hwnd);
    WSACleanup();

    // Note: Goertzel frequency analysis at the receiver requires capturing
    // shairport-sync's decoded PCM output (e.g. via its pipe/stdout backend).
    // That infra is deferred — this test currently validates the transmit path.
    SUCCEED() << "1 kHz sine streamed through full pipeline ("
              << kTotalFrames << " frames, " << kStreamMs << " ms)";
}

