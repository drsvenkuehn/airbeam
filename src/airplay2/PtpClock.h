#pragma once
// src/airplay2/PtpClock.h — PTP Precision Time Protocol clock for AirPlay 2.
// Runs entirely on Thread 5 (RTSP/AP2 session control).
// ClockOffset() returns a cached atomic<int64_t> — readable lock-free from any thread.
// §I: ClockOffset() MUST NOT be called from Thread 3 or Thread 4 hot path with I/O.
//     The cached atomic value IS safe to read from any thread without I/O.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>

namespace AirPlay2 {

/// Implements a simplified PTP (IEEE 1588) sync loop for AirPlay 2 timing.
/// Sends SYNC packets to the target device and measures round-trip offset.
/// Clock offset is available via ClockOffset() as a cached atomic value.
class PtpClock {
public:
    PtpClock();
    ~PtpClock();

    PtpClock(const PtpClock&)            = delete;
    PtpClock& operator=(const PtpClock&) = delete;

    /// Start the PTP sync loop.  Runs entirely on the calling thread (Thread 5).
    /// Call Start() from Thread 5's event loop.
    /// @param deviceIp   Dotted-decimal IP of the AirPlay 2 device.
    /// @param ptpPort    UDP port for PTP timing (default: 319).
    bool Start(const std::string& deviceIp, uint16_t ptpPort = 319);

    /// Stop the PTP sync loop. Safe to call from any thread.
    void Stop();

    /// Set an external reference clock offset (from another session or coordinator).
    /// Thread-safe.
    void SetReferenceOffset(int64_t ns) { clockOffsetNs_.store(ns, std::memory_order_release); }

    /// Returns the cached clock offset in nanoseconds (device_time - local_time).
    /// Lock-free; safe from any thread including Thread 3/4 (read-only atomic load).
    int64_t ClockOffset() const { return clockOffsetNs_.load(std::memory_order_acquire); }

    /// Returns true if at least one successful SYNC/FOLLOW_UP cycle has completed.
    bool IsSynchronised() const { return synced_.load(std::memory_order_acquire); }

    /// Run one synchronisation poll cycle (called by Thread 5 event loop).
    /// Returns false if the socket is no longer valid.
    bool Poll();

private:
    /// Get nanoseconds from QueryPerformanceCounter.
    static int64_t NowNs();

    /// Send a PTP SYNC packet and record t1.
    bool SendSync(int64_t& t1Ns);

    /// Wait for FOLLOW_UP + DELAY_RESP to compute offset.
    bool ReceiveFollowUp(int64_t t1Ns);

    SOCKET              sock_  = INVALID_SOCKET;
    sockaddr_in         addr_{};
    std::atomic<bool>   stop_{false};
    std::atomic<int64_t> clockOffsetNs_{0};
    std::atomic<bool>   synced_{false};
    int64_t             lastSyncNs_ = 0;
    LARGE_INTEGER       qpcFreq_{};
};

} // namespace AirPlay2
