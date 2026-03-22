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
#include <thread>
#include "audio/SpscRingBuffer.h"
#include "audio/Resampler.h"

class WasapiCapture : public IMMNotificationClient {
public:
    WasapiCapture();
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    /// Initializes WASAPI on the calling thread (Thread 1), then starts Thread 3.
    /// ring     — shared ring buffer (owned by ConnectionController, outlives this object)
    /// hwndMain — receives WM_DEVICE_CHANGED on IMMNotificationClient callback
    bool Start(SpscRingBufferPtr ring, HWND hwndMain);

    /// Signals Thread 3 to stop and joins (max 200 ms).
    void Stop();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

    uint64_t DroppedFrameCount() const { return droppedFrameCount_.load(std::memory_order_relaxed); }
    uint64_t UdpDropCount()      const { return udpDropCount_.load(std::memory_order_relaxed); }

    // IUnknown
    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef()  override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // IMMNotificationClient — only OnDefaultDeviceChanged is meaningful
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override          { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override        { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void ThreadProc();

    // Initializes IAudioClient for the default render device (loopback).
    // Called from Thread 3 only.
    bool InitAudioClient();
    void ReleaseAudioClient();

    // Accumulates WASAPI frames until we have exactly 352 stereo samples,
    // then pushes to the ring buffer. Stack-only; no heap.
    struct FrameAccumulator {
        int16_t  buf[704] = {};  // 352 stereo samples * 2
        int      filled  = 0;   // samples filled so far (L+R count)

        // Returns true when a full 352-sample frame was emitted (written to 'out').
        bool Push(const float* src, int frameCount, int channels, AudioFrame& out) noexcept;
        bool Push(const int16_t* src, int frameCount, int channels, AudioFrame& out) noexcept;
    };

    SpscRingBufferPtr       ring_{static_cast<SpscRingBuffer<AudioFrame,128>*>(nullptr)};
    HWND                    hwndMain_      = nullptr;

    // WASAPI objects — initialized on Thread 3
    IMMDeviceEnumerator*    pEnumerator_   = nullptr;
    IMMDevice*              pDevice_       = nullptr;
    IAudioClient*           pAudioClient_  = nullptr;
    IAudioCaptureClient*    pCaptureClient_= nullptr;
    HANDLE                  captureEvent_  = nullptr;
    WAVEFORMATEX*           pMixFormat_    = nullptr;

    // Resampler — constructed on Thread 1 before Start()
    std::unique_ptr<Resampler> resampler_;

    // Thread control
    std::thread             thread_;
    std::atomic<bool>       stopFlag_{false};
    std::atomic<bool>       running_{false};
    std::atomic<bool>       deviceChanged_{false};

    // Stats (written by Thread 3, read by Thread 1 — relaxed OK for monitoring)
    std::atomic<uint64_t>   droppedFrameCount_{0};
    std::atomic<uint64_t>   udpDropCount_{0};
    uint32_t                frameCounter_  = 0;
};
