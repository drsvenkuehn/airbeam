// RT-safety audit (T092): hot path verified clean — no heap, no mutex, no I/O.
// The steady-state loop (WaitForSingleObject → GetBuffer → ReleaseBuffer) uses
// only stack-allocated locals, atomic loads/stores, and the WASAPI capture event.
// The device-change recovery branch (InitAudioClient) touches COM/heap but fires
// only on exceptional device-change events, not in steady-state audio processing.
// Audit date: 2025. Auditor: speckit.implement agent.

#include "audio/WasapiCapture.h"
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

WasapiCapture::WasapiCapture() = default;

WasapiCapture::~WasapiCapture()
{
    Stop();
    if (pEnumerator_) {
        pEnumerator_->UnregisterEndpointNotificationCallback(this);
        pEnumerator_->Release();
        pEnumerator_ = nullptr;
    }
    if (captureEvent_) {
        CloseHandle(captureEvent_);
        captureEvent_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Start — called from Thread 1
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::Start(SpscRingBufferPtr ring, HWND hwndMain)
{
    ring_     = ring;
    hwndMain_ = hwndMain;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means the thread already has a COM apartment — that's OK.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&pEnumerator_));
    if (FAILED(hr)) return false;

    pEnumerator_->RegisterEndpointNotificationCallback(this);

    // Create the event now so InitAudioClient (on Thread 3) can set it
    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent_) return false;

    // Query the mix format on Thread 1 so we can build the Resampler before
    // the streaming loop starts.
    {
        IMMDevice*    pDev = nullptr;
        IAudioClient* pAC  = nullptr;
        WAVEFORMATEX* pFmt = nullptr;

        if (SUCCEEDED(pEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &pDev))) {
            if (SUCCEEDED(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                          nullptr,
                                          reinterpret_cast<void**>(&pAC)))) {
                pAC->GetMixFormat(&pFmt);
                pAC->Release();
            }
            pDev->Release();
        }

        if (pFmt) {
            resampler_ = std::make_unique<Resampler>(pFmt->nSamplesPerSec,
                                                     pFmt->nChannels);
            CoTaskMemFree(pFmt);
        }
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
    stopFlag_.store(true, std::memory_order_release);
    // Unblock the WaitForSingleObject so Thread 3 exits promptly.
    if (captureEvent_) SetEvent(captureEvent_);
    if (thread_.joinable()) thread_.join();
}

// ─────────────────────────────────────────────────────────────────────────────
// InitAudioClient — Thread 3 only
// ─────────────────────────────────────────────────────────────────────────────

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

    return true;
}

void WasapiCapture::ReleaseAudioClient()
{
    if (pAudioClient_)   { pAudioClient_->Stop(); }
    if (pCaptureClient_) { pCaptureClient_->Release(); pCaptureClient_ = nullptr; }
    if (pAudioClient_)   { pAudioClient_->Release();   pAudioClient_   = nullptr; }
    if (pDevice_)        { pDevice_->Release();        pDevice_        = nullptr; }
    if (pMixFormat_)     { CoTaskMemFree(pMixFormat_); pMixFormat_     = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Hot-loop helpers (inline lambdas keep the loop body readable)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Detect whether the mix format carries IEEE-float samples.
bool FormatIsFloat(const WAVEFORMATEX* pFmt) noexcept
{
    if (!pFmt) return false;
    if (pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFmt);
        return ext->SubFormat == kSubtypeFloat;
    }
    return pFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ThreadProc — Thread 3  (RT: ZERO heap alloc, ZERO mutex, ZERO logging)
// ─────────────────────────────────────────────────────────────────────────────

void WasapiCapture::ThreadProc()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // MMCSS boost — must happen before InitAudioClient
    DWORD  taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristics(L"Audio", &taskIndex);

    if (!InitAudioClient()) {
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

    // Static const silence buffer lives in BSS — zero cost, no heap.
    static const float kSilence[2048 * 2] = {};

    // Resampled int16 output — stack-allocated, large enough for any WASAPI period.
    int16_t resampledBuf[2048 * 2];

    while (!stopFlag_.load(std::memory_order_acquire)) {

        // ── Device-change recovery ────────────────────────────────────────────
        if (deviceChanged_.load(std::memory_order_acquire)) {
            deviceChanged_.store(false, std::memory_order_release);
            ReleaseAudioClient();
            if (!InitAudioClient()) break;
            fmtIsFloat = FormatIsFloat(pMixFormat_);
            fmtCh      = pMixFormat_ ? pMixFormat_->nChannels : 2u;
            pAudioClient_->Start();
        }

        // ── Wait for data ─────────────────────────────────────────────────────
        DWORD waitResult = WaitForSingleObject(captureEvent_, 200);
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0) break;

        // ── Drain all available packets in this period ────────────────────────
        HRESULT hr;
        BYTE*   pData     = nullptr;
        UINT32  numFrames = 0;
        DWORD   flags     = 0;

        while ((hr = pCaptureClient_->GetBuffer(
                        &pData, &numFrames, &flags, nullptr, nullptr)) == S_OK)
        {
            if (numFrames == 0) {
                pCaptureClient_->ReleaseBuffer(0);
                // Maintain clock sync: push 352 zero samples through the
                // accumulator so the downstream pipeline doesn't starve.
                {
                    int done = 0;
                    while (done < 352) {
                        const int space = (704 - accum.filled) / 2;
                        const int chunk = std::min(352 - done, space);
                        AudioFrame frame;
                        if (accum.Push(kSilence + done * 2, chunk, 2, frame)) {
                            frame.frameCount = frameCounter_++;
                            if (!RingTryPush(ring_, frame))
                                ++droppedFrameCount_;
                        }
                        done += chunk;
                    }
                }
                break;
            }

            const bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            // Helper: push one completed frame into the ring, counting drops.
            auto emit = [&](AudioFrame& frame) noexcept {
                frame.frameCount = frameCounter_++;
                if (!RingTryPush(ring_, frame))
                    ++droppedFrameCount_;
            };

            // Helper: feed int16 stereo data to the accumulator in exact-fit chunks.
            // 'nSrcCh' is the source channel count (1 or 2 for post-resample path).
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

            if (isSilent || pData == nullptr) {
                // Treat silence as stereo-float zeros — values don't matter, only count.
                feedFloat(kSilence, static_cast<int>(std::min(numFrames, 2048u)), 2);
                // If numFrames > 2048, feed the remainder as additional silence chunks.
                if (numFrames > 2048u) {
                    int left = static_cast<int>(numFrames) - 2048;
                    while (left > 0) {
                        int chunk = std::min(left, 2048);
                        feedFloat(kSilence, chunk, 2);
                        left -= chunk;
                    }
                }
            } else if (fmtIsFloat) {
                const float* src = reinterpret_cast<const float*>(pData);

                if (resampler_ && !resampler_->IsPassthrough()) {
                    // Rate/channel conversion: float[N*fmtCh] → int16[M*2]
                    int outFrames = resampler_->Process(
                                        src, resampledBuf, static_cast<int>(numFrames));
                    feedInt16(resampledBuf, outFrames, 2);
                } else {
                    // Native 44100/stereo float — convert directly in the accumulator.
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
        deviceChanged_.store(true, std::memory_order_release);
        if (hwndMain_)
            PostMessageW(hwndMain_, WM_DEFAULT_DEVICE_CHANGED, 0, 0);
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
