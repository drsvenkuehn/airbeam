#pragma once
// src/airplay2/AirPlay2Session.h — AirPlay 2 streaming session.
// Implements StreamSession for AirPlay 2 protocol using WinHTTP HTTP/2.
// Runs RTSP/control I/O on Thread 5 (§I — never on Thread 3/4 hot path).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <atomic>
#include <array>
#include <cstdint>
#include <thread>

#include "core/StreamSession.h"
#include "airplay2/AesGcmCipher.h"
#include "airplay2/PtpClock.h"
#include "airplay2/HapPairing.h"
#include "airplay2/CredentialStore.h"

#pragma comment(lib, "winhttp.lib")

namespace AirPlay2 {

/// AirPlay 2 session — replaces RaopSession for AP2 devices.
/// Lifecycle (all on Thread 5 except Init/Stop on Thread 1):
///   Init() → StartRaop() → [streaming] → StopRaop()
class AirPlay2Session : public StreamSession {
public:
    AirPlay2Session();
    ~AirPlay2Session() override;

    AirPlay2Session(const AirPlay2Session&)            = delete;
    AirPlay2Session& operator=(const AirPlay2Session&) = delete;

    // ── StreamSession overrides ───────────────────────────────────────────────

    /// Initialise AP2 session:
    ///   1. Load credential from CredentialStore.
    ///   2. If not paired → post WM_AP2_PAIRING_REQUIRED, return false.
    ///   3. Run HAP VERIFY (2-step) to confirm credential is fresh.
    ///   4. If VERIFY fails → post WM_AP2_PAIRING_STALE, return false.
    ///   5. Probe control port reachability (T024a).
    ///   6. Open WinHTTP HTTP/2 session.
    bool Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd) override;

    /// Send SETUP POST to /audio (HTTP/2).
    void StartRaop(float volume) override;

    /// Send TEARDOWN, close WinHTTP session.
    void StopRaop() override;

    /// Set volume via HTTP/2 POST to /controller.
    void SetVolume(float linear) override;

    /// Update PTP reference clock offset (called from MultiRoomCoordinator).
    void SetPtpReferenceOffset(int64_t ns) override { ptpClock_.SetReferenceOffset(ns); }

    /// Returns true while the AP2 stream is live.
    bool IsStreaming() const { return streaming_.load(std::memory_order_acquire); }

    /// Returns the HAP device ID (12-char hex) for this session.
    const std::string& HapDeviceId() const { return hapDeviceId_; }

private:
    /// HTTP/2 POST helper — called on Thread 5.
    std::vector<uint8_t> Post(const std::wstring& path,
                               const std::string&  contentType,
                               const std::vector<uint8_t>& body);

    /// Probe TCP reachability of control port (T024a).
    bool ProbePort(const std::string& ip, uint16_t port, int timeoutMs = 2000);

    HINTERNET           hSession_ = nullptr;
    HINTERNET           hConnect_ = nullptr;
    std::string         hapDeviceId_;
    AesGcmCipher        aesGcm_;
    PtpClock            ptpClock_;
    HapPairing          hasPairing_;
    std::atomic<bool>   streaming_{false};
    HWND                hwnd_ = nullptr;
};

} // namespace AirPlay2
