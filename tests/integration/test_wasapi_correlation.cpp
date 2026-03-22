/// Integration test: WASAPI loopback capture cross-correlation.
/// Renders a 1 kHz sine wave to the default audio endpoint via WASAPI, captures the
/// loopback with WasapiCapture, and asserts Pearson r > 0.99 between source and captured.
#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "audio/WasapiCapture.h"
#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// ── Helpers ──────────────────────────────────────────────────────────────────

std::vector<int16_t> Gen1kHzStereo(int durationMs) {
    const int sr = 44100;
    const int n  = sr * durationMs / 1000;
    std::vector<int16_t> buf;
    buf.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        auto s = static_cast<int16_t>(16000.0 * std::sin(2.0 * kPi * 1000.0 * i / sr));
        buf.push_back(s); buf.push_back(s);
    }
    return buf;
}

double PearsonR(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    const size_t n = a.size();
    double sa = 0, sb = 0;
    for (size_t i = 0; i < n; ++i) { sa += a[i]; sb += b[i]; }
    const double ma = sa/n, mb = sb/n;
    double cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < n; ++i) {
        double da = a[i]-ma, db = b[i]-mb;
        cov += da*db; va += da*da; vb += db*db;
    }
    return (va == 0 || vb == 0) ? 0.0 : cov / std::sqrt(va * vb);
}

// ── WasapiRenderer ───────────────────────────────────────────────────────────
// Renders a continuous 1 kHz sine wave to the default audio render endpoint.
// WasapiCapture's loopback will capture whatever this plays.

class WasapiRenderer {
public:
    WasapiRenderer() = default;
    ~WasapiRenderer() { Stop(); }

    WasapiRenderer(const WasapiRenderer&) = delete;
    WasapiRenderer& operator=(const WasapiRenderer&) = delete;

    /// Returns false if no render endpoint is available.
    bool Start() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
        comHr_ = hr;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&pEnum_));
        if (FAILED(hr)) return false;

        IMMDevice* pDev = nullptr;
        hr = pEnum_->GetDefaultAudioEndpoint(eRender, eConsole, &pDev);
        if (FAILED(hr)) return false;

        hr = pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&pAC_));
        pDev->Release();
        if (FAILED(hr)) return false;

        WAVEFORMATEX* pFmt = nullptr;
        hr = pAC_->GetMixFormat(&pFmt);
        if (FAILED(hr)) return false;

        sampleRate_ = pFmt->nSamplesPerSec;
        channels_   = pFmt->nChannels;
        omega_      = 2.0 * kPi * 1000.0 / sampleRate_;  // 1 kHz

        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) { CoTaskMemFree(pFmt); return false; }

        hr = pAC_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                              0, 0, pFmt, nullptr);
        CoTaskMemFree(pFmt);
        if (FAILED(hr)) return false;

        pAC_->SetEventHandle(event_);
        hr = pAC_->GetService(__uuidof(IAudioRenderClient),
                              reinterpret_cast<void**>(&pRC_));
        if (FAILED(hr)) return false;

        UINT32 bufSize = 0;
        pAC_->GetBufferSize(&bufSize);
        PrefillBuffer(bufSize);

        stop_.store(false, std::memory_order_release);
        thread_ = std::thread([this] { Run(); });
        pAC_->Start();
        return true;
    }

    void Stop() {
        stop_.store(true, std::memory_order_release);
        if (event_) SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (pAC_)  { pAC_->Stop();    pAC_->Release();   pAC_  = nullptr; }
        if (pRC_)  { pRC_->Release(); pRC_  = nullptr; }
        if (pEnum_){ pEnum_->Release(); pEnum_ = nullptr; }
        if (event_){ CloseHandle(event_); event_ = nullptr; }
        if (comHr_ == S_OK || comHr_ == S_FALSE) CoUninitialize();
    }

