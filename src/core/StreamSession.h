#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <memory>
#include <cstdint>

#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"
#include "audio/WasapiCapture.h"
#include "audio/AlacEncoderThread.h"
#include "protocol/RaopSession.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"
#include "discovery/AirPlayReceiver.h"

/// Bundles all resources for one active audio streaming session.
/// Created by ConnectionController::BeginConnect(); destroyed after all threads stop.
///
/// Thread safety: all methods called on Thread 1 only (the Win32 message loop),
/// except WasapiCapture/AlacEncoderThread/RaopSession which run their own threads
/// internally. No caller needs a mutex — all external access is on Thread 1.
///
/// The class is virtual so that unit tests can inject MockStreamSession without
/// starting real WASAPI, ALAC, or RAOP threads.
class StreamSession {
public:
    StreamSession()          = default;
    virtual ~StreamSession() = default;

    StreamSession(const StreamSession&)            = delete;
    StreamSession& operator=(const StreamSession&) = delete;
    StreamSession(StreamSession&&)                 = delete;
    StreamSession& operator=(StreamSession&&)      = delete;

    // ── Lifecycle — Thread 1 ─────────────────────────────────────────────────

    /// Allocates all session resources on Thread 1 (heap alloc OK here).
    /// Must be called before any Start*() method.
    /// @returns false if resource allocation fails (e.g. AES init).
    virtual bool Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd);

    // ── Thread 3 (WasapiCapture) ─────────────────────────────────────────────

    /// Starts WasapiCapture on the ring buffer. Posts WM_CAPTURE_ERROR on failure.
    virtual bool StartCapture();

    /// Stops WasapiCapture and joins Thread 3 (≤30 ms).
    virtual void StopCapture();

    /// Stops, re-initialises to current default audio device, and restarts capture.
    /// Used for WM_AUDIO_DEVICE_LOST recovery (FR-009). ≤50 ms total.
    /// @returns false if re-init fails (caller should BeginDisconnect).
    virtual bool ReinitCapture();

    /// Returns true while WasapiCapture's thread is running.
    virtual bool IsCaptureRunning() const;

    // ── Thread 5 (RaopSession) ──────────────────────────────────────────────

    /// Starts RaopSession RTSP handshake with the target receiver.
    /// Posts WM_RAOP_CONNECTED on success, WM_RAOP_FAILED on failure.
    virtual void StartRaop(float volume);

    /// Sends RTSP TEARDOWN and joins Thread 5 (≤200 ms).
    virtual void StopRaop();

    /// Audio UDP socket after SETUP (INVALID_SOCKET before WM_RAOP_CONNECTED).
    virtual SOCKET AudioSocket() const;

    // ── Thread 4 (AlacEncoderThread) ─────────────────────────────────────────

    /// Initialises the ALAC encoder state on Thread 1 (heap alloc OK).
    /// Must be called after WM_RAOP_CONNECTED (so AudioSocket() is valid).
    /// @param ssrc      Randomly-generated SSRC for this session's RTP stream.
    /// @param hwndMain  Window for WM_ENCODER_ERROR notifications.
    /// @returns false if ALAC init fails.
    virtual bool InitEncoder(uint32_t ssrc, HWND hwndMain);

    /// Starts AlacEncoderThread (Thread 4).
    virtual void StartEncoder();

    /// Stops AlacEncoderThread and joins Thread 4 (≤50 ms, includes ring drain).
    virtual void StopEncoder();

    // ── Volume ───────────────────────────────────────────────────────────────

    /// Sends RTSP SET_PARAMETER to propagate volume to receiver (Thread 5 picks it up).
    virtual void SetVolume(float linear);

    // ── Accessors ────────────────────────────────────────────────────────────

    virtual const AirPlayReceiver& Target() const { return target_; }
    virtual bool                   IsLowLatency() const { return lowLatency_; }

protected:
    AirPlayReceiver   target_;
    bool              lowLatency_  = false;
    HWND              hwnd_        = nullptr;

    /// Ring buffer shared between Thread 3 (producer) and Thread 4 (consumer).
    /// Initialised to the 512-slot arm (nullptr) until Init() completes.
    SpscRingBufferPtr ring_{static_cast<SpscRingBuffer<AudioFrame,512>*>(nullptr)};

private:
    // Storage for the two possible ring buffer sizes (one active at a time)
    std::unique_ptr<SpscRingBuffer<AudioFrame,512>> ringStd_;
    std::unique_ptr<SpscRingBuffer<AudioFrame,32>>  ringLL_;

    // AES key material — generated in Init(), used in StartRaop() and InitEncoder()
    uint8_t aesKey_[16] = {};
    uint8_t aesIv_[16]  = {};

    std::unique_ptr<AesCbcCipher>      cipher_;
    std::unique_ptr<RetransmitBuffer>  retransmit_;
    std::unique_ptr<WasapiCapture>     capture_;
    std::unique_ptr<AlacEncoderThread> encoder_;
    std::unique_ptr<RaopSession>       raop_;
};
