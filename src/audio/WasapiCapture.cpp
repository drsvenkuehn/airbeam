// RT-safety audit (Feature 007): hot path verified clean — no heap, no mutex, no I/O.
// The steady-state loop (WaitForMultipleObjects → GetBuffer → ReleaseBuffer) uses
// only stack-allocated locals, atomic loads/stores, and the WASAPI capture event.
// The device-change recovery branch (Reinitialise) touches COM/heap but fires
// only on exceptional device-change events, not in steady-state audio processing.

#include "audio/WasapiCapture.h"
#include "audio/Resampler.h"
#include <mmreg.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "core/Messages.h"

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT  {00000003-0000-0010-8000-00AA00389B71}
static const GUID kSubtypeFloat = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
};

// Static silence buffer — BSS segment, zero-cost
const int16_t WasapiCapture::kSilenceBuf_[kMaxFramesPerBuffer * 2] = {};

// ─────────────────────────────────────────────────────────────────────────────
// FrameAccumulator — float path
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::FrameAccumulator::Push(
    const float* src, int frameCount, int channels, AudioFrame& out) noexcept
{
    for (int i = 0; i < frameCount; ++i) {
        auto cvt = [](float f) noexcept -> int16_t {
            long v = lrintf(f * 32767.0f);
            if (v >  32767) v =  32767;
            if (v < -32767) v = -32767;
            return static_cast<int16_t>(v);
        };
        buf[filled++] = cvt(src[i * channels]);
        buf[filled++] = (channels >= 2) ? cvt(src[i * channels + 1])
                                        : cvt(src[i * channels]);
    }
    if (filled >= 704) {
        memcpy(out.samples, buf, sizeof(buf));
        filled = 0;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// FrameAccumulator — int16 path
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::FrameAccumulator::Push(
    const int16_t* src, int frameCount, int channels, AudioFrame& out) noexcept
{
    for (int i = 0; i < frameCount; ++i) {
        buf[filled++] = src[i * channels];
        buf[filled++] = (channels >= 2) ? src[i * channels + 1]
                                        : src[i * channels];
    }
    if (filled >= 704) {
        memcpy(out.samples, buf, sizeof(buf));
        filled = 0;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

WasapiCapture::WasapiCapture()
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&pEnumerator_));
    if (SUCCEEDED(hr) && pEnumerator_)
        pEnumerator_->RegisterEndpointNotificationCallback(this);
}

WasapiCapture::~WasapiCapture()
{
    Stop();
    if (pEnumerator_) {
        pEnumerator_->UnregisterEndpointNotificationCallback(this);
        pEnumerator_->Release();
        pEnumerator_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Start — called from Thread 1
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::Start(SpscRingBufferPtr ring, HWND hwndMain)
{
    // Validate ring: accept 512-slot (normal) or 32-slot (low-latency) arms.
    // The 128-slot arm is reserved for future use; reject nullptr (index 0 with null ptr).
    bool validRing = std::holds_alternative<SpscRingBuffer<AudioFrame,512>*>(ring) ||
                     std::holds_alternative<SpscRingBuffer<AudioFrame,32>*>(ring);
    if (!validRing)
        return false;

    if (!pEnumerator_)
        return false;

    ring_     = ring;
    hwndMain_ = hwndMain;

    // Create events
    stopEvent_    = CreateEventW(nullptr, TRUE,  FALSE, nullptr); // manual-reset
    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
    if (!stopEvent_ || !captureEvent_) {
        if (stopEvent_)    { CloseHandle(stopEvent_);    stopEvent_    = nullptr; }
        if (captureEvent_) { CloseHandle(captureEvent_); captureEvent_ = nullptr; }
        return false;
    }

    stopFlag_.store(false, std::memory_order_relaxed);
    thread_ = std::thread(&WasapiCapture::ThreadProc, this);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stop — called from Thread 1
// ─────────────────────────────────────────────────────────────────────────────

void WasapiCapture::Stop()
{
    if (!thread_.joinable()) {
        // Clean up events if Start was never called or already stopped
        if (stopEvent_)    { CloseHandle(stopEvent_);    stopEvent_    = nullptr; }
        if (captureEvent_) { CloseHandle(captureEvent_); captureEvent_ = nullptr; }
        return;
    }

    stopFlag_.store(true, std::memory_order_release);
    if (stopEvent_)    SetEvent(stopEvent_);
    if (captureEvent_) SetEvent(captureEvent_);
    thread_.join();

    if (stopEvent_)    { CloseHandle(stopEvent_);    stopEvent_    = nullptr; }
    if (captureEvent_) { CloseHandle(captureEvent_); captureEvent_ = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// InitAudioClient — Thread 3 only
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// Detect whether the mix format carries IEEE-float samples.
bool FormatIsFloat(const WAVEFORMATEX* pFmt) noexcept
{
    if (!pFmt) return false;
    if (pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFmt);
        static const GUID kFlt = {0x00000003, 0x0000, 0x0010,
                                   {0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
        return ext->SubFormat == kFlt;
    }
    return pFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
}
} // namespace

bool WasapiCapture::InitAudioClient()
{
    HRESULT hr = pEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice_);
    if (FAILED(hr)) return false;

    hr = pDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(&pAudioClient_));
    if (FAILED(hr)) return false;

    hr = pAudioClient_->GetMixFormat(&pMixFormat_);
    if (FAILED(hr)) return false;

    hr = pAudioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0, 0, pMixFormat_, nullptr);
    if (FAILED(hr)) return false;

    hr = pAudioClient_->SetEventHandle(captureEvent_);
    if (FAILED(hr)) return false;

    hr = pAudioClient_->GetService(__uuidof(IAudioCaptureClient),
                                    reinterpret_cast<void**>(&pCaptureClient_));
    if (FAILED(hr)) return false;

    // Create resampler if needed
    if (pMixFormat_->nSamplesPerSec != 44100) {
        resampler_ = std::make_unique<Resampler>(pMixFormat_->nSamplesPerSec,
                                                  pMixFormat_->nChannels);
    } else {
        resampler_.reset();
    }

    return true;
}

void WasapiCapture::ReleaseAudioClient()
{
    if (pAudioClient_)    { pAudioClient_->Stop(); }
    if (pCaptureClient_)  { pCaptureClient_->Release();  pCaptureClient_  = nullptr; }
    if (pAudioClient_)    { pAudioClient_->Release();    pAudioClient_    = nullptr; }
    if (pDevice_)         { pDevice_->Release();         pDevice_         = nullptr; }
    if (pMixFormat_)      { CoTaskMemFree(pMixFormat_);  pMixFormat_      = nullptr; }
    resampler_.reset();
    // NOTE: captureEvent_ and stopEvent_ are managed by Start/Stop, not here
}

// ─────────────────────────────────────────────────────────────────────────────
// Reinitialise — called from ThreadProc on device change
// ─────────────────────────────────────────────────────────────────────────────

void WasapiCapture::Reinitialise()
{
    ReleaseAudioClient();
    if (!InitAudioClient()) {
        if (hwndMain_)
            PostMessageW(hwndMain_, WM_CAPTURE_ERROR, static_cast<WPARAM>(E_FAIL), 0);
        return;
    }
    pAudioClient_->Start();
    if (hwndMain_)
        PostMessageW(hwndMain_, WM_AUDIO_DEVICE_LOST, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// ThreadProc — Thread 3  (RT: ZERO heap alloc, ZERO mutex, ZERO logging on hot path)
// ─────────────────────────────────────────────────────────────────────────────

void WasapiCapture::ThreadProc()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // MMCSS boost — must happen before InitAudioClient
    DWORD  taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristics(L"Audio", &taskIndex);

    if (!InitAudioClient()) {
        if (hwndMain_)
            PostMessageW(hwndMain_, WM_CAPTURE_ERROR, static_cast<WPARAM>(E_FAIL), 0);
        if (hMmcss) AvRevertMmThreadCharacteristics(hMmcss);
        CoUninitialize();
        return;
    }

    // Cache format properties once — avoids repeated pointer chasing in the loop.
    bool     fmtIsFloat = FormatIsFloat(pMixFormat_);
    uint32_t fmtCh      = pMixFormat_ ? pMixFormat_->nChannels : 2u;

    pAudioClient_->Start();
    running_.store(true, std::memory_order_release);

    FrameAccumulator accum;

    // Wait handles: [0]=stopEvent_, [1]=captureEvent_
    HANDLE waitHandles[2] = { stopEvent_, captureEvent_ };

    while (!stopFlag_.load(std::memory_order_acquire)) {

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 200);

        if (waitResult == WAIT_OBJECT_0) {
            // stopEvent_ signaled — exit
            break;
        }

        if (waitResult == WAIT_TIMEOUT) {
            // Check for pending device change (coalesced 20ms debounce)
            ULONGLONG t = deviceChangedAt_.load(std::memory_order_acquire);
            if (t != 0 && (GetTickCount64() - t) >= 20ULL) {
                deviceChangedAt_.store(0, std::memory_order_release);
                Reinitialise();
                // Update cached format properties after reinit
                if (pMixFormat_) {
                    fmtIsFloat = FormatIsFloat(pMixFormat_);
                    fmtCh = pMixFormat_->nChannels;
                }
            }
            continue;
        }

        if (waitResult != WAIT_OBJECT_0 + 1) break; // unexpected error

        // ── Drain all available packets in this period ────────────────────────
        if (!pCaptureClient_) continue;

        HRESULT hr;
        BYTE*   pData     = nullptr;
        UINT32  numFrames = 0;
        DWORD   flags     = 0;

        // Helper: push one completed frame into the ring, counting drops.
        auto emit = [&](AudioFrame& frame) noexcept {
            frame.frameCount = frameCounter_++;
            if (!RingTryPush(ring_, frame))
                ++droppedFrameCount_;
        };

        // Helper: feed int16 stereo data to the accumulator in exact-fit chunks.
        auto feedInt16 = [&](const int16_t* src, int totalFrames, int nSrcCh) noexcept {
            int done = 0;
            while (done < totalFrames) {
                int space = (704 - accum.filled) / 2;
                int chunk = std::min(totalFrames - done, space);
                AudioFrame frame;
                if (accum.Push(src + done * nSrcCh, chunk, nSrcCh, frame))
                    emit(frame);
                done += chunk;
            }
        };

        // Helper: feed float data to the accumulator in exact-fit chunks.
        auto feedFloat = [&](const float* src, int totalFrames, int nSrcCh) noexcept {
            int done = 0;
            while (done < totalFrames) {
                int space = (704 - accum.filled) / 2;
                int chunk = std::min(totalFrames - done, space);
                AudioFrame frame;
                if (accum.Push(src + done * nSrcCh, chunk, nSrcCh, frame))
                    emit(frame);
                done += chunk;
            }
        };

        while ((hr = pCaptureClient_->GetBuffer(
                        &pData, &numFrames, &flags, nullptr, nullptr)) == S_OK)
        {
            if (numFrames == 0) {
                pCaptureClient_->ReleaseBuffer(0);
                break;
            }

            const bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            if (isSilent || pData == nullptr) {
                // Use pre-allocated int16 silence buffer — no heap, no stack allocation
                int left = static_cast<int>(numFrames);
                while (left > 0) {
                    int chunk = std::min(left, kMaxFramesPerBuffer);
                    feedInt16(kSilenceBuf_, chunk, 2);
                    left -= chunk;
                }
            } else if (fmtIsFloat) {
                const float* src = reinterpret_cast<const float*>(pData);

                if (resampler_ && !resampler_->IsPassthrough()) {
                    // Rate/channel conversion: float[N*fmtCh] → int16[M*2]
                    int outFrames = 0;
                    const int16_t* resampled = resampler_->Process(src, static_cast<int>(numFrames), outFrames);
                    feedInt16(resampled, outFrames, 2);
                } else {
                    // Native 44100 float — convert directly in the accumulator.
                    feedFloat(src, static_cast<int>(numFrames), static_cast<int>(fmtCh));
                }
            } else {
                // Integer PCM (S16LE)
                const int16_t* src = reinterpret_cast<const int16_t*>(pData);
                feedInt16(src, static_cast<int>(numFrames), static_cast<int>(fmtCh));
            }

            pCaptureClient_->ReleaseBuffer(numFrames);
        }
    }

    ReleaseAudioClient();
    running_.store(false, std::memory_order_release);
    if (hMmcss) AvRevertMmThreadCharacteristics(hMmcss);
    CoUninitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// IMMNotificationClient
// ─────────────────────────────────────────────────────────────────────────────

HRESULT WasapiCapture::OnDefaultDeviceChanged(
    EDataFlow flow, ERole role, LPCWSTR /*pwstrDeviceId*/)
{
    if (flow == eRender && role == eConsole) {
        // CAS: only record the first notification in a 20ms window
        ULONGLONG expected = 0;
        ULONGLONG now = GetTickCount64();
        deviceChangedAt_.compare_exchange_strong(expected, now,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return S_OK;
}

HRESULT WasapiCapture::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IMMNotificationClient))
    {
        *ppv = static_cast<IMMNotificationClient*>(this);
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