private:
    void FillFrames(float* buf, UINT32 n) noexcept {
        for (UINT32 i = 0; i < n; ++i) {
            auto v = static_cast<float>(0.4 * std::sin(phase_));
            phase_ += omega_;
            for (uint32_t c = 0; c < channels_; ++c)
                buf[i * channels_ + c] = v;
        }
    }

    void PrefillBuffer(UINT32 bufSize) {
        BYTE* pData = nullptr;
        if (SUCCEEDED(pRC_->GetBuffer(bufSize, &pData)))
            FillFrames(reinterpret_cast<float*>(pData), bufSize);
        pRC_->ReleaseBuffer(bufSize, 0);
    }

    void Run() {
        while (!stop_.load(std::memory_order_acquire)) {
            WaitForSingleObject(event_, 50);
            if (stop_.load(std::memory_order_acquire)) break;

            UINT32 padding = 0, bufSize = 0;
            if (FAILED(pAC_->GetCurrentPadding(&padding))) break;
            if (FAILED(pAC_->GetBufferSize(&bufSize)))     break;
            UINT32 avail = bufSize - padding;
            if (avail == 0) continue;

            BYTE* pData = nullptr;
            if (SUCCEEDED(pRC_->GetBuffer(avail, &pData)))
                FillFrames(reinterpret_cast<float*>(pData), avail);
            pRC_->ReleaseBuffer(avail, 0);
        }
    }

    IMMDeviceEnumerator* pEnum_       = nullptr;
    IAudioClient*        pAC_         = nullptr;
    IAudioRenderClient*  pRC_         = nullptr;
    HANDLE               event_       = nullptr;
    std::thread          thread_;
    std::atomic<bool>    stop_{false};
    uint32_t             sampleRate_  = 44100;
    uint32_t             channels_    = 2;
    double               phase_       = 0.0;
    double               omega_       = 0.0;
    HRESULT              comHr_       = E_FAIL;
};

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

/// Renders 1 kHz sine via WASAPI, captures via loopback, asserts Pearson r > 0.99.
/// Skips gracefully if no audio render endpoint is available.
TEST(WasapiCorrelation, CrossCorrelationAbove0_99) {
    // 1. Start render — play 1 kHz sine to the default output.
    WasapiRenderer renderer;
    if (!renderer.Start())
        GTEST_SKIP() << "No default audio render endpoint (headless/CI environment)";

    // 2. Create ring buffer and start loopback capture.
    auto ring = std::make_unique<SpscRingBuffer<AudioFrame, 128>>();
    SpscRingBufferPtr ringPtr = ring.get();

    WasapiCapture capture;
    if (!capture.Start(ringPtr, nullptr)) {
        renderer.Stop();
        GTEST_SKIP() << "WasapiCapture failed to start (no loopback device?)";
    }

    // 3. Collect ~3 seconds of captured audio.
    constexpr int kCaptureSec   = 3;
    constexpr int kFramesNeeded = (44100 * kCaptureSec) / 352;
    std::vector<int16_t> captured;
    captured.reserve(static_cast<size_t>(kFramesNeeded) * 704);

    DWORD deadline = GetTickCount() + (kCaptureSec + 2) * 1000;
    AudioFrame frame;
    while (static_cast<int>(captured.size() / 704) < kFramesNeeded
           && GetTickCount() < deadline) {
        if (RingTryPop(ringPtr, frame)) {
            captured.insert(captured.end(),
                            frame.samples,
                            frame.samples + 704);
        } else {
            Sleep(1);
        }
    }

    capture.Stop();
    renderer.Stop();

    // 4. Build reference signal at same length.
    auto source = Gen1kHzStereo(kCaptureSec * 1000);
    const size_t len = std::min(source.size(), captured.size());
    source.resize(len);
    captured.resize(len);

    if (captured.empty()) {
        GTEST_SKIP() << "No audio captured (silent device or loopback not available)";
    }

    // 5. For a 1 kHz pure tone, any integer-millisecond loopback latency
    //    maps to an integer number of full cycles so Pearson r ≈ 1.0.
    double r = PearsonR(source, captured);
    EXPECT_GT(r, 0.99) << "Pearson r=" << r
                       << " (expected >0.99 for 1 kHz loopback)";
}

TEST(WasapiCorrelation, PearsonRSameSignalIsOne) {
    auto sig = Gen1kHzStereo(100);
    EXPECT_NEAR(PearsonR(sig, sig), 1.0, 1e-9);
}

TEST(WasapiCorrelation, PearsonROppositeIsMinusOne) {
    auto a = Gen1kHzStereo(100);
    std::vector<int16_t> b;
    b.reserve(a.size());
    for (auto s : a) b.push_back(static_cast<int16_t>(-s));
    EXPECT_NEAR(PearsonR(a, b), -1.0, 1e-3);
}

/// Requires two audio render devices and the ability to change the default — kept disabled.
TEST(WasapiCorrelation, DISABLED_DeviceChangeResumesCapture) {
    GTEST_SKIP() << "Requires two audio render devices and IMMDeviceEnumerator::SetDefaultEndpoint";
}

