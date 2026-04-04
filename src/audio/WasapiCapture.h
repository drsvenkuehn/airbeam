#pragma once
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
#include <memory>
#include <thread>
#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"

// Forward declare Resampler to avoid including it in the header
class Resampler;

/// Capacity of the SPSC ring buffer passed to Start().
/// Caller MUST create a SpscRingBuffer<AudioFrame, kCaptureQueueFrames>.
inline constexpr int kCaptureQueueFrames = 512;

/// Maximum WASAPI frames per period (for pre-allocated silence buffer)
inline constexpr int kMaxFramesPerBuffer = 2048;

class WasapiCapture : public IMMNotificationClient {
public:
    WasapiCapture();
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    /// Start capture from the default Windows render endpoint.
    /// ring must be a SpscRingBuffer<AudioFrame, 512>* arm of SpscRingBufferPtr.
    /// hwndMain receives WM_CAPTURE_ERROR on failure and WM_DEFAULT_DEVICE_CHANGED on recovery.
    bool Start(SpscRingBufferPtr ring, HWND hwndMain);

    /// Stop capture. Thread-safe. Blocks until capture thread exits. Idempotent.
    void Stop();

    bool     IsRunning()        const { return running_.load(std::memory_order_acquire); }
    uint64_t DroppedFrameCount()const { return droppedFrameCount_.load(std::memory_order_relaxed); }
    uint64_t UdpDropCount()     const { return udpDropCount_.load(std::memory_order_relaxed); }

    // IUnknown
    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef()  override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // IMMNotificationClient
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR)                     override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR)                   override { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD)       override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void ThreadProc();
    bool InitAudioClient();
    void ReleaseAudioClient();
    void Reinitialise();

    // Accumulates variable WASAPI frames into fixed 352-sample AudioFrame objects
    struct FrameAccumulator {
        int16_t buf[704] = {};
        int     filled   = 0;
        bool Push(const float*   src, int frameCount, int channels, AudioFrame& out) noexcept;
        bool Push(const int16_t* src, int frameCount, int channels, AudioFrame& out) noexcept;
    };

    SpscRingBufferPtr          ring_{static_cast<SpscRingBuffer<AudioFrame,512>*>(nullptr)};
    HWND                       hwndMain_      = nullptr;

    // WASAPI objects — initialized in ThreadProc via InitAudioClient
    IMMDeviceEnumerator*       pEnumerator_   = nullptr;
    IMMDevice*                 pDevice_       = nullptr;
    IAudioClient*              pAudioClient_  = nullptr;
    IAudioCaptureClient*       pCaptureClient_= nullptr;
    HANDLE                     captureEvent_  = nullptr;  // auto-reset, set by WASAPI
    HANDLE                     stopEvent_     = nullptr;  // manual-reset, set by Stop()
    WAVEFORMATEX*              pMixFormat_    = nullptr;

    // Resampler — constructed in InitAudioClient when format != 44100Hz
    std::unique_ptr<Resampler> resampler_;

    // Thread control
    std::thread                thread_;
    std::atomic<bool>          stopFlag_{false};
    std::atomic<bool>          running_{false};

    // Device change coalescing: set to GetTickCount64() on first notification in window
    std::atomic<ULONGLONG>     deviceChangedAt_{0};

    // Stats
    std::atomic<uint64_t>      droppedFrameCount_{0};
    std::atomic<uint64_t>      udpDropCount_{0};
    uint32_t                   frameCounter_ = 0;

    // Pre-allocated silence buffer (static BSS, zero cost)
    // Used when AUDCLNT_BUFFERFLAGS_SILENT is set on hot path
    static const int16_t kSilenceBuf_[kMaxFramesPerBuffer * 2];
};
